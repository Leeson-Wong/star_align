#pragma once
// Precompiled header stub - uses dss_qt.h instead of real Qt headers

// Qt type stubs
#include "dss_qt.h"

// Standard Libraries
#include <shared_mutex>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <mutex>
#include <deque>
#include <set>
#include <map>
#include <future>
#include <numeric>
#include <inttypes.h>
#include <filesystem>
#include <atomic>
#include <memory>
#include <functional>
#include <array>
#include <cmath>
#include <cassert>
#include <type_traits>
#include <optional>
#include <utility>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <cstddef>
#include <iterator>

// Minimal std::span polyfill for C++17
namespace std {
static constexpr ptrdiff_t dynamic_extent = -1;

template<typename T, ptrdiff_t Extent = dynamic_extent>
class span {
public:
	static constexpr ptrdiff_t extent = Extent;
	using element_type = T;
	using value_type = typename remove_cv<T>::type;
	using size_type = size_t;
	using pointer = T*;
	using const_pointer = const T*;
	using reference = T&;
	using const_reference = const T&;
	using iterator = pointer;
	using const_iterator = const_pointer;

	constexpr span() noexcept : data_(nullptr), size_(0) {}
	constexpr span(pointer ptr, size_type count) : data_(ptr), size_(count) {}
	constexpr span(pointer first, pointer last) : data_(first), size_(last - first) {}
	template<size_t N>
	constexpr span(element_type (&arr)[N]) noexcept : data_(arr), size_(N) {}
	template<typename Container>
	constexpr span(Container& c) : data_(c.data()), size_(c.size()) {}
	template<typename Container>
	constexpr span(const Container& c) : data_(c.data()), size_(c.size()) {}
	template<typename U, ptrdiff_t E, typename = enable_if_t<(E == dynamic_extent || E == Extent) && is_convertible_v<U(*)[], element_type(*)[]>>>
	constexpr span(const span<U, E>& other) : data_(other.data()), size_(other.size()) {}

	constexpr reference operator[](size_type idx) const { return data_[idx]; }
	constexpr reference front() const { return data_[0]; }
	constexpr reference back() const { return data_[size_ - 1]; }
	constexpr pointer data() const noexcept { return data_; }
	constexpr size_type size() const noexcept { return size_; }
	constexpr size_type size_bytes() const noexcept { return size_ * sizeof(element_type); }
	constexpr bool empty() const noexcept { return size_ == 0; }
	constexpr iterator begin() const noexcept { return data_; }
	constexpr iterator end() const noexcept { return data_ + size_; }
	constexpr const_iterator cbegin() const noexcept { return data_; }
	constexpr const_iterator cend() const noexcept { return data_ + size_; }
	constexpr span first(size_type count) const { return { data_, count }; }
	constexpr span last(size_type count) const { return { data_ + size_ - count, count }; }
	constexpr span subspan(size_type offset, size_type count = static_cast<size_type>(-1)) const {
		return { data_ + offset, count == static_cast<size_type>(-1) ? size_ - offset : count };
	}
private:
	pointer data_;
	size_type size_;
};

template<typename T, ptrdiff_t E>
constexpr auto as_const(span<T, E> s) noexcept { return span<const T, E>(s); }
} // namespace std

namespace fs = std::filesystem;

using std::min;
using std::max;

// ============================================================
// C++20 polyfills for C++17 compatibility
// ============================================================

// consteval -> constexpr (C++20 keyword)
#ifndef consteval
#define consteval constexpr
#endif

// char8_t (C++20 type)
#ifndef __cpp_char8_t
typedef unsigned char char8_t;
#endif

// ZClass stubs
#include "ztrace.h"
#include "zexcept.h"
