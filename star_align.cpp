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
constexpr int    MinPairsToBisquared = 25;
constexpr int    MinPairsToBicubic = 40;

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
// Uses HSL luminance formula (max+min)/2 to match DSS's GetLuminance().
std::vector<double> extractGray(const uint16_t* pData, int width, int height, int stride) {
    std::vector<double> gray(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (int row = 0; row < height; ++row) {
        const uint16_t* pRow = reinterpret_cast<const uint16_t*>(
            reinterpret_cast<const uint8_t*>(pData) + static_cast<size_t>(row) * stride);
        double* pOut = gray.data() + static_cast<size_t>(row) * width;
        for (int col = 0; col < width; ++col) {
            // BGRA16: B, G, R, A - 4 uint16_t per pixel
            const double B = pRow[col * 4 + 0];
            const double G = pRow[col * 4 + 1];
            const double R = pRow[col * 4 + 2];
            // HSL luminance: (max(R,G,B) + min(R,G,B)) / 2
            // Then scale from [0, 65535] to [0, 256) via / 256.0 / 256.0 = / 65536.0 * 256.0
            const double minVal = std::min({R, G, B});
            const double maxVal = std::max({R, G, B});
            pOut[col] = (maxVal + minVal) * (0.5 / 256.0 / 256.0) * 256.0;
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

// ---- Matrix inversion helpers ----

bool solveLinearSystem(std::vector<std::vector<double>>& A,
                       std::vector<double>& b,
                       std::vector<double>& x) {
    const size_t N = A.size();
    if (N == 0 || b.size() != N) return false;
    for (const auto& row : A)
        if (row.size() != N) return false;

    for (size_t col = 0; col < N; ++col) {
        size_t pivot = col;
        double maxAbs = std::abs(A[col][col]);
        for (size_t row = col + 1; row < N; ++row) {
            const double v = std::abs(A[row][col]);
            if (v > maxAbs) {
                maxAbs = v;
                pivot = row;
            }
        }
        if (maxAbs < 1e-12) return false;

        if (pivot != col) {
            std::swap(A[pivot], A[col]);
            std::swap(b[pivot], b[col]);
        }

        const double div = A[col][col];
        for (size_t j = col; j < N; ++j)
            A[col][j] /= div;
        b[col] /= div;

        for (size_t row = 0; row < N; ++row) {
            if (row == col) continue;
            const double factor = A[row][col];
            if (std::abs(factor) < 1e-20) continue;
            for (size_t j = col; j < N; ++j)
                A[row][j] -= factor * A[col][j];
            b[row] -= factor * b[col];
        }
    }

    x = b;
    return true;
}

bool solveLeastSquares(const std::vector<std::vector<double>>& Mcols,
                       const std::vector<double>& rhs,
                       std::vector<double>& result) {
    const size_t P = Mcols.size();
    if (P == 0) return false;
    const size_t N = rhs.size();
    for (const auto& col : Mcols)
        if (col.size() != N) return false;
    if (N < P) return false;

    std::vector<std::vector<double>> MTM(P, std::vector<double>(P, 0.0));
    std::vector<double> MTRhs(P, 0.0);

    for (size_t i = 0; i < P; ++i) {
        for (size_t j = i; j < P; ++j) {
            double sum = 0.0;
            for (size_t k = 0; k < N; ++k)
                sum += Mcols[i][k] * Mcols[j][k];
            MTM[i][j] = sum;
            MTM[j][i] = sum;
        }

        double sum = 0.0;
        for (size_t k = 0; k < N; ++k)
            sum += Mcols[i][k] * rhs[k];
        MTRhs[i] = sum;
    }

    return solveLinearSystem(MTM, MTRhs, result);
}

// Invert a 4x4 matrix using Gauss-Jordan elimination.
// mat is modified in-place. Returns true on success.
bool invert4x4(double mat[4][4]) {
    constexpr int N = 4;
    double inv[N][N] = {};
    for (int i = 0; i < N; ++i) inv[i][i] = 1.0;

    for (int col = 0; col < N; ++col) {
        // Find pivot
        int pivotRow = col;
        double pivotVal = std::abs(mat[col][col]);
        for (int row = col + 1; row < N; ++row) {
            if (std::abs(mat[row][col]) > pivotVal) {
                pivotVal = std::abs(mat[row][col]);
                pivotRow = row;
            }
        }
        if (pivotVal < 1e-12) return false;

        // Swap rows
        if (pivotRow != col) {
            for (int j = 0; j < N; ++j) {
                std::swap(mat[col][j], mat[pivotRow][j]);
                std::swap(inv[col][j], inv[pivotRow][j]);
            }
        }

        // Scale pivot row
        const double scale = mat[col][col];
        for (int j = 0; j < N; ++j) {
            mat[col][j] /= scale;
            inv[col][j] /= scale;
        }

        // Eliminate column
        for (int row = 0; row < N; ++row) {
            if (row == col) continue;
            const double factor = mat[row][col];
            for (int j = 0; j < N; ++j) {
                mat[row][j] -= factor * mat[col][j];
                inv[row][j] -= factor * inv[col][j];
            }
        }
    }
    // Copy result back
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            mat[i][j] = inv[i][j];
    return true;
}

// Solve least squares: (M^T M) result = M^T rhs, where M is Nx4.
// result is 4-element vector. Returns true on success.
bool solveLeastSquares4(const std::vector<double>& M_col0,
                        const std::vector<double>& M_col1,
                        const std::vector<double>& M_col2,
                        const std::vector<double>& M_col3,
                        const std::vector<double>& rhs,
                        double result[4]) {
    const size_t N = M_col0.size();

    // Compute M^T M (4x4 symmetric)
    double MTM[4][4] = {};
    const double* cols[4] = { M_col0.data(), M_col1.data(), M_col2.data(), M_col3.data() };
    for (int i = 0; i < 4; ++i) {
        for (int j = i; j < 4; ++j) {
            double sum = 0.0;
            for (size_t k = 0; k < N; ++k)
                sum += cols[i][k] * cols[j][k];
            MTM[i][j] = sum;
            MTM[j][i] = sum;
        }
    }

    // Compute M^T rhs (4x1)
    double MTRhs[4] = {};
    for (int i = 0; i < 4; ++i) {
        double sum = 0.0;
        for (size_t k = 0; k < N; ++k)
            sum += cols[i][k] * rhs[k];
        MTRhs[i] = sum;
    }

    // Invert M^T M
    if (!invert4x4(MTM)) return false;

    // result = (M^T M)^{-1} M^T rhs
    for (int i = 0; i < 4; ++i) {
        result[i] = 0.0;
        for (int j = 0; j < 4; ++j)
            result[i] += MTM[i][j] * MTRhs[j];
    }
    return true;
}

// ---- Bilinear parameters ----

struct BilinearParams {
    TransformType type = TransformType::Bilinear;

    double a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    double a4 = 0, a5 = 0, a6 = 0, a7 = 0, a8 = 0;
    double a9 = 0, a10 = 0, a11 = 0, a12 = 0, a13 = 0, a14 = 0, a15 = 0;
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0;
    double b4 = 0, b5 = 0, b6 = 0, b7 = 0, b8 = 0;
    double b9 = 0, b10 = 0, b11 = 0, b12 = 0, b13 = 0, b14 = 0, b15 = 0;
    double fXWidth = 1.0, fYWidth = 1.0;

    // Transform a target point to reference coordinates
    void transform(double tgtX, double tgtY, double& refX, double& refY) const {
        const double X = tgtX / fXWidth;
        const double Y = tgtY / fYWidth;

        double xn = 0.0;
        double yn = 0.0;

        if (type == TransformType::Bicubic) {
            const double X2 = X * X;
            const double X3 = X2 * X;
            const double Y2 = Y * Y;
            const double Y3 = Y2 * Y;

            xn = a0 + a1 * X + a2 * Y + a3 * X * Y
               + a4 * X2 + a5 * Y2 + a6 * X2 * Y + a7 * X * Y2 + a8 * X2 * Y2
               + a9 * X3 + a10 * Y3 + a11 * X3 * Y + a12 * X * Y3
               + a13 * X3 * Y2 + a14 * X2 * Y3 + a15 * X3 * Y3;

            yn = b0 + b1 * X + b2 * Y + b3 * X * Y
               + b4 * X2 + b5 * Y2 + b6 * X2 * Y + b7 * X * Y2 + b8 * X2 * Y2
               + b9 * X3 + b10 * Y3 + b11 * X3 * Y + b12 * X * Y3
               + b13 * X3 * Y2 + b14 * X2 * Y3 + b15 * X3 * Y3;
        } else if (type == TransformType::Bisquared) {
            const double X2 = X * X;
            const double Y2 = Y * Y;

            xn = a0 + a1 * X + a2 * Y + a3 * X * Y
               + a4 * X2 + a5 * Y2 + a6 * X2 * Y + a7 * X * Y2 + a8 * X2 * Y2;

            yn = b0 + b1 * X + b2 * Y + b3 * X * Y
               + b4 * X2 + b5 * Y2 + b6 * X2 * Y + b7 * X * Y2 + b8 * X2 * Y2;
        } else {
            xn = a0 + a1 * X + a2 * Y + a3 * X * Y;
            yn = b0 + b1 * X + b2 * Y + b3 * X * Y;
        }

        refX = xn * fXWidth;
        refY = yn * fYWidth;
    }
};

TransformType pickTransformationType(const size_t nrVotingPairs) {
    if (nrVotingPairs >= static_cast<size_t>(MinPairsToBicubic))
        return TransformType::Bicubic;
    if (nrVotingPairs >= static_cast<size_t>(MinPairsToBisquared))
        return TransformType::Bisquared;
    return TransformType::Bilinear;
}

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

// Compute transformation using bilinear least squares
bool computeTransformation(const std::vector<VotingPair>& pairs,
                           const std::vector<Star>& refStars,
                           const std::vector<Star>& tgtStars,
                           double fXWidth, double fYWidth,
                           TransformType type,
                           BilinearParams& params) {
    const size_t N = pairs.size();
    size_t nrParams = 4;
    if (type == TransformType::Bisquared) nrParams = 9;
    else if (type == TransformType::Bicubic) nrParams = 16;
    if (N < nrParams) return false;

    std::vector<std::vector<double>> M(nrParams, std::vector<double>(N, 0.0));
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

        M[0][i] = 1.0;
        M[1][i] = X1;
        M[2][i] = Y1;
        M[3][i] = X1 * Y1;

        if (nrParams >= 9) {
            const double X2 = X1 * X1;
            const double Y2 = Y1 * Y1;
            M[4][i] = X2;
            M[5][i] = Y2;
            M[6][i] = X2 * Y1;
            M[7][i] = X1 * Y2;
            M[8][i] = X2 * Y2;

            if (nrParams >= 16) {
                const double X3 = X2 * X1;
                const double Y3 = Y2 * Y1;
                M[9][i] = X3;
                M[10][i] = Y3;
                M[11][i] = X3 * Y1;
                M[12][i] = X1 * Y3;
                M[13][i] = X3 * Y2;
                M[14][i] = X2 * Y3;
                M[15][i] = X3 * Y3;
            }
        }
    }

    std::vector<double> A, B;
    if (!solveLeastSquares(M, rhsX, A)) return false;
    if (!solveLeastSquares(M, rhsY, B)) return false;

    params.type = type;
    params.a0 = A[0]; params.a1 = A[1]; params.a2 = A[2]; params.a3 = A[3];
    params.b0 = B[0]; params.b1 = B[1]; params.b2 = B[2]; params.b3 = B[3];
    params.a4 = params.a5 = params.a6 = params.a7 = params.a8 = 0.0;
    params.b4 = params.b5 = params.b6 = params.b7 = params.b8 = 0.0;
    params.a9 = params.a10 = params.a11 = params.a12 = params.a13 = params.a14 = params.a15 = 0.0;
    params.b9 = params.b10 = params.b11 = params.b12 = params.b13 = params.b14 = params.b15 = 0.0;

    if (nrParams >= 9) {
        params.a4 = A[4]; params.a5 = A[5]; params.a6 = A[6]; params.a7 = A[7]; params.a8 = A[8];
        params.b4 = B[4]; params.b5 = B[5]; params.b6 = B[6]; params.b7 = B[7]; params.b8 = B[8];
    }
    if (nrParams >= 16) {
        params.a9 = A[9]; params.a10 = A[10]; params.a11 = A[11]; params.a12 = A[12];
        params.a13 = A[13]; params.a14 = A[14]; params.a15 = A[15];
        params.b9 = B[9]; params.b10 = B[10]; params.b11 = B[11]; params.b12 = B[12];
        params.b13 = B[13]; params.b14 = B[14]; params.b15 = B[15];
    }

    params.fXWidth = fXWidth;
    params.fYWidth = fYWidth;
    return true;
}

// Compute max distance between projected target stars and reference stars
std::pair<double, size_t> computeDistances(const std::vector<VotingPair>& pairs,
                                            const std::vector<Star>& refStars,
                                            const std::vector<Star>& tgtStars,
                                            const BilinearParams& params,
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
        distances.push_back(dist);
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
                                              const BilinearParams& params) {
    std::vector<double> dummy;
    return computeDistances(pairs, refStars, tgtStars, params, dummy);
}

// ---- ComputeCoordinatesTransformation (simplified) ----

bool computeCoordinatesTransformation(std::vector<VotingPair>& vPairs,
                                      const std::vector<Star>& refStars,
                                      const std::vector<Star>& tgtStars,
                                      double fXWidth, double fYHeight,
                                      TransformType type,
                                      BilinearParams& outParams) {
    bool bResult = false;
    bool bEnd = false;
    BilinearParams okTransformation;
    std::vector<int> vAddedPairs;
    std::vector<int> vOkAddedPairs;
    std::vector<VotingPair> vTestedPairs;
    std::vector<VotingPair> vOkPairs;
    std::vector<VotingPair> vWorking = vPairs;

    while (!bEnd && !bResult) {
        size_t nrPairs = 8;
        if (type == TransformType::Bisquared) nrPairs = 9;
        else if (type == TransformType::Bicubic) nrPairs = 16;

        vAddedPairs.clear();
        vTestedPairs.clear();

        // Add locked pairs first
        for (int idx = 0; idx < static_cast<int>(vWorking.size()); ++idx) {
            if (vWorking[idx].isActive() && vWorking[idx].isLocked()) {
                vTestedPairs.push_back(vWorking[idx]);
                vAddedPairs.push_back(idx);
            }
        }

        // Add other active pairs up to limit
        for (size_t i = 0; i < vWorking.size() && vTestedPairs.size() < nrPairs; ++i) {
            if (vWorking[i].isActive() && !vWorking[i].isLocked()) {
                vTestedPairs.push_back(vWorking[i]);
                vAddedPairs.push_back(static_cast<int>(i));
            }
        }

        if (vTestedPairs.size() == nrPairs) {
            BilinearParams projection;
            if (computeTransformation(vTestedPairs, refStars, tgtStars, fXWidth, fYHeight, type, projection)) {
                std::vector<double> vDistances;
                const auto [fMaxDistance, maxDistanceIndex] = computeDistances(
                    vTestedPairs, refStars, tgtStars, projection, vDistances);

                if (fMaxDistance > 3.0) {
                    // Sigma clipping to deactivate outliers
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
                    // Good transformation found
                    okTransformation = projection;
                    vOkPairs = vTestedPairs;
                    vOkAddedPairs = vAddedPairs;
                    bResult = true;
                }
            } else {
                // Transformation failed - remove last pair
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
        // Try to add more pairs to refine
        outParams = okTransformation;
        vTestedPairs = vOkPairs;
        vAddedPairs = vOkAddedPairs;

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
                BilinearParams projection;
                if (computeTransformation(vTempPairs, refStars, tgtStars, fXWidth, fYHeight, type, projection)) {
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
                                         TransformType type,
                                         BilinearParams& params) {
    // Step 1: Compute a base transformation without corner locking
    BilinearParams baseParams;
    std::vector<VotingPair> basePairs = vPairs;
    bool bResult = computeCoordinatesTransformation(basePairs, refStars, tgtStars, fXWidth, fYHeight,
                                                    TransformType::Bilinear, baseParams);

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

    // Step 4: Compute final transformation with corners locked
    bResult = computeCoordinatesTransformation(cornerPairs, refStars, tgtStars, fXWidth, fYHeight,
                                               type, params);

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
                                         int* outMatchedPairs,
                                         BilinearParams& params) {
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
        vVotingPairs.resize(lCut);

        const TransformType type = pickTransformationType(vVotingPairs.size());
        bResult = computeSigmaClippingTransformation(vVotingPairs, refStars, tgtStars, fXWidth, fYHeight,
                                                     type, params);

        if (bResult && outMatchedPairs)
            *outMatchedPairs = static_cast<int>(vVotingPairs.size());

        // If successful and a3/b3 are negligible, zero them out for linear case
        if (bResult) {
            if (std::abs(params.a3) < 1e-10 && std::abs(params.b3) < 1e-10) {
                params.a3 = 0;
                params.b3 = 0;
            }
        }
    }

    return bResult;
}

// ---- ComputeMatchingTriangleTransformation ----

bool computeMatchingTriangleTransformation(const std::vector<StarTriangle>& refTriangles,
                                             const std::vector<Star>& refStars,
                                             const std::vector<Star>& tgtStars,
                                             double fXWidth, double fYHeight,
                                             int* outMatchedPairs,
                                             BilinearParams& params) {
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

        const TransformType type = pickTransformationType(vVotingPairs.size());
        bResult = computeSigmaClippingTransformation(vVotingPairs, refStars, tgtStars, fXWidth, fYHeight,
                                                     type, params);

        if (bResult && outMatchedPairs)
            *outMatchedPairs = static_cast<int>(vVotingPairs.size());

        if (bResult) {
            if (std::abs(params.a3) < 1e-10 && std::abs(params.b3) < 1e-10) {
                params.a3 = 0;
                params.b3 = 0;
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

    if (refStars.size() < 8 || tgtStars.size() < 8)
        return result;

    const double fXWidth = static_cast<double>(imageWidth);
    const double fYHeight = static_cast<double>(imageHeight);

    BilinearParams params;
    int matchedPairs = 0;

    // Try large triangle transformation first, fall back to matching triangle
    bool ok = computeLargeTriangleTransformation(refStars, tgtStars, fXWidth, fYHeight, &matchedPairs, params);
    if (!ok) {
        const auto refTriangles = computeTriangles(refStars);
        ok = computeMatchingTriangleTransformation(refTriangles, refStars, tgtStars, fXWidth, fYHeight,
                                                   &matchedPairs, params);
    }

    if (!ok)
        return result;

    result.type = params.type;

    // Store full bilinear transform parameters
    result.a0 = params.a0;
    result.a1 = params.a1;
    result.a2 = params.a2;
    result.a3 = params.a3;
    result.a4 = params.a4;
    result.a5 = params.a5;
    result.a6 = params.a6;
    result.a7 = params.a7;
    result.a8 = params.a8;
    result.a9 = params.a9;
    result.a10 = params.a10;
    result.a11 = params.a11;
    result.a12 = params.a12;
    result.a13 = params.a13;
    result.a14 = params.a14;
    result.a15 = params.a15;
    result.b0 = params.b0;
    result.b1 = params.b1;
    result.b2 = params.b2;
    result.b3 = params.b3;
    result.b4 = params.b4;
    result.b5 = params.b5;
    result.b6 = params.b6;
    result.b7 = params.b7;
    result.b8 = params.b8;
    result.b9 = params.b9;
    result.b10 = params.b10;
    result.b11 = params.b11;
    result.b12 = params.b12;
    result.b13 = params.b13;
    result.b14 = params.b14;
    result.b15 = params.b15;

    // Extract offsets from a0, b0
    result.offsetX = params.a0 * fXWidth;
    result.offsetY = params.b0 * fYHeight;

    // Extract angle: transform (0,0) and (width,0), compute angle
    double pt1x, pt1y, pt2x, pt2y;
    params.transform(0, 0, pt1x, pt1y);
    params.transform(fXWidth, 0, pt2x, pt2y);
    result.angle = std::atan2(pt2y - pt1y, pt2x - pt1x);

    result.matchedStars = matchedPairs;

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

// Catmull-Rom bicubic interpolation for one channel of interleaved BGRA16 data.
inline uint16_t bicubicChannel(const uint16_t* src, int width, int height, int stride,
                                double srcX, double srcY, int channelOffset) {
    srcX = std::max(0.0, std::min(static_cast<double>(width - 1), srcX));
    srcY = std::max(0.0, std::min(static_cast<double>(height - 1), srcY));

    const int x0 = static_cast<int>(std::floor(srcX));
    const int y0 = static_cast<int>(std::floor(srcY));
    const double fx = srcX - x0;
    const double fy = srcY - y0;

    // Catmull-Rom weights
    auto catmullRom = [](double t) -> double {
        // w(t) = 0.5 * (|2t|^3 - |2t|^2 - |t| + 1)  for |t| <= 1
        //        0.5 * (-|2t-3|^3 + 5|2t-3|^2 - 8|2t-3| + 4) for 1 < |t| <= 2
        // But more standard formulation:
        // w(t) for t in {-1, 0, 1, 2} offsets
        const double t2 = t * t;
        const double t3 = t2 * t;
        if (t <= 1.0)
            return 1.5 * t3 - 2.5 * t2 + 1.0;
        else
            return -0.5 * t3 + 2.5 * t2 - 4.0 * t + 2.0;
    };

    auto readCh = [&](int x, int y) -> double {
        if (x < 0) x = 0;
        if (x >= width) x = width - 1;
        if (y < 0) y = 0;
        if (y >= height) y = height - 1;
        const uint16_t* pRow = reinterpret_cast<const uint16_t*>(
            reinterpret_cast<const uint8_t*>(src) + static_cast<size_t>(y) * stride);
        return pRow[x * 4 + channelOffset];
    };

    double val = 0.0;
    for (int j = -1; j <= 2; ++j) {
        const double wy = catmullRom(std::abs(fy - j));
        if (wy == 0.0) continue;
        const int yy = y0 + j;
        for (int i = -1; i <= 2; ++i) {
            const double wx = catmullRom(std::abs(fx - i));
            if (wx == 0.0) continue;
            val += readCh(x0 + i, yy) * wx * wy;
        }
    }

    return static_cast<uint16_t>(std::min(65535.0, std::max(0.0, std::round(val))));
}

// Build a 2x3 affine matrix from BilinearParams and invert it.
// The bilinear transform maps tgt -> ref:
//   refX = (a0 + a1*(tgtX/W) + a2*(tgtY/H) + a3*(tgtX/W)*(tgtY/H)) * W
//   refY = (b0 + b1*(tgtX/W) + b2*(tgtY/H) + b3*(tgtX/W)*(tgtY/H)) * H
//
// For small rotations this is nearly affine. We evaluate the Jacobian at the
// image center to get a local affine approximation, then invert it for
// inverse mapping in transformBGRA.
//
// The affine matrix maps normalized tgt coords to normalized ref coords:
//   [refXn]   [m00 m01] [tgtXn]   [tx]
//   [refYn] = [m10 m11] [tgtYn] + [ty]
//
// The inverse maps ref coords -> tgt coords.
struct AffineInverse {
    // Inverse affine: tgtXn = inv00 * refXn + inv01 * refYn + invtx
    //                 tgtYn = inv10 * refXn + inv11 * refYn + invty
    double inv00, inv01, invtx;
    double inv10, inv11, invty;
};

// Evaluate forward transform (tgt normalized -> ref normalized) and Jacobian.
inline void evalTransformAndJacobian(const AlignResult& align, double X, double Y,
                                     double& refXn, double& refYn,
                                     double& j00, double& j01, double& j10, double& j11)
{
    const double X2 = X * X;
    const double Y2 = Y * Y;
    const double X3 = X2 * X;
    const double Y3 = Y2 * Y;

    if (align.type == TransformType::Bicubic) {
        refXn = align.a0 + align.a1 * X + align.a2 * Y + align.a3 * X * Y
              + align.a4 * X2 + align.a5 * Y2 + align.a6 * X2 * Y + align.a7 * X * Y2 + align.a8 * X2 * Y2
              + align.a9 * X3 + align.a10 * Y3 + align.a11 * X3 * Y + align.a12 * X * Y3
              + align.a13 * X3 * Y2 + align.a14 * X2 * Y3 + align.a15 * X3 * Y3;

        refYn = align.b0 + align.b1 * X + align.b2 * Y + align.b3 * X * Y
              + align.b4 * X2 + align.b5 * Y2 + align.b6 * X2 * Y + align.b7 * X * Y2 + align.b8 * X2 * Y2
              + align.b9 * X3 + align.b10 * Y3 + align.b11 * X3 * Y + align.b12 * X * Y3
              + align.b13 * X3 * Y2 + align.b14 * X2 * Y3 + align.b15 * X3 * Y3;

        j00 = align.a1 + align.a3 * Y + 2.0 * align.a4 * X + 2.0 * align.a6 * X * Y + align.a7 * Y2
            + 2.0 * align.a8 * X * Y2 + 3.0 * align.a9 * X2 + 3.0 * align.a11 * X2 * Y + align.a12 * Y3
            + 3.0 * align.a13 * X2 * Y2 + 2.0 * align.a14 * X * Y3 + 3.0 * align.a15 * X2 * Y3;
        j01 = align.a2 + align.a3 * X + 2.0 * align.a5 * Y + align.a6 * X2 + 2.0 * align.a7 * X * Y
            + 2.0 * align.a8 * X2 * Y + 3.0 * align.a10 * Y2 + align.a11 * X3 + 3.0 * align.a12 * X * Y2
            + 2.0 * align.a13 * X3 * Y + 3.0 * align.a14 * X2 * Y2 + 3.0 * align.a15 * X3 * Y2;
        j10 = align.b1 + align.b3 * Y + 2.0 * align.b4 * X + 2.0 * align.b6 * X * Y + align.b7 * Y2
            + 2.0 * align.b8 * X * Y2 + 3.0 * align.b9 * X2 + 3.0 * align.b11 * X2 * Y + align.b12 * Y3
            + 3.0 * align.b13 * X2 * Y2 + 2.0 * align.b14 * X * Y3 + 3.0 * align.b15 * X2 * Y3;
        j11 = align.b2 + align.b3 * X + 2.0 * align.b5 * Y + align.b6 * X2 + 2.0 * align.b7 * X * Y
            + 2.0 * align.b8 * X2 * Y + 3.0 * align.b10 * Y2 + align.b11 * X3 + 3.0 * align.b12 * X * Y2
            + 2.0 * align.b13 * X3 * Y + 3.0 * align.b14 * X2 * Y2 + 3.0 * align.b15 * X3 * Y2;
        return;
    }

    if (align.type == TransformType::Bisquared) {
        refXn = align.a0 + align.a1 * X + align.a2 * Y + align.a3 * X * Y
              + align.a4 * X2 + align.a5 * Y2 + align.a6 * X2 * Y + align.a7 * X * Y2 + align.a8 * X2 * Y2;
        refYn = align.b0 + align.b1 * X + align.b2 * Y + align.b3 * X * Y
              + align.b4 * X2 + align.b5 * Y2 + align.b6 * X2 * Y + align.b7 * X * Y2 + align.b8 * X2 * Y2;

        j00 = align.a1 + align.a3 * Y + 2.0 * align.a4 * X + 2.0 * align.a6 * X * Y + align.a7 * Y2 + 2.0 * align.a8 * X * Y2;
        j01 = align.a2 + align.a3 * X + 2.0 * align.a5 * Y + align.a6 * X2 + 2.0 * align.a7 * X * Y + 2.0 * align.a8 * X2 * Y;
        j10 = align.b1 + align.b3 * Y + 2.0 * align.b4 * X + 2.0 * align.b6 * X * Y + align.b7 * Y2 + 2.0 * align.b8 * X * Y2;
        j11 = align.b2 + align.b3 * X + 2.0 * align.b5 * Y + align.b6 * X2 + 2.0 * align.b7 * X * Y + 2.0 * align.b8 * X2 * Y;
        return;
    }

    refXn = align.a0 + align.a1 * X + align.a2 * Y + align.a3 * X * Y;
    refYn = align.b0 + align.b1 * X + align.b2 * Y + align.b3 * X * Y;

    j00 = align.a1 + align.a3 * Y;
    j01 = align.a2 + align.a3 * X;
    j10 = align.b1 + align.b3 * Y;
    j11 = align.b2 + align.b3 * X;
}

bool invertTransformNewton(const AlignResult& align,
                          double refXn, double refYn,
                          double seedXn, double seedYn,
                          double& outXn, double& outYn)
{
    double x = seedXn;
    double y = seedYn;

    for (int it = 0; it < 8; ++it) {
        double fx = 0.0, fy = 0.0;
        double j00 = 0.0, j01 = 0.0, j10 = 0.0, j11 = 0.0;
        evalTransformAndJacobian(align, x, y, fx, fy, j00, j01, j10, j11);

        const double ex = fx - refXn;
        const double ey = fy - refYn;
        if (std::abs(ex) + std::abs(ey) < 1e-9) {
            outXn = x;
            outYn = y;
            return true;
        }

        const double det = j00 * j11 - j01 * j10;
        if (std::abs(det) < 1e-12) {
            break;
        }

        // Newton step: [dx dy]^T = J^-1 * e
        const double invDet = 1.0 / det;
        const double dx = ( j11 * ex - j01 * ey) * invDet;
        const double dy = (-j10 * ex + j00 * ey) * invDet;

        x -= dx;
        y -= dy;

        if (std::abs(dx) + std::abs(dy) < 1e-10) {
            outXn = x;
            outYn = y;
            return true;
        }
    }

    // Final residual check for fallback acceptance.
    double fx = 0.0, fy = 0.0;
    double j00 = 0.0, j01 = 0.0, j10 = 0.0, j11 = 0.0;
    evalTransformAndJacobian(align, x, y, fx, fy, j00, j01, j10, j11);
    const double ex = std::abs(fx - refXn);
    const double ey = std::abs(fy - refYn);

    outXn = x;
    outYn = y;
    return (ex + ey) < 1e-5;
}

AffineInverse computeAffineInverse(const AlignResult& align, double W, double H) {
    // Evaluate bilinear at center to get translation
    const double cXn = 0.5; // center X normalized
    const double cYn = 0.5; // center Y normalized

    const double X = cXn;
    const double Y = cYn;
    const double X2 = X * X;
    const double Y2 = Y * Y;
    const double X3 = X2 * X;
    const double Y3 = Y2 * Y;

    double refCXn = 0.0;
    double refCYn = 0.0;
    double m00 = 0.0, m01 = 0.0, m10 = 0.0, m11 = 0.0;

    if (align.type == TransformType::Bicubic) {
        refCXn = align.a0 + align.a1 * X + align.a2 * Y + align.a3 * X * Y
               + align.a4 * X2 + align.a5 * Y2 + align.a6 * X2 * Y + align.a7 * X * Y2 + align.a8 * X2 * Y2
               + align.a9 * X3 + align.a10 * Y3 + align.a11 * X3 * Y + align.a12 * X * Y3
               + align.a13 * X3 * Y2 + align.a14 * X2 * Y3 + align.a15 * X3 * Y3;

        refCYn = align.b0 + align.b1 * X + align.b2 * Y + align.b3 * X * Y
               + align.b4 * X2 + align.b5 * Y2 + align.b6 * X2 * Y + align.b7 * X * Y2 + align.b8 * X2 * Y2
               + align.b9 * X3 + align.b10 * Y3 + align.b11 * X3 * Y + align.b12 * X * Y3
               + align.b13 * X3 * Y2 + align.b14 * X2 * Y3 + align.b15 * X3 * Y3;

        m00 = align.a1 + align.a3 * Y + 2.0 * align.a4 * X + 2.0 * align.a6 * X * Y + align.a7 * Y2
            + 2.0 * align.a8 * X * Y2 + 3.0 * align.a9 * X2 + 3.0 * align.a11 * X2 * Y + align.a12 * Y3
            + 3.0 * align.a13 * X2 * Y2 + 2.0 * align.a14 * X * Y3 + 3.0 * align.a15 * X2 * Y3;
        m01 = align.a2 + align.a3 * X + 2.0 * align.a5 * Y + align.a6 * X2 + 2.0 * align.a7 * X * Y
            + 2.0 * align.a8 * X2 * Y + 3.0 * align.a10 * Y2 + align.a11 * X3 + 3.0 * align.a12 * X * Y2
            + 2.0 * align.a13 * X3 * Y + 3.0 * align.a14 * X2 * Y2 + 3.0 * align.a15 * X3 * Y2;
        m10 = align.b1 + align.b3 * Y + 2.0 * align.b4 * X + 2.0 * align.b6 * X * Y + align.b7 * Y2
            + 2.0 * align.b8 * X * Y2 + 3.0 * align.b9 * X2 + 3.0 * align.b11 * X2 * Y + align.b12 * Y3
            + 3.0 * align.b13 * X2 * Y2 + 2.0 * align.b14 * X * Y3 + 3.0 * align.b15 * X2 * Y3;
        m11 = align.b2 + align.b3 * X + 2.0 * align.b5 * Y + align.b6 * X2 + 2.0 * align.b7 * X * Y
            + 2.0 * align.b8 * X2 * Y + 3.0 * align.b10 * Y2 + align.b11 * X3 + 3.0 * align.b12 * X * Y2
            + 2.0 * align.b13 * X3 * Y + 3.0 * align.b14 * X2 * Y2 + 3.0 * align.b15 * X3 * Y2;
    } else if (align.type == TransformType::Bisquared) {
        refCXn = align.a0 + align.a1 * X + align.a2 * Y + align.a3 * X * Y
               + align.a4 * X2 + align.a5 * Y2 + align.a6 * X2 * Y + align.a7 * X * Y2 + align.a8 * X2 * Y2;
        refCYn = align.b0 + align.b1 * X + align.b2 * Y + align.b3 * X * Y
               + align.b4 * X2 + align.b5 * Y2 + align.b6 * X2 * Y + align.b7 * X * Y2 + align.b8 * X2 * Y2;

        m00 = align.a1 + align.a3 * Y + 2.0 * align.a4 * X + 2.0 * align.a6 * X * Y + align.a7 * Y2 + 2.0 * align.a8 * X * Y2;
        m01 = align.a2 + align.a3 * X + 2.0 * align.a5 * Y + align.a6 * X2 + 2.0 * align.a7 * X * Y + 2.0 * align.a8 * X2 * Y;
        m10 = align.b1 + align.b3 * Y + 2.0 * align.b4 * X + 2.0 * align.b6 * X * Y + align.b7 * Y2 + 2.0 * align.b8 * X * Y2;
        m11 = align.b2 + align.b3 * X + 2.0 * align.b5 * Y + align.b6 * X2 + 2.0 * align.b7 * X * Y + 2.0 * align.b8 * X2 * Y;
    } else {
        refCXn = align.a0 + align.a1 * X + align.a2 * Y + align.a3 * X * Y;
        refCYn = align.b0 + align.b1 * X + align.b2 * Y + align.b3 * X * Y;

        m00 = align.a1 + align.a3 * Y;
        m01 = align.a2 + align.a3 * X;
        m10 = align.b1 + align.b3 * Y;
        m11 = align.b2 + align.b3 * X;
    }

    // Translation: ref_center - M * tgt_center
    const double tx = refCXn - (m00 * cXn + m01 * cYn);
    const double ty = refCYn - (m10 * cXn + m11 * cYn);

    // Invert the 2x2 matrix
    const double det = m00 * m11 - m01 * m10;
    
    // Check for singular matrix (det near 0)
    // This can happen when transformation is nearly degenerate (scale=0 or extreme skew)
    const double absDet = std::fabs(det);
    const double detThreshold = 1e-10;  // Minimum determinant magnitude
    
    AffineInverse inv;
    if (absDet < detThreshold) {
        // Singular or near-singular matrix: return identity transform
        // This will map output coordinates straight to themselves, preserving image as-is
        inv.inv00 = 1.0; inv.inv01 = 0.0; inv.invtx = 0.0;
        inv.inv10 = 0.0; inv.inv11 = 1.0; inv.invty = 0.0;
    } else {
        const double invDet = 1.0 / det;
        inv.inv00 =  m11 * invDet;
        inv.inv01 = -m01 * invDet;
        inv.inv10 = -m10 * invDet;
        inv.inv11 =  m00 * invDet;
    }

    // Inverse translation: invtx = -invM * t
    inv.invtx = -(inv.inv00 * tx + inv.inv01 * ty);
    inv.invty = -(inv.inv10 * tx + inv.inv11 * ty);

    return inv;
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

    if (params.autoThreshold) {
        // DSS-style auto-threshold: iteratively adjust threshold to find ~targetStarCount stars.
        // DSS starts at 65% for the first image, uses previous threshold for subsequent images,
        // and adjusts using an exponential function.
        constexpr double MinThreshold = 0.00075; // 0.075% - DSS minimum
        constexpr double MaxThreshold = 0.65;    // DSS starts at 65% for first image
        double threshold = MaxThreshold;
        const int target = params.targetStarCount;

        // First pass with high threshold
        auto stars = detectStarsInternal(gray, width, height, threshold, params.maxStarSize);

        // Iteratively lower threshold until we get enough stars
        for (int iteration = 0; iteration < 20 && static_cast<int>(stars.size()) < target; ++iteration) {
            if (threshold <= MinThreshold)
                break;
            // Exponential decrease, similar to DSS
            threshold *= 0.6;
            threshold = std::max(threshold, MinThreshold);
            stars = detectStarsInternal(gray, width, height, threshold, params.maxStarSize);
        }

        // If too many stars (>3x target), raise threshold slightly
        if (static_cast<int>(stars.size()) > target * 3) {
            threshold *= 1.3;
            stars = detectStarsInternal(gray, width, height, threshold, params.maxStarSize);
        }

        return stars;
    }

    // Fixed threshold mode
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
    const AlignResult& align)
{
    const size_t totalPixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint16_t> dstBGRA(totalPixels * 4, 0);

    if (!srcBGRA || width <= 0 || height <= 0 || !align.success)
        return dstBGRA;

    const int effectiveStride = (stride > 0) ? stride : (width * 4 * sizeof(uint16_t));
    const double W = static_cast<double>(width);
    const double H = static_cast<double>(height);

    // Affine inverse is only used as an initial seed for per-pixel Newton inversion.
    const AffineInverse inv = computeAffineInverse(align, W, H);

    for (int oy = 0; oy < height; ++oy) {
        for (int ox = 0; ox < width; ++ox) {
            // Normalize output coords to [0, 1]
            const double refXn = static_cast<double>(ox) / W;
            const double refYn = static_cast<double>(oy) / H;
            const size_t dstIdx = (static_cast<size_t>(oy) * width + ox) * 4;

            // Initial guess from global affine approximation.
            const double seedXn = inv.inv00 * refXn + inv.inv01 * refYn + inv.invtx;
            const double seedYn = inv.inv10 * refXn + inv.inv11 * refYn + inv.invty;

            // Refine inverse mapping using full transform (DSS-like inverse solve behavior).
            double tgtXn = seedXn;
            double tgtYn = seedYn;
            const bool solved = invertTransformNewton(align, refXn, refYn, seedXn, seedYn, tgtXn, tgtYn);
            if (!solved) {
                dstBGRA[dstIdx + 0] = 0;
                dstBGRA[dstIdx + 1] = 0;
                dstBGRA[dstIdx + 2] = 0;
                dstBGRA[dstIdx + 3] = 0;
                continue;
            }

            // Back to pixel coords
            const double srcFX = tgtXn * W;
            const double srcFY = tgtYn * H;

            if (srcFX >= 0.0 && srcFX < W && srcFY >= 0.0 && srcFY < H) {
                // Bicubic interpolation
                dstBGRA[dstIdx + 0] = bicubicChannel(srcBGRA, width, height, effectiveStride, srcFX, srcFY, 0);
                dstBGRA[dstIdx + 1] = bicubicChannel(srcBGRA, width, height, effectiveStride, srcFX, srcFY, 1);
                dstBGRA[dstIdx + 2] = bicubicChannel(srcBGRA, width, height, effectiveStride, srcFX, srcFY, 2);
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
    // Validate input parameters
    const size_t nFrames = images.size();
    if (nFrames == 0 || alignments.size() != nFrames) {
        return {};  // Invalid input: return empty
    }
    
    if (width <= 0 || height <= 0) {
        return {};  // Invalid dimensions: return empty
    }
    
    // Check for null pointers in images array
    for (const auto* pImg : images) {
        if (!pImg) return {};  // Null pointer detected
    }

    const size_t totalChannels = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;

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
                                          alignments[f]);

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
