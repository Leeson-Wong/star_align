#pragma once

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <opencv2/opencv.hpp>

// ============================================================================
// Linear RGBA16 -> display-ready RGBA16 post-processing
// Matches "toe_slope=0" visual: exp_shift + p99 auto bright + pure power gamma
//
// Input:  linear RGBA16 (R,G,B,A each uint16_t, alpha=0xFFFF or 0 for invalid)
// Output: display-ready RGBA16 written back to the same buffer (in-place)
// ============================================================================

struct PostProcessConfig {
    double exp_shift        = 1.20;   // exposure multiplier
    double auto_bright_pctl = 0.99;   // percentile for auto brightness (0 = disabled)
    double gamma            = 2.2;    // gamma power (pure power law, no toe slope)
    double max_scale        = 10.0;   // safety clamp for auto brightness scale
};

static const PostProcessConfig DEFAULT_POST_PROCESS = {};

inline void postProcessRGBA16(uint16_t* rgba, int width, int height,
                              const PostProcessConfig& cfg = DEFAULT_POST_PROCESS) {
    const size_t n = static_cast<size_t>(width) * height;
    const double invGamma = 1.0 / cfg.gamma;

    // Step 1: Exposure + auto brightness into float buffer
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

    // Step 2: Auto brightness via percentile
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

    // Step 3: Pure power gamma (no toe slope)
    for (size_t p = 0; p < n; ++p) {
        if (rgba[p * 4 + 3] == 0) continue;
        for (int c = 0; c < 3; ++c) {
            double norm = std::min(linear[p * 3 + c], 65535.0) / 65535.0;
            double g = std::pow(norm, invGamma);
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
