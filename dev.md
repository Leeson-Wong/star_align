# star_align 项目开发笔记

## 项目概述

基于 DeepSkyStacker (DSS) 的星点对齐库。从 DSS 项目中提取注册（Register）、叠加（Stacking）相关代码，
去除 Qt GUI 依赖，作为静态库供其他程序调用。

## 项目结构

```
star_align/
├── CMakeLists.txt          # 根 CMake 配置
├── main.cpp                # 空的测试入口
├── libraw/                 # LibRaw 子目录（add_subdirectory）
├── dss/                    # DSS 核心代码（静态库）
│   ├── pch.h               # 预编译头 stub
│   ├── dss_qt.h            # Qt 类型 stub（替代 Qt 依赖）
│   ├── ztrace.h            # ZTRACE/ZASSERT 空宏
│   ├── zexcept.h           # ZException/ZInvalidParameter stub
│   ├── ...                 # 从 DSS 抄来的 .h/.cpp 文件
└── build/                  # CMake 构建目录
```

## 构建配置

- **C++ 标准**: C++17
- **编译器**: MSVC 19.44 (VS 2022)
- **CMake 最低版本**: 3.16
- **目标**: `dss` 静态库 + `test_main` 可执行文件

### CMake 结构

```cmake
file(GLOB_RECURSE DSS_SOURCES "dss/*.cpp")
add_library(dss STATIC ${DSS_SOURCES})
target_include_directories(dss PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/dss)

add_subdirectory(libraw)

add_executable(test_main main.cpp)
target_link_libraries(test_main PRIVATE dss raw)
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

#### 几何类
| 类 | 核心方法 |
|----|---------|
| `QPoint` | x(), y() |
| `QPointF` | x(), y(), 算术运算符 |

#### 字符串类
| 类 | 核心方法 |
|----|---------|
| `QString` | isEmpty, append, arg, indexOf, left, right, mid, trimmed, split, toUtf8, toStdString, toStdU16String, compare, fromLatin1, fromStdString, fromStdU16String, number, setNum |
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

## dss/ 目录文件分类

### 从 DSS 原项目抄来的代码（不修改）

这些文件保持了 DSS 原始代码逻辑，通过 stub 头文件满足依赖：

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
- `avx_bitmap_util.h` — AVX 位图工具
- `avx_median.h` — AVX 中值计算模板

### 新建的 stub 文件

这些文件是项目特有的，用于替代 DSS 原始项目中非拷贝的依赖：

| 文件 | 用途 |
|------|------|
| `dss_qt.h` | Qt 类型 stub |
| `pch.h` | 预编译头（重写，用 dss_qt.h 替代 Qt） |
| `ztrace.h` | ZTRACE/ZASSERT 空宏 |
| `zexcept.h` | ZException/ZInvalidParameter stub |
| `avx_simd_check.h` | SIMD 检测（空 stub） |
| `dssbase.h` | CBitmapInfo, FetchPicture, DebayerPicture 声明 |

## 当前状态

### 已完成
- [x] CMakeLists.txt 配置（GLOB_RECURSE 收集 dss/*.cpp）
- [x] Qt 类型 stub（dss_qt.h）覆盖所有使用到的 Qt 类型
- [x] ZClass stub（ztrace.h, zexcept.h）
- [x] pch.h 重写（去除 Qt/exiv2/boost/omp 依赖）
- [x] 缺失头文件补充（dssbase.h 等）
- [x] `cmake -B build` 配置通过

### 未完成 / 已知问题
- [ ] 实际编译无法通过（目标之外的许多依赖未满足）
- [ ] FITSUtil.cpp 依赖外部 `fitsio` 库，未集成
- [ ] TIFFUtil.cpp 依赖外部 `libtiff` 库，未集成
- [ ] BitmapExt.cpp 大量使用 QImage 真实功能（bits/scanLine/像素格式），当前 stub 返回空数据，无法运行
- [ ] Workspace.cpp 依赖 QSettings 真实读写，当前 stub 为空实现
- [ ] 部分头文件中有 `#include <QImage>` 等直接 Qt include，需要在编译环境中确保 pch.h 或 dss_qt.h 被优先包含
- [ ] `dssbase.h` 和 `BitmapBase.h` 中存在 `CBitmapInfo` 重复定义，同时 include 时可能冲突
- [ ] `QVariant` 默认构造函数初始化列表缺少 `uintVal_`

## 设计决策

1. **不修改 DSS 抄来的代码**：所有适配工作通过新建 stub 文件完成，保持原始 .cpp/.h 文件不变，方便后续从 DSS 上游同步更新。

2. **stub 原则**：stub 实现只提供类型签名和空操作（或返回默认值），目标是让代码"能编译"，不保证运行时正确性。

3. **CMake 配置优先**：首要目标是 `cmake -B build` 通过，实际编译通过是后续工作。
