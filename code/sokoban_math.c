/* /////////////////////////////////////////////////////////////////////////// */
/* (c) copyright 2023 Lawrence D. Kern /////////////////////////////////////// */
/* /////////////////////////////////////////////////////////////////////////// */

// TODO(law): Remove dependency on math.h!
#include <math.h>

#define TAU32 6.2831853f
#define ROOT2 1.41421356f

function float sine(float turns)
{
   float result = sinf(TAU32 * turns);
   return(result);
}

function float cosine(float turns)
{
   float result = cosf(TAU32 * turns);
   return(result);
}

function s32 floor_s32(float value)
{
   s32 result = (s32)floorf(value);
   return(result);
}

function s32 ceiling_s32(float value)
{
   s32 result = (s32)ceilf(value);
   return(result);
}
