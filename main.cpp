#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <iostream>
#include <iomanip>

#include "libraw.h"
#include "star_align.h"

namespace fs = std::filesystem;

// ---------- Load a DNG file via libraw, return BGRA16 data ----------
struct RawImage {
    std::vector<uint16_t> bgra;  // BGRA16: 4 x uint16_t per pixel
    int width = 0;
    int height = 0;
    int stride = 0;              // bytes per row = width * 4 * sizeof(uint16_t)
};

static bool loadDng(const std::string& path, RawImage& out) {
    LibRaw processor;
    processor.imgdata.params.output_bps    = 16;   // 16-bit output
    processor.imgdata.params.use_camera_wb = 1;    // use camera WB
    processor.imgdata.params.output_color  = 1;    // sRGB
    processor.imgdata.params.user_qual     = 0;    // bilinear demosaic (fast)
    processor.imgdata.params.output_tiff   = 0;    // no TIFF flags

    if (int ret = processor.open_file(path.c_str()); ret != LIBRAW_SUCCESS) {
        std::cerr << "  ERROR: cannot open " << path
                  << " (libraw error " << ret << ")\n";
        return false;
    }

    if (int ret = processor.unpack(); ret != LIBRAW_SUCCESS) {
        std::cerr << "  ERROR: cannot unpack " << path
                  << " (libraw error " << ret << ")\n";
        return false;
    }

    if (int ret = processor.dcraw_process(); ret != LIBRAW_SUCCESS) {
        std::cerr << "  ERROR: cannot process " << path
                  << " (libraw error " << ret << ")\n";
        return false;
    }

    int errcode = 0;
    libraw_processed_image_t* image = processor.dcraw_make_mem_image(&errcode);
    if (!image || errcode != LIBRAW_SUCCESS) {
        std::cerr << "  ERROR: dcraw_make_mem_image failed for " << path << "\n";
        return false;
    }

    if (image->type != LIBRAW_IMAGE_BITMAP || image->colors != 3 || image->bits != 16) {
        std::cerr << "  ERROR: unexpected image format (type=" << image->type
                  << " colors=" << image->colors << " bits=" << image->bits << ")\n";
        LibRaw::dcraw_clear_mem(image);
        return false;
    }

    const int w = image->width;
    const int h = image->height;
    const int strideBytes = w * 4 * sizeof(uint16_t);

    out.width  = w;
    out.height = h;
    out.stride = strideBytes;
    out.bgra.resize(static_cast<size_t>(w) * h * 4, 0);

    // libraw returns RGB 16-bit (3 channels, ushort per channel)
    const uint16_t* src = reinterpret_cast<const uint16_t*>(image->data);
    for (int row = 0; row < h; ++row) {
        const uint16_t* srcRow = src + static_cast<size_t>(row) * w * 3;
        uint16_t* dstRow = out.bgra.data() + static_cast<size_t>(row) * w * 4;
        for (int col = 0; col < w; ++col) {
            dstRow[col * 4 + 0] = srcRow[col * 3 + 2]; // B
            dstRow[col * 4 + 1] = srcRow[col * 3 + 1]; // G
            dstRow[col * 4 + 2] = srcRow[col * 3 + 0]; // R
            dstRow[col * 4 + 3] = 0xFFFF;               // A
        }
    }

    LibRaw::dcraw_clear_mem(image);
    return true;
}

// ---------- Collect DNG file paths sorted by timestamp (filename) ----------
static std::vector<fs::path> collectDngFiles(const fs::path& directory) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".dng") {
            files.push_back(entry.path());
        }
    }
    // Sort by filename (timestamp-encoded names)
    std::sort(files.begin(), files.end(),
        [](const fs::path& a, const fs::path& b) {
            return a.filename().string() < b.filename().string();
        });
    return files;
}

int main(int argc, char* argv[]) {
    const fs::path dngDir = argc > 1 ? fs::path(argv[1]) : fs::current_path() / "dngs";

    std::cout << "=== Star Alignment Tool ===" << std::endl;
    std::cout << "Scanning directory: " << dngDir.string() << std::endl;

    auto files = collectDngFiles(dngDir);
    if (files.size() < 2) {
        std::cerr << "ERROR: need at least 2 DNG files, found " << files.size() << std::endl;
        return 1;
    }

    std::cout << "Found " << files.size() << " DNG files (sorted by timestamp):" << std::endl;
    for (size_t i = 0; i < files.size(); ++i) {
        std::cout << "  [" << i << "] " << files[i].filename().string() << std::endl;
    }
    std::cout << std::endl;

    // ---- Load all images ----
    std::cout << "Loading images..." << std::endl;
    std::vector<RawImage> images(files.size());
    for (size_t i = 0; i < files.size(); ++i) {
        auto t0 = std::chrono::steady_clock::now();
        std::cout << "  Loading [" << i << "] " << files[i].filename().string() << " ... ";
        if (!loadDng(files[i].string(), images[i])) {
            std::cerr << "FAILED" << std::endl;
            return 1;
        }
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << images[i].width << "x" << images[i].height << " (" << ms << " ms)" << std::endl;
    }
    std::cout << std::endl;

    // ---- Detect stars in reference frame (frame 0) ----
    const int W = images[0].width;
    const int H = images[0].height;
    const int stride = images[0].stride;

    // Use higher threshold to get ~50-200 stars (suitable for triangle matching)
    StarAlign::DetectParams detectParams;
    detectParams.threshold = 0.5;

    // Keep only the brightest stars for matching (sort by intensity descending)
    // Triangle matching is O(n^4), so keep this small.
    const size_t maxStarsForMatch = 30;

    std::cout << "Detecting stars in reference frame [0] (threshold=" << detectParams.threshold << ")..." << std::endl;
    auto t0 = std::chrono::steady_clock::now();
    auto refStars = StarAlign::detectStars(images[0].bgra.data(), W, H, stride, detectParams);
    auto t1 = std::chrono::steady_clock::now();
    double detectMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "  Detected " << refStars.size() << " stars (" << detectMs << " ms)" << std::endl;

    if (refStars.size() > maxStarsForMatch) {
        std::sort(refStars.begin(), refStars.end(),
            [](const StarAlign::Star& a, const StarAlign::Star& b) { return a.intensity > b.intensity; });
        refStars.resize(maxStarsForMatch);
        std::sort(refStars.begin(), refStars.end(),
            [](const StarAlign::Star& a, const StarAlign::Star& b) { return a.x < b.x; });
        std::cout << "  Trimmed to top " << maxStarsForMatch << " brightest stars" << std::endl;
    }
    std::cout << std::endl;

    if (refStars.size() < 8) {
        std::cerr << "ERROR: reference frame has only " << refStars.size()
                  << " stars (need >= 8 for alignment)" << std::endl;
        return 1;
    }

    // ---- Align each subsequent frame to reference ----
    std::cout << "Computing alignments (reference = frame 0)..." << std::endl;
    std::cout << std::endl;

    printf("%-6s  %-8s  %-8s  %-10s  %-8s  %-10s  %s\n",
           "Frame", "dX(px)", "dY(px)", "Angle(deg)", "Stars", "Matched", "File");
    printf("%-6s  %-8s  %-8s  %-10s  %-8s  %-10s  %s\n",
           "-----", "-------", "-------", "----------", "-----", "-------", "----");

    for (size_t i = 1; i < files.size(); ++i) {
        auto td0 = std::chrono::steady_clock::now();
        std::cerr << "  [" << i << "] detecting stars..." << std::flush;
        auto tgtStars = StarAlign::detectStars(images[i].bgra.data(), W, H, stride, detectParams);
        // Keep only brightest for matching
        if (tgtStars.size() > maxStarsForMatch) {
            std::sort(tgtStars.begin(), tgtStars.end(),
                [](const StarAlign::Star& a, const StarAlign::Star& b) { return a.intensity > b.intensity; });
            tgtStars.resize(maxStarsForMatch);
            std::sort(tgtStars.begin(), tgtStars.end(),
                [](const StarAlign::Star& a, const StarAlign::Star& b) { return a.x < b.x; });
        }
        auto td1 = std::chrono::steady_clock::now();
        double starMs = std::chrono::duration<double, std::milli>(td1 - td0).count();
        std::cerr << " " << tgtStars.size() << " stars (" << starMs << "ms), aligning..." << std::flush;

        auto ta0 = std::chrono::steady_clock::now();
        auto result = StarAlign::computeAlignment(refStars, tgtStars, W, H);
        auto ta1 = std::chrono::steady_clock::now();
        double alignMs = std::chrono::duration<double, std::milli>(ta1 - ta0).count();

        if (result.success) {
            double angleDeg = result.angle * 180.0 / 3.14159265358979323846;
            printf("[%-4zu]  %+8.3f  %+8.3f  %+10.5f  %-8zu  %-10d  %s  (%.0f+%0.fms)\n",
                   i, result.offsetX, result.offsetY, angleDeg,
                   tgtStars.size(), result.matchedStars,
                   files[i].filename().string().c_str(),
                   starMs, alignMs);
        } else {
            printf("[%-4zu]  %-8s  %-8s  %-10s  %-8zu  %-10s  %s  (FAILED)\n",
                   i, "---", "---", "---",
                   tgtStars.size(), "---",
                   files[i].filename().string().c_str());
        }
    }

    std::cout << std::endl;
    std::cout << "Done." << std::endl;
    return 0;
}
