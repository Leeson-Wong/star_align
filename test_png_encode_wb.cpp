// test_png_encode_wb.cpp
// Verify that modified png_encode.h produces pixel-identical output to the sweep reference.
//
// Pipeline: DNG --LibRaw linear--> BGRA16 --swap R/B--> RGBA16 --encodeRGBA16ToPNG--> PNG
// Compare:  new output vs aggressive_sweep/..._top1_p2_gtm_g10_t100.png

#include "png_encode.h"
#include "libraw.h"

#include <opencv2/opencv.hpp>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

// ============================================================================
// Load DNG with linear params (camera WB, linear gamma, no auto bright, sRGB)
// ============================================================================

static bool loadLinearBGRA(const std::string& path,
                           std::vector<uint16_t>& bgra, int& w, int& h) {
    LibRaw lr;
    lr.imgdata.params.output_bps    = 16;
    lr.imgdata.params.use_camera_wb = 1;
    lr.imgdata.params.use_auto_wb   = 0;
    lr.imgdata.params.no_auto_bright = 1;
    lr.imgdata.params.user_qual     = 3;
    lr.imgdata.params.output_color  = 1;   // sRGB
    lr.imgdata.params.gamm[0]       = 1.0;
    lr.imgdata.params.gamm[1]       = 1.0;
    lr.imgdata.params.exp_correc    = 0;
    lr.imgdata.params.exp_shift     = 1.0;
    lr.imgdata.params.highlight     = 0;
    lr.imgdata.params.fbdd_noiserd  = 0;
    lr.imgdata.params.bright        = 1.0f;

    int ret = lr.open_file(path.c_str());
    if (ret != LIBRAW_SUCCESS) {
        std::cerr << "open_file failed: " << libraw_strerror(ret) << std::endl;
        return false;
    }
    ret = lr.unpack();
    if (ret != LIBRAW_SUCCESS) {
        std::cerr << "unpack failed: " << libraw_strerror(ret) << std::endl;
        return false;
    }
    ret = lr.dcraw_process();
    if (ret != LIBRAW_SUCCESS) {
        std::cerr << "dcraw_process failed: " << libraw_strerror(ret) << std::endl;
        return false;
    }

    int errcode = 0;
    libraw_processed_image_t* image = lr.dcraw_make_mem_image(&errcode);
    if (!image || errcode != LIBRAW_SUCCESS) {
        std::cerr << "make_mem_image failed" << std::endl;
        return false;
    }

    w = image->width;
    h = image->height;
    const size_t n = static_cast<size_t>(w) * h;
    const uint16_t* rgb = reinterpret_cast<const uint16_t*>(image->data);
    bgra.resize(n * 4);
    for (size_t i = 0; i < n; i++) {
        bgra[i * 4 + 0] = rgb[i * 3 + 2]; // B
        bgra[i * 4 + 1] = rgb[i * 3 + 1]; // G
        bgra[i * 4 + 2] = rgb[i * 3 + 0]; // R
        bgra[i * 4 + 3] = 0xFFFF;          // A
    }

    LibRaw::dcraw_clear_mem(image);
    return true;
}

// ============================================================================
// Stats helper
// ============================================================================

static void printStats(const std::string& label, const cv::Mat& img) {
    double sumB = 0, sumG = 0, sumR = 0;
    for (int y = 0; y < img.rows; ++y) {
        for (int x = 0; x < img.cols; ++x) {
            auto& px = img.at<cv::Vec3w>(y, x);
            sumB += px[0]; sumG += px[1]; sumR += px[2];
        }
    }
    size_t n = (size_t)img.rows * img.cols;
    double avgR = sumR / n, avgG = sumG / n, avgB = sumB / n;
    double rg = avgG > 0 ? avgR / avgG : 0;
    double bg = avgG > 0 ? avgB / avgG : 0;
    double score = std::abs(rg - 1.0) + std::abs(bg - 1.0);
    std::cout << "[" << label << "] R/G=" << rg << " B/G=" << bg
              << " score=" << score << std::endl;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::string rawPath = R"(F:\mime\star_align\dngs\origin_40229821_raw.dng)";
    std::string refPath = R"(F:\mime\star_align\dngs\aggressive_sweep\origin_40229821_raw_top1_p2_gtm_g10_t100.png)";
    std::string outPath = R"(F:\mime\star_align\dngs\test_png_encode_output.png)";

    if (!fs::exists(rawPath)) {
        std::cerr << "DNG not found: " << rawPath << std::endl;
        return 1;
    }
    if (!fs::exists(refPath)) {
        std::cerr << "Reference not found: " << refPath << std::endl;
        return 1;
    }

    // ---- Step 1: Load DNG -> BGRA16 ----
    std::cout << "Loading DNG (linear, camera WB)..." << std::endl;
    std::vector<uint16_t> bgra;
    int w, h;
    if (!loadLinearBGRA(rawPath, bgra, w, h)) return 1;
    std::cout << "Size: " << w << "x" << h << std::endl;

    // ---- Step 2: BGRA -> RGBA (png_encode.h expects RGBA) ----
    const size_t n = static_cast<size_t>(w) * h;
    std::vector<uint16_t> rgba(n * 4);
    for (size_t i = 0; i < n; i++) {
        rgba[i * 4 + 0] = bgra[i * 4 + 2]; // R <- bgra.R
        rgba[i * 4 + 1] = bgra[i * 4 + 1]; // G
        rgba[i * 4 + 2] = bgra[i * 4 + 0]; // B <- bgra.B
        rgba[i * 4 + 3] = bgra[i * 4 + 3]; // A
    }

    // ---- Step 3: Encode via png_encode.h (uses DEFAULT_POST_PROCESS) ----
    std::cout << "Encoding via png_encode.h (GREY_WORLD + gamma=1.0, toe=10.0)..." << std::endl;
    std::vector<uchar> pngData;
    if (!encodeRGBA16ToPNG(rgba.data(), w, h, pngData, 3)) {
        std::cerr << "encodeRGBA16ToPNG failed" << std::endl;
        return 1;
    }

    // Save output PNG
    {
        std::ofstream f(outPath, std::ios::binary);
        f.write(reinterpret_cast<const char*>(pngData.data()), pngData.size());
    }
    std::cout << "Output saved: " << outPath << std::endl;

    // ---- Step 4: Load both PNGs and compare ----
    cv::Mat output = cv::imread(outPath, cv::IMREAD_UNCHANGED);
    cv::Mat ref    = cv::imread(refPath, cv::IMREAD_UNCHANGED);

    if (output.empty()) {
        std::cerr << "Failed to load output PNG" << std::endl;
        return 1;
    }
    if (ref.empty()) {
        std::cerr << "Failed to load reference PNG" << std::endl;
        return 1;
    }

    std::cout << "\n--- Stats comparison ---" << std::endl;
    printStats("OUTPUT", output);
    printStats("REF    ", ref);

    // Pixel-by-pixel comparison
    if (output.size() != ref.size() || output.type() != ref.type()) {
        std::cerr << "SIZE MISMATCH: output=" << output.size() << " ref=" << ref.size() << std::endl;
        return 1;
    }

    size_t totalPixels = (size_t)output.rows * output.cols;
    size_t matchCount = 0;
    int maxDiff = 0;
    double sumDiff = 0;

    for (int y = 0; y < output.rows; ++y) {
        for (int x = 0; x < output.cols; ++x) {
            auto& op = output.at<cv::Vec3w>(y, x);
            auto& rp = ref.at<cv::Vec3w>(y, x);
            bool pixelMatch = true;
            for (int c = 0; c < 3; c++) {
                int diff = std::abs(static_cast<int>(op[c]) - static_cast<int>(rp[c]));
                if (diff > 0) pixelMatch = false;
                maxDiff = std::max(maxDiff, diff);
                sumDiff += diff;
            }
            if (pixelMatch) matchCount++;
        }
    }

    std::cout << "\n--- Pixel comparison ---" << std::endl;
    std::cout << "Total pixels:  " << totalPixels << std::endl;
    std::cout << "Exact matches: " << matchCount << " / " << totalPixels
              << " (" << std::fixed << std::setprecision(2)
              << (100.0 * matchCount / totalPixels) << "%)" << std::endl;
    std::cout << "Max diff:      " << maxDiff << std::endl;
    std::cout << "Avg diff:      " << (sumDiff / (totalPixels * 3)) << std::endl;

    if (matchCount == totalPixels) {
        std::cout << "\n=== PASS: output is pixel-identical to reference ===" << std::endl;
    } else if (maxDiff <= 1) {
        std::cout << "\n=== PASS (tolerance +/-1): output matches reference within rounding ===" << std::endl;
    } else {
        std::cout << "\n=== FAIL: output differs from reference (maxDiff=" << maxDiff << ") ===" << std::endl;
        return 1;
    }

    return 0;
}
