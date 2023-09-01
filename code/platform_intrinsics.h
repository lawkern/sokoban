/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

#if __ARM_NEON
#   include <arm_neon.h>

typedef float32x4_t f32_4x;
typedef uint32x4_t u32_4x;

#   define u32_4x_set1(v) {(v), (v), (v), (v)}
#   define u32_4x_and(a, b) vandq_u32((a), (b))
#   define u32_4x_or(a, b) vorrq_u32((a), (b))
#   define u32_4x_slli(v, n) vshlq_n_u32((v), (n))
#   define u32_4x_srli(v, n) vshrq_n_u32((v), (n))
#   define u32_4x_loadu(p) vld1q_u32((u32 *)(p))
#   define u32_4x_storeu(p, v) vst1q_u32((u32 *)(p), (v))
#   define u32_4x_convert_f32_4x(v) vcvtq_f32_u32(v)

#   define f32_4x_set1(v) {(v), (v), (v), (v)}
#   define f32_4x_add(a, b) vaddq_f32((a), (b))
#   define f32_4x_sub(a, b) vsubq_f32((a), (b))
#   define f32_4x_mul(a, b) vmulq_f32((a), (b))
#   define f32_4x_convert_u32_4x(v) vcvtq_u32_f32(v)

#else
#   include <immintrin.h>

typedef __m128 f32_4x;
typedef __m128i u32_4x;

#   define u32_4x_set1(v) _mm_set1_epi32(v)
#   define u32_4x_and(a, b) _mm_and_si128((a), (b))
#   define u32_4x_or(a, b) _mm_or_si128((a), (b))
#   define u32_4x_slli(v, n) _mm_slli_epi32((v), (n))
#   define u32_4x_srli(v, n) _mm_srli_epi32((v), (n))
#   define u32_4x_loadu(p) _mm_loadu_si128((u32_4x *)(p))
#   define u32_4x_storeu(p, v) _mm_storeu_si128((u32_4x *)(p), (v))
#   define u32_4x_convert_f32_4x(v) _mm_cvtps_epi32(v)

#   define f32_4x_set1(v) _mm_set1_ps(v)
#   define f32_4x_add(a, b) _mm_add_ps((a), (b))
#   define f32_4x_sub(a, b) _mm_sub_ps((a), (b))
#   define f32_4x_mul(a, b) _mm_mul_ps((a), (b))
#   define f32_4x_convert_u32_4x(v) _mm_cvtepi32_ps(v)
#endif
