# star_align 模块 API 设计说明

## 设计决策：无状态方案（Plan A）

`star_align` 模块采用**无状态函数**设计，不封装为类对象。

```cpp
// 星点检测 - 纯函数，无副作用
std::vector<Star> detectStars(const uint16_t* pData, int width, int height, int stride,
                              const DetectParams& params = {});

// 星点匹配 - 纯函数，无副作用
AlignResult computeAlignment(const std::vector<Star>& refStars,
                             const std::vector<Star>& tgtStars,
                             int imageWidth, int imageHeight);

// 一步到位（内部调用上面两个函数）
AlignResult alignImages(const uint16_t* pRefData, int refWidth, int refHeight, int refStride,
                        const uint16_t* pTgtData, int tgtWidth, int tgtHeight, int tgtStride,
                        const DetectParams& params = {});
```

### 选择理由

- 模块不持有任何状态（无缓存、无内部缓冲区），调用方完全控制生命周期
- 适合批量场景：参考帧星点只检测一次，目标帧逐帧检测后匹配
- 对多线程友好：不同线程可安全并发调用，无需加锁

### 批量用法（13帧示例）

```cpp
// 参考帧星点只检测一次
auto refStars = StarAlign::detectStars(frames[0].data, frames[0].width,
                                        frames[0].height, frames[0].stride);
for (int i = 1; i < 13; i++)
{
    auto tgtStars = StarAlign::detectStars(frames[i].data, frames[i].width,
                                            frames[i].height, frames[i].stride);
    auto result = StarAlign::computeAlignment(refStars, tgtStars, w, h);
    // result.offsetX, result.offsetY, result.angle
}
```

## 性能量级参考

测试条件：4000x3000 图像，~150 颗星，单线程 CPU

| 步骤 | 估计耗时 |
|------|---------|
| 单帧星点检测 | 100ms ~ 500ms |
| 两帧星点匹配 | 5ms ~ 50ms |
| 13 帧总计 | 约 2 ~ 5 秒 |

## 图像变换与堆叠 API

除了星点检测和匹配，模块还提供了图像变换和堆叠功能：

```cpp
// 单帧刚体变换（平移 + 旋转）—— 双线性插值
std::vector<uint16_t> transformBGRA(
    const uint16_t* srcBGRA, int width, int height, int stride,
    double offsetX, double offsetY, double angle);

// 多帧堆叠（均值合成）
std::vector<uint16_t> stackBGRAImages(
    const std::vector<const uint16_t*>& images,
    int width, int height, int stride,
    const std::vector<AlignResult>& alignments);
```

### transformBGRA 细节

- 实现位置：`star_align.cpp:1153-1189`
- 对输入 BGRA16 图像施加逆变换：以图像中心为旋转原点，先平移再旋转
- 对 B/G/R 三通道分别做双线性插值，Alpha 通道固定为 `0xFFFF`
- 边界处理：clamp-to-edge（越界坐标钳制到图像边缘）

### stackBGRAImages 细节

- 实现位置：`star_align.cpp:1191-1237`
- 输入为多帧 BGRA16 图像指针数组及对应的对齐结果
- 处理流程：
  1. 第 0 帧（参考帧）直接加入 `double` 精度累加器
  2. 对后续每帧：若 `alignments[f].success == true`，先调 `transformBGRA()` 对齐到参考帧坐标系，再逐通道累加
  3. 累加完成后除以实际参与的帧数（参考帧 + 对齐成功的帧），得到均值图像
  4. 结果钳制到 `[0, 65535]` 范围，转回 `uint16_t`
- 对齐失败的帧自动跳过，不参与累加

### 完整用法（13 帧对齐 + 堆叠）

```cpp
// 1. 参考帧星点检测
auto refStars = StarAlign::detectStars(frames[0].data, w, h, stride);

// 2. 逐帧对齐
std::vector<StarAlign::AlignResult> alignments(frames.size());
alignments[0].success = true;  // 参考帧标记为成功

for (int i = 1; i < frames.size(); i++) {
    auto tgtStars = StarAlign::detectStars(frames[i].data, w, h, stride);
    alignments[i] = StarAlign::computeAlignment(refStars, tgtStars, w, h);
}

// 3. 堆叠所有帧
std::vector<const uint16_t*> ptrs;
for (auto& f : frames) ptrs.push_back(f.data);

auto stacked = StarAlign::stackBGRAImages(ptrs, w, h, stride, alignments);
// stacked 为均值合成后的 BGRA16 数据，size == w * h * 4
```

## 业务集成待办

实现完成后需要考虑以下适配工作：

1. **输入格式对接**：确认业务代码中的图像数据是否为 BGRA16 格式（4 个 uint16_t per pixel: B, G, R, A），如果不是需要做格式转换或修改 detectStars 内部的灰度提取逻辑
2. **stride 约定**：确认业务代码中图像行的字节步长（可能等于 width * 8，也可能有对齐填充）
3. **检测阈值调参**：默认 threshold = 0.10，实际需根据图像噪声水平调整
4. **错误处理**：computeAlignment 在星点不足 8 颗时返回 success = false，业务代码需处理对齐失败的情况
5. **结果单位**：offsetX/offsetY 单位为像素，angle 单位为弧度

## 已确定的数据结构
typedef struct Bgra16Raw {
    uint16* data = nullptr;
    uint32_t width;
    uint32_t height;
    size_t size;
    int error;
} Rgba16Raw, Bgra16Raw;
