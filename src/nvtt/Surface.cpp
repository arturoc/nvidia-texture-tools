// Copyright (c) 2009-2011 Ignacio Castano <castano@gmail.com>
// Copyright (c) 2007-2009 NVIDIA Corporation -- Ignacio Castano <icastano@nvidia.com>
// 
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following
// conditions:
// 
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

#include "Surface.h"

#include "nvmath/Vector.inl"
#include "nvmath/Matrix.inl"
#include "nvmath/Color.h"
#include "nvmath/Half.h"

#include "nvimage/Filter.h"
#include "nvimage/ImageIO.h"
#include "nvimage/NormalMap.h"
#include "nvimage/BlockDXT.h"
#include "nvimage/ColorBlock.h"
#include "nvimage/PixelFormat.h"
#include "nvimage/ErrorMetric.h"

#include <float.h>

using namespace nv;
using namespace nvtt;

namespace
{
    // 1 -> 1, 2 -> 2, 3 -> 2, 4 -> 4, 5 -> 4, ...
    static uint previousPowerOfTwo(const uint v)
    {
        return nextPowerOfTwo(v + 1) / 2;
    }

    static uint nearestPowerOfTwo(const uint v)
    {
        const uint np2 = nextPowerOfTwo(v);
        const uint pp2 = previousPowerOfTwo(v);

        if (np2 - v <= v - pp2)
        {
            return np2;
        }
        else
        {
            return pp2;
        }
    }

    static int blockSize(Format format)
    {
        if (format == Format_DXT1 || format == Format_DXT1a || format == Format_DXT1n) {
            return 8;
        }
        else if (format == Format_DXT3) {
            return 16;
        }
        else if (format == Format_DXT5 || format == Format_DXT5n) {
            return 16;
        }
        else if (format == Format_BC4) {
            return 8;
        }
        else if (format == Format_BC5) {
            return 16;
        }
        else if (format == Format_CTX1) {
            return 8;
        }
        else if (format == Format_BC6) {
            return 16;
        }
        else if (format == Format_BC7) {
            return 16;
        }
        return 0;
    }

    /*static int translateMask(int input) {
        if (input > 0) return 1 << input;
        return ~input;
    }*/
}

uint nv::countMipmaps(uint w)
{
    uint mipmap = 0;

    while (w != 1) {
        w = max(1U, w / 2);
        mipmap++;
    }

    return mipmap + 1;
}

uint nv::countMipmaps(uint w, uint h, uint d)
{
    uint mipmap = 0;

    while (w != 1 || h != 1 || d != 1) {
        w = max(1U, w / 2);
        h = max(1U, h / 2);
        d = max(1U, d / 2);
        mipmap++;
    }

    return mipmap + 1;
}

uint nv::computeImageSize(uint w, uint h, uint d, uint bitCount, uint pitchAlignmentInBytes, Format format)
{
    if (format == Format_RGBA) {
        return d * h * computeBytePitch(w, bitCount, pitchAlignmentInBytes);
    }
    else {
        return ((w + 3) / 4) * ((h + 3) / 4) * blockSize(format) * d;
    }
}

void nv::getTargetExtent(int * width, int * height, int * depth, int maxExtent, RoundMode roundMode, TextureType textureType) {
    nvDebugCheck(width != NULL && *width > 0);
    nvDebugCheck(height != NULL && *height > 0);
    nvDebugCheck(depth != NULL && *depth > 0);

    int w = *width;
    int h = *height;
    int d = *depth;

    if (roundMode != RoundMode_None && maxExtent > 0)
    {
        // rounded max extent should never be higher than original max extent.
        maxExtent = previousPowerOfTwo(maxExtent);
    }

    // Scale extents without changing aspect ratio.
    int m = max(max(w, h), d);
    if (maxExtent > 0 && m > maxExtent)
    {
        w = max((w * maxExtent) / m, 1);
        h = max((h * maxExtent) / m, 1);
        d = max((d * maxExtent) / m, 1);
    }

    if (textureType == TextureType_2D)
    {
        d = 1;
    }
    else if (textureType == TextureType_Cube)
    {
        w = h = (w + h) / 2;
        d = 1;
    }

    // Round to power of two.
    if (roundMode == RoundMode_ToNextPowerOfTwo)
    {
        w = nextPowerOfTwo(w);
        h = nextPowerOfTwo(h);
        d = nextPowerOfTwo(d);
    }
    else if (roundMode == RoundMode_ToNearestPowerOfTwo)
    {
        w = nearestPowerOfTwo(w);
        h = nearestPowerOfTwo(h);
        d = nearestPowerOfTwo(d);
    }
    else if (roundMode == RoundMode_ToPreviousPowerOfTwo)
    {
        w = previousPowerOfTwo(w);
        h = previousPowerOfTwo(h);
        d = previousPowerOfTwo(d);
    }

    *width = w;
    *height = h;
    *depth = d;
}



Surface::Surface() : m(new Surface::Private())
{
    m->addRef();
}

Surface::Surface(const Surface & tex) : m(tex.m)
{
    if (m != NULL) m->addRef();
}

Surface::~Surface()
{
    if (m != NULL) m->release();
    m = NULL;
}

void Surface::operator=(const Surface & tex)
{
    if (tex.m != NULL) tex.m->addRef();
    if (m != NULL) m->release();
    m = tex.m;
}

void Surface::detach()
{
    if (m->refCount() > 1)
    {
        m->release();
        m = new Surface::Private(*m);
        m->addRef();
        nvDebugCheck(m->refCount() == 1);
    }
}

void Surface::setWrapMode(WrapMode wrapMode)
{
    if (m->wrapMode != wrapMode)
    {
        detach();
        m->wrapMode = wrapMode;
    }
}

void Surface::setAlphaMode(AlphaMode alphaMode)
{
    if (m->alphaMode != alphaMode)
    {
        detach();
        m->alphaMode = alphaMode;
    }
}

void Surface::setNormalMap(bool isNormalMap)
{
    if (m->isNormalMap != isNormalMap)
    {
        detach();
        m->isNormalMap = isNormalMap;
    }
}

bool Surface::isNull() const
{
    return m->image == NULL;
}

int Surface::width() const
{
    if (m->image != NULL) return m->image->width();
    return 0;
}

int Surface::height() const
{
    if (m->image != NULL) return m->image->height();
    return 0;
}

int Surface::depth() const
{
    if (m->image != NULL) return m->image->depth();
    return 0;
}

WrapMode Surface::wrapMode() const
{
    return m->wrapMode;
}

AlphaMode Surface::alphaMode() const
{
    return m->alphaMode;
}

bool Surface::isNormalMap() const
{
    return m->isNormalMap;
}

TextureType Surface::type() const
{
    return m->type;
}

int Surface::countMipmaps() const
{
    if (m->image == NULL) return 0;
    return ::countMipmaps(m->image->width(), m->image->height(), 1);
}

float Surface::alphaTestCoverage(float alphaRef/*= 0.5*/) const
{
    if (m->image == NULL) return 0.0f;

    return m->image->alphaTestCoverage(alphaRef, 3);
}

float Surface::average(int channel, int alpha_channel/*= -1*/, float gamma /*= 2.2f*/) const
{
    if (m->image == NULL) return 0.0f;

    const uint count = m->image->width() * m->image->height();

    float sum = 0.0f;
    const float * c = m->image->channel(channel);

    float denom;

    if (alpha_channel == -1) {
        for (uint i = 0; i < count; i++) {
            sum += powf(c[i], gamma);
        }

        denom = float(count);
    }
    else {
        float alpha_sum = 0.0f;
        const float * a = m->image->channel(alpha_channel);
        
        for (uint i = 0; i < count; i++) {
            sum += powf(c[i], gamma) * a[i];
            alpha_sum += a[i];
        }

        denom = alpha_sum;
    }

    // Avoid division by zero.
    if (denom == 0.0f) return 0.0f;

    return sum / denom;
}

const float * Surface::data() const
{
    return m->image->channel(0);
}

void Surface::histogram(int channel, float rangeMin, float rangeMax, int binCount, int * binPtr) const
{
    // We assume it's clear in case we want to accumulate multiple histograms.
    //memset(bins, 0, sizeof(int)*count);

    if (m->image == NULL) return;

    const float * c = m->image->channel(channel);

    float scale = float(binCount) / rangeMax;
    float bias = - scale * rangeMin;

    const uint count = m->image->pixelCount();
    for (uint i = 0; i < count; i++) {
        float f = c[i] * scale + bias;
        int idx = ifloor(f);
        if (idx < 0) idx = 0;
        if (idx > binCount-1) idx = binCount-1;
        binPtr[idx]++;
    }
}

void Surface::range(int channel, float * rangeMin, float * rangeMax) const
{
    Vector2 range(FLT_MAX, -FLT_MAX);

    FloatImage * img = m->image;

    if (m->image != NULL)
    {
        float * c = img->channel(channel);

        const uint count = img->pixelCount();
        for (uint p = 0; p < count; p++) {
            float f = c[p];
            if (f < range.x) range.x = f;
            if (f > range.y) range.y = f;
        }
    }

    *rangeMin = range.x;
    *rangeMax = range.y;
}


bool Surface::load(const char * fileName, bool * hasAlpha/*= NULL*/)
{
    AutoPtr<FloatImage> img(ImageIO::loadFloat(fileName));
    if (img == NULL) {
        return false;
    }

    detach();

    if (hasAlpha != NULL) {
        *hasAlpha = (img->componentCount() == 4);
    }

    // @@ Have loadFloat allocate the image with the desired number of channels.
    img->resizeChannelCount(4);

    delete m->image;
    m->image = img.release();

    return true;
}

bool Surface::save(const char * fileName) const
{
    if (m->image != NULL)
    {
        return ImageIO::saveFloat(fileName, m->image, 0, 4);
    }

    return false;
}

#if 0 //NV_OS_WIN32

#include <windows.h>
#undef min
#undef max

static int filter(unsigned int code, struct _EXCEPTION_POINTERS *ep) {
   if (code == EXCEPTION_ACCESS_VIOLATION) {
      return EXCEPTION_EXECUTE_HANDLER;
   }
   else {
      return EXCEPTION_CONTINUE_SEARCH;
   };
}

#define TRY __try
    
#define CATCH __except (filter(GetExceptionCode(), GetExceptionInformation()))
#else
#define TRY
#define CATCH
#endif


bool Surface::setImage(nvtt::InputFormat format, int w, int h, int d, const void * data)
{
    detach();

    if (m->image == NULL) {
        m->image = new FloatImage();
    }
    m->image->allocate(4, w, h, d);
    m->type = (d == 1) ? TextureType_2D : TextureType_3D;

    const int count = m->image->pixelCount();

    float * rdst = m->image->channel(0);
    float * gdst = m->image->channel(1);
    float * bdst = m->image->channel(2);
    float * adst = m->image->channel(3);

    if (format == InputFormat_BGRA_8UB)
    {
        const Color32 * src = (const Color32 *)data;

        TRY {
            for (int i = 0; i < count; i++)
            {
                rdst[i] = float(src[i].r) / 255.0f;
                gdst[i] = float(src[i].g) / 255.0f;
                bdst[i] = float(src[i].b) / 255.0f;
                adst[i] = float(src[i].a) / 255.0f;
            }
        }
        CATCH {
            return false;
        }
    }
    else if (format == InputFormat_RGBA_16F)
    {
        const uint16 * src = (const uint16 *)data;

        TRY {
            for (int i = 0; i < count; i++)
            {
                ((uint32 *)rdst)[i] = half_to_float(src[4*i+0]);
                ((uint32 *)gdst)[i] = half_to_float(src[4*i+1]);
                ((uint32 *)bdst)[i] = half_to_float(src[4*i+2]);
                ((uint32 *)adst)[i] = half_to_float(src[4*i+3]);
            }
        }
        CATCH {
            return false;
        }
    }
    else if (format == InputFormat_RGBA_32F)
    {
        const float * src = (const float *)data;

        TRY {
            for (int i = 0; i < count; i++)
            {
                rdst[i] = src[4 * i + 0];
                gdst[i] = src[4 * i + 1];
                bdst[i] = src[4 * i + 2];
                adst[i] = src[4 * i + 3];
            }
        }
        CATCH {
            return false;
        }
    }

    return true;
}

bool Surface::setImage(InputFormat format, int w, int h, int d, const void * r, const void * g, const void * b, const void * a)
{
    detach();

    if (m->image == NULL) {
        m->image = new FloatImage();
    }
    m->image->allocate(4, w, h, d);
    m->type = (d == 1) ? TextureType_2D : TextureType_3D;

    const int count = m->image->pixelCount();

    float * rdst = m->image->channel(0);
    float * gdst = m->image->channel(1);
    float * bdst = m->image->channel(2);
    float * adst = m->image->channel(3);

    if (format == InputFormat_BGRA_8UB)
    {
        const uint8 * rsrc = (const uint8 *)r;
        const uint8 * gsrc = (const uint8 *)g;
        const uint8 * bsrc = (const uint8 *)b;
        const uint8 * asrc = (const uint8 *)a;

        try {
            for (int i = 0; i < count; i++) rdst[i] = float(rsrc[i]) / 255.0f;
            for (int i = 0; i < count; i++) gdst[i] = float(gsrc[i]) / 255.0f;
            for (int i = 0; i < count; i++) bdst[i] = float(bsrc[i]) / 255.0f;
            for (int i = 0; i < count; i++) adst[i] = float(asrc[i]) / 255.0f;
        }
        catch(...) {
            return false;
        }
    }
    else if (format == InputFormat_RGBA_16F)
    {
        const uint16 * rsrc = (const uint16 *)r;
        const uint16 * gsrc = (const uint16 *)g;
        const uint16 * bsrc = (const uint16 *)b;
        const uint16 * asrc = (const uint16 *)a;

        try {
            for (int i = 0; i < count; i++) ((uint32 *)rdst)[i] = half_to_float(rsrc[i]);
            for (int i = 0; i < count; i++) ((uint32 *)gdst)[i] = half_to_float(gsrc[i]);
            for (int i = 0; i < count; i++) ((uint32 *)bdst)[i] = half_to_float(bsrc[i]);
            for (int i = 0; i < count; i++) ((uint32 *)adst)[i] = half_to_float(asrc[i]);
        }
        catch(...) {
            return false;
        }
    }
    else if (format == InputFormat_RGBA_32F)
    {
        const float * rsrc = (const float *)r;
        const float * gsrc = (const float *)g;
        const float * bsrc = (const float *)b;
        const float * asrc = (const float *)a;

        try {
            memcpy(rdst, rsrc, count * sizeof(float));
            memcpy(gdst, gsrc, count * sizeof(float));
            memcpy(bdst, bsrc, count * sizeof(float));
            memcpy(adst, asrc, count * sizeof(float));
        }
        catch(...) {
            return false;
        }
    }

    return true;
}

// @@ Add support for compressed 3D textures.
bool Surface::setImage2D(Format format, Decoder decoder, int w, int h, const void * data)
{
    if (format != nvtt::Format_BC1 && format != nvtt::Format_BC2 && format != nvtt::Format_BC3 && format != nvtt::Format_BC4 && format != nvtt::Format_BC5)
    {
        return false;
    }

    detach();

    if (m->image == NULL) {
        m->image = new FloatImage();
    }
    m->image->allocate(4, w, h, 1);
    m->type = TextureType_2D;

    const int bw = (w + 3) / 4;
    const int bh = (h + 3) / 4;

    const uint bs = blockSize(format);

    const uint8 * ptr = (const uint8 *)data;

    try {
        for (int y = 0; y < bh; y++)
        {
            for (int x = 0; x < bw; x++)
            {
                ColorBlock colors;

		if (format == nvtt::Format_BC1)
		{
		    const BlockDXT1 * block = (const BlockDXT1 *)ptr;

		    if (decoder == Decoder_D3D10) {
			    block->decodeBlock(&colors, false);
		    }
		    else if (decoder == Decoder_D3D9) {
			    block->decodeBlock(&colors, false);
		    }
		    else if (decoder == Decoder_NV5x) {
			    block->decodeBlockNV5x(&colors);
		    }
		}
		else if (format == nvtt::Format_BC2)
		{
		    const BlockDXT3 * block = (const BlockDXT3 *)ptr;

		    if (decoder == Decoder_D3D10) {
			    block->decodeBlock(&colors, false);
		    }
		    else if (decoder == Decoder_D3D9) {
			    block->decodeBlock(&colors, false);
		    }
		    else if (decoder == Decoder_NV5x) {
			    block->decodeBlockNV5x(&colors);
		    }
		}
		else if (format == nvtt::Format_BC3)
		{
		    const BlockDXT5 * block = (const BlockDXT5 *)ptr;

		    if (decoder == Decoder_D3D10) {
			    block->decodeBlock(&colors, false);
		    }
		    else if (decoder == Decoder_D3D9) {
			    block->decodeBlock(&colors, false);
		    }
		    else if (decoder == Decoder_NV5x) {
			    block->decodeBlockNV5x(&colors);
		    }
		}
		else if (format == nvtt::Format_BC4)
		{
            const BlockATI1 * block = (const BlockATI1 *)ptr;
            block->decodeBlock(&colors, decoder == Decoder_D3D9);
        }
        else if (format == nvtt::Format_BC5)
        {
            const BlockATI2 * block = (const BlockATI2 *)ptr;
            block->decodeBlock(&colors, decoder == Decoder_D3D9);
        }

		for (int yy = 0; yy < 4; yy++)
		{
		    for (int xx = 0; xx < 4; xx++)
		    {
			Color32 c = colors.color(xx, yy);

			if (x * 4 + xx < w && y * 4 + yy < h)
			{
			    m->image->pixel(0, x*4 + xx, y*4 + yy, 0) = float(c.r) * 1.0f/255.0f;
			    m->image->pixel(1, x*4 + xx, y*4 + yy, 0) = float(c.g) * 1.0f/255.0f;
			    m->image->pixel(2, x*4 + xx, y*4 + yy, 0) = float(c.b) * 1.0f/255.0f;
			    m->image->pixel(3, x*4 + xx, y*4 + yy, 0) = float(c.a) * 1.0f/255.0f;
			}
		    }
		}

		ptr += bs;
	    }
	}
    }
    catch(...) {
        return false;
    }

    return true;
}


static void getDefaultFilterWidthAndParams(int filter, float * filterWidth, float params[2])
{
    if (filter == ResizeFilter_Box) {
        *filterWidth = 0.5f;
    }
    else if (filter == ResizeFilter_Triangle) {
        *filterWidth = 1.0f;
    }
    else if (filter == ResizeFilter_Kaiser)
    {
        *filterWidth = 3.0f;
        params[0] = 4.0f;
        params[1] = 1.0f;
    }
    else //if (filter == ResizeFilter_Mitchell)
    {
        *filterWidth = 2.0f;
        params[0] = 1.0f / 3.0f;
        params[1] = 1.0f / 3.0f;
    }
}

void Surface::resize(int w, int h, int d, ResizeFilter filter)
{
    float filterWidth;
    float params[2];
    getDefaultFilterWidthAndParams(filter, &filterWidth, params);

    resize(w, h, d, filter, filterWidth, params);
}

void Surface::resize(int w, int h, int d, ResizeFilter filter, float filterWidth, const float * params)
{
    if (isNull() || (w == width() && h == height() && d == depth())) {
        return;
    }

    detach();

    FloatImage * img = m->image;

    FloatImage::WrapMode wrapMode = (FloatImage::WrapMode)m->wrapMode;

    if (m->alphaMode == AlphaMode_Transparency)
    {
        if (filter == ResizeFilter_Box)
        {
            BoxFilter filter(filterWidth);
            img = img->resize(filter, w, h, d, wrapMode, 3);
        }
        else if (filter == ResizeFilter_Triangle)
        {
            TriangleFilter filter(filterWidth);
            img = img->resize(filter, w, h, d, wrapMode, 3);
        }
        else if (filter == ResizeFilter_Kaiser)
        {
            KaiserFilter filter(filterWidth);
            if (params != NULL) filter.setParameters(params[0], params[1]);
            img = img->resize(filter, w, h, d, wrapMode, 3);
        }
        else //if (filter == ResizeFilter_Mitchell)
        {
            nvDebugCheck(filter == ResizeFilter_Mitchell);
            MitchellFilter filter;
            if (params != NULL) filter.setParameters(params[0], params[1]);
            img = img->resize(filter, w, h, d, wrapMode, 3);
        }
    }
    else
    {
        if (filter == ResizeFilter_Box)
        {
            BoxFilter filter(filterWidth);
            img = img->resize(filter, w, h, d, wrapMode);
        }
        else if (filter == ResizeFilter_Triangle)
        {
            TriangleFilter filter(filterWidth);
            img = img->resize(filter, w, h, d, wrapMode);
        }
        else if (filter == ResizeFilter_Kaiser)
        {
            KaiserFilter filter(filterWidth);
            if (params != NULL) filter.setParameters(params[0], params[1]);
            img = img->resize(filter, w, h, d, wrapMode);
        }
        else //if (filter == ResizeFilter_Mitchell)
        {
            nvDebugCheck(filter == ResizeFilter_Mitchell);
            MitchellFilter filter;
            if (params != NULL) filter.setParameters(params[0], params[1]);
            img = img->resize(filter, w, h, d, wrapMode);
        }
    }

    delete m->image;
    m->image = img;
}

void Surface::resize(int maxExtent, RoundMode roundMode, ResizeFilter filter)
{
    float filterWidth;
    float params[2];
    getDefaultFilterWidthAndParams(filter, &filterWidth, params);

    resize(maxExtent, roundMode, filter, filterWidth, params);
}

void Surface::resize(int maxExtent, RoundMode roundMode, ResizeFilter filter, float filterWidth, const float * params)
{
    if (isNull()) return;

    int w = m->image->width();
    int h = m->image->height();
    int d = m->image->depth();

    getTargetExtent(&w, &h, &d, maxExtent, roundMode, m->type);

    resize(w, h, d, filter, filterWidth, params);
}

bool Surface::buildNextMipmap(MipmapFilter filter)
{
    float filterWidth;
    float params[2];
    getDefaultFilterWidthAndParams(filter, &filterWidth, params);

    return buildNextMipmap(filter, filterWidth, params);
}

bool Surface::buildNextMipmap(MipmapFilter filter, float filterWidth, const float * params)
{
    if (isNull() || (width() == 1 && height() == 1 && depth() == 1)) {
        return false;
    }

    detach();

    FloatImage * img = m->image;

    FloatImage::WrapMode wrapMode = (FloatImage::WrapMode)m->wrapMode;

    if (m->alphaMode == AlphaMode_Transparency)
    {
        if (filter == MipmapFilter_Box)
        {
            BoxFilter filter(filterWidth);
            img = img->downSample(filter, wrapMode, 3);
        }
        else if (filter == MipmapFilter_Triangle)
        {
            TriangleFilter filter(filterWidth);
            img = img->downSample(filter, wrapMode, 3);
        }
        else if (filter == MipmapFilter_Kaiser)
        {
            nvDebugCheck(filter == MipmapFilter_Kaiser);
            KaiserFilter filter(filterWidth);
            if (params != NULL) filter.setParameters(params[0], params[1]);
            img = img->downSample(filter, wrapMode, 3);
        }
    }
    else
    {
        if (filter == MipmapFilter_Box)
        {
            if (filterWidth == 0.5f && img->depth() == 1) {
                img = img->fastDownSample();
            }
            else {
                BoxFilter filter(filterWidth);
                img = img->downSample(filter, wrapMode);
            }
        }
        else if (filter == MipmapFilter_Triangle)
        {
            TriangleFilter filter(filterWidth);
            img = img->downSample(filter, wrapMode);
        }
        else //if (filter == MipmapFilter_Kaiser)
        {
            nvDebugCheck(filter == MipmapFilter_Kaiser);
            KaiserFilter filter(filterWidth);
            if (params != NULL) filter.setParameters(params[0], params[1]);
            img = img->downSample(filter, wrapMode);
        }
    }

    delete m->image;
    m->image = img;

    return true;
}

void Surface::canvasSize(int w, int h, int d)
{
    nvDebugCheck(w > 0 && h > 0 && d > 0);

    if (isNull() || (w == width() && h == height() && d == depth())) {
        return;
    }

    detach();

    FloatImage * img = m->image;

    FloatImage * new_img = new FloatImage;
    new_img->allocate(4, w, h, d);
    new_img->clear();

    w = min(uint(w), img->width());
    h = min(uint(h), img->height());
    d = min(uint(d), img->depth());

    for (int z = 0; z < d; z++) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                new_img->pixel(0, x, y, z) = img->pixel(0, x, y, z);
                new_img->pixel(1, x, y, z) = img->pixel(1, x, y, z);
                new_img->pixel(2, x, y, z) = img->pixel(2, x, y, z);
                new_img->pixel(3, x, y, z) = img->pixel(3, x, y, z);
            }
        }
    }

    delete m->image;
    m->image = new_img;
    m->type = (d == 1) ? TextureType_2D : TextureType_3D;
}


// Color transforms.
void Surface::toLinear(float gamma)
{
    if (isNull()) return;
    if (equal(gamma, 1.0f)) return;

    detach();

    m->image->toLinear(0, 3, gamma);
}

void Surface::toGamma(float gamma)
{
    if (isNull()) return;
    if (equal(gamma, 1.0f)) return;

    detach();

    m->image->toGamma(0, 3, gamma);
}

void Surface::toLinear(int channel, float gamma)
{
    if (isNull()) return;
    if (equal(gamma, 1.0f)) return;

    detach();

    m->image->toLinear(channel, 1, gamma);
}

void Surface::toGamma(int channel, float gamma)
{
    if (isNull()) return;
    if (equal(gamma, 1.0f)) return;

    detach();

    m->image->toGamma(channel, 1, gamma);
}



static float toSrgb(float f) {
    if (isNan(f))               f = 0.0f;
    else if (f <= 0.0f)         f = 0.0f;
    else if (f <= 0.0031308f)   f = 12.92f * f;
    else if (f <= 1.0f)         f = (powf(f, 0.41666f) * 1.055f) - 0.055f;
    else                        f = 1.0f;
    return f;
}

void Surface::toSrgb()
{
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;

    const uint count = img->pixelCount();
    for (uint c = 0; c < 3; c++) {
        float * channel = img->channel(c);
        for (uint i = 0; i < count; i++) {
            channel[i] = ::toSrgb(channel[i]);
        }
    }
}

static float fromSrgb(float f) {
    if (f < 0.0f)           f = 0.0f;
    else if (f < 0.04045f)  f = f / 12.92f;
    else if (f <= 1.0f)     f = powf((f + 0.055f) / 1.055f, 2.4f);
    else                    f = 1.0f;
    return f;
}

void Surface::toLinearFromSrgb()
{
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;

    const uint count = img->pixelCount();
    for (uint c = 0; c < 3; c++) {
        float * channel = img->channel(c);
        for (uint i = 0; i < count; i++) {
            channel[i] = ::fromSrgb(channel[i]);
        }
    }
}

static float toXenonSrgb(float f) {
    if (f < 0)                  f = 0;
    else if (f < (1.0f/16.0f))  f = 4.0f * f;
    else if (f < (1.0f/8.0f))   f = 0.25f  + 2.0f * (f - 0.0625f);
    else if (f < 0.5f)          f = 0.375f + 1.0f * (f - 0.125f);
    else if (f < 1.0f)          f = 0.75f  + 0.5f * (f - 0.50f);
    else                        f = 1.0f;
    return f;
}

void Surface::toXenonSrgb()
{
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;

    const uint count = img->pixelCount();
    for (uint c = 0; c < 3; c++) {
        float * channel = img->channel(c);
        for (uint i = 0; i < count; i++) {
            channel[i] = ::toXenonSrgb(channel[i]);
        }
    }
}


void Surface::transform(const float w0[4], const float w1[4], const float w2[4], const float w3[4], const float offset[4])
{
    if (isNull()) return;

    detach();

    Matrix xform(
        Vector4(w0[0], w0[1], w0[2], w0[3]),
        Vector4(w1[0], w1[1], w1[2], w1[3]),
        Vector4(w2[0], w2[1], w2[2], w2[3]),
        Vector4(w3[0], w3[1], w3[2], w3[3]));

    Vector4 voffset(offset[0], offset[1], offset[2], offset[3]);

    m->image->transform(0, xform, voffset);
}

void Surface::swizzle(int r, int g, int b, int a)
{
    if (isNull()) return;
    if (r == 0 && g == 1 && b == 2 && a == 3) return;

    detach();

    m->image->swizzle(0, r, g, b, a);
}

// color * scale + bias
void Surface::scaleBias(int channel, float scale, float bias)
{
    if (isNull()) return;
    if (equal(scale, 1.0f) && equal(bias, 0.0f)) return;

    detach();

    m->image->scaleBias(channel, 1, scale, bias);
}

void Surface::clamp(int channel, float low, float high)
{
    if (isNull()) return;

    detach();

    m->image->clamp(channel, 1, low, high);
}

void Surface::packNormal()
{
    if (isNull()) return;

    detach();

    m->image->scaleBias(0, 3, 0.5f, 0.5f);
}

void Surface::expandNormal()
{
    if (isNull()) return;

    detach();

    m->image->scaleBias(0, 3, 2.0f, -1.0f);
}

// Create a Toksvig map for this normal map.
// http://blog.selfshadow.com/2011/07/22/specular-showdown/
// @@ Assumes this is a normal map expanded in the [-1, 1] range.
Surface Surface::createToksvigMap(float power) const
{
    if (isNull()) return Surface();

    // @@ TODO

    return Surface();
}

// @@ Should I add support for LEAN maps? That requires 5 terms, which would have to be encoded in two textures.
// There's nothing stopping us from having 5 channels in a surface, and then, let the user swizzle them as they wish.
// CLEAN maps are probably more practical, though.
// http://www.cs.umbc.edu/~olano/papers/lean/
// http://gaim.umbc.edu/2011/07/24/shiny-and-clean/
// http://gaim.umbc.edu/2011/07/26/on-error/
NVTT_API Surface Surface::createCleanMap() const
{
    if (isNull()) return Surface();

    // @@ TODO

    return Surface();
}


void Surface::blend(float red, float green, float blue, float alpha, float t)
{
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;
    float * r = img->channel(0);
    float * g = img->channel(1);
    float * b = img->channel(2);
    float * a = img->channel(3);

    const uint count = img->pixelCount();
    for (uint i = 0; i < count; i++)
    {
        r[i] = lerp(r[i], red, t);
        g[i] = lerp(g[i], green, t);
        b[i] = lerp(b[i], blue, t);
        a[i] = lerp(a[i], alpha, t);
    }
}

void Surface::premultiplyAlpha()
{
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;
    float * r = img->channel(0);
    float * g = img->channel(1);
    float * b = img->channel(2);
    float * a = img->channel(3);

    const uint count = img->pixelCount();
    for (uint i = 0; i < count; i++)
    {
        r[i] *= a[i];
        g[i] *= a[i];
        b[i] *= a[i];
    }
}


void Surface::toGreyScale(float redScale, float greenScale, float blueScale, float alphaScale)
{
    if (isNull()) return;

    detach();

    float sum = redScale + greenScale + blueScale + alphaScale;
    redScale /= sum;
    greenScale /= sum;
    blueScale /= sum;
    alphaScale /= sum;

    FloatImage * img = m->image;
    float * r = img->channel(0);
    float * g = img->channel(1);
    float * b = img->channel(2);
    float * a = img->channel(3);

    const uint count = img->pixelCount();
    for (uint i = 0; i < count; i++)
    {
        float grey = r[i] * redScale + g[i] * greenScale + b[i] * blueScale + a[i] * alphaScale;
        a[i] = b[i] = g[i] = r[i] = grey;
    }
}

// Draw colored border.
void Surface::setBorder(float r, float g, float b, float a)
{
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;
    const uint w = img->width();
    const uint h = img->height();
    const uint d = img->depth();

    for (uint z = 0; z < d; z++)
    {
        for (uint i = 0; i < w; i++)
        {
            img->pixel(0, i, 0, z) = r;
            img->pixel(1, i, 0, z) = g;
            img->pixel(2, i, 0, z) = b;
            img->pixel(3, i, 0, z) = a;

            img->pixel(0, i, h-1, z) = r;
            img->pixel(1, i, h-1, z) = g;
            img->pixel(2, i, h-1, z) = b;
            img->pixel(3, i, h-1, z) = a;
        }

        for (uint i = 0; i < h; i++)
        {
            img->pixel(0, 0, i, z) = r;
            img->pixel(1, 0, i, z) = g;
            img->pixel(2, 0, i, z) = b;
            img->pixel(3, 0, i, z) = a;

            img->pixel(0, w-1, i, z) = r;
            img->pixel(1, w-1, i, z) = g;
            img->pixel(2, w-1, i, z) = b;
            img->pixel(3, w-1, i, z) = a;
        }
    }
}

// Fill image with the given color.
void Surface::fill(float red, float green, float blue, float alpha)
{
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;
    float * r = img->channel(0);
    float * g = img->channel(1);
    float * b = img->channel(2);
    float * a = img->channel(3);

    const uint count = img->pixelCount();
    for (uint i = 0; i < count; i++)
    {
        r[i] = red;
        g[i] = green;
        b[i] = blue;
        a[i] = alpha;
    }
}


void Surface::scaleAlphaToCoverage(float coverage, float alphaRef/*= 0.5f*/)
{
    if (isNull()) return;

    detach();

    m->image->scaleAlphaToCoverage(coverage, alphaRef, 3);
}

/*bool Surface::normalizeRange(float * rangeMin, float * rangeMax)
{
    if (m->image == NULL) return false;

    range(0, rangeMin, rangeMax);

    if (*rangeMin == *rangeMax) {
        // Single color image.
        return false;
    }

    const float scale = 1.0f / (*rangeMax - *rangeMin);
    const float bias = *rangeMin * scale;

    if (range.x == 0.0f && range.y == 1.0f) {
        // Already normalized.
        return true;
    }

    detach();

    // Scale to range.
    img->scaleBias(0, 4, scale, bias);
    //img->clamp(0, 4, 0.0f, 1.0f);

    return true;
}*/

// Ideally you should compress/quantize the RGB and M portions independently.
// Once you have M quantized, you would compute the corresponding RGB and quantize that.
void Surface::toRGBM(float range/*= 1*/, float threshold/*= 0.25*/)
{
    if (isNull()) return;

    detach();

    threshold = ::clamp(threshold, 1e-6f, 1.0f);
    float irange = 1.0f / range;

    FloatImage * img = m->image;
    float * r = img->channel(0);
    float * g = img->channel(1);
    float * b = img->channel(2);
    float * a = img->channel(3);

    const uint count = img->pixelCount();
    for (uint i = 0; i < count; i++) {
        float R = nv::clamp(r[i], 0.0f, 1.0f);
        float G = nv::clamp(g[i], 0.0f, 1.0f);
        float B = nv::clamp(b[i], 0.0f, 1.0f);
#if 1
        float M = max(max(R, G), max(B, threshold));

        r[i] = R / M;
        g[i] = G / M;
        b[i] = B / M;
        a[i] = (M - threshold) / (1 - threshold);

#else

        // The optimal compressor theoretically produces the best results, but unfortunately introduces
        // severe interpolation errors!
        float bestM;
        float bestError = FLT_MAX;

        int minM = iround(min(R, G, B) * 255.0f);

        for (int m = minM; m < 256; m++) {
            float fm = float(m) / 255.0f;

            // Encode.
            int ir = iround(255.0f * nv::clamp(R / fm, 0.0f, 1.0f));
            int ig = iround(255.0f * nv::clamp(G / fm, 0.0f, 1.0f));
            int ib = iround(255.0f * nv::clamp(B / fm, 0.0f, 1.0f));

            // Decode.
            float fr = (float(ir) / 255.0f) * fm;
            float fg = (float(ig) / 255.0f) * fm;
            float fb = (float(ib) / 255.0f) * fm;

            // Measure error.
            float error = square(R-fr) + square(G-fg) + square(B-fb);

            if (error < bestError) {
                bestError = error;
                bestM = fm;
            }
        }

        M = bestM;
        r[i] = nv::clamp(R / M, 0.0f, 1.0f);
        g[i] = nv::clamp(G / M, 0.0f, 1.0f);
        b[i] = nv::clamp(B / M, 0.0f, 1.0f);
        a[i] = M;
#endif
    }
}

void Surface::fromRGBM(float range/*= 1*/)
{
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;
    float * r = img->channel(0);
    float * g = img->channel(1);
    float * b = img->channel(2);
    float * a = img->channel(3);

    const uint count = img->pixelCount();
    for (uint i = 0; i < count; i++) {
        float M = a[i] * range;

        r[i] *= M;
        g[i] *= M;
        b[i] *= M;
        a[i] = 1.0f;
    }
}


static Color32 toRgbe8(float r, float g, float b)
{
    Color32 c;
    float v = max(max(r, g), b);
    if (v < 1e-32) {
        c.r = c.g = c.b = c.a = 0;
    }
    else {
        int e;
        v = frexp(v, &e) * 256.0f / v;
        c.r = uint8(clamp(r * v, 0.0f, 255.0f));
        c.g = uint8(clamp(g * v, 0.0f, 255.0f));
        c.b = uint8(clamp(b * v, 0.0f, 255.0f));
        c.a = e + 128;
    }

    return c;
}


/*
  Alen Ladavac @ GDAlgorithms-list on Feb 7, 2007:
    One trick that we use to alleviate such problems is to use RGBE5.3 -
    i.e. have a fixed point exponent. Note that it is not enough to just
    shift the exponent up for 3 bits, but you actually have to convert
    each pixel in the RGBE8 texture by unpacking it to floats and then
    repacking it with a non-integer exponent, which gives different
    mantissas as well. Now your jumps in exponent are much smaller, thus
    the bands are not that noticeable. It is still not as good as FP16,
    but it is much better than RGBE8. I hope this explanation is
    understandable, if not I can fill in more details.

    Though there still are some bands, you can get an even better
    precision if you upload that same texture as RGBA16, because you'll
    get even more interpolation then, and it works good as a scalable
    option for people with more GPU RAM). Alternatively, when some of the
    future cards (hopefully, because I'm trying to lobby for that
    everywhere :) ), start returning more than 8 bits, your scenes will
    automatically look better even without using RGBA16.

  Jon Watte:
    The interpolation of 5.3 is the same as that of 8 bits, because it's a
    fixed point format.

    The reason using 5.3 helps, is that each bit of quantization in the
    interpolation only means 1/8th of a fully significant bit. The
    quantization still happens, it's just less visible. The trade-off is
    that you get less dynamic range.

  Alen Ladavac:
    True, but it is just a small part of the improvement. The greater part
    is that RGB values have to be calculated according to the fractional
    exponent. With integer exponent, the RGB values jump by a factor of 2
    when each bit changes in exponent, and 5.3 with correct adjustment of
    RGB lowers this jump to be about 1.09, which is much better. I may not
    be entirely correct on the numbers, which I'm pulling out from my
    memory now, but it's a rough estimate.
*/
/* Ward's version:
static Color32 toRgbe8(float r, float g, float b)
{
    Color32 c;
    float v = max(max(r, g), b);
    if (v < 1e-32) {
        c.r = c.g = c.b = c.a = 0;
    }
    else {
        int e;
        v = frexp(v, &e) * 256.0f / v;
        c.r = uint8(clamp(r * v, 0.0f, 255.0f));
        c.g = uint8(clamp(g * v, 0.0f, 255.0f));
        c.b = uint8(clamp(b * v, 0.0f, 255.0f));
        c.a = e + 128;
    }

    return c;
}
*/

// For R9G9B9E5, use toRGBE(9, 5), for Ward's RGBE, use toRGBE(8, 8)
// @@ Note that most Radiance HDR loaders use an exponent bias of 128 instead of 127! This implementation
// matches the OpenGL extension.
void Surface::toRGBE(int mantissaBits, int exponentBits)
{
    // According to the OpenGL extension:
    // http://www.opengl.org/registry/specs/EXT/texture_shared_exponent.txt
    //
    // Components red, green, and blue are first clamped (in the process,
    // mapping NaN to zero) so:
    //
    //     red_c   = max(0, min(sharedexp_max, red))
    //     green_c = max(0, min(sharedexp_max, green))
    //     blue_c  = max(0, min(sharedexp_max, blue))
    //
    // where sharedexp_max is (2^N-1)/2^N * 2^(Emax-B), N is the number
    // of mantissa bits per component, Emax is the maximum allowed biased
    // exponent value (careful: not necessarily 2^E-1 when E is the number of
    // exponent bits), bits, and B is the exponent bias.  For the RGB9_E5_EXT
    // format, N=9, Emax=31, and B=15.
    //
    // The largest clamped component, max_c, is determined:
    //
    //     max_c = max(red_c, green_c, blue_c)
    //
    // A preliminary shared exponent is computed:
    //
    //     exp_shared_p = max(-B-1, floor(log2(max_c))) + 1 + B
    //
    // A refined shared exponent is then computed as:
    //
    //     max_s   = floor(max_c   / 2^(exp_shared_p - B - N) + 0.5)
    //
    //                  { exp_shared_p,    0 <= max_s <  2^N
    //     exp_shared = {
    //                  { exp_shared_p+1,       max_s == 2^N
    //
    // These integers values in the range 0 to 2^N-1 are then computed:
    //
    //     red_s   = floor(red_c   / 2^(exp_shared - B - N) + 0.5)
    //     green_s = floor(green_c / 2^(exp_shared - B - N) + 0.5)
    //     blue_s  = floor(blue_c  / 2^(exp_shared - B - N) + 0.5)

    if (isNull()) return;

    detach();

    // mantissaBits = N
    // exponentBits = E
    // exponentMax = Emax
    // exponentBias = B
    // maxValue = sharedexp_max

    // max exponent: 5 -> 31, 8 -> 255
    const int exponentMax = (1 << exponentBits) - 1;

    // exponent bias: 5 -> 15, 8 -> 127
    const int exponentBias = (1 << (exponentBits - 1)) - 1;

    // Maximum representable value: 5 -> 63488, 8 -> HUGE
    const float maxValue = float(exponentMax) / float(exponentMax + 1) * float(1 << (exponentMax - exponentBias));


    FloatImage * img = m->image;
    float * r = img->channel(0);
    float * g = img->channel(1);
    float * b = img->channel(2);
    float * a = img->channel(3);

    const uint count = img->pixelCount();
    for (uint i = 0; i < count; i++) {
        // Clamp components:
        float R = ::clamp(r[i], 0.0f, maxValue);
        float G = ::clamp(g[i], 0.0f, maxValue);
        float B = ::clamp(b[i], 0.0f, maxValue);

        // Compute max:
        float M = max(R, G, B);

        // Preliminary exponent:
        int E = max(- exponentBias - 1, floatExponent(M)) + 1 + exponentBias;
        nvDebugCheck(E >= 0 && E < (1 << exponentBits));

        double denom = pow(2.0, double(E - exponentBias - mantissaBits));

        // Refine exponent:
        int m = iround(float(M / denom));
        nvDebugCheck(m <= (1 << mantissaBits));

        if (m == (1 << mantissaBits)) {
            denom *= 2;
            E += 1;
            nvDebugCheck(E < (1 << exponentBits));
        }

        R = floatRound(float(R / denom));
        G = floatRound(float(G / denom));
        B = floatRound(float(B / denom));

        nvDebugCheck(R >= 0 && R < (1 << mantissaBits));
        nvDebugCheck(G >= 0 && G < (1 << mantissaBits));
        nvDebugCheck(B >= 0 && B < (1 << mantissaBits));

        // Store as normalized float.
        r[i] = R / ((1 << mantissaBits) - 1);
        g[i] = G / ((1 << mantissaBits) - 1);
        b[i] = B / ((1 << mantissaBits) - 1);
        a[i] = float(E) / ((1 << exponentBits) - 1);
    }
}

void Surface::fromRGBE(int mantissaBits, int exponentBits)
{
    // According to the OpenGL extension:
    // http://www.opengl.org/registry/specs/EXT/texture_shared_exponent.txt
    //
    // The 1st, 2nd, 3rd, and 4th components are called
    // p_red, p_green, p_blue, and p_exp respectively and are treated as
    // unsigned integers.  These are then used to compute floating-point
    // RGB components (ignoring the "Conversion to floating-point" section
    // below in this case) as follows:
    //
    //   red   = p_red   * 2^(p_exp - B - N)
    //   green = p_green * 2^(p_exp - B - N)
    //   blue  = p_blue  * 2^(p_exp - B - N)
    //
    // where B is 15 (the exponent bias) and N is 9 (the number of mantissa
    // bits)."


    // int exponent = v.field.biasedexponent - RGB9E5_EXP_BIAS - RGB9E5_MANTISSA_BITS;
    // float scale = (float) pow(2, exponent);
    //
    // retval[0] = v.field.r * scale;
    // retval[1] = v.field.g * scale;
    // retval[2] = v.field.b * scale;


    if (isNull()) return;

    detach();

    // exponent bias: 5 -> 15, 8 -> 127
    const int exponentBias = (1 << (exponentBits - 1)) - 1;

    FloatImage * img = m->image;
    float * r = img->channel(0);
    float * g = img->channel(1);
    float * b = img->channel(2);
    float * a = img->channel(3);

    const uint count = img->pixelCount();
    for (uint i = 0; i < count; i++) {
        // Expand normalized float to to 9995
        int R = iround(r[i] * ((1 << mantissaBits) - 1));
        int G = iround(g[i] * ((1 << mantissaBits) - 1));
        int B = iround(b[i] * ((1 << mantissaBits) - 1));
        int E = iround(a[i] * ((1 << exponentBits) - 1));

        //float scale = ldexpf(1.0f, E - exponentBias - mantissaBits);
        float scale = powf(2, float(E - exponentBias - mantissaBits));

        r[i] = R * scale;
        g[i] = G * scale;
        b[i] = B * scale;
        a[i] = 1;
    }
}

// Y is in the [0, 1] range, while CoCg are in the [-1, 1] range.
void Surface::toYCoCg()
{
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;
    float * r = img->channel(0);
    float * g = img->channel(1);
    float * b = img->channel(2);
    float * a = img->channel(3);

    const uint count = img->pixelCount();
    for (uint i = 0; i < count; i++) {
        float R = r[i];
        float G = g[i];
        float B = b[i];

        float Y = (2*G + R + B) * 0.25f;
        float Co = (R - B);
        float Cg = (2*G - R - B) * 0.5f;

        r[i] = Co;
        g[i] = Cg;
        b[i] = 1.0f;
        a[i] = Y;
    }
}

// img.toYCoCg();
// img.blockScaleCoCg();
// img.scaleBias(0, 0.5, 0.5);
// img.scaleBias(1, 0.5, 0.5);

// @@ Add support for threshold.
// We could do something to prevent scale values from adjacent blocks from being too different to each other
// and minimize bilinear interpolation artifacts.
void Surface::blockScaleCoCg(int bits/*= 5*/, float threshold/*= 0.0*/)
{
    if (isNull() || depth() != 1) return;

    detach();

    FloatImage * img = m->image;
    const uint w = img->width();
    const uint h = img->height();
    const uint bw = max(1U, w/4);
    const uint bh = max(1U, h/4);

    for (uint bj = 0; bj < bh; bj++) {
        for (uint bi = 0; bi < bw; bi++) {

            // Compute per block scale.
            float m = 1.0f / 255.0f;
            for (uint j = 0; j < 4; j++) {
                const uint y = bj*4 + j;
                if (y >= h) continue;

                for (uint i = 0; i < 4; i++) {
                    const uint x = bi*4 + i;
                    if (x >= w) continue;

                    float Co = img->pixel(0, x, y, 0);
                    float Cg = img->pixel(1, x, y, 0);

                    m = max(m, fabsf(Co));
                    m = max(m, fabsf(Cg));
                }
            }

            float scale = PixelFormat::quantizeCeil(m, bits, 8);
            nvDebugCheck(scale >= m);

            // Store block scale in blue channel and scale CoCg.
            for (uint j = 0; j < 4; j++) {
                for (uint i = 0; i < 4; i++) {
                    uint x = min(bi*4 + i, w);
                    uint y = min(bj*4 + j, h);

                    float & Co = img->pixel(0, x, y, 0);
                    float & Cg = img->pixel(1, x, y, 0);

                    Co /= scale;
                    nvDebugCheck(fabsf(Co) <= 1.0f);

                    Cg /= scale;
                    nvDebugCheck(fabsf(Cg) <= 1.0f);

                    img->pixel(2, x, y, 0) = scale;
                }
            }
        }
    }
}

void Surface::fromYCoCg()
{
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;
    float * r = img->channel(0);
    float * g = img->channel(1);
    float * b = img->channel(2);
    float * a = img->channel(3);

    const uint count = img->pixelCount();
    for (uint i = 0; i < count; i++) {
        float Co = r[i];
        float Cg = g[i];
        float scale = b[i] * 0.5f;
        float Y = a[i];

        Co *= scale;
        Cg *= scale;

        float R = Y + Co - Cg;
        float G = Y + Cg;
        float B = Y - Co - Cg;

        r[i] = R;
        g[i] = G;
        b[i] = B;
        a[i] = 1.0f;
    }
}

void Surface::toLUVW(float range/*= 1.0f*/)
{
    if (isNull()) return;

    detach();

    float irange = 1.0f / range;

    FloatImage * img = m->image;
    float * r = img->channel(0);
    float * g = img->channel(1);
    float * b = img->channel(2);
    float * a = img->channel(3);

    const uint count = img->pixelCount();
    for (uint i = 0; i < count; i++) {
        float R = nv::clamp(r[i] * irange, 0.0f, 1.0f);
        float G = nv::clamp(g[i] * irange, 0.0f, 1.0f);
        float B = nv::clamp(b[i] * irange, 0.0f, 1.0f);

        float L = max(sqrtf(R*R + G*G + B*B), 1e-6f); // Avoid division by zero.

        r[i] = R / L;
        g[i] = G / L;
        b[i] = B / L;
        a[i] = L / sqrtf(3);
    }
}

void Surface::fromLUVW(float range/*= 1.0f*/)
{
    // Decompression is the same as in RGBM.
    fromRGBM(range * sqrtf(3));
}

void Surface::abs(int channel)
{
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;
    float * c = img->channel(channel);

    const uint count = img->pixelCount();
    for (uint i = 0; i < count; i++) {
        c[i] = fabsf(c[i]);
    }
}

void Surface::convolve(int channel, int kernelSize, float * kernelData)
{
    if (isNull()) return;

    detach();

    Kernel2 k(kernelSize, kernelData);
    m->image->convolve(k, channel, (FloatImage::WrapMode)m->wrapMode);
}

// Assumes input has already been scaled by exposure.
void Surface::toneMap(ToneMapper tm, float * parameters)
{
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;
    float * r = img->channel(0);
    float * g = img->channel(1);
    float * b = img->channel(2);
    const uint count = img->pixelCount();

    if (tm == ToneMapper_Linear) {
        // Clamp preserving the hue.
        for (uint i = 0; i < count; i++) {
            float m = max(r[i], g[i], b[i]);
            if (m > 1.0f) {
                r[i] *= 1.0f / m;
                g[i] *= 1.0f / m;
                b[i] *= 1.0f / m;
            }
        }
    }
    else if (tm == ToneMapper_Reindhart) {
        for (uint i = 0; i < count; i++) {
            r[i] /= r[i] + 1;
            g[i] /= g[i] + 1;
            b[i] /= b[i] + 1;
        }
    }
    else if (tm == ToneMapper_Halo) {
        for (uint i = 0; i < count; i++) {
            r[i] = 1 - exp2f(-r[i]);
            g[i] = 1 - exp2f(-g[i]);
            b[i] = 1 - exp2f(-b[i]);
        }
    }
    else if (tm == ToneMapper_Lightmap) {
        // @@ Goals:
        // Preserve hue.
        // Avoid clamping abrubtly.
        // Minimize color difference along most of the color range. [0, alpha)
        for (uint i = 0; i < count; i++) {
            float m = max(r[i], g[i], b[i]);
            if (m > 1.0f) {
                r[i] *= 1.0f / m;
                g[i] *= 1.0f / m;
                b[i] *= 1.0f / m;
            }
        }
    }
}

void Surface::toLogScale(int channel, float base) {
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;
    float * c = img->channel(channel);

    float scale = 1.0f / log2f(base);

    const uint count = img->pixelCount();
    for (uint i = 0; i < count; i++) {
        c[i] = log2f(c[i]) * scale;
    }
}

void Surface::fromLogScale(int channel, float base) {
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;
    float * c = img->channel(channel);

    float scale = log2f(base);

    const uint count = img->pixelCount();
    for (uint i = 0; i < count; i++) {
        c[i] = exp2f(c[i] * scale);
    }
}



/*
void Surface::blockLuminanceScale(float scale)
{
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;

    //float * r = img->channel(0);
    //float * g = img->channel(1);
    //float * b = img->channel(2);
    //float * a = img->channel(3);

    const uint w = img->width();
    const uint h = img->height();
    const uint bw = max(1U, w/4);
    const uint bh = max(1U, h/4);

    Vector3 L = normalize(Vector3(1, 1, 1));

    for (uint bj = 0; bj < bh; bj++) {
        for (uint bi = 0; bi < bw; bi++) {

            // Compute block centroid.
            Vector3 centroid(0.0f);
            int count = 0;
            for (uint j = 0; j < 4; j++) {
                const uint y = bj*4 + j;
                if (y >= h) continue;

                for (uint i = 0; i < 4; i++) {
                    const uint x = bi*4 + i;
                    if (x >= w) continue;

                    float r = img->pixel(x, y, 0);
                    float g = img->pixel(x, y, 1);
                    float b = img->pixel(x, y, 2);
                    Vector3 rgb(r, g, b);

                    centroid += rgb;
                    count++;
                }
            }

            centroid /= float(count);

            // Project to luminance plane.
            for (uint j = 0; j < 4; j++) {
                const uint y = bj*4 + j;
                if (y >= h) continue;

                for (uint i = 0; i < 4; i++) {
                    const uint x = bi*4 + i;
                    if (x >= w) continue;

                    float & r = img->pixel(x, y, 0);
                    float & g = img->pixel(x, y, 1);
                    float & b = img->pixel(x, y, 2);
                    Vector3 rgb(r, g, b);

                    Vector3 delta = rgb - centroid;

                    delta -= scale * dot(delta, L) * L;

                    r = centroid.x + delta.x;
                    g = centroid.y + delta.y;
                    b = centroid.z + delta.z;
                }
            }
        }
    }
}
*/

/*
void Surface::toJPEGLS()
{
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;
    float * r = img->channel(0);
    float * g = img->channel(1);
    float * b = img->channel(2);

    const uint count = img->width() * img->height();
    for (uint i = 0; i < count; i++) {
        float R = nv::clamp(r[i], 0.0f, 1.0f);
        float G = nv::clamp(g[i], 0.0f, 1.0f);
        float B = nv::clamp(b[i], 0.0f, 1.0f);

        r[i] = R-G;
        g[i] = G;
        b[i] = B-G;
    }
}

void Surface::fromJPEGLS()
{
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;
    float * r = img->channel(0);
    float * g = img->channel(1);
    float * b = img->channel(2);

    const uint count = img->width() * img->height();
    for (uint i = 0; i < count; i++) {
        float R = nv::clamp(r[i], -1.0f, 1.0f);
        float G = nv::clamp(g[i], 0.0f, 1.0f);
        float B = nv::clamp(b[i], -1.0f, 1.0f);

        r[i] = R+G;
        g[i] = G;
        b[i] = B+G;
    }
}
*/


// If dither is true, this uses Floyd-Steinberg dithering method.
void Surface::binarize(int channel, float threshold, bool dither)
{
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;

    if (!dither) {
        float * c = img->channel(channel);
        const uint count = img->pixelCount();
        for (uint i = 0; i < count; i++) {
            c[i] = float(c[i] > threshold);
        }
    }
    else {
        const uint w = img->width();
        const uint h = img->height();
        const uint d = img->depth();

        float * row0 = new float[(w+2)];
        float * row1 = new float[(w+2)];

        // @@ Extend Floyd-Steinberg dithering to 3D properly.
        for (uint z = 0; z < d; z++) {
            memset(row0, 0, sizeof(float)*(w+2));
            memset(row1, 0, sizeof(float)*(w+2));

            for (uint y = 0; y < h; y++) {
                for (uint x = 0; x < w; x++) {

                    float & f = img->pixel(channel, x, y, 0);

                    // Add error and quantize.
                    float qf = float(f + row0[1+x] > threshold);

                    // Compute new error:
                    float diff = f - qf;

                    // Store color.
                    f = qf;

                    // Propagate new error.
                    row0[1+x+1] += (7.0f / 16.0f) * diff;
                    row1[1+x-1] += (3.0f / 16.0f) * diff;
                    row1[1+x+0] += (5.0f / 16.0f) * diff;
                    row1[1+x+1] += (1.0f / 16.0f) * diff;
                }

                swap(row0, row1);
                memset(row1, 0, sizeof(float)*(w+2));
            }
        }

        delete [] row0;
        delete [] row1;
    }
}

// Uniform quantizer.
// Assumes input is in [0, 1] range. Output is in the [0, 1] range, but rounded to the middle of each bin.
// If exactEndPoints is true, [0, 1] are represented exactly, and the correponding bins are half the size, so quantization is not truly uniform.
// When dither is true, this uses Floyd-Steinberg dithering.
void Surface::quantize(int channel, int bits, bool exactEndPoints, bool dither)
{
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;

    float scale, offset;
    if (exactEndPoints) {
        scale = float((1 << bits) - 1);
        offset = 0.0f;
    }
    else {
        scale = float(1 << bits);
        offset = 0.5f;
    }

    if (!dither) {
        float * c = img->channel(channel);
        const uint count = img->pixelCount();
        for (uint i = 0; i < count; i++) {
            c[i] = floorf(c[i] * scale + offset) / scale;
        }
    }
    else {
        const uint w = img->width();
        const uint h = img->height();
        const uint d = img->depth();

        float * row0 = new float[(w+2)];
        float * row1 = new float[(w+2)];

        for (uint z = 0; z < d; z++) {
            memset(row0, 0, sizeof(float)*(w+2));
            memset(row1, 0, sizeof(float)*(w+2));

            for (uint y = 0; y < h; y++) {
                for (uint x = 0; x < w; x++) {

                    float & f = img->pixel(channel, x, y, 0);

                    // Add error and quantize.
                    float qf = floorf((f + row0[1+x]) * scale + offset) / scale;

                    // Compute new error:
                    float diff = f - qf;

                    // Store color.
                    f = qf;

                    // Propagate new error.
                    row0[1+x+1] += (7.0f / 16.0f) * diff;
                    row1[1+x-1] += (3.0f / 16.0f) * diff;
                    row1[1+x+0] += (5.0f / 16.0f) * diff;
                    row1[1+x+1] += (1.0f / 16.0f) * diff;
                }

                swap(row0, row1);
                memset(row1, 0, sizeof(float)*(w+2));
            }
        }

        delete [] row0;
        delete [] row1;
    }
}



// Set normal map options.
void Surface::toNormalMap(float sm, float medium, float big, float large)
{
    if (isNull()) return;

    detach();

    const Vector4 filterWeights(sm, medium, big, large);

    const FloatImage * img = m->image;
    m->image = nv::createNormalMap(img, (FloatImage::WrapMode)m->wrapMode, filterWeights);

#pragma NV_MESSAGE("TODO: Pack and expand normals explicitly?")
    m->image->packNormals(0);

    delete img;

    m->isNormalMap = true;
}

void Surface::normalizeNormalMap()
{
    if (isNull()) return;
    if (!m->isNormalMap) return;

    detach();

    nv::normalizeNormalMap(m->image);
}

void Surface::transformNormals(NormalTransform xform)
{
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;
    img->expandNormals(0);

    const uint count = img->pixelCount();
    for (uint i = 0; i < count; i++) {
        float & x = img->pixel(0, i);
        float & y = img->pixel(1, i);
        float & z = img->pixel(2, i);
        Vector3 n(x, y, z);

        n = normalizeSafe(n, Vector3(0.0f), 0.0f);

        if (xform == NormalTransform_Orthographic) {
            n.z = 0.0f;
        }
        else if (xform == NormalTransform_Stereographic) {
            n.x = n.x / (1 + n.z);
            n.y = n.y / (1 + n.z);
            n.z = 0.0f;
        }
        else if (xform == NormalTransform_Paraboloid) {
            float a = (n.x * n.x) + (n.y * n.y);
            float b = n.z;
            float c = -1.0f;
            float discriminant = b * b - 4.0f * a * c;
            float t = (-b + sqrtf(discriminant)) / (2.0f * a);
            n.x = n.x * t;
            n.y = n.y * t;
            n.z = 0.0f;
        }
        else if (xform == NormalTransform_Quartic) {
            // Use Newton's method to solve equation:
            // f(t) = 1 - zt - (x^2+y^2)t^2 + x^2y^2t^4 = 0
            // f'(t) = - z - 2(x^2+y^2)t + 4x^2y^2t^3

            // Initial approximation:
            float a = (n.x * n.x) + (n.y * n.y);
            float b = n.z;
            float c = -1.0f;
            float discriminant = b * b - 4.0f * a * c;
            float t = (-b + sqrtf(discriminant)) / (2.0f * a);

            float d = fabs(n.z * t - (1 - n.x*n.x*t*t) * (1 - n.y*n.y*t*t));

            while (d > 0.0001) {
                float ft = 1 - n.z * t - (n.x*n.x + n.y*n.y)*t*t + n.x*n.x*n.y*n.y*t*t*t*t;
                float fit = - n.z - 2*(n.x*n.x + n.y*n.y)*t + 4*n.x*n.x*n.y*n.y*t*t*t;
                t -= ft / fit;
                d = fabs(n.z * t - (1 - n.x*n.x*t*t) * (1 - n.y*n.y*t*t));
            };

            n.x = n.x * t;
            n.y = n.y * t;
            n.z = 0.0f;
        }
        /*else if (xform == NormalTransform_DualParaboloid) {

        }*/

        x = n.x;
        y = n.y;
        z = n.z;
    }

    img->packNormals(0);
}

void Surface::reconstructNormals(NormalTransform xform)
{
    if (isNull()) return;

    detach();

    FloatImage * img = m->image;
    img->expandNormals(0);

    const uint count = img->pixelCount();
    for (uint i = 0; i < count; i++) {
        float & x = img->pixel(0, i);
        float & y = img->pixel(1, i);
        float & z = img->pixel(2, i);
        Vector3 n(x, y, z);

        if (xform == NormalTransform_Orthographic) {
            n.z = sqrtf(1 - nv::clamp(n.x * n.x + n.y * n.y, 0.0f, 1.0f));
        }
        else if (xform == NormalTransform_Stereographic) {
            float denom = 2.0f / (1 + nv::clamp(n.x * n.x + n.y * n.y, 0.0f, 1.0f));
            n.x *= denom;
            n.y *= denom;
            n.z = denom - 1;
        }
        else if (xform == NormalTransform_Paraboloid) {
            n.x = n.x;
            n.y = n.y;
            n.z = 1.0f - nv::clamp(n.x * n.x + n.y * n.y, 0.0f, 1.0f);
            n = normalizeSafe(n, Vector3(0.0f), 0.0f);
        }
        else if (xform == NormalTransform_Quartic) {
            n.x = n.x;
            n.y = n.y;
            n.z = nv::clamp((1 - n.x * n.x) * (1 - n.y * n.y), 0.0f, 1.0f);
            n = normalizeSafe(n, Vector3(0.0f), 0.0f);
        }
        /*else if (xform == NormalTransform_DualParaboloid) {

        }*/

        x = n.x;
        y = n.y;
        z = n.z;
    }

    img->packNormals(0);
}

void Surface::toCleanNormalMap()
{
    if (isNull()) return;

    detach();

    m->image->expandNormals(0);

    const uint count = m->image->pixelCount();
    for (uint i = 0; i < count; i++) {
        float x = m->image->pixel(0, i);
        float y = m->image->pixel(1, i);

        m->image->pixel(2, i) = x*x + y*y;
    }

    m->image->packNormals(0);
}

// [-1,1] -> [ 0,1]
void Surface::packNormals() {
    if (isNull()) return;
    detach();
    m->image->packNormals(0);
}

// [ 0,1] -> [-1,1]
void Surface::expandNormals() {
    if (isNull()) return;
    detach();
    m->image->expandNormals(0);
}


void Surface::flipX()
{
    if (isNull()) return;

    detach();

    m->image->flipX();
}

void Surface::flipY()
{
    if (isNull()) return;

    detach();

    m->image->flipY();
}

void Surface::flipZ()
{
    if (isNull()) return;

    detach();

    m->image->flipZ();
}

bool Surface::copyChannel(const Surface & srcImage, int srcChannel)
{
    return copyChannel(srcImage, srcChannel, srcChannel);
}

bool Surface::copyChannel(const Surface & srcImage, int srcChannel, int dstChannel)
{
    if (srcChannel < 0 || srcChannel > 3 || dstChannel < 0 || dstChannel > 3) return false;

    FloatImage * dst = m->image;
    const FloatImage * src = srcImage.m->image;

    if (!sameLayout(dst, src)) {
        return false;
    }
    nvDebugCheck(dst->componentCount() == 4 && src->componentCount() == 4);

    detach();

    dst = m->image;

    memcpy(dst->channel(dstChannel), src->channel(srcChannel), dst->pixelCount()*sizeof(float));

    return true;
}

bool Surface::addChannel(const Surface & srcImage, int srcChannel, int dstChannel, float scale)
{
    if (srcChannel < 0 || srcChannel > 3 || dstChannel < 0 || dstChannel > 3) return false;

    FloatImage * dst = m->image;
    const FloatImage * src = srcImage.m->image;

    if (!sameLayout(dst, src)) {
        return false;
    }
    nvDebugCheck(dst->componentCount() == 4 && src->componentCount() == 4);

    detach();

    dst = m->image;

    const uint w = src->width();
    const uint h = src->height();

    float * d = dst->channel(dstChannel);
    const float * s = src->channel(srcChannel);

    const uint count = src->pixelCount();
    for (uint i = 0; i < count; i++) {
        d[i] += s[i] * scale;
    }

    return true;
}



float nvtt::rmsError(const Surface & reference, const Surface & image)
{
    return nv::rmsColorError(reference.m->image, image.m->image, reference.alphaMode() == nvtt::AlphaMode_Transparency);
}


float nvtt::rmsAlphaError(const Surface & reference, const Surface & image)
{
    return nv::rmsAlphaError(reference.m->image, image.m->image);
}


float nvtt::cieLabError(const Surface & reference, const Surface & image)
{
    return nv::cieLabError(reference.m->image, image.m->image);
}

float nvtt::angularError(const Surface & reference, const Surface & image)
{
    //return nv::averageAngularError(reference.m->image, image.m->image);
    return nv::rmsAngularError(reference.m->image, image.m->image);
}


Surface nvtt::diff(const Surface & reference, const Surface & image, float scale)
{
    const FloatImage * ref = reference.m->image;
    const FloatImage * img = image.m->image;

    if (!sameLayout(img, ref)) {
        return Surface();
    }

    nvDebugCheck(img->componentCount() == 4);
    nvDebugCheck(ref->componentCount() == 4);

    nvtt::Surface diffImage;
    FloatImage * diff = diffImage.m->image = new FloatImage;
    diff->allocate(4, img->width(), img->height(), img->depth());

    const uint count = img->pixelCount();
    for (uint i = 0; i < count; i++)
    {
        float r0 = img->pixel(0, i);
        float g0 = img->pixel(1, i);
        float b0 = img->pixel(2, i);
        //float a0 = img->pixel(3, i);
        float r1 = ref->pixel(0, i);
        float g1 = ref->pixel(1, i);
        float b1 = ref->pixel(2, i);
        float a1 = ref->pixel(3, i);

        float dr = r0 - r1;
        float dg = g0 - g1;
        float db = b0 - b1;
        //float da = a0 - a1;

        if (reference.alphaMode() == nvtt::AlphaMode_Transparency)
        {
            dr *= a1;
            dg *= a1;
            db *= a1;
        }

        diff->pixel(0, i) = dr * scale;
        diff->pixel(1, i) = dg * scale;
        diff->pixel(2, i) = db * scale;
        diff->pixel(3, i) = a1;
    }

    return diffImage;
}



