// test_wb_aggressive.cpp
// Aggressive multi-dimensional white balance sweep: brute-force grid search
// Phase 1: LibRaw load-time parameter matrix (quick eval)
// Phase 2: Post-process parameter grid (quick eval)
// Phase 3: Save top-N candidates as PNG

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
#include <sstream>
#include <iomanip>

#include <opencv2/opencv.hpp>

namespace fs = std::filesystem;

// ============================================================================
// Shared infrastructure
// ============================================================================

struct RawImage {
    std::vector<uint16_t> bgra;
    int width = 0;
    int height = 0;
};

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
    double cam_mul_ovr[4] = {0, 0, 0, 0}; // 0 = don't override
};

static bool loadBGRA(const std::string& path, RawImage& out,
                     const LoadParams& lp) {
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
    if (ret != LIBRAW_SUCCESS) return false;

    // Apply manual cam_mul override after open_file (so we can read original values)
    if (lp.cam_mul_ovr[0] > 0 || lp.cam_mul_ovr[1] > 0 || lp.cam_mul_ovr[2] > 0) {
        for (int i = 0; i < 4; i++) {
            if (lp.cam_mul_ovr[i] > 0)
                lr.imgdata.params.user_mul[i] = lp.cam_mul_ovr[i];
        }
        lr.imgdata.params.use_camera_wb = 0;
        lr.imgdata.params.use_auto_wb = 0;
    }

    ret = lr.unpack();
    if (ret != LIBRAW_SUCCESS) return false;

    ret = lr.dcraw_process();
    if (ret != LIBRAW_SUCCESS) return false;

    int errcode = 0;
    libraw_processed_image_t* image = lr.dcraw_make_mem_image(&errcode);
    if (!image || errcode != LIBRAW_SUCCESS) return false;

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
        LibRaw::dcraw_clear_mem(image);
        return false;
    }

    LibRaw::dcraw_clear_mem(image);
    return true;
}

// ============================================================================
// Aggressive post-processing with extended balance modes
// ============================================================================

enum class BalanceMode {
    NONE = 0,
    GREY_WORLD,
    PCTL_GREY,
    WHITE_PATCH,        // scale so max(R) = max(G) = max(B)
    SHADES_OF_GREY_P2,  // Minkowski p=2 (Euclidean)
    SHADES_OF_GREY_P4,  // Minkowski p=4
    SHADES_OF_GREY_P8,  // Minkowski p=8
    MEDIAN_GREY,         // scale so median(R) = median(G) = median(B)
    PCTL_90,             // scale so p90(R) = p90(G) = p90(B)
    PCTL_95,             // scale so p95(R) = p95(G) = p95(B)
    PCTL_97,             // scale so p97(R) = p97(G) = p97(B)
    MANUAL
};

struct PostProcessParams {
    double exp_shift        = 1.20;
    double auto_bright_pctl = 0.99;
    double gamma            = 2.2;
    double toe_slope        = 4.5;
    double max_scale        = 10.0;
    BalanceMode balance     = BalanceMode::NONE;
    double manual_r         = 1.0;
    double manual_g         = 1.0;
    double manual_b         = 1.0;
};

// Helper: compute percentile from a vector (modifies in place)
static double percentileOf(std::vector<double>& v, double pctl) {
    if (v.empty()) return 1.0;
    size_t idx = static_cast<size_t>(v.size() * pctl);
    if (idx >= v.size()) idx = v.size() - 1;
    std::nth_element(v.begin(), v.begin() + idx, v.end());
    return v[idx] > 0 ? v[idx] : 1.0;
}

// Helper: compute median
static double medianOf(std::vector<double>& v) {
    if (v.empty()) return 1.0;
    size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    double val = v[mid];
    if (v.size() % 2 == 0) {
        std::nth_element(v.begin(), v.begin() + mid - 1, v.end());
        val = (val + v[mid - 1]) * 0.5;
    }
    return val > 0 ? val : 1.0;
}

// Helper: Minkowski p-norm channel balance
static void computeChannelBalance(const std::vector<double>& linear,
                                  size_t totalPixels, const uint16_t* alpha,
                                  int p, double& scaleR, double& scaleB) {
    // Compute p-norm per channel: ||ch||_p = (sum |ch_i|^p)^(1/p)
    double sumR = 0, sumG = 0, sumB = 0;
    for (size_t i = 0; i < totalPixels; ++i) {
        if (alpha[i * 4 + 3] == 0) continue;
        sumR += std::pow(std::abs(linear[i * 3 + 0]), p);
        sumG += std::pow(std::abs(linear[i * 3 + 1]), p);
        sumB += std::pow(std::abs(linear[i * 3 + 2]), p);
    }
    double normR = std::pow(sumR, 1.0 / p);
    double normG = std::pow(sumG, 1.0 / p);
    double normB = std::pow(sumB, 1.0 / p);
    scaleR = normG > 0 ? normG / normR : 1.0;
    scaleB = normG > 0 ? normG / normB : 1.0;
}

static void postProcess(const std::vector<uint16_t>& srcBgra,
                        std::vector<uint16_t>& outBgra,
                        int width, int height,
                        const PostProcessParams& params,
                        bool verbose = false) {
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
        linear[p * 3 + 0] = srcBgra[p * 4 + 2] * params.exp_shift; // R
        linear[p * 3 + 1] = srcBgra[p * 4 + 1] * params.exp_shift; // G
        linear[p * 3 + 2] = srcBgra[p * 4 + 0] * params.exp_shift; // B
    }

    // Step 1.5: Channel balance
    if (params.balance != BalanceMode::NONE) {
        double scaleR = 1.0, scaleB = 1.0;
        double manualScaleR = 1.0, manualScaleG = 1.0, manualScaleB = 1.0;

        switch (params.balance) {
            case BalanceMode::GREY_WORLD: {
                double sumR = 0, sumG = 0, sumB = 0;
                size_t count = 0;
                for (size_t p = 0; p < totalPixels; ++p) {
                    if (srcBgra[p * 4 + 3] == 0) continue;
                    sumR += linear[p * 3 + 0];
                    sumG += linear[p * 3 + 1];
                    sumB += linear[p * 3 + 2];
                    count++;
                }
                if (count > 0 && sumG > 0) {
                    scaleR = sumG / sumR;
                    scaleB = sumG / sumB;
                }
                break;
            }
            case BalanceMode::PCTL_GREY: {
                std::vector<double> valsR, valsG, valsB;
                valsR.reserve(totalPixels); valsG.reserve(totalPixels); valsB.reserve(totalPixels);
                for (size_t p = 0; p < totalPixels; ++p) {
                    if (srcBgra[p * 4 + 3] == 0) continue;
                    valsR.push_back(linear[p * 3 + 0]);
                    valsG.push_back(linear[p * 3 + 1]);
                    valsB.push_back(linear[p * 3 + 2]);
                }
                double pR = percentileOf(valsR, 0.99);
                double pG = percentileOf(valsG, 0.99);
                double pB = percentileOf(valsB, 0.99);
                scaleR = pG / pR;
                scaleB = pG / pB;
                break;
            }
            case BalanceMode::PCTL_90: case BalanceMode::PCTL_95: case BalanceMode::PCTL_97: {
                double pctl = 0.90;
                if (params.balance == BalanceMode::PCTL_95) pctl = 0.95;
                if (params.balance == BalanceMode::PCTL_97) pctl = 0.97;
                std::vector<double> valsR, valsG, valsB;
                valsR.reserve(totalPixels); valsG.reserve(totalPixels); valsB.reserve(totalPixels);
                for (size_t p = 0; p < totalPixels; ++p) {
                    if (srcBgra[p * 4 + 3] == 0) continue;
                    valsR.push_back(linear[p * 3 + 0]);
                    valsG.push_back(linear[p * 3 + 1]);
                    valsB.push_back(linear[p * 3 + 2]);
                }
                double pR = percentileOf(valsR, pctl);
                double pG = percentileOf(valsG, pctl);
                double pB = percentileOf(valsB, pctl);
                scaleR = pG / pR;
                scaleB = pG / pB;
                break;
            }
            case BalanceMode::WHITE_PATCH: {
                double maxR = 0, maxG = 0, maxB = 0;
                for (size_t p = 0; p < totalPixels; ++p) {
                    if (srcBgra[p * 4 + 3] == 0) continue;
                    maxR = std::max(maxR, linear[p * 3 + 0]);
                    maxG = std::max(maxG, linear[p * 3 + 1]);
                    maxB = std::max(maxB, linear[p * 3 + 2]);
                }
                if (maxG > 0) { scaleR = maxG / maxR; scaleB = maxG / maxB; }
                break;
            }
            case BalanceMode::SHADES_OF_GREY_P2:
                computeChannelBalance(linear, totalPixels, srcBgra.data(), 2, scaleR, scaleB);
                break;
            case BalanceMode::SHADES_OF_GREY_P4:
                computeChannelBalance(linear, totalPixels, srcBgra.data(), 4, scaleR, scaleB);
                break;
            case BalanceMode::SHADES_OF_GREY_P8:
                computeChannelBalance(linear, totalPixels, srcBgra.data(), 8, scaleR, scaleB);
                break;
            case BalanceMode::MEDIAN_GREY: {
                std::vector<double> valsR, valsG, valsB;
                valsR.reserve(totalPixels); valsG.reserve(totalPixels); valsB.reserve(totalPixels);
                for (size_t p = 0; p < totalPixels; ++p) {
                    if (srcBgra[p * 4 + 3] == 0) continue;
                    valsR.push_back(linear[p * 3 + 0]);
                    valsG.push_back(linear[p * 3 + 1]);
                    valsB.push_back(linear[p * 3 + 2]);
                }
                double mR = medianOf(valsR);
                double mG = medianOf(valsG);
                double mB = medianOf(valsB);
                scaleR = mG / mR;
                scaleB = mG / mB;
                break;
            }
            case BalanceMode::MANUAL:
                manualScaleR = params.manual_r;
                manualScaleG = params.manual_g;
                manualScaleB = params.manual_b;
                break;
            default: break;
        }

        if (params.balance != BalanceMode::MANUAL) {
            if (verbose) std::cout << "  Balance: scaleR=" << scaleR << " scaleB=" << scaleB << std::endl;
            for (size_t p = 0; p < totalPixels; ++p) {
                linear[p * 3 + 0] *= scaleR;
                linear[p * 3 + 2] *= scaleB;
            }
        } else {
            if (verbose) std::cout << "  Balance MANUAL: R*=" << manualScaleR
                                   << " G*=" << manualScaleG << " B*=" << manualScaleB << std::endl;
            for (size_t p = 0; p < totalPixels; ++p) {
                linear[p * 3 + 0] *= manualScaleR;
                linear[p * 3 + 1] *= manualScaleG;
                linear[p * 3 + 2] *= manualScaleB;
            }
        }
    }

    // Step 2: Auto brightness via percentile stretching
    if (params.auto_bright_pctl > 0.0) {
        std::vector<double> allVals;
        allVals.reserve(totalPixels);
        for (size_t p = 0; p < totalPixels; ++p) {
            if (srcBgra[p * 4 + 3] == 0) continue;
            allVals.push_back(std::max({linear[p * 3 + 0], linear[p * 3 + 1], linear[p * 3 + 2]}));
        }
        if (!allVals.empty()) {
            size_t idx = static_cast<size_t>(allVals.size() * params.auto_bright_pctl);
            if (idx >= allVals.size()) idx = allVals.size() - 1;
            std::nth_element(allVals.begin(), allVals.begin() + idx, allVals.end());
            double pVal = allVals[idx];
            if (pVal > 0) {
                double scale = std::min(65535.0 / pVal, params.max_scale);
                for (auto& v : linear) v *= scale;
            }
        }
    }

    // Step 3: Gamma correction
    const double invGamma = 1.0 / params.gamma;
    const double toe = params.toe_slope;

    auto applyGamma = [invGamma, toe](double v) -> uint16_t {
        double n = std::min(v, 65535.0) / 65535.0;
        double g = (toe != 0) ? ((1.0 + toe) * std::pow(n, invGamma) - toe * n)
                              : std::pow(n, invGamma);
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
// Stats & scoring
// ============================================================================

struct Stats {
    double avgR, avgG, avgB;
    double rgRatio, bgRatio;  // target: both == 1.0
    double neutralityScore;   // lower is better (0 = perfect)
};

static Stats computeStats(const std::vector<uint16_t>& bgra, int width, int height) {
    const size_t n = static_cast<size_t>(width) * height;
    double sumR = 0, sumG = 0, sumB = 0;
    size_t valid = 0;
    for (size_t i = 0; i < n; ++i) {
        if (bgra[i * 4 + 3] == 0) continue;
        valid++;
        sumR += bgra[i * 4 + 2];
        sumG += bgra[i * 4 + 1];
        sumB += bgra[i * 4 + 0];
    }
    Stats s{};
    if (valid > 0) {
        s.avgR = sumR / valid;
        s.avgG = sumG / valid;
        s.avgB = sumB / valid;
        s.rgRatio = (s.avgG > 0) ? s.avgR / s.avgG : 0;
        s.bgRatio = (s.avgG > 0) ? s.avgB / s.avgG : 0;
        s.neutralityScore = std::abs(s.rgRatio - 1.0) + std::abs(s.bgRatio - 1.0);
    }
    return s;
}

// ============================================================================
// Save helpers
// ============================================================================

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
    return cv::imwrite(path, img16, {cv::IMWRITE_PNG_COMPRESSION, 3});
}

// ============================================================================
// Result record
// ============================================================================

struct Result {
    std::string label;
    std::string suffix;
    Stats stats;
    bool isLibRaw;    // true = LibRaw-only result, false = post-processed
    LoadParams loadParams;
    PostProcessParams ppParams;
};

static std::string balanceName(BalanceMode b) {
    switch (b) {
        case BalanceMode::NONE: return "none";
        case BalanceMode::GREY_WORLD: return "grey_world";
        case BalanceMode::PCTL_GREY: return "pctl99";
        case BalanceMode::WHITE_PATCH: return "white_patch";
        case BalanceMode::SHADES_OF_GREY_P2: return "sog_p2";
        case BalanceMode::SHADES_OF_GREY_P4: return "sog_p4";
        case BalanceMode::SHADES_OF_GREY_P8: return "sog_p8";
        case BalanceMode::MEDIAN_GREY: return "median";
        case BalanceMode::PCTL_90: return "pctl90";
        case BalanceMode::PCTL_95: return "pctl95";
        case BalanceMode::PCTL_97: return "pctl97";
        case BalanceMode::MANUAL: return "manual";
        default: return "?";
    }
}

// ============================================================================
// MAIN: Aggressive multi-dimensional sweep
// ============================================================================

int main() {
    std::string rawPath = R"(F:\mime\star_align\dngs\origin_40229821_raw.dng)";
    if (!fs::exists(rawPath)) {
        std::cerr << "File not found: " << rawPath << std::endl;
        return 1;
    }

    std::string stem = fs::path(rawPath).stem().string();
    std::string outDir = fs::path(rawPath).parent_path().string();

    // Output subdirectory for aggressive sweep results
    std::string sweepDir = outDir + "/aggressive_sweep";
    fs::create_directories(sweepDir);

    std::vector<Result> results;

    std::cout << "==========================================" << std::endl;
    std::cout << "  AGGRESSIVE MULTI-DIMENSIONAL WB SWEEP  " << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Input: " << rawPath << std::endl;
    std::cout << "Output: " << sweepDir << std::endl << std::endl;

    // ===================================================================
    // PHASE 1: LibRaw load-time parameter matrix
    // ===================================================================
    std::cout << "\n=== PHASE 1: LibRaw load-time matrix ===" << std::endl;

    // 1A: WB mode matrix (camera_wb × auto_wb)
    {
        struct WBConfig { int cam; int autoWb; const char* name; const char* suffix; };
        WBConfig wbConfigs[] = {
            {1, 0, "CAM_WB",       "lr_cam"},
            {0, 1, "AUTO_WB",      "lr_auto"},
            {1, 1, "CAM+AUTO_WB",  "lr_cam_auto"},
            {0, 0, "NO_WB",        "lr_nowb"},
        };

        // Color spaces
        struct ColorSpace { int id; const char* name; const char* suffix; };
        ColorSpace colorSpaces[] = {
            {0, "RawLinear", "cs_raw"},
            {1, "sRGB",      "cs_srgb"},
            {2, "AdobeRGB",  "cs_ado"},
            {4, "ProPhoto",  "cs_pro"},
            {5, "WideGamut", "cs_wide"},
        };

        // Highlight modes
        struct HighlightMode { int id; const char* name; const char* suffix; };
        HighlightMode hlModes[] = {
            {0, "Clip",        "hl0"},
            {1, "Unclip",      "hl1"},
            {2, "Blend",       "hl2"},
            {4, "Recon1",      "hl4"},
            {5, "Recon2",      "hl5"},
        };

        // 1A-i: WB × ColorSpace (linear gamma, no auto bright)
        std::cout << "\n--- 1A: WB mode × Color Space ---" << std::endl;
        for (auto& wb : wbConfigs) {
            for (auto& cs : colorSpaces) {
                LoadParams lp;
                lp.use_camera_wb = wb.cam;
                lp.use_auto_wb = wb.autoWb;
                lp.output_color = cs.id;
                lp.gamm0 = 1.0; lp.gamm1 = 1.0;
                lp.no_auto_bright = 1;
                lp.exp_correc = 0;
                lp.highlight = 0;

                std::string suffix = std::string("p1_") + wb.suffix + "_" + cs.suffix;
                std::string label = std::string("P1_") + wb.name + "_" + cs.name;

                RawImage img;
                if (!loadBGRA(rawPath, img, lp)) {
                    std::cout << "  SKIP " << label << " (load failed)" << std::endl;
                    continue;
                }

                auto st = computeStats(img.bgra, img.width, img.height);
                std::cout << "  " << label << " R/G=" << std::fixed << std::setprecision(4)
                          << st.rgRatio << " B/G=" << st.bgRatio
                          << " score=" << st.neutralityScore << std::endl;

                results.push_back({label, suffix, st, true, lp, {}});
            }
        }

        // 1A-ii: Best WB × Highlight mode (sRGB only)
        std::cout << "\n--- 1B: Best WB × Highlight mode ---" << std::endl;
        for (auto& wb : wbConfigs) {
            for (auto& hl : hlModes) {
                LoadParams lp;
                lp.use_camera_wb = wb.cam;
                lp.use_auto_wb = wb.autoWb;
                lp.output_color = 1; // sRGB
                lp.gamm0 = 1.0; lp.gamm1 = 1.0;
                lp.no_auto_bright = 1;
                lp.highlight = hl.id;

                std::string suffix = std::string("p1_hl_") + wb.suffix + "_" + hl.suffix;
                std::string label = std::string("P1_") + wb.name + "_" + hl.name;

                RawImage img;
                if (!loadBGRA(rawPath, img, lp)) {
                    std::cout << "  SKIP " << label << std::endl;
                    continue;
                }

                auto st = computeStats(img.bgra, img.width, img.height);
                std::cout << "  " << label << " R/G=" << std::fixed << std::setprecision(4)
                          << st.rgRatio << " B/G=" << st.bgRatio
                          << " score=" << st.neutralityScore << std::endl;

                results.push_back({label, suffix, st, true, lp, {}});
            }
        }

        // 1C: Manual cam_mul overrides (aggressive exploration)
        std::cout << "\n--- 1C: Manual cam_mul overrides ---" << std::endl;
        {
            // cam_mul was 2.34825, 1, 2.71498
            // Try variations around these values, and some aggressive departures
            struct MulOverride { double r, g, b; const char* name; };
            MulOverride mulOverrides[] = {
                // Original camera values
                {2.34825, 1.0, 2.71498, "cam_orig"},
                // Equalize: normalize so G=1
                {2.3, 1.0, 2.7, "near_cam"},
                // Try forcing neutral: reduce R, boost B
                {1.8, 1.0, 3.0, "lessR_moreB"},
                {2.0, 1.0, 3.2, "cool_shift"},
                {2.0, 1.0, 2.5, "warm_shift"},
                {2.5, 1.0, 3.5, "both_boost"},
                // Extreme: daylight simulation (typically high B, moderate R)
                {1.5, 1.0, 2.0, "daylight_soft"},
                {1.2, 1.0, 1.5, "daylight_warm"},
                {1.8, 1.0, 2.5, "daylight_neutral"},
                // Tungsten (warm: low B)
                {2.5, 1.0, 2.0, "tungsten"},
                // Shade (cool: high B)
                {2.0, 1.0, 3.5, "shade"},
                // Flash
                {2.2, 1.0, 2.5, "flash"},
                // Extreme corrections
                {1.0, 1.0, 1.0, "identity"},
                {1.5, 1.0, 1.5, "identity_15"},
                {3.0, 1.0, 3.5, "heavy_both"},
                // Daylight multipliers from standard illuminants
                {2.1, 1.0, 2.9, "d65_approx"},
                {1.9, 1.0, 3.1, "d55_approx"},
            };

            for (auto& mo : mulOverrides) {
                LoadParams lp;
                lp.cam_mul_ovr[0] = mo.r;
                lp.cam_mul_ovr[1] = mo.g;
                lp.cam_mul_ovr[2] = mo.b;
                lp.cam_mul_ovr[3] = 0;
                lp.output_color = 1;
                lp.gamm0 = 1.0; lp.gamm1 = 1.0;
                lp.no_auto_bright = 1;

                std::string suffix = std::string("p1_mul_") + mo.name;
                std::string label = std::string("P1_MUL_") + mo.name;

                RawImage img;
                if (!loadBGRA(rawPath, img, lp)) {
                    std::cout << "  SKIP " << label << std::endl;
                    continue;
                }

                auto st = computeStats(img.bgra, img.width, img.height);
                std::cout << "  " << label << " R/G=" << std::fixed << std::setprecision(4)
                          << st.rgRatio << " B/G=" << st.bgRatio
                          << " score=" << st.neutralityScore << std::endl;

                results.push_back({label, suffix, st, true, lp, {}});
            }
        }

        // 1D: LibRaw bright/exp/gamma combinations
        std::cout << "\n--- 1D: LibRaw pretty mode variants ---" << std::endl;
        {
            struct PrettyConfig {
                double g0, g1; int noAB; double expShift; float bright;
                const char* name;
            };
            PrettyConfig prettyConfigs[] = {
                // Linear variants
                {1.0, 1.0, 1, 1.0, 1.0f, "linear"},
                {1.0, 1.0, 1, 1.5, 1.0f, "linear_exp15"},
                {1.0, 1.0, 1, 2.0, 1.0f, "linear_exp20"},
                {1.0, 1.0, 1, 3.0, 1.0f, "linear_exp30"},
                // Standard gamma variants
                {2.2, 4.5, 0, 1.0, 1.0f, "gamma22"},
                {2.2, 4.5, 0, 1.0, 1.5f, "gamma22_b15"},
                {2.2, 4.5, 0, 1.0, 2.0f, "gamma22_b20"},
                {2.2, 4.5, 0, 1.2, 1.0f, "gamma22_e12"},
                {2.2, 4.5, 0, 1.5, 1.0f, "gamma22_e15"},
                {2.2, 4.5, 0, 2.0, 1.0f, "gamma22_e20"},
                {2.2, 4.5, 0, 3.0, 1.0f, "gamma22_e30"},
                // sRGB-like gamma
                {2.4, 12.92, 0, 1.0, 1.0f, "srgb"},
                {2.4, 12.92, 0, 1.5, 1.0f, "srgb_e15"},
                {2.4, 12.92, 0, 2.0, 1.0f, "srgb_e20"},
                // Aggressive bright
                {2.2, 4.5, 0, 1.0, 3.0f, "gamma22_b30"},
                {2.2, 4.5, 0, 2.0, 2.0f, "gamma22_e2b2"},
            };

            // Try each with camera_wb and auto_wb
            for (auto& pc : prettyConfigs) {
                for (int wbMode = 0; wbMode < 2; wbMode++) {
                    LoadParams lp;
                    lp.use_camera_wb = (wbMode == 0) ? 1 : 0;
                    lp.use_auto_wb   = (wbMode == 0) ? 0 : 1;
                    lp.gamm0 = pc.g0; lp.gamm1 = pc.g1;
                    lp.no_auto_bright = pc.noAB;
                    lp.exp_correc = (pc.expShift != 1.0) ? 1 : 0;
                    lp.exp_shift = pc.expShift;
                    lp.bright = pc.bright;
                    lp.output_color = 1;

                    std::string wbTag = (wbMode == 0) ? "cam" : "auto";
                    std::string suffix = "p1_pretty_" + std::string(pc.name) + "_" + wbTag;
                    std::string label = "P1_" + std::string(pc.name) + "_" + wbTag;

                    RawImage img;
                    if (!loadBGRA(rawPath, img, lp)) continue;

                    auto st = computeStats(img.bgra, img.width, img.height);
                    std::cout << "  " << label << " R/G=" << std::fixed << std::setprecision(4)
                              << st.rgRatio << " B/G=" << st.bgRatio
                              << " score=" << st.neutralityScore << std::endl;

                    results.push_back({label, suffix, st, true, lp, {}});
                }
            }
        }
    }

    // ===================================================================
    // PHASE 2: Post-process grid search on LINEAR base image
    // ===================================================================
    std::cout << "\n=== PHASE 2: Post-process grid search ===" << std::endl;

    // Load linear base
    LoadParams linearLoad;
    linearLoad.use_camera_wb = 1;
    linearLoad.use_auto_wb = 0;
    linearLoad.no_auto_bright = 1;
    linearLoad.gamm0 = 1.0; linearLoad.gamm1 = 1.0;
    linearLoad.output_color = 1;

    RawImage linearBase;
    if (!loadBGRA(rawPath, linearBase, linearLoad)) {
        std::cerr << "Failed to load linear base" << std::endl;
        return 1;
    }
    std::cout << "Linear base loaded: " << linearBase.width << "x" << linearBase.height << std::endl;

    // 2A: Balance mode sweep (all modes × selected gamma)
    std::cout << "\n--- 2A: Balance mode × gamma ---" << std::endl;
    {
        BalanceMode balances[] = {
            BalanceMode::NONE,
            BalanceMode::GREY_WORLD,
            BalanceMode::PCTL_GREY,
            BalanceMode::WHITE_PATCH,
            BalanceMode::SHADES_OF_GREY_P2,
            BalanceMode::SHADES_OF_GREY_P4,
            BalanceMode::SHADES_OF_GREY_P8,
            BalanceMode::MEDIAN_GREY,
            BalanceMode::PCTL_90,
            BalanceMode::PCTL_95,
            BalanceMode::PCTL_97,
        };
        double gammas[] = {1.0, 1.4, 1.8, 2.0, 2.2, 2.6, 3.0};
        double toes[] = {0.0, 4.5};

        int idx = 0;
        for (auto bal : balances) {
            for (double g : gammas) {
                for (double toe : toes) {
                    PostProcessParams pp;
                    pp.balance = bal;
                    pp.gamma = g;
                    pp.toe_slope = toe;
                    pp.exp_shift = 1.2;
                    pp.auto_bright_pctl = 0.99;

                    std::ostringstream ss;
                    ss << "p2_bal_" << balanceName(bal)
                       << "_g" << (int)(g * 10) << "_t" << (int)(toe * 10);
                    std::string suffix = ss.str();
                    std::string label = "P2_" + balanceName(bal)
                                        + "_G" + std::to_string((int)(g * 10))
                                        + "_T" + std::to_string((int)(toe * 10));

                    RawImage out;
                    out.width = linearBase.width;
                    out.height = linearBase.height;
                    postProcess(linearBase.bgra, out.bgra, linearBase.width, linearBase.height, pp);

                    auto st = computeStats(out.bgra, out.width, out.height);
                    if (idx % 20 == 0) {
                        std::cout << "  [sample] " << label
                                  << " R/G=" << std::fixed << std::setprecision(4) << st.rgRatio
                                  << " B/G=" << st.bgRatio
                                  << " score=" << st.neutralityScore << std::endl;
                    }

                    results.push_back({label, suffix, st, false, {}, pp});
                    idx++;
                }
            }
        }
        std::cout << "  2A total: " << idx << " combinations" << std::endl;
    }

    // 2B: Exposure × balance × auto_bright percentile
    std::cout << "\n--- 2B: exp_shift × balance × auto_bright_pctl ---" << std::endl;
    {
        double expShifts[] = {0.5, 0.8, 1.0, 1.2, 1.5, 2.0, 3.0, 5.0};
        BalanceMode topBalances[] = {
            BalanceMode::NONE,
            BalanceMode::GREY_WORLD,
            BalanceMode::SHADES_OF_GREY_P4,
            BalanceMode::MEDIAN_GREY,
        };
        double brightPctls[] = {0.0, 0.90, 0.95, 0.99, 0.995, 0.999};

        int idx = 0;
        for (double exp : expShifts) {
            for (auto bal : topBalances) {
                for (double bp : brightPctls) {
                    PostProcessParams pp;
                    pp.exp_shift = exp;
                    pp.balance = bal;
                    pp.auto_bright_pctl = bp;
                    pp.gamma = 2.2;
                    pp.toe_slope = 4.5;

                    std::ostringstream ss;
                    ss << "p2_exp_e" << (int)(exp * 10)
                       << "_" << balanceName(bal)
                       << "_ab" << (int)(bp * 1000);
                    std::string suffix = ss.str();

                    std::string label = "P2_EXP" + std::to_string((int)(exp * 10))
                                        + "_" + balanceName(bal)
                                        + "_AB" + std::to_string((int)(bp * 1000));

                    RawImage out;
                    out.width = linearBase.width;
                    out.height = linearBase.height;
                    postProcess(linearBase.bgra, out.bgra, linearBase.width, linearBase.height, pp);

                    auto st = computeStats(out.bgra, out.width, out.height);
                    results.push_back({label, suffix, st, false, {}, pp});
                    idx++;
                }
            }
        }
        std::cout << "  2B total: " << idx << " combinations" << std::endl;
    }

    // 2C: Manual channel multiplier sweep
    std::cout << "\n--- 2C: Manual channel multiplier sweep ---" << std::endl;
    {
        double rMul[] = {0.5, 0.7, 0.8, 0.9, 0.95, 1.0, 1.05, 1.1, 1.2, 1.5};
        double bMul[] = {0.5, 0.7, 0.8, 0.9, 0.95, 1.0, 1.05, 1.1, 1.2, 1.5, 2.0};

        int idx = 0;
        for (double r : rMul) {
            for (double b : bMul) {
                PostProcessParams pp;
                pp.balance = BalanceMode::MANUAL;
                pp.manual_r = r;
                pp.manual_g = 1.0;
                pp.manual_b = b;
                pp.gamma = 2.2;
                pp.toe_slope = 4.5;
                pp.exp_shift = 1.2;
                pp.auto_bright_pctl = 0.99;

                std::ostringstream ss;
                ss << "p2_man_r" << (int)(r * 100) << "_b" << (int)(b * 100);

                std::string label = "P2_MAN_R" + std::to_string((int)(r * 100))
                                    + "_B" + std::to_string((int)(b * 100));

                RawImage out;
                out.width = linearBase.width;
                out.height = linearBase.height;
                postProcess(linearBase.bgra, out.bgra, linearBase.width, linearBase.height, pp);

                auto st = computeStats(out.bgra, out.width, out.height);
                results.push_back({label, ss.str(), st, false, {}, pp});
                idx++;
            }
        }
        std::cout << "  2C total: " << idx << " combinations" << std::endl;
    }

    // 2D: Extreme gamma / toe exploration
    std::cout << "\n--- 2D: Extreme gamma/toe with best balance ---" << std::endl;
    {
        double gammas[] = {1.0, 1.2, 1.4, 1.6, 1.8, 2.0, 2.2, 2.4, 2.6, 2.8, 3.0, 3.5, 4.0};
        double toes[] = {0.0, 1.0, 2.0, 3.0, 4.5, 6.0, 8.0, 10.0, 15.0, 20.0};
        BalanceMode bestBal = BalanceMode::GREY_WORLD;

        int idx = 0;
        for (double g : gammas) {
            for (double t : toes) {
                PostProcessParams pp;
                pp.balance = bestBal;
                pp.gamma = g;
                pp.toe_slope = t;
                pp.exp_shift = 1.2;
                pp.auto_bright_pctl = 0.99;

                std::ostringstream ss;
                ss << "p2_gtm_g" << (int)(g * 10) << "_t" << (int)(t * 10);

                std::string label = "P2_G" + std::to_string((int)(g * 10))
                                    + "_T" + std::to_string((int)(t * 10));

                RawImage out;
                out.width = linearBase.width;
                out.height = linearBase.height;
                postProcess(linearBase.bgra, out.bgra, linearBase.width, linearBase.height, pp);

                auto st = computeStats(out.bgra, out.width, out.height);
                results.push_back({label, ss.str(), st, false, {}, pp});
                idx++;
            }
        }
        std::cout << "  2D total: " << idx << " combinations" << std::endl;
    }

    // ===================================================================
    // PHASE 3: Rank and save top candidates
    // ===================================================================
    std::cout << "\n=== PHASE 3: Ranking results ===" << std::endl;

    // Sort by neutrality score (lower = better)
    std::sort(results.begin(), results.end(),
              [](const Result& a, const Result& b) {
                  return a.stats.neutralityScore < b.stats.neutralityScore;
              });

    // Write CSV report
    std::string csvPath = sweepDir + "/" + stem + "_report.csv";
    {
        std::ofstream csv(csvPath);
        csv << "rank,score,R/G,B/G,avgR,avgG,avgB,label,suffix,type" << std::endl;
        for (size_t i = 0; i < results.size(); i++) {
            auto& r = results[i];
            csv << i << ","
                << r.stats.neutralityScore << ","
                << r.stats.rgRatio << ","
                << r.stats.bgRatio << ","
                << r.stats.avgR << ","
                << r.stats.avgG << ","
                << r.stats.avgB << ","
                << "\"" << r.label << "\","
                << r.suffix << ","
                << (r.isLibRaw ? "libraw" : "postproc")
                << std::endl;
        }
        csv.close();
    }
    std::cout << "CSV report: " << csvPath << std::endl;

    // Print top 30
    std::cout << "\n=== TOP 30 (by neutrality score) ===" << std::endl;
    int topN = std::min(30, (int)results.size());
    for (int i = 0; i < topN; i++) {
        auto& r = results[i];
        std::cout << "  #" << std::setw(2) << i
                  << " score=" << std::fixed << std::setprecision(4) << r.stats.neutralityScore
                  << " R/G=" << r.stats.rgRatio
                  << " B/G=" << r.stats.bgRatio
                  << " [" << (r.isLibRaw ? "LR" : "PP") << "] "
                  << r.label << std::endl;
    }

    // Save top 20 PNGs
    std::cout << "\n=== Saving top 20 PNGs ===" << std::endl;
    int saveN = std::min(20, (int)results.size());
    for (int i = 0; i < saveN; i++) {
        auto& r = results[i];
        std::string pngPath = sweepDir + "/" + stem + "_top" + std::to_string(i) + "_" + r.suffix + ".png";

        if (r.isLibRaw) {
            // Re-load from LibRaw
            RawImage img;
            if (loadBGRA(rawPath, img, r.loadParams)) {
                savePNG(pngPath, img.bgra, img.width, img.height);
                std::cout << "  Saved #" << i << ": " << r.label << std::endl;
            }
        } else {
            // Re-run post-process
            RawImage out;
            out.width = linearBase.width;
            out.height = linearBase.height;
            postProcess(linearBase.bgra, out.bgra, linearBase.width, linearBase.height, r.ppParams);
            savePNG(pngPath, out.bgra, out.width, out.height);
            std::cout << "  Saved #" << i << ": " << r.label << std::endl;
        }
    }

    std::cout << "\n=== Aggressive sweep complete ===" << std::endl;
    std::cout << "Total combinations tested: " << results.size() << std::endl;
    std::cout << "Top 20 PNGs in: " << sweepDir << std::endl;
    std::cout << "Full CSV report: " << csvPath << std::endl;

    return 0;
}
