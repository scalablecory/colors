/*
	Color conversions
	Copyright (c) 2011, Cory Nelson (phrosty@gmail.com)
	All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:
		 * Redistributions of source code must retain the above copyright
			notice, this list of conditions and the following disclaimer.
		 * Redistributions in binary form must reproduce the above copyright
			notice, this list of conditions and the following disclaimer in the
			documentation and/or other materials provided with the distribution.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
	ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
	WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
	DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
	DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
	(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
	LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
	ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
	(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
	SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include <stdint.h>

#ifdef COLOR_STATIC
#define COLOR_EXPORT
#else
#ifdef COLOR_EXPORTS
#define COLOR_EXPORT __declspec(dllexport)
#else
#define COLOR_EXPORT __declspec(dllimport)
#endif
#endif

#define COLOR_CALL _cdecl

#ifdef __cplusplus
extern "C" {
#endif

enum color_type
{
	COLOR_NONE,
	COLOR_RGB8,
	COLOR_RGB,
	COLOR_LINEAR_RGB,
	COLOR_HSL,
	COLOR_HSV,
	COLOR_YUV,
	COLOR_YCBCR,
	COLOR_YDBDR,
	COLOR_YIQ,
	COLOR_XYZ,
	COLOR_XYY,
	COLOR_LAB,
	COLOR_LUV,
	COLOR_LCHAB,
	COLOR_LCHUV,
	COLOR_LSHUV,
	COLOR_DUMMY_END
};

enum color_extra
{
	//COLOR_NONE,
	COLOR_YUV_MAT_REC601 = 0,
	COLOR_YUV_MAT_REC709 = 1,
	COLOR_YUV_MAT_SMPTE240M = 2,
	COLOR_YUV_MAT_FCC = 3,
	COLOR_YUV_MAT_MASK = 3,
	COLOR_YCBCR_FULL_RANGE = 4,
};

struct color
{
	uint8_t type, extra;
	union
	{
		struct { uint8_t R, G, B; } RGB8;
		struct { double R, G, B; } RGB, LinearRGB;
		struct { double H, S, L; } HSL; // hue is in [0, 6)
		struct { double H, S, V; } HSV; // hue is in [0, 6)
		struct { double Y, U, V; } YUV; // Y, U, V are in [0, 1], [-0.436,0.436], [-0.615,0.615]
		struct { uint8_t Y, Cb, Cr; } YCbCr;
		struct { double Y, Db, Dr; } YDbDr;
		struct { double Y, I, Q; } YIQ;
		struct { double X, Y, Z; } XYZ;
		struct { double x, y, Y; } xyY;
		struct { double L, a, b; } Lab;
		struct { double L, u, v; } Luv;
		struct { double L, C, h; } LCHab, LCHuv;  // hue is in [0, pi*2)
		struct { double L, S, h; } LSHuv;  // hue is in [0, pi*2)
	};
};

COLOR_EXPORT void COLOR_CALL color_convert(struct color *c, enum color_type new_type, uint8_t new_extra);
COLOR_EXPORT char const* COLOR_CALL color_name(enum color_type type);
COLOR_EXPORT void COLOR_CALL color_extract_components(double *dst, struct color const *src);

#ifdef __cplusplus
}
#endif
