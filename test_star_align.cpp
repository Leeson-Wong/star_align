// test_star_align.cpp
// Test program for star_align module using LibRaw to decode RAW images
// Supports DNG, NEF, CR2, ARW, ORF, RW2, PEF, and all LibRaw-supported formats

#include "star_align.h"
#include "libraw.h"

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
// LibRaw -> BGRA16 conversion
// ============================================================================

struct RawImage {
    std::vector<uint16_t> bgra;  // BGRA16 data
    int width = 0;
    int height = 0;
};

// Load a RAW file via LibRaw and convert to BGRA16.
// LibRaw handles: unpacking, demosaicing, white balance, exposure, gamma.
// Output: BGRA16 (B, G, R, A interleaved, 4 x uint16_t per pixel).
static bool loadRawToBGRA(const std::string& path, RawImage& out) {
    LibRaw lr;

    // Configure processing parameters to match DSS's linear-space processing.
    // DSS detects stars on linear (non-gamma-corrected) data, so we must
    // disable gamma, exposure boost, and auto brightness to stay in linear space.
    lr.imgdata.params.output_bps    = 16;       // 16-bit output
    lr.imgdata.params.use_camera_wb = 1;        // use camera WB if available
    lr.imgdata.params.use_auto_wb   = 0;        // no auto WB (DSS doesn't use it for detection)
    lr.imgdata.params.no_auto_bright = 1;       // disable auto brightness
    lr.imgdata.params.user_qual     = 3;        // AHD demosaic (best quality)
    lr.imgdata.params.output_color  = 1;        // sRGB
    lr.imgdata.params.gamm[0]       = 1.0;      // linear (no gamma)
    lr.imgdata.params.gamm[1]       = 1.0;      // linear toe slope
    lr.imgdata.params.exp_correc    = 0;        // disable exposure correction
    lr.imgdata.params.exp_shift     = 1.0;      // no exposure boost
    lr.imgdata.params.highlight     = 0;        // clip highlights
    lr.imgdata.params.fbdd_noiserd  = 0;        // disable FBDD noise reduction (preserve stars)

    // Open and unpack the RAW file
    int ret = lr.open_file(path.c_str());
    if (ret != LIBRAW_SUCCESS) {
        std::cerr << "Error: LibRaw cannot open " << path
                  << " (" << libraw_strerror(ret) << ")" << std::endl;
        return false;
    }

    ret = lr.unpack();
    if (ret != LIBRAW_SUCCESS) {
        std::cerr << "Error: LibRaw unpack failed for " << path
                  << " (" << libraw_strerror(ret) << ")" << std::endl;
        return false;
    }

    // Process: demosaic + WB + exposure + gamma + color conversion
    ret = lr.dcraw_process();
    if (ret != LIBRAW_SUCCESS) {
        std::cerr << "Error: LibRaw dcraw_process failed for " << path
                  << " (" << libraw_strerror(ret) << ")" << std::endl;
        return false;
    }

    // Get processed image
    int errcode = 0;
    libraw_processed_image_t* image = lr.dcraw_make_mem_image(&errcode);
    if (!image || errcode != LIBRAW_SUCCESS) {
        std::cerr << "Error: LibRaw make_mem_image failed for " << path << std::endl;
        return false;
    }

    out.width  = image->width;
    out.height = image->height;

    if (image->colors == 3 && image->bits == 16) {
        // LibRaw outputs RGB16 interleaved (R, G, B per pixel, 3 x uint16_t)
        const size_t pixelCount = static_cast<size_t>(out.width) * out.height;
        const uint16_t* rgb = reinterpret_cast<const uint16_t*>(image->data);
        out.bgra.resize(pixelCount * 4);
        for (size_t i = 0; i < pixelCount; i++) {
            out.bgra[i * 4 + 0] = rgb[i * 3 + 2];  // B
            out.bgra[i * 4 + 1] = rgb[i * 3 + 1];  // G
            out.bgra[i * 4 + 2] = rgb[i * 3 + 0];  // R
            out.bgra[i * 4 + 3] = 0xFFFF;            // A
        }
    } else if (image->colors == 3 && image->bits == 8) {
        // 8-bit output: scale up to 16-bit
        const size_t pixelCount = static_cast<size_t>(out.width) * out.height;
        const uint8_t* rgb8 = image->data;
        out.bgra.resize(pixelCount * 4);
        for (size_t i = 0; i < pixelCount; i++) {
            out.bgra[i * 4 + 0] = static_cast<uint16_t>(rgb8[i * 3 + 2]) << 8;  // B
            out.bgra[i * 4 + 1] = static_cast<uint16_t>(rgb8[i * 3 + 1]) << 8;  // G
            out.bgra[i * 4 + 2] = static_cast<uint16_t>(rgb8[i * 3 + 0]) << 8;  // R
            out.bgra[i * 4 + 3] = 0xFFFF;                                          // A
        }
    } else {
        std::cerr << "Error: Unexpected image format from LibRaw (colors="
                  << image->colors << ", bits=" << image->bits << ")" << std::endl;
        LibRaw::dcraw_clear_mem(image);
        return false;
    }

    LibRaw::dcraw_clear_mem(image);
    return true;
}

// ============================================================================
// Raw file extension filter
// ============================================================================

// ============================================================================
// Post-processing: WB correction, exposure, gamma
// ============================================================================

struct WBCoeffs {
    double r = 1.0;
    double g = 1.0;
    double b = 1.0;
};

// Load camera WB coefficients from a RAW file using LibRaw
static bool loadWBCoeffs(const std::string& path, WBCoeffs& wb) {
    LibRaw lr;
    lr.imgdata.params.output_bps = 16;
    int ret = lr.open_file(path.c_str());
    if (ret != LIBRAW_SUCCESS) return false;
    ret = lr.unpack();
    if (ret != LIBRAW_SUCCESS) return false;

    // pre_mul: camera white balance multipliers [R, G, B, G2]
    const float* pm = lr.imgdata.color.pre_mul;
    // Normalize to green channel
    if (pm[1] > 0) {
        wb.r = pm[0] / pm[1];
        wb.g = 1.0;
        wb.b = pm[2] / pm[1];
    }
    return true;
}

// Post-process stacked BGRA16 data:
// 1. Apply camera WB correction (using pre_mul ratios)
// 2. Apply exposure boost (exp_shift)
// 3. Apply gamma correction (gamma 2.2 with toe slope)
// 4. Auto brightness via histogram stretching
static void postProcessStack(std::vector<uint16_t>& bgra, int width, int height,
                              const WBCoeffs& wb, double exp_shift = 1.2,
                              double gamma = 2.2, double toe_slope = 4.5) {
    const size_t totalPixels = static_cast<size_t>(width) * height;

    // Pass 1: Apply WB + exposure, find max brightness for auto-stretch
    double maxVal = 0.0;
    std::vector<double> linear(totalPixels * 3); // R, G, B (non-interleaved for efficiency)

    for (size_t p = 0; p < totalPixels; ++p) {
        double B = bgra[p * 4 + 0];
        double G = bgra[p * 4 + 1];
        double R = bgra[p * 4 + 2];

        // Skip invalid pixels (alpha == 0)
        if (bgra[p * 4 + 3] == 0) {
            linear[p * 3 + 0] = 0;
            linear[p * 3 + 1] = 0;
            linear[p * 3 + 2] = 0;
            continue;
        }

        // WB correction
        R *= wb.r;
        G *= wb.g;
        B *= wb.b;

        // Exposure boost
        R *= exp_shift;
        G *= exp_shift;
        B *= exp_shift;

        linear[p * 3 + 0] = R;
        linear[p * 3 + 1] = G;
        linear[p * 3 + 2] = B;

        maxVal = std::max({maxVal, R, G, B});
    }

    // Auto brightness: find the 99.5th percentile to avoid outlier clipping
    if (maxVal > 0) {
        std::vector<double> allVals;
        allVals.reserve(totalPixels * 3);
        for (size_t p = 0; p < totalPixels; ++p) {
            if (bgra[p * 4 + 3] == 0) continue;
            allVals.push_back(linear[p * 3 + 0]);
            allVals.push_back(linear[p * 3 + 1]);
            allVals.push_back(linear[p * 3 + 2]);
        }
        if (!allVals.empty()) {
            size_t idx995 = static_cast<size_t>(allVals.size() * 0.995);
            std::nth_element(allVals.begin(), allVals.begin() + idx995, allVals.end());
            double brightTarget = allVals[idx995];
            if (brightTarget > 0) {
                double scale = 65535.0 / brightTarget;
                // Clamp scale to avoid extreme boosting
                scale = std::min(scale, 8.0);
                for (size_t i = 0; i < linear.size(); ++i) {
                    linear[i] *= scale;
                }
            }
        }
    }

    // Pass 2: Apply gamma and write back
    // Gamma with toe slope (similar to LibRaw's gamm[0]=2.2, gamm[1]=4.5):
    //   out = (1 + toe_slope) * pow(val/65535, 1/gamma) - toe_slope * (val/65535)
    const double invGamma = 1.0 / gamma;

    auto applyGamma = [invGamma, toe_slope](double v) -> uint16_t {
        double n = std::min(v, 65535.0) / 65535.0;
        double g = (1.0 + toe_slope) * std::pow(n, invGamma) - toe_slope * n;
        g = std::max(0.0, std::min(1.0, g));
        return static_cast<uint16_t>(std::round(g * 65535.0));
    };

    for (size_t p = 0; p < totalPixels; ++p) {
        if (bgra[p * 4 + 3] == 0) continue;

        bgra[p * 4 + 2] = applyGamma(linear[p * 3 + 0]); // R
        bgra[p * 4 + 1] = applyGamma(linear[p * 3 + 1]); // G
        bgra[p * 4 + 0] = applyGamma(linear[p * 3 + 2]); // B
        bgra[p * 4 + 3] = 0xFFFF; // A
    }
}

static bool isRawFile(const std::string& ext) {
    static const char* rawExts[] = {
        ".dng", ".nef", ".nrw", ".cr2", ".cr3", ".crw",
        ".arw", ".srf", ".sr2", ".orf", ".rw2", ".pef",
        ".raf", ".kdc", ".dcr", ".k25", ".raw", ".srw",
        ".3fr", ".iiq", ".mef", ".mos", ".erf", ".foveon",
        ".x3f", ".mrw", ".cub"
    };
    for (const char* e : rawExts) {
        if (ext == e) return true;
    }
    return false;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <raw_directory> [--stack <output.raw>]" << std::endl;
        return 1;
    }

    std::string rawDir = argv[1];
    std::string stackOutputPath;
    int transformFrame = -1;  // --transform <N>: output single transformed frame

    // Parse options
    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--stack" && i + 1 < argc) {
            stackOutputPath = argv[i + 1];
            ++i;
        } else if (std::string(argv[i]) == "--transform" && i + 1 < argc) {
            transformFrame = std::atoi(argv[i + 1]);
            ++i;
        }
    }

    // Collect RAW files
    std::vector<fs::path> rawFiles;
    for (const auto& entry : fs::directory_iterator(rawDir)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (isRawFile(ext)) {
                rawFiles.push_back(entry.path());
            }
        }
    }

    if (rawFiles.empty()) {
        std::cerr << "No RAW files found in " << rawDir << std::endl;
        return 1;
    }

    // Sort by filename
    std::sort(rawFiles.begin(), rawFiles.end());

    std::cout << "=== star_align test with LibRaw " << LibRaw::version() << " ===" << std::endl;
    std::cout << "Image dir: " << rawDir << std::endl;
    std::cout << "Found " << rawFiles.size() << " RAW files" << std::endl;
    std::cout << std::endl;

    // Helper: keep only the brightest N stars for alignment
    auto keepBrightest = [](std::vector<StarAlign::Star>& stars, size_t maxCount) {
        if (stars.size() <= maxCount) return;
        std::partial_sort(stars.begin(), stars.begin() + maxCount, stars.end(),
            [](const StarAlign::Star& a, const StarAlign::Star& b) {
                return a.intensity > b.intensity;
            });
        stars.resize(maxCount);
        std::sort(stars.begin(), stars.end(),
            [](const StarAlign::Star& a, const StarAlign::Star& b) { return a.x < b.x; });
    };

    constexpr size_t MaxStarsForAlignment = 100;

    // Process reference frame (first image)
    RawImage refImg;
    std::cout << "--- Reference frame ---" << std::endl;
    if (!loadRawToBGRA(rawFiles[0].string(), refImg)) {
        return 1;
    }
    std::cout << rawFiles[0].filename().string() << ": "
              << refImg.width << "x" << refImg.height << " (LibRaw processed)"
              << std::endl;

    int stride = refImg.width * 4 * sizeof(uint16_t);

    StarAlign::DetectParams params;
    params.autoThreshold = true;
    params.targetStarCount = 80;

    auto refStars = StarAlign::detectStars(refImg.bgra.data(), refImg.width, refImg.height,
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
    std::vector<StarAlign::AlignResult> allAlignments(rawFiles.size());
    allAlignments[0].success = true;  // reference frame

    for (size_t fi = 1; fi < rawFiles.size(); fi++) {
        RawImage tgtImg;
        if (!loadRawToBGRA(rawFiles[fi].string(), tgtImg)) {
            std::cerr << "Failed to load " << rawFiles[fi].filename().string() << std::endl;
            continue;
        }

        int tgtStride = tgtImg.width * 4 * sizeof(uint16_t);
        auto tgtStars = StarAlign::detectStars(tgtImg.bgra.data(), tgtImg.width, tgtImg.height,
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

        std::string filename = rawFiles[fi].filename().string();
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

    // -------- Transform single frame mode --------
    if (transformFrame >= 1 && static_cast<size_t>(transformFrame) < rawFiles.size()) {
        const size_t fi = static_cast<size_t>(transformFrame);
        if (allAlignments[fi].success) {
            RawImage tgtImg;
            if (loadRawToBGRA(rawFiles[fi].string(), tgtImg)) {
                int tgtStride = tgtImg.width * 4 * sizeof(uint16_t);
                auto transformed = StarAlign::transformBGRA(
                    tgtImg.bgra.data(), tgtImg.width, tgtImg.height, tgtStride,
                    allAlignments[fi]);

                std::string outPath = rawFiles[fi].stem().string() + "_transformed.raw";
                std::ofstream outFile(outPath, std::ios::binary);
                if (outFile.is_open()) {
                    outFile.write(reinterpret_cast<const char*>(transformed.data()),
                                  transformed.size() * sizeof(uint16_t));
                    outFile.close();
                    std::cout << "Transformed frame " << transformFrame << " -> "
                              << outPath << " ("
                              << tgtImg.width << "x" << tgtImg.height << " BGRA16, "
                              << transformed.size() * sizeof(uint16_t) << " bytes)" << std::endl;
                    std::cout << "  offsetX=" << allAlignments[fi].offsetX
                              << " offsetY=" << allAlignments[fi].offsetY
                              << " angle=" << (allAlignments[fi].angle * 180.0 / M_PI) << " deg"
                              << std::endl;
                }
            }
        } else {
            std::cerr << "Frame " << transformFrame << " alignment failed, cannot transform." << std::endl;
        }
    }

    // -------- Stack mode --------
    if (!stackOutputPath.empty() && rawFiles.size() > 1) {
        std::cout << std::endl;
        std::cout << "--- Stacking BGRA16 images ---" << std::endl;

        // Collect BGRA16 data from all frames
        std::vector<const uint16_t*> bgraPtrs;
        std::vector<std::vector<uint16_t>> bgraStorage(rawFiles.size());

        for (size_t fi = 0; fi < rawFiles.size(); ++fi) {
            RawImage img;
            if (!loadRawToBGRA(rawFiles[fi].string(), img)) {
                std::cerr << "Failed to reload " << rawFiles[fi].filename().string() << " for stacking" << std::endl;
                continue;
            }
            bgraStorage[fi] = std::move(img.bgra);
            bgraPtrs.push_back(bgraStorage[fi].data());
        }

        if (bgraPtrs.size() == rawFiles.size()) {
            int stackStride = refImg.width * 4 * sizeof(uint16_t);
            auto stacked = StarAlign::stackBGRAImages(bgraPtrs, refImg.width, refImg.height,
                                                       stackStride, allAlignments);

            if (!stacked.empty()) {
                // Post-processing: WB correction + exposure + gamma
                std::cout << "Applying post-processing (WB, exposure, gamma)..." << std::endl;

                WBCoeffs wb;
                if (!loadWBCoeffs(rawFiles[0].string(), wb)) {
                    std::cerr << "Warning: Could not load WB coefficients, using defaults" << std::endl;
                }
                std::cout << "WB multipliers: R=" << std::fixed << std::setprecision(3) << wb.r
                          << " G=" << wb.g << " B=" << wb.b << std::endl;

                // postProcessStack(stacked, refImg.width, refImg.height, wb,
                //                   1.2,   // exp_shift
                //                   2.2,   // gamma
                //                   4.5);  // toe_slope

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
