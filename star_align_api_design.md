# star_align 模块设计说明

## 项目概述

星场图像对齐算法，移植自 DeepSkyStacker (DSS) 的 RegisterCore.cpp 和 MatchingStars.cpp。
无外部依赖（仅 C++ 标准库），可用于天文摄影的自动图像对齐与堆叠。

## 架构

### 核心算法流程

```
BGRA16 输入图像
  ↓ detectStars()
星点列表 (Star::x, y, intensity, meanRadius)
  ↓ computeAlignment()
双线性变换参数 (a0-a3, b0-b3)
  ↓ transformBGRA()
对齐后的 BGRA16 图像
  ↓ stackBGRAImages()（多帧均值合成）
最终堆叠结果
```

### 变换模型：双线性（与 DSS 一致）

使用双线性模型将目标帧像素映射到参考帧坐标，无需分解为"旋转中心 + 平移"：

```
X = tgtX / width,  Y = tgtY / height
refX = (a0 + a1*X + a2*Y + a3*X*Y) * width
refY = (b0 + b1*X + b2*Y + b3*X*Y) * height
```

对于纯平移/旋转场景，`a3` 和 `b3` 接近 0，退化为仿射变换。

### 对齐策略

1. **大三角形变换** (`computeLargeTriangleTransformation`)
   - 按星点间距离降序扫描，匹配距离相近的星对
   - 通过三角形投票确定星点对应关系
   - 优先使用

2. **匹配三角形变换** (`computeMatchingTriangleTransformation`)
   - 构建三角形特征（边长比），在参考帧和目标帧之间匹配
   - 作为大三角形方法的回退方案

3. **Sigma 裁剪 + 角点锁定** (`computeSigmaClippingTransformation`)
   - 先计算基础变换
   - 用基础变换映射 4 个目标帧角点到参考帧坐标
   - 将 4 个角点对作为虚拟"锁定"对（1000 万票）加入投票集
   - 重新计算变换，确保图像边缘映射稳定

## 数据结构

### Star（检测到的星点）

```cpp
struct Star {
    double x = 0.0;            // 质心 x 坐标（像素）
    double y = 0.0;            // 质心 y 坐标（像素）
    double intensity = 0.0;    // 归一化亮度 [0, 1]
    double circularity = 0.0;  // 圆度质量指标（越高越好）
    double meanRadius = 0.0;   // 平均半径（像素）
};
```

### AlignResult（对齐结果）

```cpp
struct AlignResult {
    bool    success = false;
    double  offsetX = 0.0;      // 中心偏移 X（像素，仅用于显示）
    double  offsetY = 0.0;      // 中心偏移 Y（像素，仅用于显示）
    double  angle = 0.0;        // 旋转角（弧度，仅用于显示）
    int     matchedStars = 0;   // 匹配星点数
    // 双线性变换参数（目标帧 → 参考帧映射）
    // refX = (a0 + a1*X + a2*Y + a3*X*Y) * width
    // refY = (b0 + b1*X + b2*Y + b3*X*Y) * height
    double a0 = 0, a1 = 1, a2 = 0, a3 = 0;
    double b0 = 0, b1 = 0, b2 = 1, b3 = 0;
};
```

- `offsetX/offsetY`：图像中心点经过变换后的位移，用于与 DSS 结果对比
- `angle`：从映射后的 x 轴方向提取的旋转角，用于显示
- `a0-a3, b0-b3`：实际用于像素变换的参数，`transformBGRA` 直接使用这些参数

### DetectParams（检测参数）

```cpp
struct DetectParams {
    double threshold = 0.10;    // 检测阈值 (0.0~1.0)，越高越严格
    int    maxStarSize = 50;    // 最大星点半径（像素）
};
```

## 公共 API

```cpp
namespace StarAlign {

// 星点检测
std::vector<Star> detectStars(
    const uint16_t* pData, int width, int height, int stride,
    const DetectParams& params = {});

// 星点匹配（计算双线性变换参数）
AlignResult computeAlignment(
    const std::vector<Star>& refStars,
    const std::vector<Star>& tgtStars,
    int imageWidth, int imageHeight);

// 一步到位：检测 + 匹配
AlignResult alignImages(
    const uint16_t* pRefData, int refWidth, int refHeight, int refStride,
    const uint16_t* pTgtData, int tgtWidth, int tgtHeight, int tgtStride,
    const DetectParams& params = {});

// 单帧双线性变换
std::vector<uint16_t> transformBGRA(
    const uint16_t* srcBGRA, int width, int height, int stride,
    const AlignResult& alignment);

// 多帧均值堆叠
std::vector<uint16_t> stackBGRAImages(
    const std::vector<const uint16_t*>& images,
    int width, int height, int stride,
    const std::vector<AlignResult>& alignments);
}
```

## 输入格式

- **像素格式**：BGRA16（B, G, R, A 交错，每通道 uint16_t，4 × uint16_t / 像素）
- **行 stride**：字节单位，通常为 `width * 8`，允许对齐填充
- **灰度提取**：`(B + G + R) / 3`，映射到 `[0, 256)` 范围

## transformBGRA 细节

- 对每个输出像素 `(ox, oy)`，通过双线性模型计算源坐标 `(srcFX, srcFY)`
- 对 B/G/R 三通道分别做双线性插值
- Alpha 通道固定为 `0xFFFF`（不透明）
- **越界像素**：标记为不透明红色 `(B=0, G=0, R=0xFFFF, A=0xFFFF)`

## stackBGRAImages 细节

- 第 0 帧为参考帧，直接加入累加器
- 后续帧：若 `alignments[f].success == true`，先调 `transformBGRA()` 对齐，再逐通道累加
- 红色越界像素（Alpha != 0 但 B/G/R 标记为红色）不参与累加 — 使用 Alpha 通道判断有效性
- 每个像素除以实际参与的有效帧数（per-pixel valid count）
- 无有效数据的像素标记为红色

## 测试程序 (test_star_align)

使用 LibRaw 解码 RAW 图像（DNG/NEF/CR2/ARW 等），调用 star_align 进行对齐和堆叠。

### 用法

```
test_star_align.exe <raw_directory> [--stack <output_prefix>] [--transform <N>]
```

- `<raw_directory>`：包含 RAW 文件的目录，按文件名排序，第一帧为参考帧
- `--stack <prefix>`：堆叠所有帧，输出 BGRA16 原始数据（带时间戳后缀 `.raw`）
- `--transform <N>`：仅变换第 N 帧（从 1 开始），输出单帧 BGRA16 数据

### 示例

```
test_star_align.exe E:\final\star_align\dngs --stack E:\final\star_align\dng_res\res
test_star_align.exe E:\final\star_align\dngs --transform 2
```

### 检测参数

测试程序中 `threshold = 0.32`，保留最亮的 100 颗星用于对齐。

## 构建方式

```bash
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

- `star_align`：静态库，零外部依赖
- `raw`（LibRaw）：静态库，编译为无 JPEG/LCMS 依赖
- `test_star_align`：测试可执行文件，链接 star_align + raw

## 文件结构

```
star_align/
├── star_align.h          # 公共头文件（Star, AlignResult, DetectParams, API 声明）
├── star_align.cpp        # 核心实现（星点检测、三角形匹配、双线性变换、堆叠）
├── test_star_align.cpp   # 测试程序（LibRaw 解码 RAW → BGRA16 → 对齐/堆叠）
├── CMakeLists.txt        # 根 CMake（star_align 静态库 + test_star_align）
├── libraw/               # LibRaw 0.22.0 源码
│   ├── CMakeLists.txt    # LibRaw 静态库构建（无 JPEG/LCMS）
│   ├── include/libraw.h  # LibRaw 公共头文件
│   └── src/              # LibRaw 源码（内部头文件在 internal/）
└── build/                # 构建输出目录
```

## 历史决策记录

### 2025-04: 从刚体模型迁移到双线性模型

最初 `transformBGRA` 使用刚体变换（以图像中心为旋转原点，平移 + 旋转），与 DSS 的对齐方式不一致。
DSS 内部使用双线性模型进行像素映射，不存在"旋转中心"的概念。

改为直接存储和使用双线性参数 (a0-a3, b0-b3)：
- `computeAlignmentInternal` 直接输出双线性参数
- `transformBGRA` 直接用双线性公式计算源坐标
- `offsetX/offsetY` 改为图像中心位移（仅用于显示和与 DSS 对比）
- `angle` 从映射后的 x 轴方向提取（仅用于显示）

### 越界像素标记

- 早期：Alpha = 0（透明），堆叠时通过 Alpha 判断有效性
- 现在：Alpha = 0xFFFF（不透明红色），所有像素 Alpha 均为 0xFFFF
- 堆叠中仍通过红色标记（R=0xFFFF, G=0, B=0）识别越界像素
- 改为不透明是为了让变换后的单帧图像直接可见（不会被当作透明区域）
