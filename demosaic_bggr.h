#ifndef DEMOSAIC_BGGR_H
#define DEMOSAIC_BGGR_H

#include <cstdint>
#include <cstring>

/**
 * @file demosaic_bggr.h
 * @brief BGGR拜耳阵列解拜耳为RGB16位
 *
 * BGGR排列模式：
 *   B G B G B G ...
 *   G R G R G R ...
 *   B G B G B G ...
 *   G R G R G R ...
 *   ...
 */

namespace Demosaic {

// ========== 辅助函数 ==========

/**
 * @brief 根据位深获取像素值并转换为16位
 */
inline uint16_t getPixelValue(const uint16_t* input16, const uint8_t* input8,
                              int bitDepth, int idx, int shift) {
    if (bitDepth > 8) {
        return input16[idx] << shift;
    } else {
        return static_cast<uint16_t>(input8[idx]) << 8;
    }
}

/**
 * @brief 边界安全的像素获取（边界复制策略）
 */
inline uint16_t getPixelSafe(const uint16_t* in, int x, int y, int w, int h) {
    x = (x < 0) ? 0 : (x >= w ? w - 1 : x);
    y = (y < 0) ? 0 : (y >= h ? h - 1 : y);
    return in[y * w + x];
}

/**
 * @brief 在B位置插值G和R
 */
inline void interpolateAtB(const uint16_t* in, int x, int y, int w, int h,
                           uint16_t& r, uint16_t& g) {
    // G: 取上下左右4个G的平均
    uint32_t gSum = getPixelSafe(in, x-1, y, w, h) +
                    getPixelSafe(in, x+1, y, w, h) +
                    getPixelSafe(in, x, y-1, w, h) +
                    getPixelSafe(in, x, y+1, w, h);
    g = static_cast<uint16_t>(gSum >> 2);

    // R: 取对角线4个R的平均
    uint32_t rSum = getPixelSafe(in, x-1, y-1, w, h) +
                    getPixelSafe(in, x-1, y+1, w, h) +
                    getPixelSafe(in, x+1, y-1, w, h) +
                    getPixelSafe(in, x+1, y+1, w, h);
    r = static_cast<uint16_t>(rSum >> 2);
}

/**
 * @brief 在R位置插值G和B
 */
inline void interpolateAtR(const uint16_t* in, int x, int y, int w, int h,
                           uint16_t& b, uint16_t& g) {
    // G: 取上下左右4个G的平均
    uint32_t gSum = getPixelSafe(in, x-1, y, w, h) +
                    getPixelSafe(in, x+1, y, w, h) +
                    getPixelSafe(in, x, y-1, w, h) +
                    getPixelSafe(in, x, y+1, w, h);
    g = static_cast<uint16_t>(gSum >> 2);

    // B: 取对角线4个B的平均
    uint32_t bSum = getPixelSafe(in, x-1, y-1, w, h) +
                    getPixelSafe(in, x-1, y+1, w, h) +
                    getPixelSafe(in, x+1, y-1, w, h) +
                    getPixelSafe(in, x+1, y+1, w, h);
    b = static_cast<uint16_t>(bSum >> 2);
}

/**
 * @brief 在G位置（偶数行）插值R和B
 */
inline void interpolateAtG_EvenRow(const uint16_t* in, int x, int y, int w, int h,
                                   uint16_t& r, uint16_t& b) {
    // B: 取左右2个B的平均
    b = (getPixelSafe(in, x-1, y, w, h) + getPixelSafe(in, x+1, y, w, h)) >> 1;
    // R: 取上下2个R的平均
    r = (getPixelSafe(in, x, y-1, w, h) + getPixelSafe(in, x, y+1, w, h)) >> 1;
}

/**
 * @brief 在G位置（奇数行）插值R和B
 */
inline void interpolateAtG_OddRow(const uint16_t* in, int x, int y, int w, int h,
                                  uint16_t& r, uint16_t& b) {
    // B: 取上下2个B的平均
    b = (getPixelSafe(in, x, y-1, w, h) + getPixelSafe(in, x, y+1, w, h)) >> 1;
    // R: 取左右2个R的平均
    r = (getPixelSafe(in, x-1, y, w, h) + getPixelSafe(in, x+1, y, w, h)) >> 1;
}


// ========== 主函数 ==========

/**
 * @brief BGGR拜耳阵列转RGB16位（双线性插值）
 *
 * @param rawData    输入的BGGR raw数据指针
 * @param width      图像宽度（必须为偶数）
 * @param height     图像高度（必须为偶数）
 * @param bitDepth   原始数据的位深（8/10/12/14/16）
 * @return           新分配的RGB16位数据，调用者负责delete[]
 *
 * 输出格式：RGBRGBRGB... 交错存储
 * 每个像素3个uint16_t，共 width * height * 3 个元素
 */
inline uint16_t* bggrToRGB16(const void* rawData, int width, int height, int bitDepth) {
    if (rawData == nullptr || width <= 0 || height <= 0) {
        return nullptr;
    }

    // 计算缩放因子，将原始位深映射到16位满量程
    int shift = 16 - bitDepth;

    // 分配输出内存 (RGB三通道，每个通道16位)
    uint16_t* rgb = new uint16_t[width * height * 3];

    // 根据位深选择读取方式
    const uint16_t* input16 = static_cast<const uint16_t*>(rawData);
    const uint8_t* input8 = static_cast<const uint8_t*>(rawData);

    // 如果输入是8位，先转换为16位统一处理
    uint16_t* tempBuffer = nullptr;
    if (bitDepth <= 8) {
        tempBuffer = new uint16_t[width * height];
        for (int i = 0; i < width * height; i++) {
            tempBuffer[i] = static_cast<uint16_t>(input8[i]) << 8;
        }
        input16 = tempBuffer;
    }

    // 对每个像素进行解拜耳
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            int outIdx = idx * 3;

            uint16_t r, g, b;

            // 判断当前像素在拜耳阵列中的位置
            bool isEvenRow = ((y & 1) == 0);
            bool isEvenCol = ((x & 1) == 0);

            if (isEvenRow && isEvenCol) {
                // ===== B 位置 (偶数行，偶数列) =====
                b = input16[idx];
                interpolateAtB(input16, x, y, width, height, r, g);

            } else if (isEvenRow && !isEvenCol) {
                // ===== G 位置 (偶数行，奇数列) =====
                g = input16[idx];
                interpolateAtG_EvenRow(input16, x, y, width, height, r, b);

            } else if (!isEvenRow && isEvenCol) {
                // ===== G 位置 (奇数行，偶数列) =====
                g = input16[idx];
                interpolateAtG_OddRow(input16, x, y, width, height, r, b);

            } else {
                // ===== R 位置 (奇数行，奇数列) =====
                r = input16[idx];
                interpolateAtR(input16, x, y, width, height, b, g);
            }

            // 统一缩放到16位: 直接值和插值都在原始空间，统一左移
            rgb[outIdx + 0] = (r << shift) & 0xFFFF;  // R
            rgb[outIdx + 1] = (g << shift) & 0xFFFF;  // G
            rgb[outIdx + 2] = (b << shift) & 0xFFFF;  // B
        }
    }

    // 释放临时缓冲区
    if (tempBuffer != nullptr) {
        delete[] tempBuffer;
    }

    return rgb;
}


/**
 * @brief BGGR拜耳阵列转RGB16位（简化版，边界不处理）
 *
 * @param rawData    输入的BGGR raw数据指针（假设为16位）
 * @param width      图像宽度
 * @param height     图像高度
 * @param bitDepth   原始数据的位深
 * @return           新分配的RGB16位数据
 *
 * 注意：此版本不处理边界，返回图像四周1像素未填充
 */
inline uint16_t* bggrToRGB16_Fast(const void* rawData, int width, int height, int bitDepth) {
    if (rawData == nullptr || width <= 2 || height <= 2) {
        return nullptr;
    }

    const uint16_t* in = static_cast<const uint16_t*>(rawData);
    uint16_t* rgb = new uint16_t[width * height * 3];

    int shift = 16 - bitDepth;

    // 初始化为0
    std::memset(rgb, 0, width * height * 3 * sizeof(uint16_t));

    // 不处理最外圈边界
    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int idx = y * width + x;
            int outIdx = idx * 3;

            uint16_t r, g, b;
            bool isEvenRow = ((y & 1) == 0);
            bool isEvenCol = ((x & 1) == 0);

            if (isEvenRow && isEvenCol) {
                // B位置
                b = in[idx];
                g = (in[idx-1] + in[idx+1] + in[idx-width] + in[idx+width]) >> 2;
                r = (in[idx-width-1] + in[idx-width+1] + in[idx+width-1] + in[idx+width+1]) >> 2;

            } else if (isEvenRow && !isEvenCol) {
                // G位置（偶数行）
                g = in[idx];
                b = (in[idx-1] + in[idx+1]) >> 1;
                r = (in[idx-width] + in[idx+width]) >> 1;

            } else if (!isEvenRow && isEvenCol) {
                // G位置（奇数行）
                g = in[idx];
                b = (in[idx-width] + in[idx+width]) >> 1;
                r = (in[idx-1] + in[idx+1]) >> 1;

            } else {
                // R位置
                r = in[idx];
                g = (in[idx-1] + in[idx+1] + in[idx-width] + in[idx+width]) >> 2;
                b = (in[idx-width-1] + in[idx-width+1] + in[idx+width-1] + in[idx+width+1]) >> 2;
            }

            // 统一缩放到16位: 直接值和插值都在原始空间，统一左移
            rgb[outIdx + 0] = (r << shift) & 0xFFFF;
            rgb[outIdx + 1] = (g << shift) & 0xFFFF;
            rgb[outIdx + 2] = (b << shift) & 0xFFFF;
        }
    }

    return rgb;
}


} // namespace Demosaic

#endif // DEMOSAIC_BGGR_H
