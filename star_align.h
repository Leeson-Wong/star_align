// ============ star_align.h ============
// Standalone star-field image alignment algorithm - no external dependencies
// Input:  BGRA 16-bit image data (const uint16_t* pData, int width, int height, int stride)
// Output: offsetX, offsetY (pixel shift), angle (radians rotation)

#pragma once
#include <vector>
#include <cstdint>
#include <cmath>

namespace StarAlign {

// -------- Detected star --------
struct Star {
    double x = 0.0;            // Centroid position (pixel coordinates)
    double y = 0.0;
    double intensity = 0.0;    // [0, 1] normalised brightness
    double circularity = 0.0;  // Roundness quality metric (higher is better)
    double meanRadius = 0.0;   // Mean radius (pixels)
};

// -------- Output: alignment result --------
struct AlignResult {
    bool    success = false;
    double  offsetX = 0.0;      // Center displacement X (pixels, for display)
    double  offsetY = 0.0;      // Center displacement Y (pixels, for display)
    double  angle = 0.0;        // Rotation angle (radians, for display)
    int     matchedStars = 0;   // Number of matched star pairs
    // Bilinear transformation parameters (target -> reference mapping)
    // refX = (a0 + a1*X + a2*Y + a3*X*Y) * width
    // refY = (b0 + b1*X + b2*Y + b3*X*Y) * height
    double a0 = 0, a1 = 1, a2 = 0, a3 = 0;
    double b0 = 0, b1 = 0, b2 = 1, b3 = 0;
};

// -------- Star detection parameters --------
struct DetectParams {
    double threshold = 0.10;    // Detection threshold (0.0~1.0), higher is stricter
    int    maxStarSize = 50;    // Maximum star radius (pixels)
};

// -------- Core functions --------

// Detect stars in a BGRA16 image.
// pData:  BGRA16 raw data, 4 x uint16_t per pixel (B, G, R, A), row-major.
// stride: byte stride per row (>= width * 4 * sizeof(uint16_t)).
// Internally uses (B + G + R) / 3 as luminance, mapped to [0, 256).
std::vector<Star> detectStars(
    const uint16_t* pData,
    int width,
    int height,
    int stride,                              // row stride in bytes
    const DetectParams& params = {}
);

// Compute bilinear transform from two star lists.
// Bilinear params map target -> reference (identity by default for aligned frames).
// offsetX/YoffsetY: center displacement in reference frame (for display).
// angle:     rotation angle extracted from the mapped x-axis (for display).
AlignResult computeAlignment(
    const std::vector<Star>& refStars,   // reference image stars
    const std::vector<Star>& tgtStars,   // target image stars
    int imageWidth,
    int imageHeight
);

// One-shot: align two BGRA16 images directly.
AlignResult alignImages(
    const uint16_t* pRefData, int refWidth, int refHeight, int refStride,
    const uint16_t* pTgtData, int tgtWidth, int tgtHeight, int tgtStride,
    const DetectParams& params = {}
);

// -------- BGRA16 image transform --------

// Apply bilinear transformation to a BGRA16 image using AlignResult.
// For each output pixel (ox,oy), maps to source via:
//   srcX = (a0 + a1*X + a2*Y + a3*X*Y) * width
//   srcY = (b0 + b1*X + b2*Y + b3*X*Y) * height
// where X = ox/width, Y = oy/height.
// Out-of-bounds pixels are marked as opaque red (B=0,G=0,R=0xFFFF,A=0xFFFF).
std::vector<uint16_t> transformBGRA(
    const uint16_t* srcBGRA, int width, int height, int stride,
    const AlignResult& alignment);

// Stack multiple BGRA16 images using per-frame alignment.
// images:     array of pointers to BGRA16 data (each width*height*4 elements).
// stride:     byte stride per row (>= width * 4 * sizeof(uint16_t)).
// alignments: alignment results for each frame (frame 0 is reference).
//             alignments.size() must equal images.size().
// Returns stacked BGRA16 image (width*height*4 elements) with mean pixel values.
std::vector<uint16_t> stackBGRAImages(
    const std::vector<const uint16_t*>& images,
    int width, int height, int stride,
    const std::vector<AlignResult>& alignments);

} // namespace StarAlign
