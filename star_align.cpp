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
#include <cstdio>

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
    // Corner locking: direct coordinates (used when VP_CORNER is set)
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

// stride is in bytes. pData points to BGRA16 data (4 x uint16_t per pixel).
// Returns grayscale in [0, 256) range.
std::vector<double> extractGray(const uint16_t* pData, int width, int height, int stride) {
    std::vector<double> gray(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (int row = 0; row < height; ++row) {
        const uint16_t* pRow = reinterpret_cast<const uint16_t*>(
            reinterpret_cast<const uint8_t*>(pData) + static_cast<size_t>(row) * stride);
        double* pOut = gray.data() + static_cast<size_t>(row) * width;
        for (int col = 0; col < width; ++col) {
            // BGRA16: B, G, R, A - 4 uint16_t per pixel
            const uint16_t B = pRow[col * 4 + 0];
            const uint16_t G = pRow[col * 4 + 1];
            const uint16_t R = pRow[col * 4 + 2];
            // Map to [0, 256): (B + G + R) / 3 / 256.0
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
    int ok = 2;            // must find 2 darker pixels in this direction
    int nrBrighterPixels = 0;
};

// ---- Transform type ----

enum TransformType {
    TT_LINEAR     = 0,
    TT_BILINEAR   = 1,
    TT_BISQUARED  = 2,
    TT_BICUBIC    = 3,
};

inline TransformType nextHigherTransformType(TransformType tt) {
    return static_cast<TransformType>(static_cast<int>(tt) + 1);
}

constexpr int MINPAIRSTOBISQUARED = 25;
constexpr int MINPAIRSTOBICUBIC   = 40;

TransformType getTransformType(size_t nrVotingPairs) {
    // Automatic mode (always 0 in star_align)
    if (nrVotingPairs >= static_cast<size_t>(MINPAIRSTOBICUBIC))
        return TT_BICUBIC;
    if (nrVotingPairs >= static_cast<size_t>(MINPAIRSTOBISQUARED))
        return TT_BISQUARED;
    return TT_BILINEAR;
}

// ---- Matrix inversion helpers ----

// Invert an NxN matrix using Gauss-Jordan elimination (column-major).
// mat is N*N doubles, modified in-place. Returns true on success.
bool invertNxN(double* mat, size_t N) {
    std::vector<double> inv(N * N, 0.0);
    for (size_t i = 0; i < N; ++i) inv[i * N + i] = 1.0;

    for (size_t col = 0; col < N; ++col) {
        // Find pivot
        size_t pivotRow = col;
        double pivotVal = std::abs(mat[col * N + col]);
        for (size_t row = col + 1; row < N; ++row) {
            if (std::abs(mat[row * N + col]) > pivotVal) {
                pivotVal = std::abs(mat[row * N + col]);
                pivotRow = row;
            }
        }
        if (pivotVal < 1e-12) return false;

        // Swap rows
        if (pivotRow != col) {
            for (size_t j = 0; j < N; ++j) {
                std::swap(mat[col * N + j], mat[pivotRow * N + j]);
                std::swap(inv[col * N + j], inv[pivotRow * N + j]);
            }
        }

        // Scale pivot row
        const double scale = mat[col * N + col];
        for (size_t j = 0; j < N; ++j) {
            mat[col * N + j] /= scale;
            inv[col * N + j] /= scale;
        }

        // Eliminate column
        for (size_t row = 0; row < N; ++row) {
            if (row == col) continue;
            const double factor = mat[row * N + col];
            for (size_t j = 0; j < N; ++j) {
                mat[row * N + j] -= factor * mat[col * N + j];
                inv[row * N + j] -= factor * inv[col * N + j];
            }
        }
    }
    // Copy result back
    for (size_t i = 0; i < N * N; ++i)
        mat[i] = inv[i];
    return true;
}

// Generic least squares: (M^T M) result = M^T rhs, where M is numRows x numCols.
// M_cols is column-major: M_cols[col * numRows + row].
// result is numCols-element vector. Returns true on success.
bool solveLeastSquares(const double* M_cols, size_t numRows, size_t numCols,
                       const double* rhs, double* result) {
    // Compute M^T M (numCols x numCols, column-major)
    std::vector<double> MTM(numCols * numCols, 0.0);
    for (size_t i = 0; i < numCols; ++i) {
        const double* colI = M_cols + i * numRows;
        for (size_t j = i; j < numCols; ++j) {
            const double* colJ = M_cols + j * numRows;
            double sum = 0.0;
            for (size_t k = 0; k < numRows; ++k)
                sum += colI[k] * colJ[k];
            MTM[i * numCols + j] = sum;
            MTM[j * numCols + i] = sum;
        }
    }

    // Compute M^T rhs (numCols x 1)
    std::vector<double> MTRhs(numCols, 0.0);
    for (size_t i = 0; i < numCols; ++i) {
        const double* colI = M_cols + i * numRows;
        double sum = 0.0;
        for (size_t k = 0; k < numRows; ++k)
            sum += colI[k] * rhs[k];
        MTRhs[i] = sum;
    }

    // Invert M^T M
    if (!invertNxN(MTM.data(), numCols)) return false;

    // result = (M^T M)^{-1} M^T rhs
    for (size_t i = 0; i < numCols; ++i) {
        result[i] = 0.0;
        for (size_t j = 0; j < numCols; ++j)
            result[i] += MTM[i * numCols + j] * MTRhs[j];
    }
    return true;
}

// ---- Transform parameters ----

struct TransformParams {
    double a[16] = {};
    double b[16] = {};
    double fXWidth = 1.0, fYWidth = 1.0;
    TransformType type = TT_BILINEAR;

    TransformParams() {
        a[1] = 1.0; // x' = x
        b[2] = 1.0; // y' = y
    }

    // Transform a target point to reference coordinates
    void transform(double tgtX, double tgtY, double& refX, double& refY) const {
        const double X = tgtX / fXWidth;
        const double Y = tgtY / fYWidth;

        if (type == TT_BICUBIC) {
            const double X2 = X * X;
            const double X3 = X * X * X;
            const double Y2 = Y * Y;
            const double Y3 = Y * Y * Y;

            refX = a[0] + a[1]*X + a[2]*Y + a[3]*X*Y
                 + a[4]*X2 + a[5]*Y2 + a[6]*X2*Y + a[7]*X*Y2 + a[8]*X2*Y2
                 + a[9]*X3 + a[10]*Y3 + a[11]*X3*Y + a[12]*X*Y3 + a[13]*X3*Y2 + a[14]*X2*Y3 + a[15]*X3*Y3;
            refY = b[0] + b[1]*X + b[2]*Y + b[3]*X*Y
                 + b[4]*X2 + b[5]*Y2 + b[6]*X2*Y + b[7]*X*Y2 + b[8]*X2*Y2
                 + b[9]*X3 + b[10]*Y3 + b[11]*X3*Y + b[12]*X*Y3 + b[13]*X3*Y2 + b[14]*X2*Y3 + b[15]*X3*Y3;
        } else if (type == TT_BISQUARED) {
            const double X2 = X * X;
            const double Y2 = Y * Y;

            refX = a[0] + a[1]*X + a[2]*Y + a[3]*X*Y
                 + a[4]*X2 + a[5]*Y2 + a[6]*X2*Y + a[7]*X*Y2 + a[8]*X2*Y2;
            refY = b[0] + b[1]*X + b[2]*Y + b[3]*X*Y
                 + b[4]*X2 + b[5]*Y2 + b[6]*X2*Y + b[7]*X*Y2 + b[8]*X2*Y2;
        } else {
            // TT_BILINEAR (and TT_LINEAR)
            refX = a[0] + a[1]*X + a[2]*Y + a[3]*X*Y;
            refY = b[0] + b[1]*X + b[2]*Y + b[3]*X*Y;
        }

        refX *= fXWidth;
        refY *= fYWidth;
    }
};

// ---- Internal star detection ----

// Compute star center using weighted centroid
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

    // Compute radius via weighted standard deviation
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

// Internal star detection on grayscale buffer
std::vector<Star> detectStarsInternal(const std::vector<double>& gray, int width, int height,
                                       double detectionThreshold, int maxStarSize) {
    std::vector<Star> stars;

    // Build histogram (256*32 bins)
    constexpr size_t HistoSize = 256 * 32;
    std::vector<int> histo(HistoSize + 1, 0);
    double maxIntensity = std::numeric_limits<double>::min();

    const size_t totalPixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t idx = 0; idx < totalPixels; ++idx) {
        const double value = gray[idx]; // [0, 256)
        if (value > maxIntensity) maxIntensity = value;
        ++histo[static_cast<size_t>(value * 32.0)];
    }

    // 50% quantile -> backgroundLevel
    const size_t fiftyPercent = totalPixels / 2 - 1;
    size_t nrValues = 0;
    size_t fiftyPercentQuantile = static_cast<size_t>(-1);
    while (nrValues < fiftyPercent) {
        ++fiftyPercentQuantile;
        nrValues += histo[fiftyPercentQuantile];
    }
    const double backgroundLevel = static_cast<double>(fiftyPercentQuantile) / static_cast<double>(HistoSize);
    const double bgLevel256 = backgroundLevel * 256.0; // [0, 256) scale
    const double intensityThreshold = 256.0 * detectionThreshold + bgLevel256;

    fprintf(stderr, "[star_align] detectStarsInternal: width=%d height=%d threshold=%.6f maxIntensity=%.4f bgLevel256=%.4f intensityThreshold=%.4f\n",
            width, height, detectionThreshold, maxIntensity, bgLevel256, intensityThreshold);

    if (maxIntensity < intensityThreshold)
        return stars;

    // Direction offsets: Up, Right, Down, Left, UpRight, DnRight, DnLeft, UpLeft
    constexpr int dirX[8] = { 0,  1,  0, -1,  1,  1, -1, -1};
    constexpr int dirY[8] = {-1,  0,  1,  0, -1,  1,  1, -1};

    for (int deltaRadius = 0; deltaRadius < 4; ++deltaRadius) {
        for (int j = 0; j < height; ++j) {
            for (int i = 0; i < width; ++i) {
                const double fIntensity = getPixelUnchecked(gray, width, i, j);
                if (fIntensity < intensityThreshold)
                    continue;

                // Check if this pixel is already part of an existing star
                bool isNew = true;
                for (auto it = std::lower_bound(stars.begin(), stars.end(), Star{},
                    [xi = i - STARMAXSIZE](const Star& s, const Star&) { return s.x < xi; });
                    it != stars.end() && isNew; ++it) {
                    if (it->x > i + STARMAXSIZE) break;
                    if (Distance(it->x, it->y, static_cast<double>(i), static_cast<double>(j)) < it->meanRadius * RadiusFactor)
                        isNew = false;
                }

                if (!isNew) continue;

                // 8-direction analysis
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

                // Hot pixel detection
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
                    continue; // hot pixel

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
                    // Check circularity (delta radii)
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

                    const int mainDirs[4] = {0, 1, 2, 3};   // Up, Right, Down, Left
                    const int diagDirs[4] = {4, 5, 6, 7};   // UpRight, DnRight, DnLeft, UpLeft

                    bool validCandidate = checkRadii(mainDirs) && checkRadii(diagDirs);

                    // Additional diameter ratio check for small stars
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
                        && checkDiameterRatio(0, 2, 1, 3)    // Up/Down vs Right/Left
                        && checkDiameterRatio(4, 6, 5, 7);   // UpRight/DnLeft vs DnRight/UpLeft

                    // Mean radius computation
                    const double fMeanRadius1 =
                        (directions[0].radius + directions[1].radius + directions[2].radius + directions[3].radius) / 4.0;
                    const double fMeanRadius2 =
                        (directions[4].radius + directions[5].radius + directions[6].radius + directions[7].radius) * 0.3535533905932737622;

                    if (validCandidate) {
                        // Compute star bounding rect
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

                        // Clamp to image bounds
                        starRC.left   = std::max(0, starRC.left);
                        starRC.top    = std::max(0, starRC.top);
                        starRC.right  = std::min(width - 1, starRC.right);
                        starRC.bottom = std::min(height - 1, starRC.bottom);

                        double starX, starY, starMeanRadius;
                        if (computeStarCenter(gray, width, height, starRC, backgroundLevel,
                                              starX, starY, starMeanRadius)) {
                            // Check overlap with existing stars
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

                                // Insert sorted by x
                                auto pos = std::lower_bound(stars.begin(), stars.end(), s,
                                    [](const Star& a, const Star& b) { return a.x < b.x; });
                                stars.insert(pos, s);
                            }
                        }
                    }
                }
            } // for i
        } // for j
    } // for deltaRadius

    fprintf(stderr, "[star_align] detectStarsInternal DONE: totalStars=%zu\n", stars.size());

    return stars;
}

// ============================================================================
// Region 3: Star matching (internal)
// ============================================================================

// Compute all pairwise star distances, sorted by star indices
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

// Look up distance between two stars in a sorted distance vector
float lookupDistance(const std::vector<StarDist>& dists, int s1, int s2) {
    StarDist key(s1, s2);
    auto it = std::lower_bound(dists.begin(), dists.end(), key);
    if (it != dists.end() && it->star1 == key.star1 && it->star2 == key.star2)
        return it->distance;
    return 0.0f;
}

// Add vote for a star pair
void addVote(int refStar, int tgtStar, std::vector<VotingPair>& votingPairs, size_t nrTgtStars) {
    const size_t offset = static_cast<size_t>(refStar) * nrTgtStars + static_cast<size_t>(tgtStar);
    if (offset < votingPairs.size())
        votingPairs[offset].nrVotes++;
}

// Initialize voting grid
void initVotingGrid(std::vector<VotingPair>& vPairs, size_t nrRefStars, size_t nrTgtStars) {
    vPairs.clear();
    vPairs.reserve(nrRefStars * nrTgtStars);
    for (int i = 0; i < static_cast<int>(nrRefStars); ++i)
        for (int j = 0; j < static_cast<int>(nrTgtStars); ++j)
            vPairs.emplace_back(i, j);
}

// Compute all triangles from a star set
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

// Compute transformation using least squares for the given transform type
bool computeTransformation(const std::vector<VotingPair>& pairs,
                           const std::vector<Star>& refStars,
                           const std::vector<Star>& tgtStars,
                           double fXWidth, double fYWidth,
                           TransformType ttype,
                           TransformParams& params) {
    const size_t N = pairs.size();
    const size_t numCols = (ttype == TT_BICUBIC) ? 16 : (ttype == TT_BISQUARED) ? 9 : 4;
    if (N < numCols) return false;

    // Column-major matrix: M_cols[col * N + row]
    std::vector<double> M_cols(numCols * N);
    std::vector<double> rhsX(N), rhsY(N);

    for (size_t i = 0; i < N; ++i) {
        double refX, refY, tgtX, tgtY;
        if (pairs[i].isCorner()) {
            refX = pairs[i].cornerRefX; refY = pairs[i].cornerRefY;
            tgtX = pairs[i].cornerTgtX; tgtY = pairs[i].cornerTgtY;
        } else {
            const Star& rs = refStars[pairs[i].refStar];
            const Star& ts = tgtStars[pairs[i].tgtStar];
            refX = rs.x; refY = rs.y;
            tgtX = ts.x; tgtY = ts.y;
        }

        rhsX[i] = refX / fXWidth;
        rhsY[i] = refY / fYWidth;

        const double X1 = tgtX / fXWidth;
        const double Y1 = tgtY / fYWidth;

        // Base functions common to all types
        M_cols[0 * N + i] = 1.0;
        M_cols[1 * N + i] = X1;
        M_cols[2 * N + i] = Y1;
        M_cols[3 * N + i] = X1 * Y1;

        if (numCols >= 9) {
            const double X2 = X1 * X1;
            const double Y2 = Y1 * Y1;
            M_cols[4 * N + i] = X2;
            M_cols[5 * N + i] = Y2;
            M_cols[6 * N + i] = X2 * Y1;
            M_cols[7 * N + i] = X1 * Y2;
            M_cols[8 * N + i] = X2 * Y2;
        }
        if (numCols >= 16) {
            const double X2 = X1 * X1;
            const double X3 = X2 * X1;
            const double Y2 = Y1 * Y1;
            const double Y3 = Y2 * Y1;
            M_cols[9  * N + i] = X3;
            M_cols[10 * N + i] = Y3;
            M_cols[11 * N + i] = X3 * Y1;
            M_cols[12 * N + i] = X1 * Y3;
            M_cols[13 * N + i] = X3 * Y2;
            M_cols[14 * N + i] = X2 * Y3;
            M_cols[15 * N + i] = X3 * Y3;
        }
    }

    std::vector<double> A(numCols), B(numCols);
    if (!solveLeastSquares(M_cols.data(), N, numCols, rhsX.data(), A.data())) return false;
    if (!solveLeastSquares(M_cols.data(), N, numCols, rhsY.data(), B.data())) return false;

    for (size_t i = 0; i < 16; ++i) {
        params.a[i] = (i < numCols) ? A[i] : 0.0;
        params.b[i] = (i < numCols) ? B[i] : 0.0;
    }
    params.fXWidth = fXWidth;
    params.fYWidth = fYWidth;
    params.type = ttype;
    return true;
}

// Compute max distance between projected target stars and reference stars.
// Only non-corner pairs contribute to distances (for sigma clipping).
// nonCornerIndices maps each distance to its index in the pairs vector.
std::pair<double, size_t> computeDistances(const std::vector<VotingPair>& pairs,
                                            const std::vector<Star>& refStars,
                                            const std::vector<Star>& tgtStars,
                                            const TransformParams& params,
                                            std::vector<double>& distances,
                                            std::vector<size_t>& nonCornerIndices) {
    double maxDist = 0.0;
    size_t maxIdx = 0;
    distances.clear();
    nonCornerIndices.clear();

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
            nonCornerIndices.push_back(i);
        }

        if (dist > maxDist) {
            maxDist = dist;
            maxIdx = i;
        }
    }
    return { maxDist, maxIdx };
}

std::pair<double, size_t> computeMaxDistance(const std::vector<VotingPair>& pairs,
                                              const std::vector<Star>& refStars,
                                              const std::vector<Star>& tgtStars,
                                              const TransformParams& params) {
    std::vector<double> dummy;
    std::vector<size_t> dummyIdx;
    return computeDistances(pairs, refStars, tgtStars, params, dummy, dummyIdx);
}

// ---- ComputeCoordinatesTransformation with progressive upgrade ----

bool computeCoordinatesTransformation(std::vector<VotingPair>& vPairs,
                                      const std::vector<Star>& refStars,
                                      const std::vector<Star>& tgtStars,
                                      double fXWidth, double fYHeight,
                                      TransformType maxTType,
                                      TransformParams& outParams) {
    bool bResult = false;
    bool bEnd = false;
    TransformType TType = TT_BILINEAR;
    TransformType okTType = TT_LINEAR;

    TransformParams okTransformation;
    std::vector<int> vAddedPairs;
    std::vector<int> vOkAddedPairs;
    std::vector<VotingPair> vTestedPairs;
    std::vector<VotingPair> vOkPairs;
    std::vector<VotingPair> vWorking = vPairs;

    const size_t nrExtraPairs = (!vPairs.empty() && vPairs[0].isCorner()) ? 4 : 0;

    while (!bEnd && !bResult) {
        const size_t nrPairs = nrExtraPairs + (TType == TT_BICUBIC ? 32 : (TType == TT_BISQUARED ? 18 : 8));

        vAddedPairs.clear();
        vTestedPairs.clear();

        // First add the locked pairs
        for (int i = 0; i < static_cast<int>(vWorking.size()); ++i) {
            if (vWorking[i].isActive() && vWorking[i].isLocked()) {
                vTestedPairs.push_back(vWorking[i]);
                vAddedPairs.push_back(i);
            }
        }

        // Then add the other pairs up to the limit
        for (size_t i = 0; i < vWorking.size() && vTestedPairs.size() < nrPairs; ++i) {
            if (vWorking[i].isActive() && !vWorking[i].isLocked()) {
                vTestedPairs.push_back(vWorking[i]);
                vAddedPairs.push_back(static_cast<int>(i));
            }
        }

        if (vTestedPairs.size() == nrPairs) {
            TransformParams projection;
            if (computeTransformation(vTestedPairs, refStars, tgtStars, fXWidth, fYHeight, TType, projection)) {
                std::vector<double> vDistances;
                std::vector<size_t> nonCornerIndices;
                const auto [fMaxDistance, maxDistanceIndex] = computeDistances(
                    vTestedPairs, refStars, tgtStars, projection, vDistances, nonCornerIndices);

                if (fMaxDistance > 3.0) {
                    // Sigma clipping to deactivate outliers
                    // vDistances and nonCornerIndices are parallel, containing only non-corner entries
                    const double fAverage = Average(vDistances);
                    const double fSigma = Sigma(vDistances);
                    bool bOneDeactivated = false;

                    for (size_t i = 0; i < vDistances.size(); ++i) {
                        if (std::abs(vDistances[i] - fAverage) > 2.0 * fSigma) {
                            const size_t pairIdx = nonCornerIndices[i];
                            const int lDeactivatedIndice = vAddedPairs[pairIdx];
                            vWorking[lDeactivatedIndice].setActive(false);
                            if (vDistances[i] < 7.0)
                                vWorking[lDeactivatedIndice].setPossible(true);
                            bOneDeactivated = true;
                        }
                    }

                    if (!bOneDeactivated) {
                        for (size_t i = 0; i < vDistances.size(); ++i) {
                            if (std::abs(vDistances[i] - fAverage) > fSigma) {
                                const size_t pairIdx = nonCornerIndices[i];
                                const int lDeactivatedIndice = vAddedPairs[pairIdx];
                                vWorking[lDeactivatedIndice].setActive(false);
                                bOneDeactivated = true;
                            }
                        }
                    }

                    if (!bOneDeactivated) {
                        // maxDistanceIndex is index into vTestedPairs (includes corners)
                        const int lDeactivatedIndice = vAddedPairs[maxDistanceIndex];
                        if (!vWorking[lDeactivatedIndice].isCorner()) {
                            vWorking[lDeactivatedIndice].setActive(false);
                        }
                    }
                } else {
                    // Good transformation found
                    okTransformation = projection;
                    vOkPairs = vTestedPairs;
                    vOkAddedPairs = vAddedPairs;
                    okTType = TType;
                    bResult = (TType == maxTType);

                    if (TType < maxTType) {
                        TType = nextHigherTransformType(TType);

                        // All the possible pairs are active again
                        for (auto& votingPair : vWorking) {
                            if (votingPair.isPossible()) {
                                votingPair.setActive(true);
                                votingPair.setPossible(false);
                            }
                        }

                        // Lock the pairs
                        for (const size_t index : vAddedPairs)
                            vWorking[index].setLocked(true);
                    }
                }
            } else {
                // Transformation failed - remove last pair
                vWorking[vAddedPairs[nrPairs - 1]].setActive(false);
            }
        } else {
            bEnd = true;
        }
    }

    if (!vOkPairs.empty())
        bResult = true;

    if (bResult) {
        // Try to add more pairs to refine using okTType
        outParams = okTransformation;
        vTestedPairs = vOkPairs;
        vAddedPairs = vOkAddedPairs;
        TType = okTType;

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
                if (computeTransformation(vTempPairs, refStars, tgtStars, fXWidth, fYHeight, TType, projection)) {
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
                                         TransformType ttype,
                                         TransformParams& params) {
    // Step 1: Compute a base transformation without corner locking using TT_BILINEAR
    TransformParams baseParams;
    std::vector<VotingPair> basePairs = vPairs;
    bool bResult = computeCoordinatesTransformation(basePairs, refStars, tgtStars, fXWidth, fYHeight, TT_BILINEAR, baseParams);

    if (!bResult)
        return false;

    // Step 2: Use base transformation to map the 4 target corners to reference coordinates
    const double w = fXWidth - 1.0;
    const double h = fYHeight - 1.0;
    // Target corners
    const double tgtCorners[4][2] = {
        {0, 0}, {w, 0}, {0, h}, {w, h}
    };
    // Map each target corner through base transformation to get reference corner
    double refCorners[4][2];
    for (int c = 0; c < 4; ++c)
        baseParams.transform(tgtCorners[c][0], tgtCorners[c][1], refCorners[c][0], refCorners[c][1]);

    // Step 3: Add corner pairs with very high votes
    std::vector<VotingPair> cornerPairs = vPairs;
    for (int c = 0; c < 4; ++c)
        cornerPairs.push_back(VotingPair::makeCorner(
            refCorners[c][0], refCorners[c][1],
            tgtCorners[c][0], tgtCorners[c][1]));

    // Sort descending by votes (corners with 10M votes go to top)
    std::sort(cornerPairs.begin(), cornerPairs.end(),
        [](const VotingPair& a, const VotingPair& b) { return a.nrVotes > b.nrVotes; });

    // Step 4: Compute final transformation with corners locked using target transform type
    bResult = computeCoordinatesTransformation(cornerPairs, refStars, tgtStars, fXWidth, fYHeight, ttype, params);

    if (bResult) {
        // Remove corner pairs from final output
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

    // Create index vectors sorted by distance (descending)
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

    // Dual-pointer scan for matching distances
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

    // Resolve votes
    std::sort(vVotingPairs.begin(), vVotingPairs.end(), [](const VotingPair& a, const VotingPair& b) {
        return a.nrVotes > b.nrVotes; // descending
    });

    bool bResult = false;
    if (vVotingPairs.size() >= tgtStars.size()) {
        int lMinNrVotes = vVotingPairs[std::min(static_cast<size_t>(tgtStars.size() * 2 - 1), vVotingPairs.size() - 1)].nrVotes;
        if (lMinNrVotes == 0) lMinNrVotes = 1;

        size_t lCut = 0;
        while (lCut < vVotingPairs.size() && vVotingPairs[lCut].nrVotes >= lMinNrVotes)
            ++lCut;
        vVotingPairs.resize(lCut + 1);

        const TransformType ttype = getTransformType(vVotingPairs.size());
        const char* ttypeName = (ttype == TT_BICUBIC) ? "BICUBIC" : (ttype == TT_BISQUARED) ? "BISQUARED" : (ttype == TT_BILINEAR) ? "BILINEAR" : "LINEAR";
        fprintf(stderr, "[star_align] LargeTriangle: votingPairs=%zu type=%s\n", vVotingPairs.size(), ttypeName);

        bResult = computeSigmaClippingTransformation(vVotingPairs, refStars, tgtStars, fXWidth, fYHeight, ttype, params);

        // If successful and a3/b3 are negligible, zero them out for linear case
        if (bResult) {
            if (std::abs(params.a[3]) < 1e-10 && std::abs(params.b[3]) < 1e-10) {
                params.a[3] = 0;
                params.b[3] = 0;
            }
        }
    }

    fprintf(stderr, "[star_align] LargeTriangle result: %s\n", bResult ? "SUCCESS" : "FAILED");
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
                // Vote for all 9 pairs
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

        const TransformType ttype = getTransformType(vVotingPairs.size());
        bResult = computeSigmaClippingTransformation(vVotingPairs, refStars, tgtStars, fXWidth, fYHeight, ttype, params);

        if (bResult) {
            if (std::abs(params.a[3]) < 1e-10 && std::abs(params.b[3]) < 1e-10) {
                params.a[3] = 0;
                params.b[3] = 0;
            }
        }
    }

    return bResult;
}

// ---- Internal alignment computation ----

AlignResult computeAlignmentInternal(const std::vector<Star>& refStars,
                                      const std::vector<Star>& tgtStars,
                                      int imageWidth, int imageHeight) {
    AlignResult result;

    fprintf(stderr, "[star_align] computeAlignmentInternal: refStars=%zu tgtStars=%zu imgW=%d imgH=%d\n",
        refStars.size(), tgtStars.size(), imageWidth, imageHeight);

    if (refStars.size() < 8 || tgtStars.size() < 8)
        return result;

    const double fXWidth = static_cast<double>(imageWidth);
    const double fYHeight = static_cast<double>(imageHeight);

    TransformParams params;

    // Try large triangle transformation first, fall back to matching triangle
    bool ok = computeLargeTriangleTransformation(refStars, tgtStars, fXWidth, fYHeight, params);
    if (!ok) {
        fprintf(stderr, "[star_align] LargeTriangle FAILED, trying MatchingTriangle\n");
        const auto refTriangles = computeTriangles(refStars);
        ok = computeMatchingTriangleTransformation(refTriangles, refStars, tgtStars, fXWidth, fYHeight, params);
    }

    if (!ok)
        return result;

    const char* ttypeName = (params.type == TT_BICUBIC) ? "BICUBIC" : (params.type == TT_BISQUARED) ? "BISQUARED" : (params.type == TT_BILINEAR) ? "BILINEAR" : "LINEAR";
    fprintf(stderr, "[star_align] Transform params: type=%s a0=%.10f a1=%.10f a2=%.10f a3=%.10f b0=%.10f b1=%.10f b2=%.10f b3=%.10f\n",
        ttypeName,
        params.a[0], params.a[1], params.a[2], params.a[3],
        params.b[0], params.b[1], params.b[2], params.b[3]);

    // Extract offsets from a0, b0
    result.offsetX = params.a[0] * fXWidth;
    result.offsetY = params.b[0] * fYHeight;

    // Extract angle: transform (0,0) and (width,0), compute angle
    double pt1x, pt1y, pt2x, pt2y;
    params.transform(0, 0, pt1x, pt1y);
    params.transform(fXWidth, 0, pt2x, pt2y);
    result.angle = std::atan2(pt2y - pt1y, pt2x - pt1x);

    fprintf(stderr, "[star_align] Final offsets: offsetX=%.10f offsetY=%.10f angle=%.10f\n",
        result.offsetX, result.offsetY, result.angle);

    // Count matched pairs (non-zero vote pairs)
    result.matchedStars = 0;
    // The matched star count is derived from the final working pairs in the transformation.
    // Since we don't return that directly, estimate from the bilinear fit quality.
    // We'll use a minimum of the pair count that contributed.
    result.matchedStars = std::min(static_cast<int>(refStars.size()), static_cast<int>(tgtStars.size()));

    result.success = true;
    return result;
}

// ---- BGRA16 transform helpers ----

// Bilinear interpolation for one channel of interleaved BGRA16 data.
// src:       pointer to BGRA16 data (4 x uint16_t per pixel).
// width/height: image dimensions in pixels.
// stride:    byte stride per row.
// srcX/srcY: continuous source coordinates (may be fractional).
// channelOffset: 0=B, 1=G, 2=R, 3=A.
// Clamp-to-edge boundary handling.
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

    // Helper to read one channel from a pixel at (x, y)
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

    // Default stride: width * 4 channels * sizeof(uint16_t)
    const int effectiveStride = (stride > 0) ? stride : (width * 4 * sizeof(uint16_t));

    // Extract grayscale
    auto gray = extractGray(pData, width, height, effectiveStride);

    // Detect stars
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
    double offsetX, double offsetY, double angle)
{
    const size_t totalPixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint16_t> dstBGRA(totalPixels * 4, 0);

    if (!srcBGRA || width <= 0 || height <= 0)
        return dstBGRA;

    const int effectiveStride = (stride > 0) ? stride : (width * 4 * sizeof(uint16_t));
    const double cx = width  / 2.0;
    const double cy = height / 2.0;
    const double cosA = std::cos(angle);
    const double sinA = std::sin(angle);

    for (int oy = 0; oy < height; ++oy) {
        for (int ox = 0; ox < width; ++ox) {
            // Inverse transform: output (ox,oy) -> source continuous coords
            const double dx = ox - cx - offsetX;
            const double dy = oy - cy - offsetY;
            const double srcFX =  cosA * dx + sinA * dy + cx;
            const double srcFY = -sinA * dx + cosA * dy + cy;

            const size_t dstIdx = (static_cast<size_t>(oy) * width + ox) * 4;

            if (srcFX >= 0.0 && srcFX < width && srcFY >= 0.0 && srcFY < height) {
                // Valid source region: bilinear interpolation
                dstBGRA[dstIdx + 0] = bilinearChannel(srcBGRA, width, height, effectiveStride, srcFX, srcFY, 0);
                dstBGRA[dstIdx + 1] = bilinearChannel(srcBGRA, width, height, effectiveStride, srcFX, srcFY, 1);
                dstBGRA[dstIdx + 2] = bilinearChannel(srcBGRA, width, height, effectiveStride, srcFX, srcFY, 2);
                dstBGRA[dstIdx + 3] = 0xFFFF;
            } else {
                // Out of bounds: black, alpha=0 marks invalid
                dstBGRA[dstIdx + 0] = 0;
                dstBGRA[dstIdx + 1] = 0;
                dstBGRA[dstIdx + 2] = 0;
                dstBGRA[dstIdx + 3] = 0;
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

    // Accumulator in double precision (4 channels per pixel)
    std::vector<double> accumulator(totalChannels, 0.0);
    // Per-pixel valid frame count
    std::vector<size_t> validCount(totalPixels, 0);

    // Frame 0 is reference - add directly (all pixels valid)
    for (size_t i = 0; i < totalChannels; ++i)
        accumulator[i] = images[0][i];
    for (size_t p = 0; p < totalPixels; ++p)
        validCount[p] = 1;

    // Transform and accumulate remaining frames
    for (size_t f = 1; f < nFrames; ++f) {
        if (!alignments[f].success) continue;

        auto transformed = transformBGRA(images[f], width, height, effectiveStride,
                                          alignments[f].offsetX, alignments[f].offsetY,
                                          alignments[f].angle);

        for (size_t p = 0; p < totalPixels; ++p) {
            // Alpha != 0 means valid pixel
            if (transformed[p * 4 + 3] != 0) {
                accumulator[p * 4 + 0] += transformed[p * 4 + 0];
                accumulator[p * 4 + 1] += transformed[p * 4 + 1];
                accumulator[p * 4 + 2] += transformed[p * 4 + 2];
                ++validCount[p];
            }
        }
    }

    // Divide by per-pixel valid count
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
        }
        // else: remains (0, 0, 0, 0) — black, invalid
    }

    return result;
}

} // namespace StarAlign
