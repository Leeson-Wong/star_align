// Standalone test program for star_align
#include "star_align.h"
#include "create_test_image.h"
#include <iostream>
#include <iomanip>

int main() {
    std::cout << "=== Star Align Standalone Test ===" << std::endl;

    // Create test images
    auto refImg = createTestImage(200, 200);
    auto tgtImg = createTestImage(200, 200);

    // Add slight offset to target image (simulating misalignment)
    for (int y = 0; y < tgtImg.height; ++y) {
        for (int x = 0; x < tgtImg.width; ++x) {
            int srcIdx = ((y - 2) * refImg.width + (x + 1)) * 4;
            int dstIdx = (y * tgtImg.width + x) * 4;

            if (y - 2 >= 0 && x + 1 < refImg.width) {
                tgtImg.bgra[dstIdx + 0] = refImg.bgra[srcIdx + 0];
                tgtImg.bgra[dstIdx + 1] = refImg.bgra[srcIdx + 1];
                tgtImg.bgra[dstIdx + 2] = refImg.bgra[srcIdx + 2];
                tgtImg.bgra[dstIdx + 3] = refImg.bgra[srcIdx + 3];
            } else {
                tgtImg.bgra[dstIdx + 3] = 0;  // Mark as invalid
            }
        }
    }

    int stride = refImg.width * 4 * sizeof(uint16_t);

    std::cout << "\n--- Star Detection ---" << std::endl;

    StarAlign::DetectParams params;
    params.threshold = 0.2;
    params.maxStarSize = 20;

    auto refStars = StarAlign::detectStars(refImg.bgra.data(), refImg.width, refImg.height, stride, params);
    auto tgtStars = StarAlign::detectStars(tgtImg.bgra.data(), tgtImg.width, tgtImg.height, stride, params);

    std::cout << "Reference stars: " << refStars.size() << std::endl;
    std::cout << "Target stars: " << tgtStars.size() << std::endl;

    // Show top 3 stars from reference
    int showCount = std::min(3, static_cast<int>(refStars.size()));
    std::cout << "\nTop " << showCount << " reference stars:" << std::endl;
    for (int i = 0; i < showCount; i++) {
        std::cout << "  #" << (i + 1) << ": (" << std::fixed << std::setprecision(1)
                  << refStars[i].x << ", " << refStars[i].y
                  << ") intensity=" << std::setprecision(3) << refStars[i].intensity
                  << std::endl;
    }

    std::cout << "\n--- Alignment ---" << std::endl;

    auto result = StarAlign::computeAlignment(refStars, tgtStars, refImg.width, refImg.height);

    std::cout << "Alignment " << (result.success ? "SUCCESS" : "FAILED") << std::endl;
    if (result.success) {
        std::cout << "  Offset: (" << std::fixed << std::setprecision(2)
                  << result.offsetX << ", " << result.offsetY << ")" << std::endl;
        std::cout << "  Angle: " << std::setprecision(3) << result.angle
                  << " rad (" << result.angle * 180.0 / 3.14159 << " deg)" << std::endl;
        std::cout << "  Matched stars: " << result.matchedStars << std::endl;
        std::cout << "  Transform type: " << result.transformType << std::endl;

        // Test transformation
        std::cout << "\n--- Image Transformation ---" << std::endl;
        auto transformed = StarAlign::transformBGRA(
            tgtImg.bgra.data(), tgtImg.width, tgtImg.height, stride, result);

        std::cout << "Transformed image size: " << transformed.size() << " elements" << std::endl;

        // Count non-zero pixels (should be most of them)
        int validPixels = 0;
        for (size_t i = 0; i < transformed.size(); i += 4) {
            if (transformed[i + 3] > 0) {
                validPixels++;
            }
        }
        std::cout << "Valid pixels in transformed image: " << validPixels
                  << " / " << (transformed.size() / 4) << std::endl;
    } else {
        std::cout << "  Could not align images - not enough stars or poor match" << std::endl;
    }

    std::cout << "\n=== Test Complete ===" << std::endl;
    return result.success ? 0 : 1;
}