// pch.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//      are changed infrequently
//
#pragma once

// Qt stubs (replaces all real Qt includes)
#include "dss_qt.h"

// Windows types needed by libraw headers
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Standard Libraries
#include <shared_mutex>
#include <omp.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <mutex>
#include <deque>
#include <set>
#include <future>
#include <inttypes.h>
#include <filesystem>
#include <ranges>
#include <span>
#include <numeric>
#include <cmath>
#include <functional>
#include <map>
#include <unordered_map>
#include <sstream>
#include <optional>
#include <tuple>
#include <memory>
#include <limits>
#include <type_traits>
#include <cassert>
#include <array>
#include <bit>

// C++23 std::byteswap compat shim for C++20
namespace std {
	template<typename T>
	constexpr T byteswap(T value) noexcept {
		if constexpr (sizeof(T) == 1) return value;
		else if constexpr (sizeof(T) == 2) {
			return static_cast<T>(_byteswap_ushort(static_cast<uint16_t>(value)));
		} else if constexpr (sizeof(T) == 4) {
			return static_cast<T>(_byteswap_ulong(static_cast<uint32_t>(value)));
		} else if constexpr (sizeof(T) == 8) {
			return static_cast<T>(_byteswap_uint64(static_cast<uint64_t>(value)));
		}
	}
}

namespace fs = std::filesystem;

using std::min;
using std::max;

#include "zexcept.h"
#include "ztrace.h"

// As this interface is used everywhere for error reporting.
// If it got too big, or changed a lot, then we could move out to specific cpp files.
#include "dssbase.h"
