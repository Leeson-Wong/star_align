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

// Apply rigid transform (translation + rotation) to a BGRA16 image.
// srcBGRA:  BGRA16 raw data, 4 x uint16_t per pixel (B, G, R, A), row-major.
// stride:   byte stride per row (>= width * 4 * sizeof(uint16_t)).
// offsetX/Y: translation to apply (pixels). The output pixel at (ox,oy) is
//            sampled from source at (ox+offsetX, oy+offsetY) after rotation.
// angle:     rotation angle in radians (counter-clockwise around image center).
// Returns transformed BGRA16 data (same dimensions as input).
std::vector<uint16_t> transformBGRA(
    const uint16_t* srcBGRA, int width, int height, int stride,
    double offsetX, double offsetY, double angle);

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
