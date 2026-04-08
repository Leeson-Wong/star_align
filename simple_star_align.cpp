// Simple test program for star_align
#include "star_align.h"
#include <iostream>
#include <vector>
#include <cstdint>

int main() {
    // Create a simple test image (100x100)
    const int width = 100;
    const int height = 100;
    std::vector<uint16_t> testImage(width * height * 4, 0);

    // Add a bright star at center
    int cx = width / 2;
    int cy = height / 2;
    for (int dy = -5; dy <= 5; ++dy) {
        for (int dx = -5; dx <= 5; ++dx) {
            if (cx + dx >= 0 && cx + dx < width && cy + dy >= 0 && cy + dy < height) {
                int idx = ((cy + dy) * width + (cx + dx)) * 4;
                testImage[idx + 0] = 0xFFFF;  // B
                testImage[idx + 1] = 0xFFFF;  // G
                testImage[idx + 2] = 0xFFFF;  // R
                testImage[idx + 3] = 0xFFFF;  // A
            }
        }
    }

    // Detect stars
    StarAlign::DetectParams params;
    params.threshold = 0.1;

    auto stars = StarAlign::detectStars(testImage.data(), width, height, width * 4 * sizeof(uint16_t), params);

    std::cout << "Detected " << stars.size() << " stars" << std::endl;

    // Test alignment with same image
    auto result = StarAlign::computeAlignment(stars, stars, width, height);

    if (result.success) {
        std::cout << "Alignment successful!" << std::endl;
        std::cout << "Offset: (" << result.offsetX << ", " << result.offsetY << ")" << std::endl;
        std::cout << "Angle: " << result.angle << " radians" << std::endl;
    } else {
        std::cout << "Alignment failed" << std::endl;
    }

    return 0;
}