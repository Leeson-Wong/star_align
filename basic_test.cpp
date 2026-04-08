// Basic functionality test for star_align
#include "star_align.h"
#include <iostream>
#include <vector>
#include <cstdint>

int main() {
    std::cout << "=== Star Align Basic Test ===" << std::endl;

    // Create a simple test image (50x50)
    const int width = 50;
    const int height = 50;
    std::vector<uint16_t> testImage(width * height * 4, 0);

    // Add a bright star at center
    int cx = width / 2;
    int cy = height / 2;
    for (int dy = -3; dy <= 3; ++dy) {
        for (int dx = -3; dx <= 3; ++dx) {
            if (cx + dx >= 0 && cx + dx < width && cy + dy >= 0 && cy + dy < height) {
                int idx = ((cy + dy) * width + (cx + dx)) * 4;
                // Create a bright spot
                testImage[idx + 0] = 30000;  // B
                testImage[idx + 1] = 30000;  // G
                testImage[idx + 2] = 30000;  // R
                testImage[idx + 3] = 65535;  // A
            }
        }
    }

    // Add a dimmer star
    int cx2 = cx + 10;
    int cy2 = cy + 10;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            if (cx2 + dx >= 0 && cx2 + dx < width && cy2 + dy >= 0 && cy2 + dy < height) {
                int idx = ((cy2 + dy) * width + (cx2 + dx)) * 4;
                testImage[idx + 0] = 15000;  // B
                testImage[idx + 1] = 15000;  // G
                testImage[idx + 2] = 15000;  // R
                testImage[idx + 3] = 65535;  // A
            }
        }
    }

    int stride = width * 4 * sizeof(uint16_t);

    // Test 1: Detect stars with different thresholds
    std::cout << "\n--- Star Detection ---" << std::endl;

    StarAlign::DetectParams params;
    params.threshold = 0.1;
    auto stars_low = StarAlign::detectStars(testImage.data(), width, height, stride, params);
    std::cout << "Threshold 0.1: " << stars_low.size() << " stars detected" << std::endl;

    params.threshold = 0.3;
    auto stars_high = StarAlign::detectStars(testImage.data(), width, height, stride, params);
    std::cout << "Threshold 0.3: " << stars_high.size() << " stars detected" << std::endl;

    // Test 2: Transform the image
    std::cout << "\n--- Image Transformation ---" << std::endl;

    auto transformed = StarAlign::transformBGRA(testImage.data(), width, height, stride,
        StarAlign::AlignResult{});
    std::cout << "Transformed image size: " << transformed.size() << " elements" << std::endl;

    // Test 3: Self-alignment (should be zero offset)
    std::cout << "\n--- Self-Alignment ---" << std::endl;

    auto result = StarAlign::computeAlignment(stars_low, stars_low, width, height);
    std::cout << "Self-alignment result: " << (result.success ? "SUCCESS" : "FAILED") << std::endl;
    if (result.success) {
        std::cout << "  Offset: (" << result.offsetX << ", " << result.offsetY << ")" << std::endl;
        std::cout << "  Angle: " << result.angle << " radians ("
                  << result.angle * 180.0 / 3.14159 << " degrees)" << std::endl;
        std::cout <<  "  Matched stars: " << result.matchedStars << std::endl;
        std::cout << "  Transform type: " << result.transformType << std::endl;
    }

    std::cout << "\n=== Test Complete ===" << std::endl;
    return 0;
}