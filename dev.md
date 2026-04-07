# star_align 项目开发笔记

## 项目概述

基于 DeepSkyStacker (DSS) 的星点对齐库。从 DSS 项目中提取注册（Register）、叠加（Stacking）相关代码，
去除 Qt GUI 依赖，作为静态库供其他程序调用。

## 项目结构

```
star_align/
├── CMakeLists.txt          # 根 CMake 配置
├── dev.md                  # 本文档
├── main.cpp                # 空的测试入口
├── libraw/                 # LibRaw 子目录（add_subdirectory）
├── dss/                    # DSS 核心代码（静态库）
│   ├── pch.h               # 预编译头 stub（含 std::span polyfill、C++20 polyfill）
│   ├── dss_qt.h            # Qt 类型 stub（替代 Qt 依赖）
│   ├── ztrace.h            # ZTRACE/ZASSERT 空宏
│   ├── zexcept.h           # ZException/ZInvalidParameter stub
│   ├── ...                 # 从 DSS 抄来的 .h/.cpp 文件
└── build/                  # CMake 构建目录
```

## 构建配置

- **C++ 标准**: C++17（strict，不使用 /std:c++20）
- **编译器**: MSVC 19.44 (VS 2022)
- **CMake 最低版本**: 3.16
- **目标**: `dss` 静态库 + `test_main` 可执行文件

### CMake 结构

使用 `file(GLOB_RECURSE)` 收集 `dss/*.cpp` 后通过 `list(FILTER ... EXCLUDE)` 排除无法编译的文件：

```cmake
file(GLOB_RECURSE DSS_ALL_SOURCES "dss/*.cpp")
# 排除依赖外部库或复杂未移植类型的文件
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*FITSUtil\\.cpp$")
list(FILTER DSS_ALL_SOURCES EXCLUDE REGEX ".*TIFFUtil\\.cpp$")
# ... 更多排除规则
add_library(dss STATIC ${DSS_ALL_SOURCES})
```

## Qt 去依赖方案

原始 DSS 代码大量依赖 Qt（QImage、QString、QFile、QSettings 等）。
通过 `dss/dss_qt.h` 提供 Qt 类型的最小 stub 实现，避免链接 Qt。

### dss_qt.h 提供的类型

#### 基础类型
| 类型 | 说明 |
|------|------|
| `qreal` | `typedef double` |
| `qint64` / `quint64` | 整数类型别名 |
| `uint` / `ulong` | `unsigned int` / `unsigned long` 别名 |

#### 几何类
| 类 | 核心方法 |
|----|---------|
| `QPoint` | x(), y() |
| `QPointF` | x(), y(), rx(), ry(), 算术运算符 |

#### 字符串类
| 类 | 核心方法 |
|----|---------|
| `QString` | isEmpty, append, arg, indexOf, left, right, mid, trimmed, split, toUtf8, toStdString, toStdU16String, compare, fromLatin1, fromStdString, fromStdU16String, number, setNum, toDouble, toFloat, toInt, toLong, startsWith, endsWith, contains, toUpper, toLower, replace, remove, simplified, section, operator+=, operator>, operator<=, operator>= |
| `QStringList` | 继承 std::vector<QString>, count(), isEmpty() |
| `QByteArray` | size, isEmpty, constData, data, resize, append |

#### 文件 I/O 类
| 类 | 核心方法 |
|----|---------|
| `QFile` | open, close, write, remove, fileName, peek, exists |
| `QFileInfo` | path, fileName, completeBaseName, suffix, birthTime, lastModified, exists, isDir, isFile |
| `QDir` | separator, toNativeSeparators |
| `QTextStream` | operator<<, readLine, atEnd |

#### 图像类
| 类 | 核心方法 |
|----|---------|
| `QImage` | 构造(width,height,Format), isNull, width, height, format, bytesPerLine, bitPlaneCount, bits, constBits, scanLine |
| `QRgb` | `typedef uint32_t` |
| `QRgba64` | fromRgba64, red, green, blue, alpha |
| `qRed/qGreen/qBlue/qAlpha` | QRgb 分量提取函数 |

#### 日期时间类
| 类 | 核心方法 |
|----|---------|
| `QDate` | fromString, toString, isValid, year, month, day |
| `QTime` | fromString, toString, isValid, hour, minute, second |
| `QDateTime` | currentDateTime, fromString, toString, isValid, 构造(QDate,QTime) |

#### 其他
| 类/宏 | 说明 |
|-------|------|
| `QVariant` | toBool, toInt, toUInt, toDouble, toString, canConvert |
| `QSettings` | value, setValue (空实现) |
| `QCoreApplication` | translate (返回 key 原值) |
| `QSysInfo` | prettyProductName, buildAbi, currentCpuArchitecture |
| `QLocale` | FloatingPointShortest 常量 |
| `QMimeDatabase` | 空构造 |
| `QIODevice` / `QIODeviceBase` | ReadOnly/WriteOnly/Text 常量 |
| `Qt::endl` | `"\n"` |
| `Qt::CaseInsensitive` | 常量 |
| `Qt::CheckState` | Unchecked/PartiallyChecked/Checked 枚举 |
| `Q_DECLARE_TR_FUNCTIONS` | 空宏 |
| `_T` | 直接展开为参数 |

## ZClass stub 方案

DSS 原始代码使用 ZClass 库（ztrace、zexception）。通过空实现替代：

### ztrace.h
```cpp
#define ZFUNCTRACE_RUNTIME()                    // 函数入口跟踪
#define ZTRACE_RUNTIME(...)                     // 运行时跟踪日志
#define ZASSERTSTATE(x)  do { if (!(x)) { } } while(0)  // 状态断言
#define ZASSERT(x)        do { if (!(x)) { } } while(0)  // 断言
#define ZEXCEPTION_LOCATION()  ""               // 异常位置信息
```

### zexcept.h
```cpp
class ZException : public std::runtime_error {
    void addLocation(const std::string&) {}     // 空操作
};
class ZInvalidParameter : public ZException {
    // 支持单参数和双参数构造
};
```

## C++20 → C++17 适配

DSS 上游代码使用 C++20 特性。项目约束为 C++17，通过以下方式适配：

### pch.h 中提供的 polyfill

| C++20 特性 | C++17 适配方式 |
|-----------|--------------|
| `consteval` | `#define consteval constexpr` |
| `char8_t` | `typedef unsigned char char8_t`（当 `__cpp_char8_t` 未定义时） |
| `std::span` | pch.h 中提供完整的 span polyfill 实现 |
| `std::dynamic_extent` | `static constexpr ptrdiff_t dynamic_extent = -1`（移到 span 定义之前） |

### 需要修改 DSS 源码的适配

以下修改是最小化的，仅将 C++20 语法替换为 C++17 等价形式：

| 文件 | C++20 特性 | C++17 替换 |
|------|-----------|-----------|
| `Stars.h` | `operator<=>` (三路比较) | `operator==` + `operator<` |
| `Stars.h` | `for (int i = 0; const auto& x : v)` (带初始化器的范围for) | 外部声明计数器变量 |
| `MatchingStars.h` | `operator<=>` (3处) | `operator==` + `operator<` |
| `MatchingStars.h` | `constexpr` 构造函数 (CStarDist, CMatchingStars) | 移除 `constexpr` |
| `MatchingStars.h` | `#include <boost/container/vector.hpp>` | 内联 stub：`namespace boost::container { using vector = std::vector; }` |
| `MatchingStars.cpp` | `for (size_t i = 0; const auto& x : v)` (2处) | 外部声明计数器变量 |
| `BackgroundCalibration.h` | `template <IsCalibrator C>` (C++20 concept) | `template <typename C>` |
| `BackgroundCalibration.h/.cpp` | `char8_t*` | `char*` |
| `BackgroundCalibration.cpp` | `template <IsCalibrator... Cals>` (5处) | `template <typename... Cals>` |
| `BackgroundCalibration.cpp` | `TVariant` 依赖类型 | 添加 `typename` 前缀 |
| `BitmapExtraInfo.h` | 指定初始化器 `.m_Type = ...` | 普通赋值初始化 |
| `StackingTasks.cpp` | 指定初始化器 `CTaskInfo{.m_dwTaskID = ...}` | 普通构造+赋值 |
| `dssrect.h` | 缺少 `#include "dss_qt.h"` | 添加 include |
| `FlatPart.h` | 缺少 `#include <cstdint>` | 添加 include |
| `DynamicStats.h` | 缺少 `#include <cmath>` | 添加 include |
| `matrix.h` | `#include "../ZClass/ztrace.h"` | `#include "ztrace.h"` |
| `RegisterEngine.cpp` | 缺少 `#include "pch.h"` | 添加为第一个 include |
| `DSSProgress.h` | `#include <pch.h>` (尖括号) | `#include "pch.h"` (双引号) |

## dss/ 目录文件分类

### 从 DSS 原项目抄来的代码（尽量不修改）

这些文件保持了 DSS 原始代码逻辑，通过 stub 头文件和少量 C++20→C++17 适配满足依赖：

**注册相关（Register）:**
- `RegisterEngine.h/.cpp` — 星点检测与注册引擎
- `Stars.h` — 星点数据结构
- `MatchingStars.h/.cpp` — 星点匹配算法
- `BilinearParameters.h/.cpp` — 变换参数
- `SkyBackground.h` — 天空背景计算

**位图相关（Bitmap）:**
- `BitmapBase.h/.cpp` — CMemoryBitmap 基类及工厂方法
- `BitmapExt.h/.cpp` — CAllDepthBitmap，QImage 加载/转换
- `GrayBitmap.h/.cpp` — CGrayBitmapT<T> 灰度位图模板
- `ColorBitmap.h/.cpp` — CColorBitmapT<T> 彩色位图模板
- `ColorRef.h` — COLORREF16, COLORREF 结构体
- `CFABitmapInfo.h` — CFA（Bayer）信息

**叠加相关（Stacking）:**
- `StackingTasks.h/.cpp` — CAllStackingTasks 叠加任务
- `MasterFrames.h/.cpp` — CMasterFrames 主帧管理
- `Filters.h/.cpp` — 中值滤波
- `BackgroundCalibration.h/.cpp` — 背景校准

**文件读写:**
- `FITSUtil.h/.cpp` — FITS 文件读写（依赖 fitsio 库）
- `TIFFUtil.h/.cpp` — TIFF 文件读写（依赖 libtiff）

**基础设施:**
- `Workspace.h/.cpp` — 工作区配置（QSettings 后端）
- `FrameInfo.h/.cpp` — 帧信息
- `DarkFrame.h/.cpp` — 暗场
- `FlatFrame.h/.cpp` — 平场
- `TaskInfo.h/.cpp` — 任务信息
- `Multitask.h/.cpp` — 多线程工具
- `DSSCommon.h` — 公共枚举和类型定义
- `DSSVersion.h` — 版本宏
- `DSSTools.h` — 通用工具函数
- `dssrect.h` — DSSRect 矩形类
- `PixelTransform.h` — 像素变换
- `BitmapExtraInfo.h` — 位图附加信息
- `ExtraInfo.h` — 扩展信息
- `DynamicStats.h` — 动态统计
- `FlatPart.h` — CFlatPart
- `cfa.h` — CFATYPE 枚举
- `ColorHelpers.h/.cpp` — 颜色辅助函数

**AVX/SIMD:**
- `avx_simd_check.h/.cpp` — SIMD 特性检测
- `avx_luminance.h/.cpp` — AVX 亮度计算
- `avx_bitmap_util.h/.cpp` — AVX 位图工具
- `avx_median.h` — AVX 中值计算模板

### 新建的 stub 文件

#### 核心 stub（提供类型签名和空操作）

| 文件 | 用途 |
|------|------|
| `pch.h` | 预编译头（含 std::span polyfill、C++20 polyfill、标准库 include） |
| `dss_qt.h` | Qt 类型 stub |
| `ztrace.h` | ZTRACE/ZASSERT 空宏 |
| `zexcept.h` | ZException/ZInvalidParameter stub |
| `zexcbase.h` | ZClass 异常基类 stub（include zexcept.h） |
| `dssbase.h` | FetchPicture/DebayerPicture 声明，CBitmapInfo 前向声明 |

#### 缺失 DSS 类的 stub（提供最小可编译定义）

| 文件 | 用途 |
|------|------|
| `BitmapInfo.h` | CBitmapInfo 完整类定义（bitmap 元数据） |
| `avx_includes.h` | AVX 头文件集合 stub（include pch.h + immintrin.h） |
| `avx_histogram.h` | AvxHistogram 类 stub |
| `avx_support.h` | AvxSupport 工具类 stub |
| `avx_cfa.h` | AVX CFA 处理 stub |
| `MultiBitmap.h` | CMultiBitmap 基类及 CColorMultiBitmapT/CGrayMultiBitmapT 前向声明 |
| `GreyMultiBitmap.h` | CGreyMultiBitmap stub |
| `ColorMultiBitmap.h` | CColorMultiBitmap stub |
| `MedianFilterEngine.h` | CMedianFilterEngine 及 CColorMedianFilterEngineT/CGrayMedianFilterEngineT 前向声明 |
| `DeBloom.h` | DeBloom 函数 stub |
| `BitmapIterator.h` | CBitmapIterator stub |
| `AHDDemosaicing.h` | AHD_Demosaic 函数 stub |
| `histogram.h` | CHistogram stub |
| `RAWUtils.h` | RAW 文件工具函数 stub |
| `Settings.h` | Settings 类型别名（= QSettings） |
| `fitsio.h` | CFITSIO 库最小类型 stub |
| `omp.h` | OpenMP 运行时函数空实现 |

## 当前状态

### 已完成
- [x] CMakeLists.txt 配置（GLOB_RECURSE + EXCLUDE 过滤）
- [x] Qt 类型 stub（dss_qt.h）覆盖所有使用到的 Qt 类型
- [x] ZClass stub（ztrace.h, zexcept.h, zexcbase.h）
- [x] pch.h 重写（去除 Qt/exiv2/boost/omp 依赖，含 std::span polyfill）
- [x] C++20 → C++17 语法适配（consteval, char8_t, <=>, concepts, 指定初始化器, range-for with init）
- [x] 缺失头文件补充（BitmapInfo.h 等 18 个 stub 文件）
- [x] `cmake -B build` 配置通过
- [x] `cmake --build build --config Release` 编译通过
- [x] `test_main.exe` 可执行文件编译、链接、运行成功

### CMakeLists.txt 中排除的 .cpp 文件

以下文件因依赖尚未满足而被排除编译，待后续逐步解决：

**依赖外部库：**
| 文件 | 缺失依赖 |
|------|---------|
| `FITSUtil.cpp` | fitsio (CFITSIO) 库 |
| `TIFFUtil.cpp` | libtiff + zlib 库 |

**依赖复杂未移植类型：**
| 文件 | 主要问题 |
|------|---------|
| `BitmapExt.cpp` | QImage 真实功能、BitmapIterator、AHDDemosaicing、DSSBase、ZException 方法 |
| `DarkFrame.cpp` | RGBHistogram、BitmapIteratorConst、OpenMP、numbers (C++20) |
| `FlatFrame.cpp` | OpenMP |
| `MasterFrames.cpp` | DeBloom 真实实现、StackingTasks |
| `Multitask.cpp` | OpenMP 并行框架 |
| `StackingTasks.cpp` | 深度依赖链、RAW/FITS/TIFF |
| `TaskInfo.cpp` | MultiBitmap 虚方法 |
| `Workspace.cpp` | QSettings 真实读写 |
| `FrameInfo.cpp` | 深度依赖 |
| `Filters.cpp` | 中值滤波引擎 |
| `DSSProgress.cpp` | 进度回调 |
| `ColorHelpers.cpp` | 颜色辅助函数 |
| `BackgroundCalibration.cpp` | OpenMP 并行、MSVC ICE |
| `ColorBitmap.cpp` | CColorMultiBitmapT/CColorMedianFilterEngineT 完整实现 |
| `GrayBitmap.cpp` | CGrayMultiBitmapT/CGrayMedianFilterEngineT 完整实现 |
| `RegisterEngine.cpp` | CLightFrameInfo::filePath、深度依赖链 |
| `MatchingStars.cpp` | std::span 构造推导问题 |

**AVX/SIMD（需 immintrin.h 但代码复杂）：**
| 文件 | 说明 |
|------|------|
| `avx_luminance.cpp` | AVX2 亮度计算，使用 __m256i/__m256d 内联函数 |
| `avx_simd_check.cpp` | SIMD 特性检测，使用平台特定头文件 |
| `avx_bitmap_util.cpp` | AVX 位图工具 |

### 后续工作方向
- [ ] 逐步恢复排除的 .cpp 文件编译（优先级：MatchingStars → RegisterEngine → ColorBitmap/GrayBitmap → BitmapExt → Stacking）
- [ ] 集成 fitsio 库（FITSUtil.cpp）
- [ ] 集成 libtiff + zlib（TIFFUtil.cpp）
- [ ] 完善 MultiBitmap/MedianFilterEngine 的真实实现
- [ ] 完善 BitmapIterator 的真实实现
- [ ] 完善 QImage stub 的真实像素操作
- [ ] 解决 MSVC C++17 对 BackgroundCalibration.cpp 模板的 ICE 问题

## 设计决策

1. **尽量不修改 DSS 抄来的代码**：所有适配工作优先通过新建 stub 文件完成。仅对 C++20 语法做最小替换（因为 C++17 编译器不支持这些语法）。

2. **stub 原则**：stub 实现只提供类型签名和空操作（或返回默认值），目标是让代码"能编译"，不保证运行时正确性。

3. **CMake 排除策略**：对于依赖链过深或依赖外部库的 .cpp 文件，通过 CMake EXCLUDE 排除，而非强行 stub 整个依赖树。待依赖就绪后逐个恢复。

4. **C++17 严格约束**：不使用 /std:c++20 编译选项。C++20 特性通过 polyfill（consteval, char8_t, std::span）或源码最小修改（<=>, concepts, 指定初始化器）适配到 C++17。
