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
│   ├── pch.h               # 预编译头（需改写：去除 Qt/exiv2/boost，改为引用 dss_qt.h）
│   ├── dss_qt.h            # Qt 类型 stub（替代 Qt 依赖，本项目自建）
│   ├── ztrace.h            # ZTRACE/ZASSERT 空宏（本项目自建 stub）
│   ├── zexcept.h           # ZException/ZInvalidParameter stub（本项目自建）
│   ├── zexcbase.h          # ZClass 异常基类 stub（本项目自建）
│   ├── ...                 # 从 DSS DeepSkyStackerKernel 抄来的 .h/.cpp 文件
└── build/                  # CMake 构建目录
```

## 构建配置

- **C++ 标准**: C++20（strict，使用 /std:c++20）
- **编译器**: MSVC 19.44 (VS 2022)
- **CMake 最低版本**: 3.16
- **目标**: `dss` 静态库 + `test_main` 可执行文件

### CMake 结构

使用 `file(GLOB_RECURSE)` 收集 `dss/*.cpp` 中所有源文件，无排除规则：

```cmake
file(GLOB_RECURSE DSS_ALL_SOURCES "dss/*.cpp")
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

## 文件来源说明

### 从 DSS DeepSkyStackerKernel 同步的文件（91 个同名文件已覆盖）

所有同名文件已从 `E:\final\DSS\DeepSkyStackerKernel\` 同步覆盖到 `E:\final\star_align\dss\`，
使用 DSS 原始代码，配合 C++20 标准直接编译，不再需要 C++20→C++17 适配。

**关键变化：**
- `pch.h` — 使用 DSS 原版（包含 Qt、exiv2、boost 引用），需配合 dss_qt.h stub 编译
- `dssbase.h` — 使用 DSS 原版（DSSBase 类，含 reportError 虚方法）
- `avx_histogram.h` — 使用 DSS 原版（AvxHistogram + AvxBezierAndSaturation + AVX-256/NonAVX 版本）
- `avx_cfa.h` — 使用 DSS 原版（AvxCfaProcessing + AVX-256/NonAVX 版本）
- `avx_simd_factory.h` — 使用 DSS 原版（SimdFactory<CRTP> + SimdSelector 模板）
- `RegisterEngine.cpp` / `MatchingStars.cpp` 等 — 使用 DSS 原版

### 本项目自建的 stub 文件（DSS 中不存在）

| 文件 | 用途 |
|------|------|
| `dss_qt.h` | Qt 类型 stub（DSS 原项目使用真实 Qt） |
| `ztrace.h` | ZTRACE/ZASSERT 空宏 |
| `zexcept.h` | ZException/ZInvalidParameter stub |
| `zexcbase.h` | ZClass 异常基类 stub（include zexcept.h） |
| `fitsio.h` | CFITSIO 库最小类型 stub |
| `omp.h` | OpenMP 运行时函数空实现 |

## dss/ 目录文件分类

### 注册相关（Register）
- `RegisterEngine.h/.cpp` — 星点检测与注册引擎
- `Stars.h` — 星点数据结构
- `MatchingStars.h/.cpp` — 星点匹配算法
- `BilinearParameters.h/.cpp` — 变换参数
- `SkyBackground.h` — 天空背景计算

### 位图相关（Bitmap）
- `BitmapBase.h/.cpp` — CMemoryBitmap 基类及工厂方法
- `BitmapExt.h/.cpp` — CAllDepthBitmap，QImage 加载/转换
- `GrayBitmap.h/.cpp` — CGrayBitmapT<T> 灰度位图模板
- `ColorBitmap.h/.cpp` — CColorBitmapT<T> 彩色位图模板
- `ColorRef.h` — COLORREF16, COLORREF 结构体
- `CFABitmapInfo.h` — CFA（Bayer）信息
- `MemoryBitmap.h/.cpp` — 内存位图

### 叠加相关（Stacking）
- `StackingTasks.h/.cpp` — CAllStackingTasks 叠加任务
- `MasterFrames.h/.cpp` — CMasterFrames 主帧管理
- `Filters.h/.cpp` — 中值滤波
- `BackgroundCalibration.h/.cpp` — 背景校准
- `DeBloom.h/.cpp` — 去除星芒

### 文件读写
- `FITSUtil.h/.cpp` — FITS 文件读写（依赖 fitsio 库）
- `TIFFUtil.h/.cpp` — TIFF 文件读写（依赖 libtiff）
- `RAWUtils.h/.cpp` — RAW 文件工具

### AVX/SIMD
- `avx_simd_check.h/.cpp` — SIMD 特性检测
- `avx_simd_factory.h` — SimdFactory<CRTP> + SimdSelector 模板
- `avx_luminance.h/.cpp` — AVX 亮度计算
- `avx_bitmap_util.h/.cpp` — AVX 位图工具
- `avx_cfa.h/.cpp` — AVX CFA 处理（AVX-256 + NonAVX）
- `avx_histogram.h/.cpp` — AVX 直方图 + Bezier/Saturation（AVX-256 + NonAVX）
- `avx_median.h` — AVX 中值计算模板
- `avx_includes.h` — AVX 头文件集合
- `avx_support.h` — AvxSupport 工具类

### 基础设施
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
- `histogram.h` — CHistogram/DSS::RGBHistogramAdjust
- `Settings.h/.cpp` — Settings 类型别名
- `Bayer.h/.cpp` — Bayer 去马赛克
- `Matrix.h` — 矩阵工具

### 其他
- `BitmapInfo.h/.cpp` — CBitmapInfo 位图元数据
- `BitmapIterator.h` — CBitmapIterator
- `BitmapCharacteristics.h` — 位图特征
- `BitmapConstants.h` — 位图常量
- `AHDDemosaicing.h/.cpp` — AHD 去马赛ic算法
- `MultiBitmap.h` — CMultiBitmap 基类
- `GreyMultiBitmap.h/.cpp` — CGreyMultiBitmap
- `ColorMultiBitmap.h/.cpp` — CColorMultiBitmap
- `MedianFilterEngine.h/.cpp` — 中值滤波引擎

## 当前状态

### 已完成
- [x] CMakeLists.txt 配置（GLOB_RECURSE，无排除规则）
- [x] C++ 标准升级为 C++20，不再需要 polyfill
- [x] 从 DSS 同步覆盖 91 个同名文件，使用原始 DSS 代码
- [x] Qt 类型 stub（dss_qt.h）覆盖大部分 Qt 类型
- [x] ZClass stub（ztrace.h, zexcept.h, zexcbase.h）

### 待解决

**pch.h 改写**：当前 pch.h 直接引用了 Qt/boost/exiv2 真实头文件，需改写为引用 dss_qt.h + 标准库。

**dss_qt.h 补充**：经过排查，业务代码实际只用了 dss_qt.h 中已有的类型，唯一缺的是：
- `QElapsedTimer` — 业务代码中用到（计时）
- 平台检测宏 `Q_OS_WIN` / `Q_OS_LINUX` / `Q_OS_MACOS` 等
- `QMetaObject` — dssbase.h include 了（但实际只是前向声明，可能不需要实现）

**boost/exiv2 处理**：pch.h 中引用的 boost::interprocess 和 exiv2 在业务代码中未实际使用，从 pch.h 中移除即可。

**omp.h**：已有 omp.h stub。

### 后续工作方向
- [ ] 改写 pch.h：去除 Qt/boost/exiv2 直接引用，改为 include dss_qt.h 和标准库
- [ ] 补充 dss_qt.h 中缺失的 Qt 类型 stub（QMetaObject 等）
- [ ] 全量编译通过
- [ ] 保证注册（Register）和叠加（Stacking）功能正常

## 设计决策

1. **Mock Qt，不引入真实 Qt**：通过 dss_qt.h stub 替代所有 Qt 依赖。性能优化无关紧要，目标是功能正确。

2. **C++20 标准**：使用 C++20，直接支持 consteval、char8_t、std::span、operator<=>、concepts 等特性，无需 polyfill。

3. **DSS 原始代码优先**：所有同名文件直接从 DSS 同步覆盖。如有编译问题，优先改 stub 文件和 pch.h，尽量不改 DSS 业务代码。

4. **全量编译**：CMakeLists.txt 不排除任何 .cpp 文件，所有 36 个 .cpp 文件全部参与编译。编译问题逐个解决。

5. **功能优先于性能**：AVX/SIMD 代码如果编译有问题可以 fallback 到非 SIMD 路径，关键是注册和叠加的最终结果正确。
