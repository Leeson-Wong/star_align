// test_white_balance.cpp
// Parameter sweep: vary post-processing parameters one at a time
// to identify the root cause of green tint in custom post-processing.

#include "libraw.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <filesystem>
#include <cstdio>

#include <opencv2/opencv.hpp>

namespace fs = std::filesystem;

// ============================================================================
// RawImage struct
// ============================================================================

struct RawImage {
    std::vector<uint16_t> bgra;  // BGRA16 interleaved
    int width = 0;
    int height = 0;
};

// ============================================================================
// Parameterized load struct & function
// ============================================================================

struct LoadParams {
    int output_bps      = 16;
    int use_camera_wb   = 1;
    int use_auto_wb     = 0;
    int no_auto_bright  = 1;
    int user_qual       = 3;
    int output_color    = 1;
    double gamm0        = 1.0;
    double gamm1        = 1.0;
    int exp_correc      = 0;
    double exp_shift    = 1.0;
    int highlight       = 0;
    int fbdd_noiserd    = 0;
    float bright        = 1.0f;
};

static const LoadParams LINEAR_PARAMS = {
    16, 1, 0, 1, 3, 1,  1.0, 1.0,  0, 1.0,  0, 0,  1.0f
};

static const LoadParams PRETTY_PARAMS = {
    16, 1, 1, 0, 3, 1,  2.2, 4.5,  1, 1.20,  0, 0,  1.0f
};

static bool loadBGRA(const std::string& path, RawImage& out,
                     const LoadParams& lp,
                     bool printColorData = false) {
    LibRaw lr;

    lr.imgdata.params.output_bps    = lp.output_bps;
    lr.imgdata.params.use_camera_wb = lp.use_camera_wb;
    lr.imgdata.params.use_auto_wb   = lp.use_auto_wb;
    lr.imgdata.params.no_auto_bright = lp.no_auto_bright;
    lr.imgdata.params.user_qual     = lp.user_qual;
    lr.imgdata.params.output_color  = lp.output_color;
    lr.imgdata.params.gamm[0]       = lp.gamm0;
    lr.imgdata.params.gamm[1]       = lp.gamm1;
    lr.imgdata.params.exp_correc    = lp.exp_correc;
    lr.imgdata.params.exp_shift     = lp.exp_shift;
    lr.imgdata.params.highlight     = lp.highlight;
    lr.imgdata.params.fbdd_noiserd  = lp.fbdd_noiserd;
    lr.imgdata.params.bright        = lp.bright;

    int ret = lr.open_file(path.c_str());
    if (ret != LIBRAW_SUCCESS) {
        std::cerr << "Error: cannot open " << path << " (" << libraw_strerror(ret) << ")" << std::endl;
        return false;
    }

    ret = lr.unpack();
    if (ret != LIBRAW_SUCCESS) {
        std::cerr << "Error: unpack failed (" << libraw_strerror(ret) << ")" << std::endl;
        return false;
    }

    ret = lr.dcraw_process();
    if (ret != LIBRAW_SUCCESS) {
        std::cerr << "Error: dcraw_process failed (" << libraw_strerror(ret) << ")" << std::endl;
        return false;
    }

    int errcode = 0;
    libraw_processed_image_t* image = lr.dcraw_make_mem_image(&errcode);
    if (!image || errcode != LIBRAW_SUCCESS) {
        std::cerr << "Error: make_mem_image failed" << std::endl;
        return false;
    }

    out.width  = image->width;
    out.height = image->height;

    if (image->colors == 3 && image->bits == 16) {
        const size_t n = static_cast<size_t>(out.width) * out.height;
        const uint16_t* rgb = reinterpret_cast<const uint16_t*>(image->data);
        out.bgra.resize(n * 4);
        for (size_t i = 0; i < n; i++) {
            out.bgra[i * 4 + 0] = rgb[i * 3 + 2]; // B
            out.bgra[i * 4 + 1] = rgb[i * 3 + 1]; // G
            out.bgra[i * 4 + 2] = rgb[i * 3 + 0]; // R
            out.bgra[i * 4 + 3] = 0xFFFF;          // A
        }
    } else {
        std::cerr << "Error: unexpected format (colors=" << image->colors
                  << ", bits=" << image->bits << ")" << std::endl;
        LibRaw::dcraw_clear_mem(image);
        return false;
    }

    LibRaw::dcraw_clear_mem(image);

    if (printColorData) {
        std::cout << "  black=" << lr.imgdata.color.black
                  << " maximum=" << lr.imgdata.color.maximum << std::endl;
        std::cout << "  cam_mul: "
                  << lr.imgdata.color.cam_mul[0] << " "
                  << lr.imgdata.color.cam_mul[1] << " "
                  << lr.imgdata.color.cam_mul[2] << " "
                  << lr.imgdata.color.cam_mul[3] << std::endl;
        std::cout << "  pre_mul: "
                  << lr.imgdata.color.pre_mul[0] << " "
                  << lr.imgdata.color.pre_mul[1] << " "
                  << lr.imgdata.color.pre_mul[2] << " "
                  << lr.imgdata.color.pre_mul[3] << std::endl;
        std::cout << "  rgb_cam:" << std::endl;
        for (int i = 0; i < 3; i++) {
            std::cout << "    [";
            for (int j = 0; j < 4; j++)
                std::cout << " " << lr.imgdata.color.rgb_cam[i][j];
            std::cout << " ]" << std::endl;
        }
    }

    return true;
}

// ============================================================================
// Parameterized post-processing
// ============================================================================

// Channel balance mode
enum class BalanceMode {
    NONE = 0,       // no extra balancing
    GREY_WORLD,     // scale channels so avgR = avgG = avgB
    PCTL_GREY,      // scale channels so p99_R = p99_G = p99_B
    MANUAL          // manual R/G/B multipliers
};

struct PostProcessParams {
    double exp_shift        = 1.20;
    double auto_bright_pctl = 0.99;  // 0 = disabled
    double gamma            = 2.2;
    double toe_slope        = 4.5;
    double max_scale        = 10.0;
    BalanceMode balance     = BalanceMode::NONE;
    double manual_r         = 1.0;   // only used when balance=MANUAL
    double manual_g         = 1.0;
    double manual_b         = 1.0;
};

static void postProcess(const std::vector<uint16_t>& srcBgra,
                        std::vector<uint16_t>& outBgra,
                        int width, int height,
                        const PostProcessParams& params) {
    const size_t totalPixels = static_cast<size_t>(width) * height;
    outBgra.resize(totalPixels * 4);

    // Step 1: Exposure boost into linear float buffer
    std::vector<double> linear(totalPixels * 3);

    for (size_t p = 0; p < totalPixels; ++p) {
        if (srcBgra[p * 4 + 3] == 0) {
            linear[p * 3 + 0] = 0;
            linear[p * 3 + 1] = 0;
            linear[p * 3 + 2] = 0;
            continue;
        }

        double B = srcBgra[p * 4 + 0] * params.exp_shift;
        double G = srcBgra[p * 4 + 1] * params.exp_shift;
        double R = srcBgra[p * 4 + 2] * params.exp_shift;

        linear[p * 3 + 0] = R;
        linear[p * 3 + 1] = G;
        linear[p * 3 + 2] = B;
    }

    // Step 1.5: Channel balance (before auto brightness & gamma)
    if (params.balance != BalanceMode::NONE) {
        double sumR = 0, sumG = 0, sumB = 0;
        size_t count = 0;

        if (params.balance == BalanceMode::PCTL_GREY) {
            // Collect per-channel p99
            std::vector<double> valsR, valsG, valsB;
            valsR.reserve(totalPixels);
            valsG.reserve(totalPixels);
            valsB.reserve(totalPixels);
            for (size_t p = 0; p < totalPixels; ++p) {
                if (srcBgra[p * 4 + 3] == 0) continue;
                valsR.push_back(linear[p * 3 + 0]);
                valsG.push_back(linear[p * 3 + 1]);
                valsB.push_back(linear[p * 3 + 2]);
            }
            auto pctl99 = [](std::vector<double>& v) -> double {
                if (v.empty()) return 1.0;
                size_t idx = static_cast<size_t>(v.size() * 0.99);
                if (idx >= v.size()) idx = v.size() - 1;
                std::nth_element(v.begin(), v.begin() + idx, v.end());
                return v[idx] > 0 ? v[idx] : 1.0;
            };
            double pR = pctl99(valsR);
            double pG = pctl99(valsG);
            double pB = pctl99(valsB);
            // Scale so that all channels have same p99
            double scaleR = pG / pR;
            double scaleB = pG / pB;
            std::cout << "  Balance PCTL_GREY: p99 R=" << pR << " G=" << pG
                      << " B=" << pB << " => scaleR=" << scaleR << " scaleB=" << scaleB << std::endl;
            for (size_t p = 0; p < totalPixels; ++p) {
                linear[p * 3 + 0] *= scaleR;
                linear[p * 3 + 2] *= scaleB;
            }
        } else if (params.balance == BalanceMode::GREY_WORLD) {
            for (size_t p = 0; p < totalPixels; ++p) {
                if (srcBgra[p * 4 + 3] == 0) continue;
                sumR += linear[p * 3 + 0];
                sumG += linear[p * 3 + 1];
                sumB += linear[p * 3 + 2];
                count++;
            }
            if (count > 0 && sumG > 0) {
                double scaleR = sumG / sumR;
                double scaleB = sumG / sumB;
                std::cout << "  Balance GREY_WORLD: avgR=" << (sumR/count)
                          << " avgG=" << (sumG/count) << " avgB=" << (sumB/count)
                          << " => scaleR=" << scaleR << " scaleB=" << scaleB << std::endl;
                for (size_t p = 0; p < totalPixels; ++p) {
                    linear[p * 3 + 0] *= scaleR;
                    linear[p * 3 + 2] *= scaleB;
                }
            }
        } else if (params.balance == BalanceMode::MANUAL) {
            std::cout << "  Balance MANUAL: R*=" << params.manual_r
                      << " G*=" << params.manual_g << " B*=" << params.manual_b << std::endl;
            for (size_t p = 0; p < totalPixels; ++p) {
                linear[p * 3 + 0] *= params.manual_r;
                linear[p * 3 + 1] *= params.manual_g;
                linear[p * 3 + 2] *= params.manual_b;
            }
        }
    }

    // Step 2: Auto brightness via percentile stretching
    if (params.auto_bright_pctl > 0.0) {
        std::vector<double> allVals;
        allVals.reserve(totalPixels);
        for (size_t p = 0; p < totalPixels; ++p) {
            if (srcBgra[p * 4 + 3] == 0) continue;
            double brightness = std::max({linear[p * 3 + 0], linear[p * 3 + 1], linear[p * 3 + 2]});
            allVals.push_back(brightness);
        }
        if (!allVals.empty()) {
            size_t idx = static_cast<size_t>(allVals.size() * params.auto_bright_pctl);
            if (idx >= allVals.size()) idx = allVals.size() - 1;
            std::nth_element(allVals.begin(), allVals.begin() + idx, allVals.end());
            double pVal = allVals[idx];
            if (pVal > 0) {
                double scale = 65535.0 / pVal;
                scale = std::min(scale, params.max_scale);
                std::cout << "  Auto brightness: p" << (int)(params.auto_bright_pctl * 100)
                          << "=" << pVal << ", scale=" << scale << std::endl;
                for (auto& v : linear) v *= scale;
            }
        }
    }

    // Step 3: Gamma correction
    const double invGamma = 1.0 / params.gamma;
    const double toe = params.toe_slope;

    auto applyGamma = [invGamma, toe](double v) -> uint16_t {
        double n = std::min(v, 65535.0) / 65535.0;
        double g = (1.0 + toe) * std::pow(n, invGamma) - toe * n;
        g = std::max(0.0, std::min(1.0, g));
        return static_cast<uint16_t>(std::round(g * 65535.0));
    };

    for (size_t p = 0; p < totalPixels; ++p) {
        if (srcBgra[p * 4 + 3] == 0) {
            outBgra[p * 4 + 0] = 0;
            outBgra[p * 4 + 1] = 0;
            outBgra[p * 4 + 2] = 0;
            outBgra[p * 4 + 3] = 0;
            continue;
        }
        outBgra[p * 4 + 2] = applyGamma(linear[p * 3 + 0]); // R
        outBgra[p * 4 + 1] = applyGamma(linear[p * 3 + 1]); // G
        outBgra[p * 4 + 0] = applyGamma(linear[p * 3 + 2]); // B
        outBgra[p * 4 + 3] = 0xFFFF;
    }
}

// ============================================================================
// Save helpers
// ============================================================================

static bool saveRaw(const std::string& path, const std::vector<uint16_t>& bgra) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) {
        std::cerr << "Error: cannot write " << path << std::endl;
        return false;
    }
    f.write(reinterpret_cast<const char*>(bgra.data()), bgra.size() * sizeof(uint16_t));
    f.close();
    return true;
}

static bool savePNG(const std::string& path, const std::vector<uint16_t>& bgra,
                    int width, int height) {
    cv::Mat img16(height, width, CV_16UC3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = (static_cast<size_t>(y) * width + x) * 4;
            img16.at<cv::Vec3w>(y, x)[0] = bgra[idx + 0]; // B
            img16.at<cv::Vec3w>(y, x)[1] = bgra[idx + 1]; // G
            img16.at<cv::Vec3w>(y, x)[2] = bgra[idx + 2]; // R
        }
    }
    std::vector<int> params = {cv::IMWRITE_PNG_COMPRESSION, 3};
    if (!cv::imwrite(path, img16, params)) {
        std::cerr << "Error: cv::imwrite failed for " << path << std::endl;
        return false;
    }
    return true;
}

// ============================================================================
// Print statistics
// ============================================================================

static void printStats(const std::string& label, const std::vector<uint16_t>& bgra,
                       int width, int height) {
    const size_t n = static_cast<size_t>(width) * height;
    double sumR = 0, sumG = 0, sumB = 0;
    uint16_t maxR = 0, maxG = 0, maxB = 0;
    double minR = 65535, minG = 65535, minB = 65535;

    size_t valid = 0;
    for (size_t i = 0; i < n; ++i) {
        if (bgra[i * 4 + 3] == 0) continue;
        valid++;
        double r = bgra[i * 4 + 2];
        double g = bgra[i * 4 + 1];
        double b = bgra[i * 4 + 0];

        sumR += r; sumG += g; sumB += b;
        maxR = std::max(maxR, (uint16_t)r);
        maxG = std::max(maxG, (uint16_t)g);
        maxB = std::max(maxB, (uint16_t)b);
        minR = std::min(minR, r); minG = std::min(minG, g); minB = std::min(minB, b);
    }

    if (valid > 0) {
        std::cout << "[" << label << "] "
                  << "avg R=" << (sumR / valid)
                  << " G=" << (sumG / valid)
                  << " B=" << (sumB / valid)
                  << " | R/G=" << (sumG > 0 ? sumR / sumG : 0)
                  << " B/G=" << (sumG > 0 ? sumB / sumG : 0)
                  << " | max R=" << maxR << " G=" << maxG << " B=" << maxB
                  << std::endl;
    }
}

// ============================================================================
// Sweep helpers
// ============================================================================

static void runSweep(const std::string& label,
                     const std::string& suffix,
                     const std::vector<uint16_t>& linearBgra,
                     int width, int height,
                     const PostProcessParams& ppParams,
                     const std::string& outDir,
                     const std::string& stem) {
    std::cout << "--- " << label << " ---" << std::endl;
    RawImage img;
    img.width = width;
    img.height = height;
    postProcess(linearBgra, img.bgra, width, height, ppParams);
    printStats(label, img.bgra, width, height);
    std::string pngPath = outDir + "/" + stem + "_" + suffix + ".png";
    savePNG(pngPath, img.bgra, width, height);
    std::cout << "Saved: " << pngPath << std::endl << std::endl;
}

static void runLoadSweep(const std::string& label,
                         const std::string& suffix,
                         const std::string& rawPath,
                         const LoadParams& loadParams,
                         bool printColor,
                         const std::string& outDir,
                         const std::string& stem) {
    std::cout << "--- " << label << " (LibRaw load) ---" << std::endl;
    RawImage img;
    if (!loadBGRA(rawPath, img, loadParams, printColor)) return;
    printStats(label, img.bgra, img.width, img.height);
    std::string pngPath = outDir + "/" + stem + "_" + suffix + ".png";
    savePNG(pngPath, img.bgra, img.width, img.height);
    std::cout << "Saved: " << pngPath << std::endl << std::endl;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::string rawPath = R"(F:\mime\star_align\dngs\origin_40229821_raw.dng)";
    if (!fs::exists(rawPath)) {
        std::cerr << "File not found: " << rawPath << std::endl;
        return 1;
    }

    std::string stem = fs::path(rawPath).stem().string();
    std::string outDir = fs::path(rawPath).parent_path().string();

    std::cout << "=== White Balance Parameter Sweep ===" << std::endl;
    std::cout << "Input: " << rawPath << std::endl << std::endl;

    // ---- Load LINEAR (source for all custom sweeps) ----
    std::cout << "=== Loading LINEAR ===" << std::endl;
    RawImage linearImg;
    if (!loadBGRA(rawPath, linearImg, LINEAR_PARAMS, true)) return 1;
    std::cout << "Size: " << linearImg.width << "x" << linearImg.height << std::endl;
    printStats("LINEAR", linearImg.bgra, linearImg.width, linearImg.height);

    std::string linearPng = outDir + "/" + stem + "_linear.png";
    savePNG(linearPng, linearImg.bgra, linearImg.width, linearImg.height);
    std::cout << "Saved: " << linearPng << std::endl << std::endl;

    // ---- Load REFERENCE ----
    std::cout << "=== Loading REFERENCE ===" << std::endl;
    RawImage refImg;
    if (!loadBGRA(rawPath, refImg, PRETTY_PARAMS, true)) return 1;
    printStats("REFERENCE", refImg.bgra, refImg.width, refImg.height);

    std::string refPng = outDir + "/" + stem + "_reference.png";
    savePNG(refPng, refImg.bgra, refImg.width, refImg.height);
    std::cout << "Saved: " << refPng << std::endl << std::endl;

    // ---- Original POSTPROC (baseline custom) ----
    {
        PostProcessParams pp;
        runSweep("POSTPROC", "postproc", linearImg.bgra, linearImg.width,
                 linearImg.height, pp, outDir, stem);
    }

    // ====================================================================
    // SWEEP A: Auto WB (LibRaw-based)
    // ====================================================================
    std::cout << "\n========================================" << std::endl;
    std::cout << "SWEEP A: Auto WB (LibRaw load-time)" << std::endl;
    std::cout << "========================================" << std::endl;

    // A1: Linear params but with auto_wb=1
    {
        LoadParams p = LINEAR_PARAMS;
        p.use_auto_wb = 1;
        runLoadSweep("LINEAR+AUTOWB", "sweep_autowb_on", rawPath, p, true, outDir, stem);
    }

    // A2: Pretty params but auto_wb=0 (isolate auto_wb effect on reference)
    {
        LoadParams p = PRETTY_PARAMS;
        p.use_auto_wb = 0;
        runLoadSweep("PRETTY_NO_AUTOWB", "sweep_pretty_noautowb", rawPath, p, true, outDir, stem);
    }

    // ====================================================================
    // SWEEP B: Auto Brightness Percentile
    // ====================================================================
    std::cout << "\n========================================" << std::endl;
    std::cout << "SWEEP B: Auto Brightness Percentile" << std::endl;
    std::cout << "========================================" << std::endl;

    {
        const double pcts[] = {0.0, 0.95, 0.97, 0.99, 0.995};
        const char* labels[] = {"none", "p95", "p97", "p99", "p995"};
        for (int i = 0; i < 5; i++) {
            PostProcessParams pp;
            pp.auto_bright_pctl = pcts[i];
            std::string suffix = std::string("sweep_ab_") + labels[i];
            std::string label = std::string("AB_") + labels[i];
            runSweep(label, suffix, linearImg.bgra, linearImg.width,
                     linearImg.height, pp, outDir, stem);
        }
    }

    // ====================================================================
    // SWEEP C: Gamma
    // ====================================================================
    std::cout << "\n========================================" << std::endl;
    std::cout << "SWEEP C: Gamma" << std::endl;
    std::cout << "========================================" << std::endl;

    {
        const double gammas[] = {1.8, 2.0, 2.2, 2.4};
        for (double g : gammas) {
            PostProcessParams pp;
            pp.gamma = g;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "sweep_gamma_%.1f", g);
            std::string label = std::string("GAMMA_") + std::to_string((int)(g * 10));
            runSweep(label, buf, linearImg.bgra, linearImg.width,
                     linearImg.height, pp, outDir, stem);
        }
    }

    // ====================================================================
    // SWEEP D: Toe Slope
    // ====================================================================
    std::cout << "\n========================================" << std::endl;
    std::cout << "SWEEP D: Toe Slope" << std::endl;
    std::cout << "========================================" << std::endl;

    {
        const double toes[] = {0.0, 2.0, 4.5, 6.0};
        for (double t : toes) {
            PostProcessParams pp;
            pp.toe_slope = t;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "sweep_toe_%.1f", t);
            std::string label = std::string("TOE_") + std::to_string((int)(t * 10));
            runSweep(label, buf, linearImg.bgra, linearImg.width,
                     linearImg.height, pp, outDir, stem);
        }
    }

    // ====================================================================
    // SWEEP E: Exposure Shift
    // ====================================================================
    std::cout << "\n========================================" << std::endl;
    std::cout << "SWEEP E: Exposure Shift" << std::endl;
    std::cout << "========================================" << std::endl;

    {
        const double shifts[] = {1.0, 1.1, 1.2, 1.3};
        for (double s : shifts) {
            PostProcessParams pp;
            pp.exp_shift = s;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "sweep_exp_%.1f", s);
            std::string label = std::string("EXP_") + std::to_string((int)(s * 10));
            runSweep(label, buf, linearImg.bgra, linearImg.width,
                     linearImg.height, pp, outDir, stem);
        }
    }

    // ====================================================================
    // SWEEP F: LibRaw auto_bright modes
    // ====================================================================
    std::cout << "\n========================================" << std::endl;
    std::cout << "SWEEP F: LibRaw auto_bright modes" << std::endl;
    std::cout << "========================================" << std::endl;

    // F1: LibRaw full pretty (auto_bright ON) - same as reference but re-dump color data
    {
        runLoadSweep("LR_AUTO_BRIGHT", "sweep_lr_autobright", rawPath,
                     PRETTY_PARAMS, true, outDir, stem);
    }

    // F2: LibRaw pretty but no auto bright
    {
        LoadParams p = PRETTY_PARAMS;
        p.no_auto_bright = 1;
        runLoadSweep("LR_NO_AUTO_BRIGHT", "sweep_lr_noautobright", rawPath,
                     p, true, outDir, stem);
    }

    // F3: LibRaw pretty with bright=2.0
    {
        LoadParams p = PRETTY_PARAMS;
        p.bright = 2.0f;
        runLoadSweep("LR_BRIGHT_2", "sweep_lr_bright2", rawPath,
                     p, true, outDir, stem);
    }

    // F4: Custom pipeline with no auto bright at all
    {
        PostProcessParams pp;
        pp.auto_bright_pctl = 0.0;
        runSweep("CUSTOM_NO_AB", "sweep_custom_noab", linearImg.bgra,
                 linearImg.width, linearImg.height, pp, outDir, stem);
    }

    // ====================================================================
    // SWEEP G: Channel balance modes (post-processing level)
    // ====================================================================
    std::cout << "\n========================================" << std::endl;
    std::cout << "SWEEP G: Channel balance modes" << std::endl;
    std::cout << "========================================" << std::endl;

    // G1: Grey-world balance
    {
        PostProcessParams pp;
        pp.balance = BalanceMode::GREY_WORLD;
        runSweep("BAL_GREY_WORLD", "sweep_bal_greyworld", linearImg.bgra,
                 linearImg.width, linearImg.height, pp, outDir, stem);
    }

    // G2: Percentile-based balance (p99)
    {
        PostProcessParams pp;
        pp.balance = BalanceMode::PCTL_GREY;
        runSweep("BAL_PCTL99", "sweep_bal_pctl99", linearImg.bgra,
                 linearImg.width, linearImg.height, pp, outDir, stem);
    }

    // G3: Balance + no auto bright (isolate balance effect)
    {
        PostProcessParams pp;
        pp.balance = BalanceMode::GREY_WORLD;
        pp.auto_bright_pctl = 0.0;
        runSweep("BAL_GREY_NO_AB", "sweep_bal_greyworld_noab", linearImg.bgra,
                 linearImg.width, linearImg.height, pp, outDir, stem);
    }

    // G4: Balance + no auto bright + no gamma (pure balance check)
    {
        PostProcessParams pp;
        pp.balance = BalanceMode::GREY_WORLD;
        pp.auto_bright_pctl = 0.0;
        pp.gamma = 1.0;
        pp.toe_slope = 1.0;
        runSweep("BAL_GREY_LINEAR", "sweep_bal_greyworld_linear", linearImg.bgra,
                 linearImg.width, linearImg.height, pp, outDir, stem);
    }

    // G5: Percentile balance + no auto bright + no gamma
    {
        PostProcessParams pp;
        pp.balance = BalanceMode::PCTL_GREY;
        pp.auto_bright_pctl = 0.0;
        pp.gamma = 1.0;
        pp.toe_slope = 1.0;
        runSweep("BAL_PCTL_LINEAR", "sweep_bal_pctl_linear", linearImg.bgra,
                 linearImg.width, linearImg.height, pp, outDir, stem);
    }

    std::cout << "=== Sweep complete. Check _sweep_*.png files ===" << std::endl;
    return 0;
}
