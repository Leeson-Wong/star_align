#pragma once

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <opencv2/opencv.hpp>

// ============================================================================
// Linear RGBA16 -> display-ready RGBA16 post-processing
//
// Pipeline: exposure -> GREY_WORLD balance -> auto brightness -> gamma(toe)
//
// Input:  linear RGBA16 (R,G,B,A each uint16_t, alpha=0xFFFF or 0 for invalid)
// Output: display-ready RGBA16 written back to the same buffer (in-place)
// ============================================================================

struct PostProcessConfig {
    double exp_shift           = 1.20;   // exposure multiplier
    double auto_bright_pctl    = 0.99;   // percentile for auto brightness (0 = disabled)
    double gamma               = 1.0;    // gamma power
    double toe_slope           = 10.0;   // toe compensation (only meaningful when gamma != 1.0)
    double max_scale           = 10.0;   // safety clamp for auto brightness scale
    bool   grey_world_balance  = true;   // scale R,B so avgR=avgG=avgB before gamma
};

static const PostProcessConfig DEFAULT_POST_PROCESS = {};

inline void postProcessRGBA16(uint16_t* rgba, int width, int height,
                              const PostProcessConfig& cfg = DEFAULT_POST_PROCESS) {
    const size_t n = static_cast<size_t>(width) * height;
    const double invGamma = 1.0 / cfg.gamma;

    // Step 1: Exposure into float buffer
    std::vector<double> linear(n * 3);

    for (size_t p = 0; p < n; ++p) {
        if (rgba[p * 4 + 3] == 0) {
            linear[p * 3 + 0] = 0;
            linear[p * 3 + 1] = 0;
            linear[p * 3 + 2] = 0;
            continue;
        }
        linear[p * 3 + 0] = rgba[p * 4 + 0] * cfg.exp_shift; // R
        linear[p * 3 + 1] = rgba[p * 4 + 1] * cfg.exp_shift; // G
        linear[p * 3 + 2] = rgba[p * 4 + 2] * cfg.exp_shift; // B
    }

    // Step 2: GREY_WORLD channel balance
    if (cfg.grey_world_balance) {
        double sumR = 0, sumG = 0, sumB = 0;
        size_t count = 0;
        for (size_t p = 0; p < n; ++p) {
            if (rgba[p * 4 + 3] == 0) continue;
            sumR += linear[p * 3 + 0];
            sumG += linear[p * 3 + 1];
            sumB += linear[p * 3 + 2];
            count++;
        }
        if (count > 0 && sumG > 0) {
            double scaleR = sumG / sumR;
            double scaleB = sumG / sumB;
            for (size_t p = 0; p < n; ++p) {
                linear[p * 3 + 0] *= scaleR;
                linear[p * 3 + 2] *= scaleB;
            }
        }
    }

    // Step 3: Auto brightness via percentile
    if (cfg.auto_bright_pctl > 0.0) {
        std::vector<double> allVals;
        allVals.reserve(n);
        for (size_t p = 0; p < n; ++p) {
            if (rgba[p * 4 + 3] == 0) continue;
            allVals.push_back(std::max({linear[p * 3 + 0],
                                        linear[p * 3 + 1],
                                        linear[p * 3 + 2]}));
        }
        if (!allVals.empty()) {
            size_t idx = static_cast<size_t>(allVals.size() * cfg.auto_bright_pctl);
            if (idx >= allVals.size()) idx = allVals.size() - 1;
            std::nth_element(allVals.begin(), allVals.begin() + idx, allVals.end());
            double pVal = allVals[idx];
            if (pVal > 0) {
                double scale = std::min(65535.0 / pVal, cfg.max_scale);
                for (auto& v : linear) v *= scale;
            }
        }
    }

    // Step 4: Gamma with toe slope
    const double toe = cfg.toe_slope;
    for (size_t p = 0; p < n; ++p) {
        if (rgba[p * 4 + 3] == 0) continue;
        for (int c = 0; c < 3; ++c) {
            double norm = std::min(linear[p * 3 + c], 65535.0) / 65535.0;
            double g;
            if (toe > 0)
                g = (1.0 + toe) * std::pow(norm, invGamma) - toe * norm;
            else
                g = std::pow(norm, invGamma);
            rgba[p * 4 + c] = static_cast<uint16_t>(std::round(
                std::max(0.0, std::min(1.0, g)) * 65535.0));
        }
    }
}

// ============================================================================
// Encode RGBA16 buffer to PNG bytes
// ============================================================================

inline bool encodeRGBA16ToPNG(const void* buffer, int width, int height,
                              std::vector<uchar>& png_data, int compression = 1,
                              const PostProcessConfig& cfg = DEFAULT_POST_PROCESS) {
    auto totalPixels = static_cast<size_t>(width) * height;

    // Copy + post-process (don't touch original data)
    std::vector<uint16_t> buf(static_cast<const uint16_t*>(buffer),
                              static_cast<const uint16_t*>(buffer) + totalPixels * 4);
    postProcessRGBA16(buf.data(), width, height, cfg);

    // RGBA16 -> BGR16
    cv::Mat bgr16(height, width, CV_16UC3);
    for (size_t i = 0; i < totalPixels; ++i) {
        bgr16.at<cv::Vec3w>(i)[0] = buf[i * 4 + 2]; // B
        bgr16.at<cv::Vec3w>(i)[1] = buf[i * 4 + 1]; // G
        bgr16.at<cv::Vec3w>(i)[2] = buf[i * 4 + 0]; // R
    }

    std::vector<int> params = {cv::IMWRITE_PNG_COMPRESSION, compression};
    return cv::imencode(".png", bgr16, png_data, params);
}
