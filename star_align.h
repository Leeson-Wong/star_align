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
    double  offsetX = 0.0;      // x offset (pixels)
    double  offsetY = 0.0;      // y offset (pixels)
    double  angle = 0.0;        // Rotation angle (radians)
    int     matchedStars = 0;   // Number of matched star pairs

    // Full bilinear transform parameters (tgt -> ref)
    double a0 = 0, a1 = 1, a2 = 0, a3 = 0;
    double b0 = 0, b1 = 0, b2 = 1, b3 = 0;
};

// -------- Star detection parameters --------
struct DetectParams {
    double threshold = 0.10;    // Detection threshold (0.0~1.0), higher is stricter
    int    maxStarSize = 50;    // Maximum star radius (pixels)
    bool   autoThreshold = false; // If true, iteratively adjust threshold to find ~targetStarCount stars
    int    targetStarCount = 80;  // Target number of stars for auto-threshold
};

// -------- Core functions --------

// Detect stars in a BGRA16 image.
// pData:  BGRA16 raw data, 4 x uint16_t per pixel (B, G, R, A), row-major.
// stride: byte stride per row (>= width * 4 * sizeof(uint16_t)).
// Internally uses HSL luminance (max+min)/2, mapped to [0, 256).
std::vector<Star> detectStars(
    const uint16_t* pData,
    int width,
    int height,
    int stride,                              // row stride in bytes
    const DetectParams& params = {}
);

// Compute linear transform (translation + rotation) from two star lists.
// imageWidth/Height: image dimensions used for normalising coordinates.
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

// Apply bilinear/affine transform to a BGRA16 image using alignment result.
// srcBGRA:  BGRA16 raw data, 4 x uint16_t per pixel (B, G, R, A), row-major.
// stride:   byte stride per row (>= width * 4 * sizeof(uint16_t)).
// align:    alignment result containing full bilinear parameters.
// Returns transformed BGRA16 data (same dimensions as input).
// For each output pixel, an inverse affine transform maps back to the source
// image, then bicubic (Catmull-Rom) interpolation is applied.
std::vector<uint16_t> transformBGRA(
    const uint16_t* srcBGRA, int width, int height, int stride,
    const AlignResult& align);

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
