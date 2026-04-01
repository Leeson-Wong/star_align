// ============ star_align.cpp ============
// Standalone star-field image alignment algorithm
// Ported from DeepSkyStacker's RegisterCore.cpp and MatchingStars.cpp
// No external dependencies - only C++ standard library

#include "star_align.h"

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

struct StarDist {
    int star1, star2;
    float distance;

    StarDist(int s1, int s2, float d) : distance(d) {
        if (s1 < s2) { star1 = s1; star2 = s2; }
        else { star1 = s2; star2 = s1; }
    }
    explicit StarDist(int s1, int s2) : StarDist(s1, s2, 0.0f) {}

    friend bool operator<(const StarDist& lhs, const StarDist& rhs) noexcept {
        if (lhs.star1 != rhs.star1) return lhs.star1 < rhs.star1;
        return lhs.star2 < rhs.star2;
    }
};

struct StarTriangle {
    float fX, fY;
    int star1, star2, star3;

    StarTriangle(int s1, int s2, int s3, float fx, float fy)
        : fX(fx), fY(fy), star1(s1), star2(s2), star3(s3) {}

    friend bool operator<(const StarTriangle& lhs, const StarTriangle& rhs) noexcept {
        return lhs.fX < rhs.fX;
    }
};

// Voting flags
constexpr int VP_ACTIVE   = 1;
constexpr int VP_USED     = 0x100;
constexpr int VP_LOCKED   = 0x200;
constexpr int VP_POSSIBLE = 0x400;
constexpr int VP_CORNER   = 0x800;

struct VotingPair {
    int refStar = 0;
    int tgtStar = 0;
    int nrVotes = 0;
    int flags   = VP_ACTIVE;
    double cornerRefX = 0, cornerRefY = 0;
    double cornerTgtX = 0, cornerTgtY = 0;

    VotingPair() = default;
    VotingPair(int rs, int ts) : refStar(rs), tgtStar(ts), nrVotes(0), flags(VP_ACTIVE) {}

    bool isActive()   const { return (flags & VP_ACTIVE)   != 0; }
    bool isUsed()     const { return (flags & VP_USED)     != 0; }
    bool isLocked()   const { return (flags & VP_LOCKED)   != 0; }
    bool isPossible() const { return (flags & VP_POSSIBLE) != 0; }
    bool isCorner()   const { return (flags & VP_CORNER)   != 0; }

    void setActive(bool v)   { if (v) flags |= VP_ACTIVE;   else flags &= ~VP_ACTIVE; }
    void setUsed(bool v)     { if (v) flags |= VP_USED;     else flags &= ~VP_USED; }
    void setLocked(bool v)   { if (v) flags |= VP_LOCKED;   else flags &= ~VP_LOCKED; }
    void setPossible(bool v) { if (v) flags |= VP_POSSIBLE; else flags &= ~VP_POSSIBLE; }

    static VotingPair makeCorner(double refX, double refY, double tgtX, double tgtY) {
        VotingPair vp;
        vp.flags = VP_ACTIVE | VP_CORNER | VP_LOCKED;
        vp.nrVotes = 10000000;
        vp.cornerRefX = refX; vp.cornerRefY = refY;
        vp.cornerTgtX = tgtX; vp.cornerTgtY = tgtY;
        return vp;
    }

    friend bool operator<(const VotingPair& lhs, const VotingPair& rhs) noexcept {
        return lhs.nrVotes < rhs.nrVotes;
    }
};

// ---- Constants ----
constexpr int    STARMAXSIZE = 50;
constexpr double RoundnessTolerance = 2.0;
constexpr double RadiusFactor = 2.35 / 1.5;
constexpr float  TriangleMaxRatio = 0.9f;
constexpr float  TriangleTolerance = 0.002f;
constexpr double MaxStarDistanceDelta = 2.0;

constexpr int MIN_PAIRS_TO_BISQUARED = 25;
constexpr int MIN_PAIRS_TO_BICUBIC   = 40;

// ---- Utility functions ----

inline double Distance(double x1, double y1, double x2, double y2) {
    const double dx = x1 - x2;
    const double dy = y1 - y2;
    return std::sqrt(dx * dx + dy * dy);
}

inline double Average(const std::vector<double>& v) {
    return std::accumulate(v.cbegin(), v.cend(), 0.0) / static_cast<double>(v.size());
}

inline double Sigma(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    const double avg = Average(v);
    double sqDiff = 0.0;
    for (double val : v)
        sqDiff += (val - avg) * (val - avg);
    return std::sqrt(sqDiff / static_cast<double>(v.size()));
}

// ---- BGRA16 -> grayscale ----

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

// ---- Matrix inversion helpers ----

bool invertNxN(std::vector<double>& mat, int N) {
    std::vector<double> inv(N * N, 0.0);
    for (int i = 0; i < N; ++i) inv[i * N + i] = 1.0;
    for (int col = 0; col < N; ++col) {
        int pivotRow = col;
        double pivotVal = std::abs(mat[col * N + col]);
        for (int row = col + 1; row < N; ++row) {
            if (std::abs(mat[row * N + col]) > pivotVal) {
                pivotVal = std::abs(mat[row * N + col]);
                pivotRow = row;
            }
        }
        if (pivotVal < 1e-12) return false;
        if (pivotRow != col) {
            for (int j = 0; j < N; ++j) {
                std::swap(mat[col * N + j], mat[pivotRow * N + j]);
                std::swap(inv[col * N + j], inv[pivotRow * N + j]);
            }
        }
        const double scale = mat[col * N + col];
        for (int j = 0; j < N; ++j) {
            mat[col * N + j] /= scale;
            inv[col * N + j] /= scale;
        }
        for (int row = 0; row < N; ++row) {
            if (row == col) continue;
            const double factor = mat[row * N + col];
            for (int j = 0; j < N; ++j) {
                mat[row * N + j] -= factor * mat[col * N + j];
                inv[row * N + j] -= factor * inv[col * N + j];
            }
        }
    }
    mat = inv;
    return true;
}

bool solveLeastSquares(const std::vector<const double*>& cols, int nrCols,
                       const double* rhs, int nrRows, double* result) {
    std::vector<double> MTM(nrCols * nrCols, 0.0);
    for (int i = 0; i < nrCols; ++i) {
        for (int j = i; j < nrCols; ++j) {
            double sum = 0.0;
            for (int k = 0; k < nrRows; ++k)
                sum += cols[i][k] * cols[j][k];
            MTM[i * nrCols + j] = sum;
            MTM[j * nrCols + i] = sum;
        }
    }
    std::vector<double> MTRhs(nrCols, 0.0);
    for (int i = 0; i < nrCols; ++i) {
        double sum = 0.0;
        for (int k = 0; k < nrRows; ++k)
            sum += cols[i][k] * rhs[k];
        MTRhs[i] = sum;
    }
    if (!invertNxN(MTM, nrCols)) return false;
    for (int i = 0; i < nrCols; ++i) {
        result[i] = 0.0;
        for (int j = 0; j < nrCols; ++j)
            result[i] += MTM[i * nrCols + j] * MTRhs[j];
    }
    return true;
}

// ---- Transformation parameters ----

inline int nrCoeffsForType(TransformationType tt) {
    if (tt == TT_BICUBIC)   return 16;
    if (tt == TT_BISQUARED) return 9;
    return 4;
}

inline int nrPairsForType(TransformationType tt) {
    return nrCoeffsForType(tt) * 2;
}

inline TransformationType nextHigherType(TransformationType tt) {
    if (tt == TT_BILINEAR)  return TT_BISQUARED;
    if (tt == TT_BISQUARED) return TT_BICUBIC;
    return TT_BICUBIC;
}

inline TransformationType determineTransformType(size_t nrVotingPairs) {
    if (nrVotingPairs >= MIN_PAIRS_TO_BICUBIC)   return TT_BICUBIC;
    if (nrVotingPairs >= MIN_PAIRS_TO_BISQUARED) return TT_BISQUARED;
    return TT_BILINEAR;
}

struct TransformParams {
    TransformationType type = TT_BILINEAR;
    double a[16] = {};
    double b[16] = {};
    double fXWidth = 1.0, fYWidth = 1.0;

    TransformParams() {
        a[1] = 1.0;
        b[2] = 1.0;
    }

    void transform(double tgtX, double tgtY, double& refX, double& refY) const {
        const double X = tgtX / fXWidth;
        const double Y = tgtY / fYWidth;
        if (type == TT_BICUBIC) {
            const double X2 = X * X, X3 = X2 * X;
            const double Y2 = Y * Y, Y3 = Y2 * Y;
            refX = (a[0]  + a[1]*X  + a[2]*Y  + a[3]*X*Y
                  + a[4]*X2 + a[5]*Y2 + a[6]*X2*Y + a[7]*X*Y2 + a[8]*X2*Y2
                  + a[9]*X3 + a[10]*Y3 + a[11]*X3*Y + a[12]*X*Y3
                  + a[13]*X3*Y2 + a[14]*X2*Y3 + a[15]*X3*Y3) * fXWidth;
            refY = (b[0]  + b[1]*X  + b[2]*Y  + b[3]*X*Y
                  + b[4]*X2 + b[5]*Y2 + b[6]*X2*Y + b[7]*X*Y2 + b[8]*X2*Y2
                  + b[9]*X3 + b[10]*Y3 + b[11]*X3*Y + b[12]*X*Y3
                  + b[13]*X3*Y2 + b[14]*X2*Y3 + b[15]*X3*Y3) * fYWidth;
        } else if (type == TT_BISQUARED) {
            const double X2 = X * X;
            const double Y2 = Y * Y;
            refX = (a[0] + a[1]*X + a[2]*Y + a[3]*X*Y
                  + a[4]*X2 + a[5]*Y2 + a[6]*X2*Y + a[7]*X*Y2 + a[8]*X2*Y2) * fXWidth;
            refY = (b[0] + b[1]*X + b[2]*Y + b[3]*X*Y
                  + b[4]*X2 + b[5]*Y2 + b[6]*X2*Y + b[7]*X*Y2 + b[8]*X2*Y2) * fYWidth;
        } else {
            refX = (a[0] + a[1]*X + a[2]*Y + a[3]*X*Y) * fXWidth;
            refY = (b[0] + b[1]*X + b[2]*Y + b[3]*X*Y) * fYWidth;
        }
    }
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
    double fSquareSumY = 0, fNrValuesY2 = 0;
    for (int y = rc.top; y <= rc.bottom; ++y) {
        double fValue = std::max(0.0, getPixel(gray, width, height, xCoord, y) - backgroundLevel);
        fSquareSumY += (y - fAverageY) * (y - fAverageY) * fValue;
        fNrValuesY2 += fValue;
    }
    if (fNrValuesY2 <= 0) return false;
    const double fStdDevY = std::sqrt(fSquareSumY / fNrValuesY2);

    outMeanRadius = (fStdDevX + fStdDevY) * (1.5 / 2.0);

    return std::abs(fStdDevX - fStdDevY) < RoundnessTolerance;
}

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

// ============================================================================
// Region 3: Star matching (internal)
// ============================================================================

std::vector<StarDist> computeStarDistances(const std::vector<Star>& stars) {
    std::vector<StarDist> dists;
    dists.reserve((stars.size() * (stars.size() - 1)) / 2);
    for (size_t i = 0; i < stars.size(); ++i) {
        for (size_t j = i + 1; j < stars.size(); ++j) {
            const float d = static_cast<float>(Distance(stars[i].x, stars[i].y, stars[j].x, stars[j].y));
            dists.emplace_back(static_cast<int>(i), static_cast<int>(j), d);
        }
    }
    std::sort(dists.begin(), dists.end());
    return dists;
}

float lookupDistance(const std::vector<StarDist>& dists, int s1, int s2) {
    StarDist key(s1, s2);
    auto it = std::lower_bound(dists.begin(), dists.end(), key);
    if (it != dists.end() && it->star1 == key.star1 && it->star2 == key.star2)
        return it->distance;
    return 0.0f;
}

void addVote(int refStar, int tgtStar, std::vector<VotingPair>& votingPairs, size_t nrTgtStars) {
    const size_t offset = static_cast<size_t>(refStar) * nrTgtStars + static_cast<size_t>(tgtStar);
    if (offset < votingPairs.size())
        votingPairs[offset].nrVotes++;
}

void initVotingGrid(std::vector<VotingPair>& vPairs, size_t nrRefStars, size_t nrTgtStars) {
    vPairs.clear();
    vPairs.reserve(nrRefStars * nrTgtStars);
    for (int i = 0; i < static_cast<int>(nrRefStars); ++i)
        for (int j = 0; j < static_cast<int>(nrTgtStars); ++j)
            vPairs.emplace_back(i, j);
}

std::vector<StarTriangle> computeTriangles(const std::vector<Star>& stars) {
    std::vector<StarTriangle> triangles;
    const auto dists = computeStarDistances(stars);

    for (size_t i = 0; i < stars.size(); ++i) {
        for (size_t j = i + 1; j < stars.size(); ++j) {
            const float d0 = lookupDistance(dists, static_cast<int>(i), static_cast<int>(j));
            for (size_t k = j + 1; k < stars.size(); ++k) {
                float vDist[3];
                vDist[0] = d0;
                vDist[1] = lookupDistance(dists, static_cast<int>(j), static_cast<int>(k));
                vDist[2] = lookupDistance(dists, static_cast<int>(i), static_cast<int>(k));

                std::sort(vDist, vDist + 3);
                if (vDist[2] > 0) {
                    const float fX = vDist[1] / vDist[2];
                    const float fY = vDist[0] / vDist[2];
                    if (fX < TriangleMaxRatio) {
                        triangles.emplace_back(static_cast<int>(i), static_cast<int>(j), static_cast<int>(k), fX, fY);
                    }
                }
            }
        }
    }
    std::sort(triangles.begin(), triangles.end());
    return triangles;
}

// ---- Compute transformation for any type (Bilinear/Bisquared/Bicubic) ----

bool computeTransformation(const std::vector<VotingPair>& pairs,
                           const std::vector<Star>& refStars,
                           const std::vector<Star>& tgtStars,
                           double fXWidth, double fYWidth,
                           TransformationType tType,
                           TransformParams& params) {
    const int nrCols = nrCoeffsForType(tType);
    const size_t N = pairs.size();
    if (static_cast<int>(N) < nrCols) return false;

    std::vector<std::vector<double>> basis(nrCols);
    for (int c = 0; c < nrCols; ++c)
        basis[c].resize(N);
    std::vector<double> rhsX(N), rhsY(N);

    for (size_t i = 0; i < N; ++i) {
        double refXVal, refYVal, tgtXVal, tgtYVal;
        if (pairs[i].isCorner()) {
            refXVal = pairs[i].cornerRefX; refYVal = pairs[i].cornerRefY;
            tgtXVal = pairs[i].cornerTgtX; tgtYVal = pairs[i].cornerTgtY;
        } else {
            const Star& rs = refStars[pairs[i].refStar];
            const Star& ts = tgtStars[pairs[i].tgtStar];
            refXVal = rs.x; refYVal = rs.y;
            tgtXVal = ts.x; tgtYVal = ts.y;
        }

        rhsX[i] = refXVal / fXWidth;
        rhsY[i] = refYVal / fYWidth;

        const double X1 = tgtXVal / fXWidth;
        const double Y1 = tgtYVal / fYWidth;
        const double X2 = X1 * X1;
        const double Y2 = Y1 * Y1;
        const double X3 = X2 * X1;
        const double Y3 = Y2 * Y1;

        // Basis: 1, X, Y, XY, X2, Y2, X2Y, XY2, X2Y2, X3, Y3, X3Y, XY3, X3Y2, X2Y3, X3Y3
        if (nrCols >= 1) basis[0][i] = 1.0;
        if (nrCols >= 2) basis[1][i] = X1;
        if (nrCols >= 3) basis[2][i] = Y1;
        if (nrCols >= 4) basis[3][i] = X1 * Y1;
        if (nrCols >= 5) basis[4][i] = X2;
        if (nrCols >= 6) basis[5][i] = Y2;
        if (nrCols >= 7) basis[6][i] = X2 * Y1;
        if (nrCols >= 8) basis[7][i] = X1 * Y2;
        if (nrCols >= 9) basis[8][i] = X2 * Y2;
        if (nrCols >= 10) basis[9][i] = X3;
        if (nrCols >= 11) basis[10][i] = Y3;
        if (nrCols >= 12) basis[11][i] = X3 * Y1;
        if (nrCols >= 13) basis[12][i] = X1 * Y3;
        if (nrCols >= 14) basis[13][i] = X3 * Y2;
        if (nrCols >= 15) basis[14][i] = X2 * Y3;
        if (nrCols >= 16) basis[15][i] = X3 * Y3;
    }

    std::vector<const double*> colPtrs(nrCols);
    for (int c = 0; c < nrCols; ++c)
        colPtrs[c] = basis[c].data();

    std::vector<double> A(nrCols), B(nrCols);
    if (!solveLeastSquares(colPtrs, nrCols, rhsX.data(), static_cast<int>(N), A.data())) return false;
    if (!solveLeastSquares(colPtrs, nrCols, rhsY.data(), static_cast<int>(N), B.data())) return false;

    params.type = tType;
    for (int c = 0; c < 16; ++c) {
        params.a[c] = (c < nrCols) ? A[c] : 0.0;
        params.b[c] = (c < nrCols) ? B[c] : 0.0;
    }
    params.fXWidth = fXWidth;
    params.fYWidth = fYWidth;
    return true;
}

// Compute max distance between projected target stars and reference stars
std::pair<double, size_t> computeDistances(const std::vector<VotingPair>& pairs,
                                            const std::vector<Star>& refStars,
                                            const std::vector<Star>& tgtStars,
                                            const TransformParams& params,
                                            std::vector<double>& distances) {
    double maxDist = 0.0;
    size_t maxIdx = 0;
    distances.clear();

    for (size_t i = 0; i < pairs.size(); ++i) {
        double tgtX, tgtY, refX, refY;
        if (pairs[i].isCorner()) {
            tgtX = pairs[i].cornerTgtX; tgtY = pairs[i].cornerTgtY;
            refX = pairs[i].cornerRefX; refY = pairs[i].cornerRefY;
        } else {
            const Star& ts = tgtStars[pairs[i].tgtStar];
            const Star& rs = refStars[pairs[i].refStar];
            tgtX = ts.x; tgtY = ts.y;
            refX = rs.x; refY = rs.y;
        }
        double projRefX, projRefY;
        params.transform(tgtX, tgtY, projRefX, projRefY);
        const double dist = Distance(projRefX, projRefY, refX, refY);
        if (!pairs[i].isCorner()) {
            distances.push_back(dist);
            if (dist > maxDist) {
                maxDist = dist;
                maxIdx = distances.size() - 1;
            }
        }
    }
    return { maxDist, maxIdx };
}

std::pair<double, size_t> computeMaxDistance(const std::vector<VotingPair>& pairs,
                                              const std::vector<Star>& refStars,
                                              const std::vector<Star>& tgtStars,
                                              const TransformParams& params) {
    std::vector<double> dummy;
    return computeDistances(pairs, refStars, tgtStars, params, dummy);
}

// ---- ComputeCoordinatesTransformation with progressive upgrade ----

bool computeCoordinatesTransformation(std::vector<VotingPair>& vPairs,
                                      const std::vector<Star>& refStars,
                                      const std::vector<Star>& tgtStars,
                                      double fXWidth, double fYHeight,
                                      TransformationType maxTType,
                                      TransformParams& outParams) {
    bool bResult = false;
    bool bEnd = false;
    TransformationType tType = TT_BILINEAR;
    TransformationType okTType = TT_BILINEAR;

    TransformParams okTransformation;
    std::vector<int> vAddedPairs;
    std::vector<int> vOkAddedPairs;
    std::vector<VotingPair> vTestedPairs;
    std::vector<VotingPair> vOkPairs;
    std::vector<VotingPair> vWorking = vPairs;

    const size_t nrExtraPairs = (!vWorking.empty() && vWorking[0].isCorner()) ? 4 : 0;

    while (!bEnd && !bResult) {
        const size_t nrPairs = nrExtraPairs + static_cast<size_t>(nrPairsForType(tType));

        vAddedPairs.clear();
        vTestedPairs.clear();

        for (int idx = 0; idx < static_cast<int>(vWorking.size()); ++idx) {
            if (vWorking[idx].isActive() && vWorking[idx].isLocked()) {
                vTestedPairs.push_back(vWorking[idx]);
                vAddedPairs.push_back(idx);
            }
        }

        for (size_t i = 0; i < vWorking.size() && vTestedPairs.size() < nrPairs; ++i) {
            if (vWorking[i].isActive() && !vWorking[i].isLocked()) {
                vTestedPairs.push_back(vWorking[i]);
                vAddedPairs.push_back(static_cast<int>(i));
            }
        }

        if (vTestedPairs.size() == nrPairs) {
            TransformParams projection;
            if (computeTransformation(vTestedPairs, refStars, tgtStars, fXWidth, fYHeight, tType, projection)) {
                std::vector<double> vDistances;
                const auto [fMaxDistance, maxDistanceIndex] = computeDistances(
                    vTestedPairs, refStars, tgtStars, projection, vDistances);

                if (fMaxDistance > 3.0) {
                    const double fAverage = Average(vDistances);
                    const double fSigma = Sigma(vDistances);
                    bool bOneDeactivated = false;

                    for (size_t i = 0; i < vDistances.size(); ++i) {
                        if (vWorking[vAddedPairs[i]].isCorner()) continue;
                        if (std::abs(vDistances[i] - fAverage) > 2.0 * fSigma) {
                            vWorking[vAddedPairs[i]].setActive(false);
                            if (vDistances[i] < 7.0)
                                vWorking[vAddedPairs[i]].setPossible(true);
                            bOneDeactivated = true;
                        }
                    }

                    if (!bOneDeactivated) {
                        for (size_t i = 0; i < vDistances.size(); ++i) {
                            if (vWorking[vAddedPairs[i]].isCorner()) continue;
                            if (std::abs(vDistances[i] - fAverage) > fSigma) {
                                vWorking[vAddedPairs[i]].setActive(false);
                                bOneDeactivated = true;
                            }
                        }
                    }

                    if (!bOneDeactivated) {
                        if (!vWorking[vAddedPairs[maxDistanceIndex]].isCorner())
                            vWorking[vAddedPairs[maxDistanceIndex]].setActive(false);
                    }
                } else {
                    okTransformation = projection;
                    vOkPairs = vTestedPairs;
                    vOkAddedPairs = vAddedPairs;
                    okTType = tType;
                    bResult = (tType == maxTType);

                    if (tType < maxTType) {
                        tType = nextHigherType(tType);

                        for (auto& vp : vWorking) {
                            if (vp.isPossible()) {
                                vp.setActive(true);
                                vp.setPossible(false);
                            }
                        }

                        for (const auto idx : vAddedPairs)
                            vWorking[idx].setLocked(true);
                    }
                }
            } else {
                if (!vAddedPairs.empty())
                    vWorking[vAddedPairs[nrPairs - 1]].setActive(false);
            }
        } else {
            bEnd = true;
        }
    }

    if (!vOkPairs.empty())
        bResult = true;

    if (bResult) {
        outParams = okTransformation;
        vTestedPairs = vOkPairs;
        vAddedPairs = vOkAddedPairs;
        tType = okTType;

        for (const auto idx : vAddedPairs)
            vPairs[idx].setUsed(true);

        int lNrFails = 0;
        bEnd = false;
        while (!bEnd) {
            bool bTransformOk = false;
            int lAddedPair = -1;
            std::vector<VotingPair> vTempPairs = vTestedPairs;

            for (size_t i = 0; i < vPairs.size() && lAddedPair < 0; ++i) {
                if (vPairs[i].isActive() && !vPairs[i].isUsed()) {
                    lAddedPair = static_cast<int>(i);
                    vTempPairs.push_back(vPairs[i]);
                    vPairs[lAddedPair].setUsed(true);
                }
            }

            if (lAddedPair >= 0) {
                TransformParams projection;
                if (computeTransformation(vTempPairs, refStars, tgtStars, fXWidth, fYHeight, tType, projection)) {
                    const double maxDist = computeMaxDistance(vTempPairs, refStars, tgtStars, projection).first;
                    if (maxDist <= 2.0) {
                        vTestedPairs = vTempPairs;
                        outParams = projection;
                        vAddedPairs.push_back(lAddedPair);
                        bTransformOk = true;
                    } else {
                        vPairs[lAddedPair].setActive(false);
                    }
                }

                if (!bTransformOk) {
                    ++lNrFails;
                    if (lNrFails > 3) bEnd = true;
                }
            } else {
                bEnd = true;
            }
        }

        vPairs = vTestedPairs;
    }

    return bResult;
}

// ---- ComputeSigmaClippingTransformation with corner locking ----

bool computeSigmaClippingTransformation(std::vector<VotingPair>& vPairs,
                                         const std::vector<Star>& refStars,
                                         const std::vector<Star>& tgtStars,
                                         double fXWidth, double fYHeight,
                                         TransformationType maxTType,
                                         TransformParams& params) {
    TransformParams baseParams;
    std::vector<VotingPair> basePairs = vPairs;
    bool bResult = computeCoordinatesTransformation(basePairs, refStars, tgtStars, fXWidth, fYHeight, TT_BILINEAR, baseParams);

    if (!bResult)
        return false;

    const double w = fXWidth - 1.0;
    const double h = fYHeight - 1.0;
    const double tgtCorners[4][2] = {
        {0, 0}, {w, 0}, {0, h}, {w, h}
    };
    double refCorners[4][2];
    for (int c = 0; c < 4; ++c)
        baseParams.transform(tgtCorners[c][0], tgtCorners[c][1], refCorners[c][0], refCorners[c][1]);

    std::vector<VotingPair> cornerPairs = vPairs;
    for (int c = 0; c < 4; ++c)
        cornerPairs.push_back(VotingPair::makeCorner(
            refCorners[c][0], refCorners[c][1],
            tgtCorners[c][0], tgtCorners[c][1]));

    std::sort(cornerPairs.begin(), cornerPairs.end(),
        [](const VotingPair& a, const VotingPair& b) { return a.nrVotes > b.nrVotes; });

    bResult = computeCoordinatesTransformation(cornerPairs, refStars, tgtStars, fXWidth, fYHeight, maxTType, params);

    if (bResult) {
        std::vector<VotingPair> cleanPairs;
        for (const auto& vp : cornerPairs) {
            if (vp.isActive() && !vp.isCorner())
                cleanPairs.push_back(vp);
        }
        vPairs = cleanPairs;
    }

    return bResult;
}

// ---- ComputeLargeTriangleTransformation ----

bool computeLargeTriangleTransformation(const std::vector<Star>& refStars,
                                         const std::vector<Star>& tgtStars,
                                         double fXWidth, double fYHeight,
                                         TransformParams& params) {
    const auto refDists = computeStarDistances(refStars);
    const auto tgtDists = computeStarDistances(tgtStars);

    std::vector<int> refIndices(refDists.size());
    std::iota(refIndices.begin(), refIndices.end(), 0);
    std::sort(refIndices.begin(), refIndices.end(),
        [&](int a, int b) { return refDists[a].distance > refDists[b].distance; });

    std::vector<int> tgtIndices(tgtDists.size());
    std::iota(tgtIndices.begin(), tgtIndices.end(), 0);
    std::sort(tgtIndices.begin(), tgtIndices.end(),
        [&](int a, int b) { return tgtDists[a].distance > tgtDists[b].distance; });

    std::vector<VotingPair> vVotingPairs;
    initVotingGrid(vVotingPairs, refStars.size(), tgtStars.size());

    for (size_t ti = 0, ri = 0; ti < tgtDists.size() && ri < refDists.size();) {
        const auto& td = tgtDists[tgtIndices[ti]];
        const auto& rd = refDists[refIndices[ri]];
        const float tgtDist = td.distance;
        const float refDist = rd.distance;

        if (std::fabs(tgtDist - refDist) <= static_cast<float>(MaxStarDistanceDelta)) {
            const int lRefStar1 = rd.star1;
            const int lRefStar2 = rd.star2;
            const int lTgtStar1 = td.star1;
            const int lTgtStar2 = td.star2;

            for (int lTgtStar3 = 0; lTgtStar3 < static_cast<int>(tgtStars.size()); ++lTgtStar3) {
                if (lTgtStar3 == lTgtStar1 || lTgtStar3 == lTgtStar2) continue;

                const double fTgtDist13 = lookupDistance(tgtDists, lTgtStar1, lTgtStar3);
                const double fTgtDist23 = lookupDistance(tgtDists, lTgtStar2, lTgtStar3);
                const double fRatio = std::max(fTgtDist13, fTgtDist23) / tgtDist;

                if (fRatio < TriangleMaxRatio) {
                    for (int lRefStar3 = 0; lRefStar3 < static_cast<int>(refStars.size()); ++lRefStar3) {
                        if (lRefStar3 == lRefStar1 || lRefStar3 == lRefStar2) continue;

                        const double fRefDist13 = lookupDistance(refDists, lRefStar1, lRefStar3);
                        const double fRefDist23 = lookupDistance(refDists, lRefStar2, lRefStar3);

                        if (std::abs(fRefDist13 - fTgtDist13) < MaxStarDistanceDelta &&
                            std::abs(fRefDist23 - fTgtDist23) < MaxStarDistanceDelta) {
                            addVote(lRefStar1, lTgtStar1, vVotingPairs, tgtStars.size());
                            addVote(lRefStar2, lTgtStar2, vVotingPairs, tgtStars.size());
                            addVote(lRefStar3, lTgtStar3, vVotingPairs, tgtStars.size());
                        } else if (std::abs(fRefDist23 - fTgtDist13) < MaxStarDistanceDelta &&
                                   std::abs(fRefDist13 - fTgtDist23) < MaxStarDistanceDelta) {
                            addVote(lRefStar1, lTgtStar2, vVotingPairs, tgtStars.size());
                            addVote(lRefStar2, lTgtStar1, vVotingPairs, tgtStars.size());
                            addVote(lRefStar3, lTgtStar3, vVotingPairs, tgtStars.size());
                        }
                    }
                }
            }
        }

        if (tgtDist < refDist) ++ri;
        else ++ti;
    }

    std::sort(vVotingPairs.begin(), vVotingPairs.end(), [](const VotingPair& a, const VotingPair& b) {
        return a.nrVotes > b.nrVotes;
    });

    bool bResult = false;
    if (vVotingPairs.size() >= tgtStars.size()) {
        int lMinNrVotes = vVotingPairs[std::min(static_cast<size_t>(tgtStars.size() * 2 - 1), vVotingPairs.size() - 1)].nrVotes;
        if (lMinNrVotes == 0) lMinNrVotes = 1;

        size_t lCut = 0;
        while (lCut < vVotingPairs.size() && vVotingPairs[lCut].nrVotes >= lMinNrVotes)
            ++lCut;
        vVotingPairs.resize(lCut + 1);

        TransformationType maxTType = determineTransformType(vVotingPairs.size());
        bResult = computeSigmaClippingTransformation(vVotingPairs, refStars, tgtStars, fXWidth, fYHeight, maxTType, params);
    }

    return bResult;
}

// ---- ComputeMatchingTriangleTransformation ----

bool computeMatchingTriangleTransformation(const std::vector<StarTriangle>& refTriangles,
                                             const std::vector<Star>& refStars,
                                             const std::vector<Star>& tgtStars,
                                             double fXWidth, double fYHeight,
                                             TransformParams& params) {
    const auto tgtTriangles = computeTriangles(tgtStars);

    std::vector<VotingPair> vVotingPairs;
    initVotingGrid(vVotingPairs, refStars.size(), tgtStars.size());

    auto itLastUsedRef = refTriangles.begin();

    for (auto itTgt = tgtTriangles.cbegin(); itTgt != tgtTriangles.cend(); ++itTgt) {
        while (itLastUsedRef != refTriangles.end() && (itTgt->fX > itLastUsedRef->fX + TriangleTolerance)) {
            ++itLastUsedRef;
        }

        if (itLastUsedRef == refTriangles.end()) break;

        auto itRef = itLastUsedRef;
        while (itRef != refTriangles.end() && (itRef->fX < itTgt->fX + TriangleTolerance)) {
            const double dist = Distance(static_cast<double>(itRef->fX), static_cast<double>(itRef->fY),
                                          static_cast<double>(itTgt->fX), static_cast<double>(itTgt->fY));
            if (dist <= TriangleTolerance) {
                const int refStars_arr[3] = { itRef->star1, itRef->star2, itRef->star3 };
                const int tgtStars_arr[3] = { itTgt->star1, itTgt->star2, itTgt->star3 };
                for (int ri = 0; ri < 3; ++ri)
                    for (int ti = 0; ti < 3; ++ti)
                        addVote(refStars_arr[ri], tgtStars_arr[ti], vVotingPairs, tgtStars.size());
            }
            ++itRef;
        }
    }

    std::sort(vVotingPairs.begin(), vVotingPairs.end(), [](const VotingPair& a, const VotingPair& b) {
        return a.nrVotes > b.nrVotes;
    });

    bool bResult = false;
    if (vVotingPairs.size() >= tgtStars.size()) {
        int lMinNrVotes = vVotingPairs[std::min(static_cast<size_t>(tgtStars.size() * 2 - 1), vVotingPairs.size() - 1)].nrVotes;
        if (lMinNrVotes == 0) lMinNrVotes = 1;

        size_t lCut = 0;
        while (lCut < vVotingPairs.size() && vVotingPairs[lCut].nrVotes >= lMinNrVotes)
            ++lCut;
        vVotingPairs.resize(lCut);

        TransformationType maxTType = determineTransformType(vVotingPairs.size());
        bResult = computeSigmaClippingTransformation(vVotingPairs, refStars, tgtStars, fXWidth, fYHeight, maxTType, params);
    }

    return bResult;
}

// ---- Internal alignment computation ----

AlignResult computeAlignmentInternal(const std::vector<Star>& refStars,
                                      const std::vector<Star>& tgtStars,
                                      int imageWidth, int imageHeight) {
    AlignResult result;

    if (refStars.size() < 8 || tgtStars.size() < 8)
        return result;

    const double fXWidth = static_cast<double>(imageWidth);
    const double fYHeight = static_cast<double>(imageHeight);

    TransformParams params;

    bool ok = computeLargeTriangleTransformation(refStars, tgtStars, fXWidth, fYHeight, params);
    if (!ok) {
        const auto refTriangles = computeTriangles(refStars);
        ok = computeMatchingTriangleTransformation(refTriangles, refStars, tgtStars, fXWidth, fYHeight, params);
    }

    if (!ok)
        return result;

    result.transformType = params.type;
    for (int i = 0; i < 16; ++i) {
        result.a[i] = params.a[i];
        result.b[i] = params.b[i];
    }

    const double cx = fXWidth / 2.0;
    const double cy = fYHeight / 2.0;
    double refCX, refCY;
    params.transform(cx, cy, refCX, refCY);
    result.offsetX = refCX - cx;
    result.offsetY = refCY - cy;

    double pt1x, pt1y, pt2x, pt2y;
    params.transform(0, 0, pt1x, pt1y);
    params.transform(fXWidth, 0, pt2x, pt2y);
    result.angle = std::atan2(pt2y - pt1y, pt2x - pt1x);

    result.matchedStars = std::min(static_cast<int>(refStars.size()), static_cast<int>(tgtStars.size()));

    result.success = true;
    return result;
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

inline double evalPolynomial(const double coeff[16], double X, double Y, TransformationType type) {
    if (type == TT_BICUBIC) {
        const double X2 = X * X, X3 = X2 * X;
        const double Y2 = Y * Y, Y3 = Y2 * Y;
        return coeff[0]  + coeff[1]*X  + coeff[2]*Y  + coeff[3]*X*Y
             + coeff[4]*X2 + coeff[5]*Y2 + coeff[6]*X2*Y + coeff[7]*X*Y2 + coeff[8]*X2*Y2
             + coeff[9]*X3 + coeff[10]*Y3 + coeff[11]*X3*Y + coeff[12]*X*Y3
             + coeff[13]*X3*Y2 + coeff[14]*X2*Y3 + coeff[15]*X3*Y3;
    } else if (type == TT_BISQUARED) {
        const double X2 = X * X;
        const double Y2 = Y * Y;
        return coeff[0] + coeff[1]*X + coeff[2]*Y + coeff[3]*X*Y
             + coeff[4]*X2 + coeff[5]*Y2 + coeff[6]*X2*Y + coeff[7]*X*Y2 + coeff[8]*X2*Y2;
    } else {
        return coeff[0] + coeff[1]*X + coeff[2]*Y + coeff[3]*X*Y;
    }
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
    return computeAlignmentInternal(refStars, tgtStars, imageWidth, imageHeight);
}

AlignResult alignImages(
    const uint16_t* pRefData, int refWidth, int refHeight, int refStride,
    const uint16_t* pTgtData, int tgtWidth, int tgtHeight, int tgtStride,
    const DetectParams& params)
{
    auto refStars = detectStars(pRefData, refWidth, refHeight, refStride, params);
    auto tgtStars = detectStars(pTgtData, tgtWidth, tgtHeight, tgtStride, params);
    return computeAlignmentInternal(refStars, tgtStars, refWidth, refHeight);
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
            const double srcFX = evalPolynomial(alignment.a, X, Y, alignment.transformType) * fXWidth;
            const double srcFY = evalPolynomial(alignment.b, X, Y, alignment.transformType) * fYHeight;

            const size_t dstIdx = (static_cast<size_t>(oy) * width + ox) * 4;

            if (srcFX >= 0.0 && srcFX < fXWidth && srcFY >= 0.0 && srcFY < fYHeight) {
                dstBGRA[dstIdx + 0] = bilinearChannel(srcBGRA, width, height, effectiveStride, srcFX, srcFY, 0);
                dstBGRA[dstIdx + 1] = bilinearChannel(srcBGRA, width, height, effectiveStride, srcFX, srcFY, 1);
                dstBGRA[dstIdx + 2] = bilinearChannel(srcBGRA, width, height, effectiveStride, srcFX, srcFY, 2);
                dstBGRA[dstIdx + 3] = 0xFFFF;
            } else {
                dstBGRA[dstIdx + 0] = 0;
                dstBGRA[dstIdx + 1] = 0;
                dstBGRA[dstIdx + 2] = 0xFFFF;
                dstBGRA[dstIdx + 3] = 0xFFFF;
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
            result[base + 0] = 0;
            result[base + 1] = 0;
            result[base + 2] = 0xFFFF;
            result[base + 3] = 0xFFFF;
        }
    }

    return result;
}

} // namespace StarAlign
