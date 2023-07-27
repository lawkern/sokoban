/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

#include <immintrin.h>

typedef __m128 f32_4x;
typedef __m128i u32_4x;

#define u32_4x_set1(v) _mm_set1_epi32(v)
#define u32_4x_and(a, b) _mm_and_si128((a), (b))
#define u32_4x_or(a, b) _mm_or_si128((a), (b))
#define u32_4x_slli(v, n) _mm_slli_epi32((v), (n))
#define u32_4x_srli(v, n) _mm_srli_epi32((v), (n))
#define u32_4x_loadu(p) _mm_loadu_si128(p)
#define u32_4x_storeu(p, v) _mm_storeu_si128((p), (v))
#define u32_4x_convert_f32_4x(v) _mm_cvtps_epi32(v)

#define f32_4x_set1(v) _mm_set1_ps(v)
#define f32_4x_add(a, b) _mm_add_ps((a), (b))
#define f32_4x_sub(a, b) _mm_sub_ps((a), (b))
#define f32_4x_mul(a, b) _mm_mul_ps((a), (b))
#define f32_4x_convert_u32_4x(v) _mm_cvtepi32_ps(v)
