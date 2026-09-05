// license:BSD-3-Clause
//
// The handful of MAME core primitives the extracted device code relies on.
// Reimplemented here so that mcs51ops.cpp and ef9345.cpp can be used with (very
// nearly) no changes, without dragging in src/emu and src/lib/util.

#ifndef MINITEL_TYPES_H
#define MINITEL_TYPES_H

#pragma once

#include <cstdint>
#include <cstdio>

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8  = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;

using offs_t = std::uint32_t;

// MAME's line states
enum { CLEAR_LINE = 0, ASSERT_LINE = 1, HOLD_LINE = 2 };

// BIT(x, n) -> bit n of x; BIT(x, n, w) -> the w bits of x starting at n
template <typename T> constexpr T BIT(T x, unsigned n) noexcept { return (x >> n) & T(1); }
template <typename T> constexpr T BIT(T x, unsigned n, unsigned w) noexcept { return (x >> n) & ((T(1) << w) - 1); }

// bitswap<N>(val, b(N-1), ..., b0) - assemble a value from the named bit
// positions of val, most significant first.
template <typename T, typename U> constexpr T bitswap_impl(T val, U b) noexcept
{
	return BIT(val, b);
}

template <typename T, typename U, typename... V> constexpr T bitswap_impl(T val, U b, V... c) noexcept
{
	return (T(BIT(val, b)) << sizeof...(c)) | bitswap_impl<T>(val, c...);
}

template <unsigned N, typename T, typename... U> constexpr T bitswap(T val, U... b) noexcept
{
	static_assert(sizeof...(b) == N, "wrong number of bits");
	return bitswap_impl<T>(val, b...);
}

// 32-bit packed color, as MAME's rgb_t stores it in a bitmap_rgb32
class rgb_t
{
public:
	constexpr rgb_t() : m_data(0) { }
	constexpr rgb_t(u32 data) : m_data(data) { }
	constexpr rgb_t(u8 r, u8 g, u8 b) : m_data(0xff000000u | (u32(r) << 16) | (u32(g) << 8) | b) { }

	constexpr operator u32() const { return m_data; }

private:
	u32 m_data;
};

// MAME logs unimplemented cases through logerror(); route them to stderr so the
// same diagnostics survive the extraction.
#ifdef MINITEL_QUIET
#define logerror(...) do { } while (false)
#else
#define logerror(...) std::fprintf(stderr, __VA_ARGS__)
#endif

// MAME's LOG() with VERBOSE = 0: compiled away, but the arguments still have to
// typecheck, exactly as in src/lib/util/logmacro.h.
template <typename... T> inline void mame_log_sink(T &&...) { }
#define LOG(...) mame_log_sink(__VA_ARGS__)

#endif // MINITEL_TYPES_H
