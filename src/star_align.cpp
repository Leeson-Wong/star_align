// ============ star_align.cpp ============
// Standalone star-field image alignment algorithm
// Uses DSS CMatchingStars for alignment (exact same algorithm as DeepSkyStacker)

#include "star_align.h"
#include "MatchingStars.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <utility>
#include <vector>

namespace StarAlign {

// ============================================================================
// Region 1: Internal utilities (anonymous namespace)
// ============================================================================
namespace {

// ---- Data structures ----

struct Rect {
    int left, top, right, bottom;
};

// ---- Constants ----
constexpr int    STARMAXSIZE = 50;
constexpr double RoundnessTolerance = 2.0;
constexpr double RadiusFactor = 2.35 / 1.5;

// ---- Utility functions ----

inline double Distance(double x1, double y1, double x2, double y2) {
    const double dx = x1 - x2;
    const double dy = y1 - y2;
    return std::sqrt(dx * dx + dy * dy);
}

// ---- BGRA16 -> grayscale ----

// stride is in bytes. pData points to BGRA16 data (4 x uint16_t per pixel).
// Returns grayscale in [0, 256) range.
std::vector<double> extractGray(const uint16_t* pData, int width, int height, int stride) {
    std::vector<double> gray(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (int row = 0; row < height; ++row) {
        const uint16_t* pRow = reinterpret_cast<const uint16_t*>(
            reinterpret_cast<const uint8_t*>(pData) + static_cast<size_t>(row) * stride);
        double* pOut = gray.data() + static_cast<size_t>(row) * width;
        for (int col = 0; col < width; ++col) {
            const uint16_t B = pRow[col * 4 + 0];
            const uint16_t G = pRow[col * 4 + 1];
            const uint16_t R = pRow[col * 4 + 2];
            pOut[col] = (static_cast<double>(B) + static_cast<double>(G) + static_cast<double>(R)) / 768.0;
        }
    }
    return gray;
}

inline double getPixel(const std::vector<double>& gray, int width, int height, int x, int y) {
    if (x < 0 || x >= width || y < 0 || y >= height) return 0.0;
    return gray[static_cast<size_t>(y) * width + x];
}

inline double getPixelUnchecked(const std::vector<double>& gray, int width, int x, int y) {
    return gray[static_cast<size_t>(y) * width + x];
}

// ---- Pixel direction helper for star detection ----

struct PixelDirection {
    int xDir;
    int yDir;
    double intensity = 0.0;
    int radius = 0;
    int ok = 2;
    int nrBrighterPixels = 0;
};

// ---- Internal star detection ----

bool computeStarCenter(const std::vector<double>& gray, int width, int height,
                       const Rect& rc, double backgroundLevel,
                       double& outX, double& outY, double& outMeanRadius) {
    double fNrValuesX = 0, fAverageX = 0;
    int lNrLines = 0;

    for (int y = rc.top; y <= rc.bottom; ++y) {
        double fSumX = 0;
        fNrValuesX = 0;
        for (int x = rc.left; x <= rc.right; ++x) {
            const double fValue = getPixelUnchecked(gray, width, x, y);
            fSumX += fValue * x;
            fNrValuesX += fValue;
        }
        if (fNrValuesX > 0) {
            ++lNrLines;
            fAverageX += fSumX / fNrValuesX;
        }
    }
    if (lNrLines == 0) return false;
    fAverageX /= static_cast<double>(lNrLines);

    double fAverageY = 0;
    int lNrColumns = 0;
    for (int x = rc.left; x <= rc.right; ++x) {
        double fSumY = 0;
        double fNrValuesY = 0;
        for (int y = rc.top; y <= rc.bottom; ++y) {
            const double fValue = getPixelUnchecked(gray, width, x, y);
            fSumY += fValue * y;
            fNrValuesY += fValue;
        }
        if (fNrValuesY > 0) {
            ++lNrColumns;
            fAverageY += fSumY / fNrValuesY;
        }
    }
    if (lNrColumns == 0) return false;
    fAverageY /= static_cast<double>(lNrColumns);

    outX = fAverageX;
    outY = fAverageY;

    const int yCoord = static_cast<int>(std::round(fAverageY));
    double fSquareSumX = 0, fNrValuesX2 = 0;
    for (int x = rc.left; x <= rc.right; ++x) {
        double fValue = std::max(0.0, getPixel(gray, width, height, x, yCoord) - backgroundLevel);
        fSquareSumX += (x - fAverageX) * (x - fAverageX) * fValue;
        fNrValuesX2 += fValue;
    }
    if (fNrValuesX2 <= 0) return false;
    const double fStdDevX = std::sqrt(fSquareSumX / fNrValuesX2);

    const int xCoord = static_cast<int>(std::round(fAverageX));
    double fSquareSumY = 0, fNrValuesY = 0;
    for (int y = rc.top; y <= rc.bottom; ++y) {
        double fValue = std::max(0.0, getPixel(gray, width, height, xCoord, y) - backgroundLevel);
        fSquareSumY += (y - fAverageY) * (y - fAverageY) * fValue;
        fNrValuesY += fValue;
    }
    if (fNrValuesY <= 0) return false;
    const double fStdDevY = std::sqrt(fSquareSumY / fNrValuesY);

    outMeanRadius = (fStdDevX + fStdDevY) * (1.5 / 2.0);

    return std::abs(fStdDevX - fStdDevY) < RoundnessTolerance;
}

// Internal star detection on grayscale buffer
std::vector<Star> detectStarsInternal(const std::vector<double>& gray, int width, int height,
                                       double detectionThreshold, int maxStarSize) {
    std::vector<Star> stars;

    constexpr size_t HistoSize = 256 * 32;
    std::vector<int> histo(HistoSize + 1, 0);
    double maxIntensity = std::numeric_limits<double>::min();

    const size_t totalPixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t idx = 0; idx < totalPixels; ++idx) {
        const double value = gray[idx];
        if (value > maxIntensity) maxIntensity = value;
        ++histo[static_cast<size_t>(value * 32.0)];
    }

    const size_t fiftyPercent = totalPixels / 2 - 1;
    size_t nrValues = 0;
    size_t fiftyPercentQuantile = static_cast<size_t>(-1);
    while (nrValues < fiftyPercent) {
        ++fiftyPercentQuantile;
        nrValues += histo[fiftyPercentQuantile];
    }
    const double backgroundLevel = static_cast<double>(fiftyPercentQuantile) / static_cast<double>(HistoSize);
    const double bgLevel256 = backgroundLevel * 256.0;
    const double intensityThreshold = 256.0 * detectionThreshold + bgLevel256;

    if (maxIntensity < intensityThreshold)
        return stars;

    constexpr int dirX[8] = { 0,  1,  0, -1,  1,  1, -1, -1};
    constexpr int dirY[8] = {-1,  0,  1,  0, -1,  1,  1, -1};

    for (int deltaRadius = 0; deltaRadius < 4; ++deltaRadius) {
        for (int j = 0; j < height; ++j) {
            for (int i = 0; i < width; ++i) {
                const double fIntensity = getPixelUnchecked(gray, width, i, j);
                if (fIntensity < intensityThreshold)
                    continue;

                bool isNew = true;
                for (auto it = std::lower_bound(stars.begin(), stars.end(), Star{},
                    [xi = i - STARMAXSIZE](const Star& s, const Star&) { return s.x < xi; });
                    it != stars.end() && isNew; ++it) {
                    if (it->x > i + STARMAXSIZE) break;
                    if (Distance(it->x, it->y, static_cast<double>(i), static_cast<double>(j)) < it->meanRadius * RadiusFactor)
                        isNew = false;
                }

                if (!isNew) continue;

                PixelDirection directions[8];
                for (int d = 0; d < 8; ++d) {
                    directions[d].xDir = dirX[d];
                    directions[d].yDir = dirY[d];
                    const int nx = i + dirX[d];
                    const int ny = j + dirY[d];
                    directions[d].intensity = getPixel(gray, width, height, nx, ny);
                    directions[d].radius = 0;
                    directions[d].ok = 2;
                    directions[d].nrBrighterPixels = 0;
                }

                const double th1 = fIntensity - bgLevel256;
                const double th2 = 0.6 * th1;
                int nrDarker = 0, nrMuchDarker = 0;
                for (int d = 0; d < 8; ++d) {
                    const double testVal = directions[d].intensity - bgLevel256;
                    if (testVal < th1) {
                        ++nrDarker;
                        if (testVal < th2) ++nrMuchDarker;
                    }
                }
                if (nrDarker >= 7 && nrMuchDarker >= 4)
                    continue;

                bool brighterPixel = false;
                bool mainOk = true;
                int lMaxRadius = 0;

                for (int testedRadius = 1; testedRadius < maxStarSize && mainOk && !brighterPixel; ++testedRadius) {
                    if (testedRadius > 1) {
                        for (int d = 0; d < 8; ++d) {
                            const int nx = i + dirX[d] * testedRadius;
                            const int ny = j + dirY[d] * testedRadius;
                            directions[d].intensity = getPixel(gray, width, height, nx, ny);
                        }
                    }

                    mainOk = false;
                    for (int d = 0; d < 8; ++d) {
                        if (brighterPixel) break;
                        if (directions[d].ok > 0) {
                            if (directions[d].intensity - bgLevel256 < 0.25 * (fIntensity - bgLevel256)) {
                                directions[d].radius = testedRadius;
                                --directions[d].ok;
                                lMaxRadius = std::max(lMaxRadius, testedRadius);
                            } else if (directions[d].intensity > 1.05 * fIntensity) {
                                brighterPixel = true;
                            } else if (directions[d].intensity > fIntensity) {
                                ++directions[d].nrBrighterPixels;
                            }
                        }
                        if (directions[d].ok > 0) mainOk = true;
                        if (directions[d].nrBrighterPixels >= 2) brighterPixel = true;
                    }
                }

                if (!mainOk && !brighterPixel && lMaxRadius > 2) {
                    int maxDeltaRadii = 0;
                    auto checkRadii = [&](const int dirs[4]) -> bool {
                        bool ok = true;
                        for (int ii = 0; ii < 4; ++ii) {
                            const int r1 = directions[dirs[ii]].radius;
                            for (int jj = 0; jj < 4; ++jj) {
                                const int dr = std::abs(directions[dirs[jj]].radius - r1);
                                maxDeltaRadii = std::max(maxDeltaRadii, dr);
                                ok = ok && (dr <= deltaRadius);
                            }
                        }
                        return ok;
                    };

                    const int mainDirs[4] = {0, 1, 2, 3};
                    const int diagDirs[4] = {4, 5, 6, 7};

                    bool validCandidate = checkRadii(mainDirs) && checkRadii(diagDirs);

                    auto checkDiameterRatio = [&](int d1, int d2, int d3, int d4) -> bool {
                        if (lMaxRadius > 10) return true;
                        constexpr double MaxAllowedRatio = 1.5;
                        const int diam1 = directions[d1].radius + directions[d2].radius;
                        const int diam2 = directions[d3].radius + directions[d4].radius;
                        if (diam1 == 0 || diam2 == 0) return true;
                        const double ratio1 = static_cast<double>(diam2) / diam1;
                        const double ratio2 = static_cast<double>(diam1) / diam2;
                        return ratio1 <= MaxAllowedRatio && ratio2 <= MaxAllowedRatio;
                    };

                    validCandidate = validCandidate
                        && checkDiameterRatio(0, 2, 1, 3)
                        && checkDiameterRatio(4, 6, 5, 7);

                    const double fMeanRadius1 =
                        (directions[0].radius + directions[1].radius + directions[2].radius + directions[3].radius) / 4.0;
                    const double fMeanRadius2 =
                        (directions[4].radius + directions[5].radius + directions[6].radius + directions[7].radius) * 0.3535533905932737622;

                    if (validCandidate) {
                        int lLeftRadius = 0, lRightRadius = 0, lTopRadius = 0, lBottomRadius = 0;
                        for (int d = 0; d < 8; ++d) {
                            if (dirX[d] < 0) lLeftRadius   = std::max(lLeftRadius, directions[d].radius);
                            else if (dirX[d] > 0) lRightRadius  = std::max(lRightRadius, directions[d].radius);
                            if (dirY[d] < 0) lTopRadius    = std::max(lTopRadius, directions[d].radius);
                            else if (dirY[d] > 0) lBottomRadius = std::max(lBottomRadius, directions[d].radius);
                        }

                        Rect starRC;
                        starRC.left   = i - lLeftRadius;
                        starRC.top    = j - lTopRadius;
                        starRC.right  = i + lRightRadius;
                        starRC.bottom = j + lBottomRadius;

                        starRC.left   = std::max(0, starRC.left);
                        starRC.top    = std::max(0, starRC.top);
                        starRC.right  = std::min(width - 1, starRC.right);
                        starRC.bottom = std::min(height - 1, starRC.bottom);

                        double starX, starY, starMeanRadius;
                        if (computeStarCenter(gray, width, height, starRC, backgroundLevel,
                                              starX, starY, starMeanRadius)) {
                            bool overlap = false;
                            for (auto it = std::lower_bound(stars.begin(), stars.end(), Star{},
                                [x = starX - starMeanRadius * RadiusFactor - STARMAXSIZE](const Star& s, const Star&) { return s.x < x; });
                                it != stars.end() && !overlap; ++it) {
                                if (it->x > starX + starMeanRadius * RadiusFactor + STARMAXSIZE) break;
                                if (Distance(starX, starY, it->x, it->y) < (starMeanRadius + it->meanRadius) * RadiusFactor)
                                    overlap = true;
                            }

                            if (!overlap) {
                                Star s;
                                s.x = starX;
                                s.y = starY;
                                s.intensity = fIntensity / 256.0;
                                s.circularity = (fIntensity - bgLevel256) / (0.1 + maxDeltaRadii);
                                s.meanRadius = starMeanRadius;

                                auto pos = std::lower_bound(stars.begin(), stars.end(), s,
                                    [](const Star& a, const Star& b) { return a.x < b.x; });
                                stars.insert(pos, s);
                            }
                        }
                    }
                }
            }
        }
    }

    return stars;
}

// ---- BGRA16 transform helpers ----

inline uint16_t bilinearChannel(const uint16_t* src, int width, int height, int stride,
                                 double srcX, double srcY, int channelOffset) {
    srcX = std::max(0.0, std::min(static_cast<double>(width - 1), srcX));
    srcY = std::max(0.0, std::min(static_cast<double>(height - 1), srcY));

    const int x0 = static_cast<int>(std::floor(srcX));
    const int y0 = static_cast<int>(std::floor(srcY));
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);

    const double fx = srcX - x0;
    const double fy = srcY - y0;

    auto readChannel = [&](int x, int y) -> double {
        const uint16_t* pRow = reinterpret_cast<const uint16_t*>(
            reinterpret_cast<const uint8_t*>(src) + static_cast<size_t>(y) * stride);
        return pRow[x * 4 + channelOffset];
    };

    const double v00 = readChannel(x0, y0);
    const double v10 = readChannel(x1, y0);
    const double v01 = readChannel(x0, y1);
    const double v11 = readChannel(x1, y1);

    const double val = v00 * (1 - fx) * (1 - fy)
                     + v10 * fx       * (1 - fy)
                     + v01 * (1 - fx) * fy
                     + v11 * fx       * fy;

    return static_cast<uint16_t>(std::min(65535.0, std::max(0.0, std::round(val))));
}

} // anonymous namespace

// ============================================================================
// Region 5 & 6: Public API
// ============================================================================

std::vector<Star> detectStars(
    const uint16_t* pData,
    int width,
    int height,
    int stride,
    const DetectParams& params)
{
    if (!pData || width <= 0 || height <= 0)
        return {};

    const int effectiveStride = (stride > 0) ? stride : (width * 4 * sizeof(uint16_t));

    auto gray = extractGray(pData, width, height, effectiveStride);

    return detectStarsInternal(gray, width, height, params.threshold, params.maxStarSize);
}

AlignResult computeAlignment(
    const std::vector<Star>& refStars,
    const std::vector<Star>& tgtStars,
    int imageWidth,
    int imageHeight)
{
    AlignResult result;

    if (refStars.size() < 8 || tgtStars.size() < 8)
        return result;

    // Delegate to DSS CMatchingStars
    CMatchingStars matcher(imageWidth, imageHeight);
    for (const auto& s : refStars)
        matcher.AddReferenceStar(s.x, s.y);
    for (const auto& s : tgtStars)
        matcher.AddTargetedStar(s.x, s.y);

    CBilinearParameters dssParams;
    bool ok = matcher.ComputeCoordinateTransformation(dssParams);
    if (!ok)
        return result;

    // Extract bilinear parameters from DSS result
    result.a0 = dssParams.a0; result.a1 = dssParams.a1;
    result.a2 = dssParams.a2; result.a3 = dssParams.a3;
    result.b0 = dssParams.b0; result.b1 = dssParams.b1;
    result.b2 = dssParams.b2; result.b3 = dssParams.b3;

    // Report center displacement (for display / comparison with DSS)
    const double cx = static_cast<double>(imageWidth) / 2.0;
    const double cy = static_cast<double>(imageHeight) / 2.0;
    QPointF centerPt(cx, cy);
    QPointF mappedCenter = dssParams.transform(centerPt);
    result.offsetX = mappedCenter.x() - cx;
    result.offsetY = mappedCenter.y() - cy;

    // Report rotation angle
    result.angle = dssParams.Angle(imageWidth);

    // Count matched pairs
    auto votedPairs = matcher.GetVotedPairsCopy();
    result.matchedStars = static_cast<int>(votedPairs.size());

    result.success = true;
    return result;
}

AlignResult alignImages(
    const uint16_t* pRefData, int refWidth, int refHeight, int refStride,
    const uint16_t* pTgtData, int tgtWidth, int tgtHeight, int tgtStride,
    const DetectParams& params)
{
    auto refStars = detectStars(pRefData, refWidth, refHeight, refStride, params);
    auto tgtStars = detectStars(pTgtData, tgtWidth, tgtHeight, tgtStride, params);
    return computeAlignment(refStars, tgtStars, refWidth, refHeight);
}

std::vector<uint16_t> transformBGRA(
    const uint16_t* srcBGRA, int width, int height, int stride,
    const AlignResult& alignment)
{
    const size_t totalPixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint16_t> dstBGRA(totalPixels * 4, 0);

    if (!srcBGRA || width <= 0 || height <= 0)
        return dstBGRA;

    const int effectiveStride = (stride > 0) ? stride : (width * 4 * sizeof(uint16_t));
    const double fXWidth = static_cast<double>(width);
    const double fYHeight = static_cast<double>(height);

    for (int oy = 0; oy < height; ++oy) {
        for (int ox = 0; ox < width; ++ox) {
            const double X = ox / fXWidth;
            const double Y = oy / fYHeight;
            const double srcFX = (alignment.a0 + alignment.a1 * X + alignment.a2 * Y + alignment.a3 * X * Y) * fXWidth;
            const double srcFY = (alignment.b0 + alignment.b1 * X + alignment.b2 * Y + alignment.b3 * X * Y) * fYHeight;

            const size_t dstIdx = (static_cast<size_t>(oy) * width + ox) * 4;

            if (srcFX >= 0.0 && srcFX < fXWidth && srcFY >= 0.0 && srcFY < fYHeight) {
                dstBGRA[dstIdx + 0] = bilinearChannel(srcBGRA, width, height, effectiveStride, srcFX, srcFY, 0);
                dstBGRA[dstIdx + 1] = bilinearChannel(srcBGRA, width, height, effectiveStride, srcFX, srcFY, 1);
                dstBGRA[dstIdx + 2] = bilinearChannel(srcBGRA, width, height, effectiveStride, srcFX, srcFY, 2);
                dstBGRA[dstIdx + 3] = 0xFFFF;
            } else {
                dstBGRA[dstIdx + 0] = 0;       // B
                dstBGRA[dstIdx + 1] = 0;       // G
                dstBGRA[dstIdx + 2] = 0xFFFF;  // R
                dstBGRA[dstIdx + 3] = 0xFFFF;  // A
            }
        }
    }

    return dstBGRA;
}

std::vector<uint16_t> stackBGRAImages(
    const std::vector<const uint16_t*>& images,
    int width, int height, int stride,
    const std::vector<AlignResult>& alignments)
{
    const size_t totalChannels = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    const size_t nFrames = images.size();

    if (nFrames == 0 || alignments.size() != nFrames)
        return {};

    const int effectiveStride = (stride > 0) ? stride : (width * 4 * sizeof(uint16_t));

    const size_t totalPixels = static_cast<size_t>(width) * static_cast<size_t>(height);

    std::vector<double> accumulator(totalChannels, 0.0);
    std::vector<size_t> validCount(totalPixels, 0);

    for (size_t i = 0; i < totalChannels; ++i)
        accumulator[i] = images[0][i];
    for (size_t p = 0; p < totalPixels; ++p)
        validCount[p] = 1;

    for (size_t f = 1; f < nFrames; ++f) {
        if (!alignments[f].success) continue;

        auto transformed = transformBGRA(images[f], width, height, effectiveStride,
                                          alignments[f]);

        for (size_t p = 0; p < totalPixels; ++p) {
            if (transformed[p * 4 + 3] != 0) {
                accumulator[p * 4 + 0] += transformed[p * 4 + 0];
                accumulator[p * 4 + 1] += transformed[p * 4 + 1];
                accumulator[p * 4 + 2] += transformed[p * 4 + 2];
                ++validCount[p];
            }
        }
    }

    std::vector<uint16_t> result(totalChannels, 0);
    for (size_t p = 0; p < totalPixels; ++p) {
        if (validCount[p] > 0) {
            const double invCount = 1.0 / static_cast<double>(validCount[p]);
            const size_t base = p * 4;
            result[base + 0] = static_cast<uint16_t>(std::min(65535.0, std::max(0.0,
                std::round(accumulator[base + 0] * invCount))));
            result[base + 1] = static_cast<uint16_t>(std::min(65535.0, std::max(0.0,
                std::round(accumulator[base + 1] * invCount))));
            result[base + 2] = static_cast<uint16_t>(std::min(65535.0, std::max(0.0,
                std::round(accumulator[base + 2] * invCount))));
            result[base + 3] = 0xFFFF;
        } else {
            const size_t base = p * 4;
            result[base + 0] = 0;       // B
            result[base + 1] = 0;       // G
            result[base + 2] = 0xFFFF;  // R
            result[base + 3] = 0xFFFF;  // A
        }
    }

    return result;
}

} // namespace StarAlign
