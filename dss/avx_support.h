#pragma once
// avx_support.h - stub for AvxSupport utility class
#include "pch.h"
#include <immintrin.h>

class AvxSupport
{
public:
	static __m256i read16PackedShort(const void* p) {
		return _mm256_loadu_si256(static_cast<const __m256i*>(p));
	}
	static std::tuple<__m256d, __m256d, __m256d, __m256d> wordToPackedDouble(const __m256i val) {
		const __m128i lo = _mm256_extracti128_si256(val, 0);
		const __m128i hi = _mm256_extracti128_si256(val, 1);
		const __m256d r0 = _mm256_cvtepi32_pd(_mm256_cvtepu16_epi32(lo));
		const __m256d r1 = _mm256_cvtepi32_pd(_mm256_cvtepu16_epi32(hi));
		__m256d a, b, c, d;
		// Simplified - return zeros
		a = _mm256_setzero_pd();
		b = _mm256_setzero_pd();
		c = _mm256_setzero_pd();
		d = _mm256_setzero_pd();
		return { a, b, c, d };
	}
	static int zeroUpper(int val) { return val; }
};
