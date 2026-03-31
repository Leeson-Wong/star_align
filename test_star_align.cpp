// test_star_align.cpp
// Test program for star_align module using real DNG images
// No external dependencies - only C++ standard library

#include "star_align.h"
#include "demosaic_bggr.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <iomanip>
#include <filesystem>
#include <chrono>
#include <sstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace fs = std::filesystem;

// ============================================================================
// Minimal DNG Parser (extracted from raw2png.cpp, no OpenCV/color correction)
// ============================================================================

struct DNGImage {
    std::vector<uint16_t> rawPixels;  // Bayer raw data
    int width = 0;
    int height = 0;
    int bitsPerSample = 0;
    int cfaPattern[4] = {0};  // Bayer pattern
};

static uint16_t dngTypeSizes[] = {
    0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8, 4, 1, 1, 8, 8, 8
};

class DNGReader {
    std::vector<uint8_t> fileData;
    bool bigEndian = false;

    uint16_t read16(size_t offset) const {
        if (bigEndian)
            return (fileData[offset] << 8) | fileData[offset + 1];
        return fileData[offset] | (fileData[offset + 1] << 8);
    }

    uint32_t read32(size_t offset) const {
        if (bigEndian)
            return (fileData[offset] << 24) | (fileData[offset + 1] << 16) |
                   (fileData[offset + 2] << 8) | fileData[offset + 3];
        return fileData[offset] | (fileData[offset + 1] << 8) |
               (fileData[offset + 2] << 16) | (fileData[offset + 3] << 24);
    }

    void parseMainIFD(uint32_t offset, uint32_t& subIFDOffset) {
        size_t pos = offset;
        uint16_t numEntries = read16(pos);
        pos += 2;
        for (int i = 0; i < numEntries; i++) {
            uint16_t tag = read16(pos);
            // uint16_t type = read16(pos + 2);
            // uint32_t count = read32(pos + 4);
            uint32_t value = read32(pos + 8);
            if (tag == 330) {  // SubIFDs
                subIFDOffset = value;
                break;
            }
            pos += 12;
        }
    }

    bool parseSubIFD(uint32_t offset, DNGImage& img) {
        size_t pos = offset;
        uint16_t numEntries = read16(pos);
        pos += 2;

        int rawOffset = 0;

        for (int i = 0; i < numEntries; i++) {
            uint16_t tag = read16(pos);
            uint16_t type = read16(pos + 2);
            uint32_t count = read32(pos + 4);

            size_t dataSize = count * dngTypeSizes[type];
            std::vector<uint8_t> data;
            if (dataSize <= 4) {
                data.assign(fileData.begin() + pos + 8,
                            fileData.begin() + pos + 8 + dataSize);
            } else {
                uint32_t dataOffset = read32(pos + 8);
                data.assign(fileData.begin() + dataOffset,
                            fileData.begin() + dataOffset + dataSize);
            }

            switch (tag) {
                case 256:  // ImageWidth
                    img.width = static_cast<int>(read32(pos + 8));
                    break;
                case 257:  // ImageLength
                    img.height = static_cast<int>(read32(pos + 8));
                    break;
                case 258:  // BitsPerSample
                    if (count == 1)
                        img.bitsPerSample = read16(pos + 8);
                    break;
                case 273:  // StripOffsets
                    rawOffset = static_cast<int>(read32(pos + 8));
                    break;
                case 33422:  // CFAPattern
                    for (int j = 0; j < 4 && j < static_cast<int>(count); j++)
                        img.cfaPattern[j] = data[j];
                    break;
                default:
                    break;
            }
            pos += 12;
        }

        // Read RAW pixel data
        if (img.width > 0 && img.height > 0 && rawOffset > 0) {
            size_t pixelCount = static_cast<size_t>(img.width) * img.height;
            img.rawPixels.resize(pixelCount);

            if (img.bitsPerSample == 16) {
                for (size_t i = 0; i < pixelCount; i++) {
                    size_t p = rawOffset + i * 2;
                    img.rawPixels[i] = fileData[p] | (fileData[p + 1] << 8);
                }
            } else if (img.bitsPerSample == 12) {
                size_t p = rawOffset;
                for (size_t i = 0; i < pixelCount; i += 2) {
                    uint32_t v = fileData[p] | (fileData[p+1] << 8) | (fileData[p+2] << 16);
                    img.rawPixels[i] = v & 0xFFF;
                    if (i + 1 < pixelCount)
                        img.rawPixels[i+1] = (v >> 12) & 0xFFF;
                    p += 3;
                }
            } else if (img.bitsPerSample == 14) {
                // 14-bit stored as 16-bit (LSB-aligned)
                for (size_t i = 0; i < pixelCount; i++) {
                    size_t p = rawOffset + i * 2;
                    img.rawPixels[i] = fileData[p] | (fileData[p + 1] << 8);
                }
            } else {
                std::cerr << "Error: Unsupported bits per sample: " << img.bitsPerSample << std::endl;
                return false;
            }
        }

        return true;
    }

public:
    bool load(const std::string& path, DNGImage& img) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot open " << path << std::endl;
            return false;
        }

        file.seekg(0, std::ios::end);
        size_t fileSize = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        fileData.resize(fileSize);
        file.read(reinterpret_cast<char*>(fileData.data()), fileSize);
        file.close();

        // Parse TIFF header
        if (fileData[0] == 'I' && fileData[1] == 'I')
            bigEndian = false;
        else if (fileData[0] == 'M' && fileData[1] == 'M')
            bigEndian = true;
        else {
            std::cerr << "Error: Invalid byte order in " << path << std::endl;
            return false;
        }

        uint16_t magic = read16(2);
        if (magic != 42) {
            std::cerr << "Error: Invalid TIFF magic in " << path << std::endl;
            return false;
        }

        uint32_t firstIFD = read32(4);
        uint32_t subIFDOffset = 0;
        parseMainIFD(firstIFD, subIFDOffset);

        if (subIFDOffset == 0) {
            std::cerr << "Error: No SubIFD found in " << path << std::endl;
            return false;
        }

        if (!parseSubIFD(subIFDOffset, img)) {
            std::cerr << "Error: Failed to parse SubIFD in " << path << std::endl;
            return false;
        }

        return true;
    }
};

// ============================================================================
// Bayer (BGGR) -> BGRA16 conversion
// ============================================================================

// Estimate actual bit depth from the data (DNG may report 16-bit but values
// are 12-bit or 14-bit).
static int estimateBitDepth(const std::vector<uint16_t>& pixels) {
    uint16_t maxVal = 0;
    for (auto v : pixels)
        maxVal = std::max(maxVal, v);
    // Find the smallest power of 2 that covers maxVal
    int bits = 1;
    while ((1 << bits) <= maxVal && bits < 16)
        bits++;
    return bits;
}

// ============================================================================
// LibRaw-style processing parameters
// ============================================================================

struct LibRawParams {
    int    output_bps    = 16;
    int    no_auto_bright = 0;
    int    use_camera_wb = 1;
    int    use_auto_wb   = 1;
    int    user_sat      = 4095;
    int    user_black    = 256;
    int    exp_correc    = 1;
    double exp_shift     = 1.20;
    double gamm[2]       = {2.2, 4.5};
};

// ============================================================================
// Auto white balance
// ============================================================================

// Compute auto white balance multipliers from linear RGB16 data.
// Samples the central region to avoid vignetting bias.
static void computeAutoWB(const uint16_t* rgb16, int width, int height,
                          double& rMul, double& gMul, double& bMul) {
    // Sample central 50% of image
    int x0 = width / 4, x1 = width * 3 / 4;
    int y0 = height / 4, y1 = height * 3 / 4;
    double sumR = 0, sumG = 0, sumB = 0;
    size_t count = 0;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            size_t idx = (static_cast<size_t>(y) * width + x) * 3;
            sumR += rgb16[idx + 0];
            sumG += rgb16[idx + 1];
            sumB += rgb16[idx + 2];
            ++count;
        }
    }
    if (count == 0) { rMul = gMul = bMul = 1.0; return; }
    double avgR = sumR / count;
    double avgG = sumG / count;
    double avgB = sumB / count;
    // Normalize so green multiplier = 1.0
    gMul = 1.0;
    rMul = (avgR > 0) ? avgG / avgR : 1.0;
    bMul = (avgB > 0) ? avgG / avgB : 1.0;
}

// ============================================================================
// LibRaw gamma curve
// ============================================================================

// Build a 65536-entry lookup table for gamma correction.
// LibRaw/dcraw style: toe slope for shadows, power curve for highlights.
// gamm[0] = power, gamm[1] = toe slope
static std::vector<uint16_t> buildGammaLUT(double gamma_power, double gamma_slope) {
    std::vector<uint16_t> lut(65536);
    for (int i = 0; i < 65536; i++) {
        double x = i / 65535.0;
        double y;
        if (gamma_slope > 0 && x < 1.0 / gamma_slope)
            y = gamma_slope * x;
        else
            y = std::pow(x, 1.0 / gamma_power);
        y = std::max(0.0, std::min(1.0, y));
        lut[i] = static_cast<uint16_t>(std::round(y * 65535.0));
    }
    return lut;
}

// ============================================================================
// Bayer (BGGR) -> BGRA16 with LibRaw-style processing
// ============================================================================

// Convert DNG raw Bayer pixels to BGRA16 with full LibRaw-style processing:
//   1. Demosaic (bilinear interpolation)
//   2. Black level subtraction
//   3. Auto white balance
//   4. Exposure correction
//   5. Auto brightness normalization
//   6. Gamma correction
// Output: BGRA16 (B, G, R, A interleaved, 4 x uint16_t per pixel)
std::vector<uint16_t> bayerToBGRA16(const DNGImage& img, const LibRawParams& params = {}) {
    int effectiveBits = estimateBitDepth(img.rawPixels);

    // Demosaic BGGR -> RGB16 (linear, scaled to 16-bit by shift)
    uint16_t* rgb16 = Demosaic::bggrToRGB16_Fast(
        img.rawPixels.data(), img.width, img.height, effectiveBits);
    if (!rgb16) {
        std::cerr << "Error: Demosaic failed" << std::endl;
        return {};
    }

    const int width = img.width;
    const int height = img.height;
    const size_t pixelCount = static_cast<size_t>(width) * height;

    // The demosaic output is in 16-bit space: raw_value << (16 - effectiveBits).
    // Convert black/sat from raw space to 16-bit space.
    const int shift = 16 - effectiveBits;
    const double black16 = static_cast<double>(params.user_black << shift);
    const double sat16   = static_cast<double>(params.user_sat   << shift);
    const double range   = sat16 - black16;
    if (range <= 0) {
        delete[] rgb16;
        return {};
    }

    // Compute auto white balance multipliers from linear data
    double rMul = 1.0, gMul = 1.0, bMul = 1.0;
    if (params.use_auto_wb) {
        computeAutoWB(rgb16, width, height, rMul, gMul, bMul);
    }

    // Build gamma LUT
    auto gammaLUT = buildGammaLUT(params.gamm[0], params.gamm[1]);

    // First pass: compute auto brightness scale factor.
    // Find the maximum value after black subtract + WB + exposure,
    // then scale so the peak just reaches 1.0.
    double expShift = params.exp_correc ? params.exp_shift : 1.0;
    double maxLinear = 0.0;
    for (size_t i = 0; i < pixelCount; i++) {
        double r = (rgb16[i * 3 + 0] - black16) * rMul * expShift;
        double g = (rgb16[i * 3 + 1] - black16) * gMul * expShift;
        double b = (rgb16[i * 3 + 2] - black16) * bMul * expShift;
        maxLinear = std::max(maxLinear, std::max(r, std::max(g, b)));
    }
    double brightScale = (!params.no_auto_bright && maxLinear > 0)
                         ? 1.0 / maxLinear : 1.0 / range;

    // Second pass: apply full pipeline
    std::vector<uint16_t> bgra(pixelCount * 4);
    for (size_t i = 0; i < pixelCount; i++) {
        double r = (rgb16[i * 3 + 0] - black16) * rMul * expShift * brightScale;
        double g = (rgb16[i * 3 + 1] - black16) * gMul * expShift * brightScale;
        double b = (rgb16[i * 3 + 2] - black16) * bMul * expShift * brightScale;

        // Clamp to [0, 1] then apply gamma LUT
        auto applyLUT = [&](double v) -> uint16_t {
            int idx = static_cast<int>(std::round(std::max(0.0, std::min(1.0, v)) * 65535.0));
            return gammaLUT[idx];
        };

        bgra[i * 4 + 0] = applyLUT(b);   // B
        bgra[i * 4 + 1] = applyLUT(g);   // G
        bgra[i * 4 + 2] = applyLUT(r);   // R
        bgra[i * 4 + 3] = 0xFFFF;        // A
    }

    delete[] rgb16;
    return bgra;
}

// ============================================================================
// Utility
// ============================================================================

// Get CFA pattern as string
std::string cfaToString(const int cfa[4]) {
    const char* names[] = {"R", "G", "B", "??"};
    std::string result;
    for (int i = 0; i < 4; i++) {
        if (cfa[i] >= 0 && cfa[i] <= 2)
            result += names[cfa[i]];
        else
            result += "??";
    }
    return result;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <dng_directory> [--stack <output.raw>]" << std::endl;
        return 1;
    }

    std::string dngDir = argv[1];
    std::string stackOutputPath;

    // Parse --stack option
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--stack" && i + 1 < argc) {
            stackOutputPath = argv[i + 1];
            ++i;
        }
    }

    // Collect DNG files
    std::vector<fs::path> dngFiles;
    for (const auto& entry : fs::directory_iterator(dngDir)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".dng") {
                dngFiles.push_back(entry.path());
            }
        }
    }

    if (dngFiles.empty()) {
        std::cerr << "No DNG files found in " << dngDir << std::endl;
        return 1;
    }

    // Sort by filename
    std::sort(dngFiles.begin(), dngFiles.end());

    std::cout << "=== star_align test with real DNG images ===" << std::endl;
    std::cout << "Image dir: " << dngDir << std::endl;
    std::cout << "Found " << dngFiles.size() << " DNG files" << std::endl;
    std::cout << std::endl;

    // Process reference frame (first image)
    DNGReader reader;
    DNGImage refImg;
    std::cout << "--- Reference frame ---" << std::endl;
    if (!reader.load(dngFiles[0].string(), refImg)) {
        return 1;
    }
    std::cout << dngFiles[0].filename().string() << ": "
              << refImg.width << "x" << refImg.height << ", "
              << refImg.bitsPerSample << "-bit, "
              << cfaToString(refImg.cfaPattern) << std::endl;

    auto refBGRA = bayerToBGRA16(refImg);
    if (refBGRA.empty()) {
        return 1;
    }

    int stride = refImg.width * 4 * sizeof(uint16_t);

    StarAlign::DetectParams params;
    params.threshold = 0.32;

    // Helper: keep only the brightest N stars for alignment (too many stars
    // causes O(n^3) triangle computation to be extremely slow).
    auto keepBrightest = [](std::vector<StarAlign::Star>& stars, size_t maxCount) {
        if (stars.size() <= maxCount) return;
        std::partial_sort(stars.begin(), stars.begin() + maxCount, stars.end(),
            [](const StarAlign::Star& a, const StarAlign::Star& b) {
                return a.intensity > b.intensity;
            });
        stars.resize(maxCount);
        // Re-sort by x for the alignment algorithm
        std::sort(stars.begin(), stars.end(),
            [](const StarAlign::Star& a, const StarAlign::Star& b) { return a.x < b.x; });
    };

    constexpr size_t MaxStarsForAlignment = 100;

    auto refStars = StarAlign::detectStars(refBGRA.data(), refImg.width, refImg.height,
                                            stride, params);

    std::cout << "Detected " << refStars.size() << " stars (threshold="
              << params.threshold << ")" << std::endl;

    // Keep only brightest stars for alignment
    size_t origCount = refStars.size();
    keepBrightest(refStars, MaxStarsForAlignment);
    if (refStars.size() < origCount) {
        std::cout << "Using top " << refStars.size() << " brightest stars for alignment" << std::endl;
    }

    if (refStars.empty()) {
        std::cerr << "No stars detected in reference frame. Try adjusting threshold." << std::endl;
        return 1;
    }

    // Print top 5 stars
    int showCount = std::min(5, static_cast<int>(refStars.size()));
    std::cout << "Top " << showCount << " stars:" << std::endl;
    for (int i = 0; i < showCount; i++) {
        std::cout << "  #" << (i + 1) << ": ("
                  << std::fixed << std::setprecision(1) << refStars[i].x << ", "
                  << refStars[i].y << ") intensity="
                  << std::setprecision(3) << refStars[i].intensity
                  << " radius=" << std::setprecision(1) << refStars[i].meanRadius
                  << std::endl;
    }

    std::cout << std::endl;

    // Alignment results table
    std::cout << "--- Alignment results ---" << std::endl;

    // Print header
    std::cout << std::left << std::setw(40) << "File"
              << std::right << std::setw(7) << "Stars"
              << std::setw(5) << "OK"
              << std::setw(10) << "offsetX"
              << std::setw(10) << "offsetY"
              << std::setw(10) << "angle(\u00B0)"
              << std::setw(8) << "match"
              << std::endl;

    int successCount = 0;
    int totalCount = 0;
    double sumOffsetX = 0, sumOffsetY = 0, sumAngle = 0;

    // Collect alignment results for stacking (frame 0 = reference)
    std::vector<StarAlign::AlignResult> allAlignments(dngFiles.size());
    allAlignments[0].success = true;  // reference frame

    for (size_t fi = 1; fi < dngFiles.size(); fi++) {
        DNGImage tgtImg;
        if (!reader.load(dngFiles[fi].string(), tgtImg)) {
            std::cerr << "Failed to load " << dngFiles[fi].filename().string() << std::endl;
            continue;
        }

        auto tgtBGRA = bayerToBGRA16(tgtImg);
        if (tgtBGRA.empty()) {
            continue;
        }

        int tgtStride = tgtImg.width * 4 * sizeof(uint16_t);
        auto tgtStars = StarAlign::detectStars(tgtBGRA.data(), tgtImg.width, tgtImg.height,
                                                tgtStride, params);
        keepBrightest(tgtStars, MaxStarsForAlignment);

        auto result = StarAlign::computeAlignment(refStars, tgtStars,
                                                   tgtImg.width, tgtImg.height);

        allAlignments[fi] = result;

        totalCount++;
        if (result.success) {
            successCount++;
            sumOffsetX += result.offsetX;
            sumOffsetY += result.offsetY;
            sumAngle += result.angle;
        }

        std::string filename = dngFiles[fi].filename().string();
        // Truncate long filenames for display
        if (filename.length() > 38)
            filename = "..." + filename.substr(filename.length() - 35);

        std::cout << std::left << std::setw(40) << filename
                  << std::right << std::setw(7) << tgtStars.size()
                  << std::setw(5) << (result.success ? "yes" : "no");

        if (result.success) {
            std::cout << std::fixed << std::setprecision(2)
                      << std::setw(10) << result.offsetX
                      << std::setw(10) << result.offsetY
                      << std::setw(10) << (result.angle * 180.0 / M_PI)
                      << std::setw(8) << result.matchedStars;
        } else {
            std::cout << std::setw(10) << "-"
                      << std::setw(10) << "-"
                      << std::setw(10) << "-"
                      << std::setw(8) << "-";
        }
        std::cout << std::endl;
    }

    // Summary
    std::cout << std::endl;
    std::cout << "--- Summary ---" << std::endl;
    std::cout << "Aligned: " << successCount << "/" << totalCount << " successful" << std::endl;

    if (successCount > 0) {
        std::cout << std::fixed << std::setprecision(1);
        std::cout << "Average offset: ("
                  << (sumOffsetX / successCount) << ", "
                  << (sumOffsetY / successCount) << ") pixels" << std::endl;
        std::cout << "Average rotation: "
                  << (sumAngle / successCount * 180.0 / M_PI) << " degrees" << std::endl;
    }

    // -------- Stack mode --------
    if (!stackOutputPath.empty() && dngFiles.size() > 1) {
        std::cout << std::endl;
        std::cout << "--- Stacking BGRA16 images ---" << std::endl;

        // Collect BGRA16 data from all frames
        std::vector<const uint16_t*> bgraPtrs;
        std::vector<std::vector<uint16_t>> bgraStorage(dngFiles.size());

        for (size_t fi = 0; fi < dngFiles.size(); ++fi) {
            DNGImage img;
            if (!reader.load(dngFiles[fi].string(), img)) {
                std::cerr << "Failed to reload " << dngFiles[fi].filename().string() << " for stacking" << std::endl;
                continue;
            }
            bgraStorage[fi] = bayerToBGRA16(img);
            if (bgraStorage[fi].empty()) {
                std::cerr << "Failed to demosaic " << dngFiles[fi].filename().string() << std::endl;
                continue;
            }
            bgraPtrs.push_back(bgraStorage[fi].data());
        }

        if (bgraPtrs.size() == dngFiles.size()) {
            int stackStride = refImg.width * 4 * sizeof(uint16_t);
            auto stacked = StarAlign::stackBGRAImages(bgraPtrs, refImg.width, refImg.height,
                                                       stackStride, allAlignments);

            if (!stacked.empty()) {
                // Build output path with timestamp
                auto now = std::chrono::system_clock::now();
                auto time_t_now = std::chrono::system_clock::to_time_t(now);
                std::stringstream tsStream;
                tsStream << std::put_time(std::localtime(&time_t_now), "_%Y%m%d_%H%M%S");
                std::string outPath = stackOutputPath + tsStream.str() + ".raw";

                std::ofstream outFile(outPath, std::ios::binary);
                if (outFile.is_open()) {
                    outFile.write(reinterpret_cast<const char*>(stacked.data()),
                                  stacked.size() * sizeof(uint16_t));
                    outFile.close();
                    std::cout << "Stacked " << bgraPtrs.size() << " frames -> "
                              << outPath << " ("
                              << refImg.width << "x" << refImg.height << " BGRA16, "
                              << stacked.size() * sizeof(uint16_t) << " bytes)" << std::endl;
                } else {
                    std::cerr << "Error: Cannot write to " << outPath << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: Stacking failed" << std::endl;
                return 1;
            }
        } else {
            std::cerr << "Error: Could not reload all frames for stacking" << std::endl;
            return 1;
        }
    }

    return (successCount > 0) ? 0 : 1;
}
