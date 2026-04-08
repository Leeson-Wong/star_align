// Helper functions for testing
#include <vector>
#include <cstdint>

struct TestImage {
    std::vector<uint16_t> bgra;
    int width;
    int height;
};

// Create a synthetic image with stars
inline TestImage createTestImage(int w, int h) {
    TestImage img;
    img.width = w;
    img.height = h;
    img.bgra.resize(w * h * 4, 0);

    // Add stars at known positions
    struct Star {
        int x, y;
        int brightness;
    };

    Star stars[] = {
        {w/4, h/4, 60000},
        {w*3/4, h/4, 50000},
        {w/2, h/2, 70000},
        {w/4, h*3/4, 45000},
        {w*3/4, h*3/4, 55000}
    };

    for (const auto& star : stars) {
        for (int dy = -5; dy <= 5; ++dy) {
            for (int dx = -5; dx <= 5; ++dx) {
                int px = star.x + dx;
                int py = star.y + dy;
                if (px >= 0 && px < w && py >= 0 && py < h) {
                    int idx = (py * w + px) * 4;
                    int dist = dx*dx + dy*dy;
                    int val = star.brightness * (1 - dist/50.0);
                    if (val > 0) {
                        img.bgra[idx + 0] = val;  // B
                        img.bgra[idx + 1] = val;  // G
                        img.bgra[idx + 2] = val;  // R
                        img.bgra[idx + 3] = 65535; // A
                    }
                }
            }
        }
    }

    return img;
}