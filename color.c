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

	Some notes:

	- Performance is important, but takes a back seat to accuracy.
	- Constants are expressed as rationals when possible. When not, they are given to 96 bits of precision.
	- Care has been taken to allow emitting of high-accuracy FMA instructions.
	- Specialized shortcuts between some colorspaces have been made to improve performance and accuracy.
*/

#define COLOR_EXPORTS

#define STRICT
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <math.h>
#include <assert.h>
#include <stddef.h>
#include "color.h"

static double const COLOR_REF_X = 31271.0/32902.0;
static double const COLOR_REF_Xr = 32902.0/31271.0;

//static double const COLOR_REF_Y = 1.0;
//static double const COLOR_REF_Yr = 1.0;

static double const COLOR_REF_Z = 35827.0/32902.0;
static double const COLOR_REF_Zr = 32902.0/35827.0;

static double const COLOR_REF_U13 = 813046.0/316141.0; // X * 4 / (X + Y * 15 + Z * 3) * 13
static double const COLOR_REF_V13 = 1924767.0/316141.0; // Y * 9 / (X + Y * 15 + Z * 3) * 13

__declspec(align(64)) double const rgb_to_yuv[][8] =
{
	// Rec. 601
	{ 0.299, 0.587, 0.114, -32591.0/221500.0, -63983.0/221500.0, -72201.0/140200.0, -7011.0/70100.0 },
	// Rec. 709
	{ 0.2126, 0.7152, 0.0722, -115867.0/1159750.0, -194892.0/579875.0, -54981.0/98425.0, -44403.0/787400.0 },
	// SMPTE 240M
	{ 0.212, 0.701, 0.087, -11554.0/114125.0, -76409.0/228250.0, -86223.0/157600.0, -10701.0/157600.0 },
	// FCC
	{ 0.3, 0.59, 0.11, -327.0/2225.0, -6431.0/22250.0, -7257.0/14000.0, -1353.0/14000.0 }
};

__declspec(align(32)) double const yuv_to_rgb[][4] =
{
	// Rec. 601
	{ 701.0/615.0, -25251.0/63983.0, -209599.0/361005.0, 443.0/218.0 },
	// Rec. 709
	{ 3937.0/3075.0, -1674679.0/7795680.0, -4185031.0/10996200.0, 4639.0/2180.0 },
	// SMPTE 240M
	{ 788.0/615.0, -79431.0/305636.0, -167056.0/431115.0, 913.0/436.0 },
	// FCC
	{ 140.0/123.0, -4895.0/12862.0, -1400.0/2419.0, 445.0/218.0 }
};

static void color_RGB8_extract(double *dst, struct color const *src)
{
	assert(dst != NULL);
	assert(src != NULL);
	assert(src->type == COLOR_RGB8);

	dst[0] = src->RGB8.R;
	dst[1] = src->RGB8.G;
	dst[2] = src->RGB8.B;
}

static void color_RGB8_to_RGB(struct color *c, uint8_t extra)
{
	double R, G, B;

	assert(c != NULL);
	assert(c->type == COLOR_RGB8);
	
	R = c->RGB8.R * (1.0 / 255.0);
	G = c->RGB8.G * (1.0 / 255.0);
	B = c->RGB8.B * (1.0 / 255.0);
	
	c->RGB.R = R;
	c->RGB.G = G;
	c->RGB.B = B;
	c->type = COLOR_RGB;
}

static double rgb8_to_linear(uint8_t c)
{
	return c >= 11 ? pow(c * (40.0 / 10761.0) + (11.0 / 211.0), 2.4) : c * (5.0 / 16473.0);
}

static void color_RGB8_to_LinearRGB(struct color *c, uint8_t extra)
{
	double R, G, B;

	assert(c != NULL);
	assert(c->type == COLOR_RGB8);

	R = rgb8_to_linear(c->RGB8.R);
	G = rgb8_to_linear(c->RGB8.G);
	B = rgb8_to_linear(c->RGB8.B);
	
	c->LinearRGB.R = R;
	c->LinearRGB.G = G;
	c->LinearRGB.B = B;
	c->type = COLOR_LINEAR_RGB;
}

static void color_RGB_extract(double *dst, struct color const *src)
{
	assert(dst != NULL);
	assert(src != NULL);
	assert(src->type == COLOR_RGB);

	dst[0] = src->RGB.R;
	dst[1] = src->RGB.G;
	dst[2] = src->RGB.B;
}

static void color_RGB_to_RGB8(struct color *c, uint8_t extra)
{
	double R, G, B;

	assert(c != NULL);
	assert(c->type == COLOR_RGB);
	
	R = c->RGB.R * 255.0 + 0.5;
	G = c->RGB.G * 255.0 + 0.5;
	B = c->RGB.B * 255.0 + 0.5;
	
	c->type = COLOR_RGB8;
	
	c->RGB8.R = R < 0.0 ? 0 : R > 255.0 ? 255 : (uint8_t)R;
	c->RGB8.G = G < 0.0 ? 0 : G > 255.0 ? 255 : (uint8_t)G;
	c->RGB8.B = B < 0.0 ? 0 : B > 255.0 ? 255 : (uint8_t)B;
}

static double rgb_to_linear(double c)
{
	return c > (0.0031308 * 12.92) ? pow(c * (1.0 / 1.055) + (0.055 / 1.055), 2.4) : c * (1.0 / 12.92);
}

static void color_RGB_to_LinearRGB(struct color *c, uint8_t extra)
{
	double R, G, B;

	assert(c != NULL);
	assert(c->type == COLOR_RGB);

	R = rgb_to_linear(c->RGB.R);
	G = rgb_to_linear(c->RGB.G);
	B = rgb_to_linear(c->RGB.B);
	
	c->LinearRGB.R = R;
	c->LinearRGB.G = G;
	c->LinearRGB.B = B;
	c->type = COLOR_LINEAR_RGB;
}

static void color_RGB_to_HSL(struct color *c, uint8_t extra)
{
	double R, G, B, min, max, delta, L;

	assert(c != NULL);
	assert(c->type == COLOR_RGB);
	
	R = c->RGB.R;
	G = c->RGB.G;
	B = c->RGB.B;

	min = R < G ? R : G;
	if(B < min) min = B;

	max = R > G ? R : G;
	if(B > max) max = B;

	delta = max - min;

	L = (max + min) * 0.5;

	c->HSL.L = L;
	c->type = COLOR_HSL;

	if(fabs(delta) > 0.0)
	{
		c->HSL.S =
			L < 0.5 ? delta / (max + min) :
			delta / (2.0 - max - min);

		c->HSL.H =
			max == R ? (G - B) / delta :
			max == G ? (B - R) / delta + 2.0 :
			(R - G) / delta + 4.0;

		return;
	}

	c->HSL.S = 0.0;
	c->HSL.H = 0.0;
}

static void color_RGB_to_HSV(struct color *c, uint8_t extra)
{
	double R, G, B, min, max, delta;

	assert(c != NULL);
	assert(c->type == COLOR_RGB);
	
	R = c->RGB.R;
	G = c->RGB.G;
	B = c->RGB.B;

	min = R < G ? R : G;
	if(B < min) min = B;

	max = R > G ? R : G;
	if(B > max) max = B;

	delta = max - min;

	c->HSV.V = max;
	c->type = COLOR_HSV;

	if(fabs(delta) > 0.0)
	{
		c->HSV.S = delta / max;

		c->HSV.H =
			max == R ? (G - B) / delta :
			max == G ? (B - R) / delta + 2.0 :
			(R - G) / delta + 4.0;

		return;
	}

	c->HSV.S = 0.0;
	c->HSV.H = 0.0;
}

static void color_RGB_to_YUV(struct color *c, uint8_t extra)
{
	double R, G, B;
	double const *mat;

	assert(c != NULL);
	assert(c->type == COLOR_RGB);
	
	R = c->RGB.R;
	G = c->RGB.G;
	B = c->RGB.B;

	mat = rgb_to_yuv[extra & COLOR_YUV_MAT_MASK];

	c->YUV.Y = R * mat[0] + G * mat[1] + B * mat[2];
	c->YUV.U = R * mat[3] + G * mat[4] + B * 0.436;
	c->YUV.V = R * 0.615  + G * mat[5] + B * mat[6];
	
	c->type = COLOR_YUV;
	c->extra = extra;
}

static void color_RGB_to_YDbDr(struct color *c, uint8_t extra)
{
	double R, G, B;

	assert(c != NULL);
	assert(c->type == COLOR_RGB);
	
	R = c->RGB.R;
	G = c->RGB.G;
	B = c->RGB.B;

	c->YDbDr.Y =  R * (299.0/1000.0)       + G * (587.0/1000.0)       + B * (57.0/500.0);
	c->YDbDr.Db = R * (-398567.0/886000.0) + G * (-782471.0/886000.0) + B * (1333.0/1000.0);
	c->YDbDr.Dr = R * (1333.0/1000.0)      + G * (-782471.0/701000.0) + B * (-75981.0/350500.0);
	c->type = COLOR_YDBDR;
}

static void color_RGB_to_YIQ(struct color *c, uint8_t extra)
{
	double R, G, B;

	assert(c != NULL);
	assert(c->type == COLOR_RGB);
	
	R = c->RGB.R;
	G = c->RGB.G;
	B = c->RGB.B;

	c->YIQ.Y = R *  0.299                          + G *  0.587                          + B *  0.114;
	c->YIQ.I = R *  0.5957                         + G * -0.2744766323826577035751015648 + B * -0.3212233676173422964248984352;
	c->YIQ.Q = R * -0.2114956266791979792324116478 + G *  0.5226                         + B * -0.3111043733208020207675883522;
	c->type = COLOR_YIQ;
}

static uint8_t linear_to_rgb8(double c)
{
	if(c <= 0.0)
	{
		return 0;
	}

	if(c <= 0.0031308)
	{
		return (uint8_t)(int)(c * 3294.6 + 0.5);
	}

	if(c < 1.0)
	{
		return (uint8_t)(int)(pow(c, 1.0 / 2.4) * 269.025 - (14.025 - 0.5));
	}

	return 255;
}

static void color_LinearRGB_extract(double *dst, struct color const *src)
{
	assert(dst != NULL);
	assert(src != NULL);
	assert(src->type == COLOR_LINEAR_RGB);

	dst[0] = src->LinearRGB.R;
	dst[1] = src->LinearRGB.G;
	dst[2] = src->LinearRGB.B;
}

static void color_LinearRGB_to_RGB8(struct color *c, uint8_t extra)
{
	uint8_t R, G, B;

	assert(c != NULL);
	assert(c->type == COLOR_LINEAR_RGB);
	
	R = linear_to_rgb8(c->LinearRGB.R);
	G = linear_to_rgb8(c->LinearRGB.G);
	B = linear_to_rgb8(c->LinearRGB.B);

	c->RGB8.R = R;
	c->RGB8.G = G;
	c->RGB8.B = B;
	c->type = COLOR_RGB8;
}

static double linear_to_rgb(double c)
{
	return c > 0.0031308 ? pow(c, 1.0 / 2.4) * 1.055 - 0.055 : c * 12.92;
}

static void color_LinearRGB_to_RGB(struct color *c, uint8_t extra)
{
	double R, G, B;

	assert(c != NULL);
	assert(c->type == COLOR_LINEAR_RGB);
	
	R = linear_to_rgb(c->LinearRGB.R);
	G = linear_to_rgb(c->LinearRGB.G);
	B = linear_to_rgb(c->LinearRGB.B);

	c->RGB.R = R;
	c->RGB.G = G;
	c->RGB.B = B;
	c->type = COLOR_RGB;
}

static void color_LinearRGB_to_XYZ(struct color *c, uint8_t extra)
{
	double R, G, B;

	assert(c != NULL);
	assert(c->type == COLOR_LINEAR_RGB);
	
	R = c->LinearRGB.R;
	G = c->LinearRGB.G;
	B = c->LinearRGB.B;

	c->XYZ.X = R * (5067776.0/12288897.0) + G * (4394405.0/12288897.0) + B * (4435075.0/24577794.0);
	c->XYZ.Y = R * (871024.0/4096299.0)   + G * (8788810.0/12288897.0) + B * (887015.0/12288897.0);
	c->XYZ.Z = R * (79184.0/4096299.0)    + G * (4394405.0/36866691.0) + B * (70074185.0/73733382.0);
	c->type = COLOR_XYZ;
}

static double xyz_to_lab(double c)
{
	return c > 216.0 / 24389.0 ? pow(c, 1.0 / 3.0) : c * (841.0/108.0) + (4.0/29.0);
}

static void color_LinearRGB_to_Lab(struct color *c, uint8_t extra)
{
	double R, G, B, X, Y, Z;

	assert(c != NULL);
	assert(c->type == COLOR_LINEAR_RGB);
	
	R = c->LinearRGB.R;
	G = c->LinearRGB.G;
	B = c->LinearRGB.B;
	
	// linear sRGB -> normalized XYZ (X,Y,Z are all in 0...1)
	
	X = xyz_to_lab(R * (10135552.0/23359437.0) + G * (8788810.0/23359437.0) + B * (4435075.0/23359437.0));
	Y = xyz_to_lab(R * (871024.0/4096299.0)    + G * (8788810.0/12288897.0) + B * (887015.0/12288897.0));
	Z = xyz_to_lab(R * (158368.0/8920923.0)    + G * (8788810.0/80288307.0) + B * (70074185.0/80288307.0));

	// normalized XYZ -> Lab

	c->Lab.L = Y * 116.0 - 16.0;
	c->Lab.a = (X - Y) * 500.0;
	c->Lab.b = (Y - Z) * 200.0;
	c->type = COLOR_LAB;
}

static void color_HSL_extract(double *dst, struct color const *src)
{
	assert(dst != NULL);
	assert(src != NULL);
	assert(src->type == COLOR_HSL);

	dst[0] = src->HSL.H;
	dst[1] = src->HSL.S;
	dst[2] = src->HSL.L;
}

static void finish_HSL_to_RGB(struct color *c, double h, double C, double m)
{
	static const char rgb_tbl[][3] =
	{
		{ 0, 2, 1 },
		{ 2, 0, 1 },
		{ 1, 0, 2 },
		{ 1, 2, 0 },
		{ 2, 1, 0 },
		{ 0, 1, 2 }
	};

	double absh, h2, vars[3];
	int idx;

	// clamps hue to [0, 2), and returns (1.0 - fabs(hue - 1.0))

	absh = fabs(h);

	h2 =
		absh >= 2.0 ? floor(h * 0.5) * -2.0 + h - 1.0 :
		h < 0.0 ? h + 1.0 :
		h - 1.0;
	h2 = 1.0 - fabs(h2);

	// clamps hue to [0, 6), for indexing into rgb_tbl.

	idx = (int)
		(absh >= 6.0 ? floor(h * (1.0 / 6.0)) * -6.0 + h :
		h < 0.0 ? h + 6.0 :
		h);

	assert(idx >= 0 && idx <= 5);

	// finish HSL->RGB.

	vars[0] = C + m;
	vars[1] = m;
	vars[2] = C * h2 + m;
	
	c->RGB.R = vars[rgb_tbl[idx][0]];
	c->RGB.G = vars[rgb_tbl[idx][1]];
	c->RGB.B = vars[rgb_tbl[idx][2]];
}

static void color_HSL_to_RGB(struct color *c, uint8_t extra)
{
	double H, S, L, C, m;

	assert(c != NULL);
	assert(c->type == COLOR_HSL);

	H = c->HSL.H;
	S = c->HSL.S;
	L = c->HSL.L;

	c->type = COLOR_RGB;

	if(fabs(S) > 0.0)
	{
		C = (1.0 - fabs(L * 2.0 - 1.0)) * S;
		m = C * -0.5 + L;

		finish_HSL_to_RGB(c, H, C, m);
		return;
	}
	
	c->RGB.R = L; c->RGB.G = L; c->RGB.B = L;
}

static void color_HSV_extract(double *dst, struct color const *src)
{
	assert(dst != NULL);
	assert(src != NULL);
	assert(src->type == COLOR_HSV);

	dst[0] = src->HSV.H;
	dst[1] = src->HSV.S;
	dst[2] = src->HSV.V;
}

static void color_HSV_to_RGB(struct color *c, uint8_t extra)
{
	double H, S, V, C, m;

	assert(c != NULL);
	assert(c->type == COLOR_HSV);

	H = c->HSV.H;
	S = c->HSV.S;
	V = c->HSV.V;

	c->type = COLOR_RGB;

	if(fabs(S) > 0.0)
	{
		C = V * S;
		m = V - C;

		finish_HSL_to_RGB(c, H, C, m);
		return;
	}
	
	c->RGB.R = V; c->RGB.G = V; c->RGB.B = V;
}

static void color_YUV_extract(double *dst, struct color const *src)
{
	assert(dst != NULL);
	assert(src != NULL);
	assert(src->type == COLOR_YUV);

	dst[0] = src->YUV.Y;
	dst[1] = src->YUV.U;
	dst[2] = src->YUV.V;
}

static void color_YUV_to_RGB(struct color *c, uint8_t extra)
{
	double Y, U, V;
	double const *mat;

	assert(c != NULL);
	assert(c->type == COLOR_YUV);
	
	Y = c->YUV.Y;
	U = c->YUV.U;
	V = c->YUV.V;

	mat = yuv_to_rgb[c->extra & COLOR_YUV_MAT_MASK];

	c->RGB.R = Y              + V * mat[0];
	c->RGB.G = Y + U * mat[1] + V * mat[2];
	c->RGB.B = Y + U * mat[3];
	c->type = COLOR_RGB;
	c->extra = 0;
}

static void color_YUV_to_YUV(struct color *c, uint8_t extra)
{
	assert(c != NULL);
	assert(c->type == COLOR_YUV);
	assert(c->extra != extra);

	color_YUV_to_RGB(c, extra);
	color_RGB_to_YUV(c, extra);

	assert(c->extra == extra);
}

static void color_YUV_to_YCbCr(struct color *c, uint8_t extra)
{
	double Y, U, V;

	assert(c != NULL);
	assert(c->type == COLOR_YUV);

	if((c->extra & COLOR_YUV_MAT_MASK) != (extra & COLOR_YUV_MAT_MASK))
	{
		color_YUV_to_YUV(c, extra);
	}

	Y = c->YUV.Y;
	U = c->YUV.U;
	V = c->YUV.V;

	if(extra & COLOR_YCBCR_FULL_RANGE)
	{
		Y = Y * 255.0 + 0.5;
		U = U * (31875.0/109.0) + 128;
		V = V * (8500.0/41.0) + 128;
	}
	else
	{
		Y = Y * 219.0 + 16.5;
		U = U * (28000.0/109.0) + 144;
		V = V * (22400.0/123.0) + 144;
	}
	
	c->YCbCr.Y = Y < 0.0 ? 0 : Y > 255.0 ? 255 : (int)Y;
	c->YCbCr.Cb = U < 0.0 ? 0 : U > 255.0 ? 255 : (int)U;
	c->YCbCr.Cr = V < 0.0 ? 0 : V > 255.0 ? 255 : (int)V;

	c->type = COLOR_YCBCR;
	c->extra = extra;
}

static void color_YCbCr_extract(double *dst, struct color const *src)
{
	assert(dst != NULL);
	assert(src != NULL);
	assert(src->type == COLOR_YCBCR);

	dst[0] = src->YCbCr.Y;
	dst[1] = src->YCbCr.Cb;
	dst[2] = src->YCbCr.Cr;
}

static void color_YCbCr_to_YUV(struct color *c, uint8_t extra)
{
	double Y, U, V;

	assert(c != NULL);
	assert(c->type == COLOR_YCBCR);
	
	Y = c->YCbCr.Y;
	U = c->YCbCr.Cb;
	V = c->YCbCr.Cr;

	if(c->extra & COLOR_YCBCR_FULL_RANGE)
	{
		Y *= (1.0 / 255.0);
		U = U * (109.0/31875.0) - 0.436;
		V = V * (41.0/8500.0) - 0.615;
	}
	else
	{
		Y = Y * (1.0/219.0) - (16.0/219.0);
		U = U * (109.0/28000.0) - 0.558625;
		V = V * (123.0/22400.0) - 0.78796875;
	}

	c->YUV.Y = Y;
	c->YUV.U = U;
	c->YUV.V = V;
	c->type = COLOR_YUV;
	c->extra &= ~(uint8_t)COLOR_YCBCR_FULL_RANGE;

	if((c->extra & COLOR_YUV_MAT_MASK) != (extra & COLOR_YUV_MAT_MASK))
	{
		color_YUV_to_YUV(c, extra);
	}
}

static void color_YCbCr_to_YCbCr(struct color *c, uint8_t extra)
{
	assert(c != NULL);
	assert(c->type == COLOR_YCBCR);
	assert(c->extra != extra);

	color_YCbCr_to_YUV(c, extra);
	color_YUV_to_YCbCr(c, extra);

	assert(c->extra == extra);
}

static void color_YDbDr_extract(double *dst, struct color const *src)
{
	assert(dst != NULL);
	assert(src != NULL);
	assert(src->type == COLOR_YDBDR);

	dst[0] = src->YDbDr.Y;
	dst[1] = src->YDbDr.Db;
	dst[2] = src->YDbDr.Dr;
}

static void color_YDbDr_to_RGB(struct color *c, uint8_t extra)
{
	double Y, Db, Dr;

	assert(c != NULL);
	assert(c->type == COLOR_YDBDR);
	
	Y = c->YDbDr.Y;
	Db = c->YDbDr.Db;
	Dr = c->YDbDr.Dr;

	c->RGB.R = Y                             + Dr * (701.0/1333.0);
	c->RGB.G = Y + Db * (-101004.0/782471.0) + Dr * (-209599.0/782471.0);
	c->RGB.B = Y + Db * (886.0/1333.0);
	c->type = COLOR_RGB;
}

static void color_YDbDr_to_YIQ(struct color *c, uint8_t extra)
{
	double Y, Db, Dr;

	assert(c != NULL);
	assert(c->type == COLOR_YDBDR);
	
	Y = c->YDbDr.Y;
	Db = c->YDbDr.Db;
	Dr = c->YDbDr.Dr;

	c->YUV.Y = Y;
	c->YUV.U =                                         Db * -1.780759334211551067290090872e-1 + Dr *  3.867911188667345780375729105e-1;
	c->YUV.V = Y * 3.155443620884047221646914261e-30 + Db * -2.742395246410785275938007739e-1 + Dr * -2.512094867865302853146089398e-1;
	c->type = COLOR_YIQ;
}

static void color_YIQ_extract(double *dst, struct color const *src)
{
	assert(dst != NULL);
	assert(src != NULL);
	assert(src->type == COLOR_YIQ);

	dst[0] = src->YIQ.Y;
	dst[1] = src->YIQ.I;
	dst[2] = src->YIQ.Q;
}

static void color_YIQ_to_RGB(struct color *c, uint8_t extra)
{
	double Y, I, Q;

	assert(c != NULL);
	assert(c->type == COLOR_YIQ);
	
	Y = c->YIQ.Y;
	I = c->YIQ.I;
	Q = c->YIQ.Q;

	c->RGB.R = Y + I *  9.563000521420394701478042310e-1 + Q * -6.209682015704038246103012680e-1;
	c->RGB.G = Y + I * -2.720883840788609953919979558e-1 + Q *  6.473748500336683799608873068e-1;
	c->RGB.B = Y + I * -1.107173983650687695430619869e0  + Q * -1.704732848247478907706673421e0;
	c->type = COLOR_RGB;
}

static void color_YIQ_to_YDbDr(struct color *c, uint8_t extra)
{
	double Y, I, Q;

	assert(c != NULL);
	assert(c->type == COLOR_YIQ);
	
	Y = c->YIQ.Y;
	I = c->YIQ.I;
	Q = c->YIQ.Q;

	c->YDbDr.Y =  Y                                      + I *  6.310887241768094443293828522e-30;
	c->YDbDr.Db =                                          I * -1.665759503618924038384894227e0 + Q * -2.564795583198520749405186986e0;
	c->YDbDr.Dr = Y * 1.009741958682895110927012564e-28  + I *  1.818470712561110718554954408e0 + Q * -1.180813998136017543802470171e0;
	c->type = COLOR_YDBDR;
}

static void color_XYZ_extract(double *dst, struct color const *src)
{
	assert(dst != NULL);
	assert(src != NULL);
	assert(src->type == COLOR_XYZ);

	dst[0] = src->XYZ.X;
	dst[1] = src->XYZ.Y;
	dst[2] = src->XYZ.Z;
}

static void color_XYZ_to_LinearRGB(struct color *c, uint8_t extra)
{
	double X, Y, Z;

	assert(c != NULL);
	assert(c->type == COLOR_XYZ);
	
	X = c->XYZ.X;
	Y = c->XYZ.Y;
	Z = c->XYZ.Z;

	c->LinearRGB.R = X * (641589.0/197960.0)      + Y * (-608687.0/395920.0)    + Z * (-49353.0/98980.0);
	c->LinearRGB.G = X * (-42591639.0/43944050.0) + Y * (82435961.0/43944050.0) + Z * (1826061.0/43944050.0);
	c->LinearRGB.B = X * (49353.0/887015.0)       + Y * (-180961.0/887015.0)    + Z * (49353.0/46685.0);
	c->type = COLOR_LINEAR_RGB;
}

static void color_XYZ_to_xyY(struct color *c, uint8_t extra)
{
	double X, Y, div;

	assert(c != NULL);
	assert(c->type == COLOR_XYZ);
	
	X = c->XYZ.X;
	Y = c->XYZ.Y;

	div = X + Y + c->XYZ.Z;
	
	c->xyY.Y = Y;
	c->type = COLOR_XYY;

	if(fabs(div) > 0.0)
	{
		c->xyY.x = X / div;
		c->xyY.y = Y / div;

		return;
	}

	c->xyY.x = X;
	c->xyY.y = Y;
}

static void color_XYZ_to_Lab(struct color *c, uint8_t extra)
{
	double X, Y, Z;

	assert(c != NULL);
	assert(c->type == COLOR_XYZ);

	X = c->XYZ.X;
	Y = c->XYZ.Y;
	Z = c->XYZ.Z;

	X = X > 3377268.0/401223439.0 ? pow(X * COLOR_REF_Xr, 1.0/3.0) : X * (13835291.0/1688634.0) + (4.0/29.0);
	Y = Y > 216.0/24389.0 ? pow(Y, 1.0/3.0) : Y * (841.0/108.0) + (4.0/29.0);
	Z = Z > 3869316.0/401223439.0 ? pow(Z * COLOR_REF_Zr, 1.0/3.0) : Z * (13835291.0/1934658.0) + (4.0/29.0);

	c->Lab.L = Y * 116.0 - 16.0;
	c->Lab.a = (X - Y) * 500.0;
	c->Lab.b = (Y - Z) * 200.0;

	c->type = COLOR_LAB;
}

static void color_XYZ_to_Luv(struct color *c, uint8_t extra)
{
	double X, Y, div, L;

	assert(c != NULL);
	assert(c->type == COLOR_XYZ);
	
	X = c->XYZ.X;
	Y = c->XYZ.Y;

	div = X + Y * 15.0 + c->XYZ.Z * 3.0;
	L = Y > 216.0/24389.0 ? pow(Y, 1.0 / 3.0) * 116.0 - 16.0 : Y * (24389.0/27.0);
	
	if(fabs(div) > 0.0)
	{
		div = 1.0 / div;
		X *= div;
		Y *= div;
	}
	
	c->Luv.L = L;
	c->Luv.u = (X * 52.0 - COLOR_REF_U13) * L;
	c->Luv.v = (Y * 117.0 - COLOR_REF_V13) * L;
	c->type = COLOR_LUV;
}

static void color_xyY_extract(double *dst, struct color const *src)
{
	assert(dst != NULL);
	assert(src != NULL);
	assert(src->type == COLOR_XYY);

	dst[0] = src->xyY.x;
	dst[1] = src->xyY.y;
	dst[2] = src->xyY.Y;
}

static void color_xyY_to_XYZ(struct color *c, uint8_t extra)
{
	double x, y, Y;

	assert(c != NULL);
	assert(c->type == COLOR_XYY);
	
	x = c->xyY.x;
	y = c->xyY.y;
	Y = c->xyY.Y;
	
	c->type = COLOR_XYZ;

	if(fabs(y) > 0.0)
	{
		double mul = Y / y;
		
		c->XYZ.X = x * mul;
		c->XYZ.Y = Y;
		c->XYZ.Z = (1.0 - x - y) * mul;

		return;
	}

	c->XYZ.X = 0.0;
	c->XYZ.Y = 0.0;
	c->XYZ.Z = 0.0;
}

static void color_Lab_extract(double *dst, struct color const *src)
{
	assert(dst != NULL);
	assert(src != NULL);
	assert(src->type == COLOR_LAB);

	dst[0] = src->Lab.L;
	dst[1] = src->Lab.a;
	dst[2] = src->Lab.b;
}

static void color_Lab_to_LinearRGB(struct color *c, uint8_t extra)
{
	double X, Y, Z;

	assert(c != NULL);
	assert(c->type == COLOR_LAB);

	// Lab -> normalized XYZ (X,Y,Z are all in 0...1)
	
	Y = c->Lab.L * (1.0/116.0) + 16.0/116.0;
	X = c->Lab.a * (1.0/500.0) + Y;
	Z = c->Lab.b * (-1.0/200.0) + Y;

	X = X > 6.0/29.0 ? X * X * X : X * (108.0/841.0) - 432.0/24389.0;
	Y = c->Lab.L > 8.0 ? Y * Y * Y : c->Lab.L * (27.0/24389.0);
	Z = Z > 6.0/29.0 ? Z * Z * Z : Z * (108.0/841.0) - 432.0/24389.0;

	// normalized XYZ -> linear sRGB

	c->LinearRGB.R = X * (1219569.0/395920.0)     + Y * (-608687.0/395920.0)    + Z * (-107481.0/197960.0);
	c->LinearRGB.G = X * (-80960619.0/87888100.0) + Y * (82435961.0/43944050.0) + Z * (3976797.0/87888100.0);
	c->LinearRGB.B = X * (93813.0/1774030.0)      + Y * (-180961.0/887015.0)    + Z * (107481.0/93370.0);
	c->type = COLOR_LINEAR_RGB;
}

static void color_Lab_to_XYZ(struct color *c, uint8_t extra)
{
	double L, X, Y, Z;

	assert(c != NULL);
	assert(c->type == COLOR_LAB);
	
	L = c->Lab.L;
	
	Y = L * (1.0/116.0) + 16.0/116.0;
	X = c->Lab.a * (1.0/500.0) + Y;
	Z = c->Lab.b * (-1.0/200.0) + Y;

	c->XYZ.X = X > 6.0/29.0 ? X * X * X * COLOR_REF_X : X * (1688634.0/13835291.0) - 6754536.0/401223439.0;
	c->XYZ.Y = L > 8.0 ? Y * Y * Y : L * (27.0/24389.0);
	c->XYZ.Z = Z > 6.0/29.0 ? Z * Z * Z * COLOR_REF_Z : Z * (1934658.0/13835291.0) - 7738632.0/401223439.0;

	c->type = COLOR_XYZ;
}

static void color_Lab_to_LCHab(struct color *c, uint8_t extra)
{
	double L, a, b;

	assert(c != NULL);
	assert(c->type == COLOR_LAB);
	
	L = c->Lab.L;
	a = c->Lab.a;
	b = c->Lab.b;

	c->LCHab.L = L;
	c->LCHab.C = sqrt(a * a + b * b);
	c->LCHab.h = atan2(b, a);
	c->type = COLOR_LCHAB;
}

static void color_LCHab_extract(double *dst, struct color const *src)
{
	assert(dst != NULL);
	assert(src != NULL);
	assert(src->type == COLOR_LCHAB);

	dst[0] = src->LCHab.L;
	dst[1] = src->LCHab.C;
	dst[2] = src->LCHab.h;
}

static void color_LCHab_to_Lab(struct color *c, uint8_t extra)
{
	double L, C, h;

	assert(c != NULL);
	assert(c->type == COLOR_LCHAB);
	
	L = c->LCHab.L;
	C = c->LCHab.C;
	h = c->LCHab.h;

	c->Lab.L = L;
	c->Lab.a = cos(h) * C;
	c->Lab.b = sin(h) * C;
	c->type = COLOR_LAB;
}

static void color_Luv_extract(double *dst, struct color const *src)
{
	assert(dst != NULL);
	assert(src != NULL);
	assert(src->type == COLOR_LUV);

	dst[0] = src->Luv.L;
	dst[1] = src->Luv.u;
	dst[2] = src->Luv.v;
}

static void color_Luv_to_XYZ(struct color *c, uint8_t extra)
{
	double L, u, v, y, a, b, cc, x, z;

	assert(c != NULL);
	assert(c->type == COLOR_LUV);

	L = c->Luv.L;
	u = c->Luv.u;
	v = c->Luv.v;

	if(L > 8.0)
	{
		y = L * (1.0 / 116.0) + 16.0 / 116.0;
		y = y * y * y;
	}
	else
	{
		y = L * (27.0 / 24389.0);
	}

	a = L / (L * COLOR_REF_U13 + u) * (52.0 / 3.0) - 1.0 / 3.0;
	b = 5.0 * y;
	cc = (L / (L * COLOR_REF_V13 + v) * 39.0 - 5.0) * y;

	x = (cc + b) / (a + 1.0 / 3.0);
	z = x * a - b;
	
	c->XYZ.X = x;
	c->XYZ.Y = y;
	c->XYZ.Z = z;
	c->type = COLOR_XYZ;
}

static void color_Luv_to_LCHuv(struct color *c, uint8_t extra)
{
	assert(c != NULL);
	assert(c->type == COLOR_LUV);

	c->type = COLOR_LAB;
	color_Lab_to_LCHab(c, extra);
	c->type = COLOR_LCHUV;
}

static void color_LCHuv_extract(double *dst, struct color const *src)
{
	assert(dst != NULL);
	assert(src != NULL);
	assert(src->type == COLOR_LCHUV);

	dst[0] = src->LCHuv.L;
	dst[1] = src->LCHuv.C;
	dst[2] = src->LCHuv.h;
}

static void color_LCHuv_to_Luv(struct color *c, uint8_t extra)
{
	assert(c != NULL);
	assert(c->type == COLOR_LCHUV);

	c->type = COLOR_LCHAB;
	color_LCHab_to_Lab(c, extra);
	c->type = COLOR_LUV;
}

static void color_LCHuv_to_LSHuv(struct color *c, uint8_t extra)
{
	double L, C, h;

	assert(c != NULL);
	assert(c->type == COLOR_LCHUV);
	
	L = c->LCHuv.L;
	C = c->LCHuv.C;
	h = c->LCHuv.h;

	c->LSHuv.L = L;
	c->LSHuv.S = C / L;
	c->LSHuv.h = h;
	c->type = COLOR_LSHUV;
}

static void color_LSHuv_extract(double *dst, struct color const *src)
{
	assert(dst != NULL);
	assert(src != NULL);
	assert(src->type == COLOR_LSHUV);

	dst[0] = src->LSHuv.L;
	dst[1] = src->LSHuv.S;
	dst[2] = src->LSHuv.h;
}

static void color_LSHuv_to_LCHuv(struct color *c, uint8_t extra)
{
	double L, S, h;

	assert(c != NULL);
	assert(c->type == COLOR_LSHUV);
	
	L = c->LSHuv.L;
	S = c->LSHuv.S;
	h = c->LSHuv.h;

	c->LCHuv.L = L;
	c->LCHuv.C = S * L;
	c->LCHuv.h = h;
	c->type = COLOR_LCHUV;
}

typedef void (*conversion_func)(struct color*, uint8_t);

static struct color_descriptor
{
	char const *name;
	void (*extract)(double*,struct color const*);
	conversion_func conversions[COLOR_DUMMY_END - 1];
	uint8_t proxy_conversions[COLOR_DUMMY_END - 1];
} const g_descriptors[] =
{
	{
		// RGB8
		"RGB8",
		color_RGB8_extract,
		{
			NULL, // RGB8
			color_RGB8_to_RGB, // RGB
			color_RGB8_to_LinearRGB // Linear RGB
		},
		{
			COLOR_NONE, // RGB8
			COLOR_NONE, // RGB
			COLOR_NONE, // Linear RGB
			COLOR_RGB, // HSL
			COLOR_RGB, // HSV
			COLOR_RGB, // YUV
			COLOR_RGB, // YCbCr
			COLOR_RGB, // YDbDr
			COLOR_RGB, // YIQ
			COLOR_LINEAR_RGB, // XYZ
			COLOR_LINEAR_RGB, // xyY
			COLOR_LINEAR_RGB, // Lab
			COLOR_LINEAR_RGB, // Luv
			COLOR_LINEAR_RGB, // LCHab
			COLOR_LINEAR_RGB, // LCHuv
			COLOR_LINEAR_RGB // LSHuv
		}
	},
	{
		// RGB
		"RGB",
		color_RGB_extract,
		{
			color_RGB_to_RGB8, // RGB8
			NULL, // RGB
			color_RGB_to_LinearRGB, // Linear RGB
			color_RGB_to_HSL, // HSL
			color_RGB_to_HSV, // HSV
			color_RGB_to_YUV, // YUV
			NULL, // YCbCr
			color_RGB_to_YDbDr, // YDbDr
			color_RGB_to_YIQ
		},
		{
			COLOR_NONE, // RGB8
			COLOR_NONE, // RGB
			COLOR_NONE, // Linear RGB
			COLOR_NONE, // HSL
			COLOR_NONE, // HSV
			COLOR_NONE, // YUV
			COLOR_YUV, // YCbCr
			COLOR_NONE, // YDbDr
			COLOR_NONE, // YIQ
			COLOR_LINEAR_RGB, // XYZ
			COLOR_LINEAR_RGB, // xyY
			COLOR_LINEAR_RGB, // Lab
			COLOR_LINEAR_RGB, // Luv
			COLOR_LINEAR_RGB, // LCHab
			COLOR_LINEAR_RGB, // LCHuv
			COLOR_LINEAR_RGB // LSHuv
		}
	},
	{
		// Linear RGB
		"Linear RGB",
		color_LinearRGB_extract,
		{
			color_LinearRGB_to_RGB8, // RGB8
			color_LinearRGB_to_RGB, // RGB
			NULL, // Linear RGB
			NULL, // HSL
			NULL, // HSV
			NULL, // YUV
			NULL, // YCbCr
			NULL, // YDbDr
			NULL, // YIQ
			color_LinearRGB_to_XYZ, // XYZ
			NULL, // xyY
			color_LinearRGB_to_Lab, // Lab
		},
		{
			COLOR_NONE, // RGB8
			COLOR_NONE, // RGB
			COLOR_NONE, // Linear RGB
			COLOR_RGB, // HSL
			COLOR_RGB, // HSV
			COLOR_RGB, // YUV
			COLOR_RGB, // YCbCr
			COLOR_RGB, // YDbDr
			COLOR_RGB, // YIQ
			COLOR_NONE, // XYZ
			COLOR_XYZ, // xyY
			COLOR_NONE, // Lab
			COLOR_XYZ, // Luv
			COLOR_LAB, // LCHab
			COLOR_XYZ, // LCHuv
			COLOR_XYZ // LSHuv
		}
	},
	{
		// HSL
		"HSL",
		color_HSL_extract,
		{
			NULL, // RGB8
			color_HSL_to_RGB // RGB
		},
		{
			COLOR_RGB, // RGB8
			COLOR_NONE, // RGB
			COLOR_RGB, // Linear RGB
			COLOR_NONE, // HSL
			COLOR_RGB, // HSV
			COLOR_RGB, // YUV
			COLOR_RGB, // YCbCr
			COLOR_RGB, // YDbDr
			COLOR_RGB, // YIQ
			COLOR_RGB, // XYZ
			COLOR_RGB, // xyY
			COLOR_RGB, // Lab
			COLOR_RGB, // Luv
			COLOR_RGB, // LCHab
			COLOR_RGB, // LCHuv
			COLOR_RGB // LSHuv
		}
	},
	{
		// HSV
		"HSV",
		color_HSV_extract,
		{
			NULL, // RGB8
			color_HSV_to_RGB // RGB
		},
		{
			COLOR_RGB, // RGB8
			COLOR_NONE, // RGB
			COLOR_RGB, // Linear RGB
			COLOR_RGB, // HSL
			COLOR_NONE, // HSV
			COLOR_RGB, // YUV
			COLOR_RGB, // YCbCr
			COLOR_RGB, // YDbDr
			COLOR_RGB, // YIQ
			COLOR_RGB, // XYZ
			COLOR_RGB, // xyY
			COLOR_RGB, // Lab
			COLOR_RGB, // Luv
			COLOR_RGB, // LCHab
			COLOR_RGB, // LCHuv
			COLOR_RGB // LSHuv
		}
	},
	{
		// YUV
		"YUV",
		color_YUV_extract,
		{
			NULL, // RGB8
			color_YUV_to_RGB, // RGB
			NULL, // Linear RGB
			NULL, // HSL
			NULL, // HSV
			color_YUV_to_YUV, // YUV
			color_YUV_to_YCbCr // YCbCr
		},
		{
			COLOR_RGB, // RGB8
			COLOR_NONE, // RGB
			COLOR_RGB, // Linear RGB
			COLOR_RGB, // HSL
			COLOR_RGB, // HSV
			COLOR_NONE, // YUV
			COLOR_NONE, // YCbCr
			COLOR_RGB, // YDbDr
			COLOR_RGB, // YIQ
			COLOR_RGB, // XYZ
			COLOR_RGB, // xyY
			COLOR_RGB, // Lab
			COLOR_RGB, // Luv
			COLOR_RGB, // LCHab
			COLOR_RGB, // LCHuv
			COLOR_RGB // LSHuv
		}
	},
	{
		// YCbCr
		"YCbCr",
		color_YCbCr_extract,
		{
			NULL, // RGB8
			NULL, // RGB
			NULL, // Linear RGB
			NULL, // HSL
			NULL, // HSV
			color_YCbCr_to_YUV, // YUV
			color_YCbCr_to_YCbCr, // YCbCr
		},
		{
			COLOR_YUV, // RGB8
			COLOR_YUV, // RGB
			COLOR_YUV, // Linear RGB
			COLOR_YUV, // HSL
			COLOR_YUV, // HSV
			COLOR_NONE, // YUV
			COLOR_NONE, // YCbCr
			COLOR_YUV, // YDbDr
			COLOR_YUV, // YIQ
			COLOR_YUV, // XYZ
			COLOR_YUV, // xyY
			COLOR_YUV, // Lab
			COLOR_YUV, // Luv
			COLOR_YUV, // LCHab
			COLOR_YUV, // LCHuv
			COLOR_YUV // LSHuv
		}
	},
	{
		// YDbDr
		"YDbDr",
		color_YDbDr_extract,
		{
			NULL, // RGB8
			color_YDbDr_to_RGB, // RGB
			NULL, // Linear RGB
			NULL, // HSL
			NULL, // HSV
			NULL, // YUV
			NULL, // YCbCr
			NULL, // YDbDr
			color_YDbDr_to_YIQ
		},
		{
			COLOR_RGB, // RGB8
			COLOR_NONE, // RGB
			COLOR_RGB, // Linear RGB
			COLOR_RGB, // HSL
			COLOR_RGB, // HSV
			COLOR_RGB, // YUV
			COLOR_RGB, // YCbCr
			COLOR_NONE, // YDbDr
			COLOR_NONE, // YIQ
			COLOR_RGB, // XYZ
			COLOR_RGB, // xyY
			COLOR_RGB, // Lab
			COLOR_RGB, // Luv
			COLOR_RGB, // LCHab
			COLOR_RGB, // LCHuv
			COLOR_RGB // LSHuv
		}
	},
	{
		// YIQ
		"YIQ",
		color_YIQ_extract,
		{
			NULL, // RGB8
			color_YIQ_to_RGB, // RGB
			NULL, // Linear RGB
			NULL, // HSL
			NULL, // HSV
			NULL, // YUV
			NULL, // YCbCr
			color_YIQ_to_YDbDr, // YDbDr
		},
		{
			COLOR_RGB, // RGB8
			COLOR_NONE, // RGB
			COLOR_RGB, // Linear RGB
			COLOR_RGB, // HSL
			COLOR_RGB, // HSV
			COLOR_RGB, // YUV
			COLOR_RGB, // YCbCr
			COLOR_NONE, // YDbDr
			COLOR_NONE, // YIQ
			COLOR_RGB, // XYZ
			COLOR_RGB, // xyY
			COLOR_RGB, // Lab
			COLOR_RGB, // Luv
			COLOR_RGB, // LCHab
			COLOR_RGB, // LCHuv
			COLOR_RGB // LSHuv
		}
	},
	{
		// XYZ
		"XYZ",
		color_XYZ_extract,
		{
			NULL, // RGB8
			NULL, // RGB
			color_XYZ_to_LinearRGB, // Linear RGB
			NULL, // HSL
			NULL, // HSV
			NULL, // YUV
			NULL, // YCbCr
			NULL, // YDbDr
			NULL, // YIQ
			NULL, // XYZ
			color_XYZ_to_xyY, // xyY
			color_XYZ_to_Lab, // Lab
			color_XYZ_to_Luv, // Luv
		},
		{
			COLOR_LINEAR_RGB, // RGB8
			COLOR_LINEAR_RGB, // RGB
			COLOR_NONE, // Linear RGB
			COLOR_LINEAR_RGB, // HSL
			COLOR_LINEAR_RGB, // HSV
			COLOR_LINEAR_RGB, // YUV
			COLOR_LINEAR_RGB, // YCbCr
			COLOR_LINEAR_RGB, // YDbDr
			COLOR_LINEAR_RGB, // YIQ
			COLOR_NONE, // XYZ
			COLOR_NONE, // xyY
			COLOR_NONE, // Lab
			COLOR_NONE, // Luv
			COLOR_LAB, // LCHab
			COLOR_LUV, // LCHuv
			COLOR_LUV // LSHuv
		}
	},
	{
		// xyY
		"xyY",
		color_xyY_extract,
		{
			NULL, // RGB8
			NULL, // RGB
			NULL, // Linear RGB
			NULL, // HSL
			NULL, // HSV
			NULL, // YUV
			NULL, // YCbCr
			NULL, // YDbDr
			NULL, // YIQ
			color_xyY_to_XYZ, // XYZ
		},
		{
			COLOR_XYZ, // RGB8
			COLOR_XYZ, // RGB
			COLOR_XYZ, // Linear RGB
			COLOR_XYZ, // HSL
			COLOR_XYZ, // HSV
			COLOR_XYZ, // YUV
			COLOR_XYZ, // YCbCr
			COLOR_XYZ, // YDbDr
			COLOR_XYZ, // YIQ
			COLOR_NONE, // XYZ
			COLOR_NONE, // xyY
			COLOR_XYZ, // Lab
			COLOR_XYZ, // Luv
			COLOR_XYZ, // LCHab
			COLOR_XYZ, // LCHuv
			COLOR_XYZ // LSHuv
		}
	},
	{
		// Lab
		"Lab",
		color_Lab_extract,
		{
			NULL, // RGB8
			NULL, // RGB
			color_Lab_to_LinearRGB, // Linear RGB
			NULL, // HSL
			NULL, // HSV
			NULL, // YUV
			NULL, // YCbCr
			NULL, // YDbDr
			NULL, // YIQ
			color_Lab_to_XYZ, // XYZ
			NULL, // xyY
			NULL, // Lab
			NULL, // Luv
			color_Lab_to_LCHab // LCHab
		},
		{
			COLOR_LINEAR_RGB, // RGB8
			COLOR_LINEAR_RGB, // RGB
			COLOR_NONE, // Linear RGB
			COLOR_LINEAR_RGB, // HSL
			COLOR_LINEAR_RGB, // HSV
			COLOR_LINEAR_RGB, // YUV
			COLOR_LINEAR_RGB, // YCbCr
			COLOR_LINEAR_RGB, // YDbDr
			COLOR_LINEAR_RGB, // YIQ
			COLOR_NONE, // XYZ
			COLOR_XYZ, // xyY
			COLOR_NONE, // Lab
			COLOR_XYZ, // Luv
			COLOR_NONE, // LCHab
			COLOR_XYZ, // LCHuv
			COLOR_XYZ // LSHuv
		}
	},
	{
		// Luv
		"Luv",
		color_Luv_extract,
		{
			NULL, // RGB8
			NULL, // RGB
			NULL, // Linear RGB
			NULL, // HSL
			NULL, // HSV
			NULL, // YUV
			NULL, // YCbCr
			NULL, // YDbDr
			NULL, // YIQ
			color_Luv_to_XYZ, // XYZ
			NULL, // xyY
			NULL, // Lab
			NULL, // Luv
			NULL, // LCHab
			color_Luv_to_LCHuv // LCHuv
		},
		{
			COLOR_XYZ, // RGB8
			COLOR_XYZ, // RGB
			COLOR_XYZ, // Linear RGB
			COLOR_XYZ, // HSL
			COLOR_XYZ, // HSV
			COLOR_XYZ, // YUV
			COLOR_XYZ, // YCbCr
			COLOR_XYZ, // YDbDr
			COLOR_XYZ, // YIQ
			COLOR_NONE, // XYZ
			COLOR_XYZ, // xyY
			COLOR_XYZ, // Lab
			COLOR_NONE, // Luv
			COLOR_XYZ, // LCHab
			COLOR_NONE, // LCHuv
			COLOR_LCHUV // LSHuv
		}
	},
	{
		// LCHab
		"LCHab",
		color_LCHab_extract,
		{
			NULL, // RGB8
			NULL, // RGB
			NULL, // Linear RGB
			NULL, // HSL
			NULL, // HSV
			NULL, // YUV
			NULL, // YCbCr
			NULL, // YDbDr
			NULL, // YIQ
			NULL, // XYZ
			NULL, // xyY
			color_LCHab_to_Lab // Lab
		},
		{
			COLOR_LAB, // RGB8
			COLOR_LAB, // RGB
			COLOR_LAB, // Linear RGB
			COLOR_LAB, // HSL
			COLOR_LAB, // HSV
			COLOR_LAB, // YUV
			COLOR_LAB, // YCbCr
			COLOR_LAB, // YDbDr
			COLOR_LAB, // YIQ
			COLOR_LAB, // XYZ
			COLOR_LAB, // xyY
			COLOR_NONE, // Lab
			COLOR_LAB, // Luv
			COLOR_NONE, // LCHab
			COLOR_LAB, // LCHuv
			COLOR_LAB // LSHuv
		}
	},
	{
		// LCHuv
		"LCHuv",
		color_LCHuv_extract,
		{
			NULL, // RGB8
			NULL, // RGB
			NULL, // Linear RGB
			NULL, // HSL
			NULL, // HSV
			NULL, // YUV
			NULL, // YCbCr
			NULL, // YDbDr
			NULL, // YIQ
			NULL, // XYZ
			NULL, // xyY
			NULL, // Lab
			color_LCHuv_to_Luv, // Luv
			NULL, // LCHab
			NULL, // LCHuv
			color_LCHuv_to_LSHuv // LSHuv
		},
		{
			COLOR_LUV, // RGB8
			COLOR_LUV, // RGB
			COLOR_LUV, // Linear RGB
			COLOR_LUV, // HSL
			COLOR_LUV, // HSV
			COLOR_LUV, // YUV
			COLOR_LUV, // YCbCr
			COLOR_LUV, // YDbDr
			COLOR_LUV, // YIQ
			COLOR_LUV, // XYZ
			COLOR_LUV, // xyY
			COLOR_LUV, // Lab
			COLOR_NONE, // Luv
			COLOR_LUV, // LCHab
			COLOR_NONE, // LCHuv
			COLOR_NONE // LSHuv
		}
	},
	{
		// LSHuv
		"LSHuv",
		color_LSHuv_extract,
		{
			NULL, // RGB8
			NULL, // RGB
			NULL, // Linear RGB
			NULL, // HSL
			NULL, // HSV
			NULL, // YUV
			NULL, // YCbCr
			NULL, // YDbDr
			NULL, // YIQ
			NULL, // XYZ
			NULL, // xyY
			NULL, // Lab
			NULL, // Luv
			NULL, // LCHab
			color_LSHuv_to_LCHuv, // LCHuv
		},
		{
			COLOR_LCHUV, // RGB8
			COLOR_LCHUV, // RGB
			COLOR_LCHUV, // Linear RGB
			COLOR_LCHUV, // HSL
			COLOR_LCHUV, // HSV
			COLOR_LCHUV, // YUV
			COLOR_LCHUV, // YCbCr
			COLOR_LCHUV, // YDbDr
			COLOR_LCHUV, // YIQ
			COLOR_LCHUV, // XYZ
			COLOR_LCHUV, // xyY
			COLOR_LCHUV, // Lab
			COLOR_LCHUV, // Luv
			COLOR_LCHUV, // LCHab
			COLOR_NONE, // LCHuv
			COLOR_NONE // LSHuv
		}
	}
};

void COLOR_CALL color_convert(struct color *c, enum color_type new_type, uint8_t new_extra)
{
	struct color_descriptor const *desc;

	assert(c != NULL);
	assert(new_type > COLOR_NONE);
	assert(new_type < COLOR_DUMMY_END);

	while(c->type != new_type || c->extra != new_extra)
	{
		conversion_func func;
		enum color_type tmp_type;

		assert(c->type > COLOR_NONE);
		assert(c->type < COLOR_DUMMY_END);

		desc = &g_descriptors[c->type - 1];
		func = desc->conversions[new_type - 1];
		tmp_type = new_type;

		if(!func)
		{
			tmp_type = (enum color_type)desc->proxy_conversions[new_type - 1];

			assert(tmp_type > COLOR_NONE);
			assert(tmp_type < COLOR_DUMMY_END);

			func = desc->conversions[tmp_type - 1];
			assert(func != NULL);
		}

		func(c, new_extra);

		assert(c->type == tmp_type);
	}

	assert(c->type > COLOR_NONE);
	assert(c->type < COLOR_DUMMY_END);
}

COLOR_EXPORT char const* COLOR_CALL color_name(enum color_type type)
{
	assert(type > COLOR_NONE);
	assert(type < COLOR_DUMMY_END);
	return g_descriptors[type - 1].name;
}

COLOR_EXPORT void COLOR_CALL color_extract_components(double *dst, struct color const *src)
{
	assert(dst != NULL);
	assert(src != NULL);
	assert(src->type > COLOR_NONE);
	assert(src->type < COLOR_DUMMY_END);

	g_descriptors[src->type - 1].extract(dst, src);
}
