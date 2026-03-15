/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */


#include "Compositor.h"

#include <algorithm>
#include <cmath>
#include <stdio.h>
#include <string.h>

#include <Region.h>

#ifdef __SSE2__
#	include <emmintrin.h>
#endif

#include "RenderingBuffer.h"
#include "Window.h"


namespace {

static inline bool
_IsSupported32BitBuffer(const RenderingBuffer& buffer)
{
	return buffer.ColorSpace() == B_RGBA32 || buffer.ColorSpace() == B_RGB32;
}


static inline int64
_RegionPixelCount(const BRegion& region)
{
	int64 count = 0;
	for (int32 i = 0; i < region.CountRects(); i++) {
		BRect r = region.RectAt(i);
		count += (int64)(r.IntegerWidth() + 1) * (r.IntegerHeight() + 1);
	}
	return count;
}


static inline bool
_RegionContains(const BRegion& region, const BRect& rect)
{
	int32 count = region.CountRects();
	if (count == 0 || !region.Frame().Contains(rect))
		return false;
	
	for (int32 i = 0; i < count; i++) {
		if (region.RectAt(i).Contains(rect))
			return true;
	}

	return false;
}

// Helper for _mm_mullo_epi32 which is SSE4.1
#ifndef __SSE4_1__
static inline __m128i _mm_mullo_epi32(__m128i a, __m128i b) {
	__m128i tmp1 = _mm_mul_epu32(a, b); /* a0*b0, a2*b2 */
	__m128i tmp2 = _mm_mul_epu32(_mm_srli_si128(a, 4), _mm_srli_si128(b, 4)); /* a1*b1, a3*b3 */
	return _mm_unpacklo_epi32(_mm_shuffle_epi32(tmp1, _MM_SHUFFLE(0, 0, 2, 0)), _mm_shuffle_epi32(tmp2, _MM_SHUFFLE(0, 0, 2, 0)));
}
#endif

} // namespace


Compositor::BlurCacheEntry::BlurCacheEntry()
	:
	surfaceToken(0),
	pixels(NULL),
	generation(-1),
	width(0),
	height(0),
	radius(0),
	buffer(NULL)
{
}


Compositor::BlurCacheEntry::~BlurCacheEntry()
{
	delete buffer;
}


Compositor::Compositor()
	:
	fLogLevel(0),
	fShowOverlay(false),
	fLogTimings(false),
	fStressInvalidate(false)
{
}


Compositor::~Compositor()
{
}


void
Compositor::SetDebugOptions(bool showOverlay, bool logTimings, bool stressInvalidate)
{
	fShowOverlay = showOverlay;
	fLogTimings = logTimings;
	fStressInvalidate = stressInvalidate;
}


void
Compositor::SetLogLevel(int32 level)
{
	fLogLevel = level;
}


void
Compositor::Compose(RenderingBuffer& dst, RenderingBuffer& backbuffer,
	const BRegion& dirty, const SurfaceList& surfaces, const rgb_color& background,
	bool trueShadows)
{
	if (!_IsSupported32BitBuffer(dst))
		return;

	// 1. Clear regions of dst that are dirty but not covered by any opaque surfaces
	BRegion remainder(dirty);
	for (size_t i = 0; i < surfaces.size(); i++) {
		const Surface& s = surfaces[i];
		if (s.isOpaque && s.alpha >= 1.0f) {
			remainder.Exclude(&s.opaqueRegion);
		}
	}
	
	if (remainder.CountRects() > 0) {
		_ClearRegion(dst, remainder, background);
	}

	// 2. Compose surfaces bottom-to-top
	for (size_t i = 0; i < surfaces.size(); i++) {
		const Surface& s = surfaces[i];
		
		if (s.skipDrawing)
			continue;

		BRegion drawRegion(dirty);
		drawRegion.IntersectWith(&s.damage);
		if (drawRegion.CountRects() == 0)
			continue;

		RenderingBuffer* src = s.buffer;
		BPoint translation = s.translation;

		// If window has no private buffer yet, fallback to backbuffer (Stage 7 path)
		if (src == NULL) {
			src = &backbuffer;
			translation = BPoint(0, 0);
		}

		if (s.isOpaque && s.alpha >= 1.0f) {
			_CopyRegionTranslated(dst, *src, drawRegion, translation);
		} else {
			_BlendRegionTranslated(dst, *src, drawRegion, s.alpha, translation);
		}
	}
}


void
Compositor::ComposeTileGrid(RenderingBuffer& dst, RenderingBuffer& backbuffer,
	const SurfaceList& surfaces, TileDamageTracker* damageTracker,
	const rgb_color& background)
{
	int32 columns = damageTracker->Columns();
	int32 rows = damageTracker->Rows();

	for (int32 y = 0; y < rows; y++) {
		for (int32 x = 0; x < columns; x++) {
			if (damageTracker->IsTileDirty(x, y)) {
				_ComposeSingleTile(dst, backbuffer, x, y, surfaces, background);
			}
		}
	}
	damageTracker->Clear();
}


void
Compositor::_ComposeSingleTile(RenderingBuffer& dst, RenderingBuffer& backbuffer,
	int32 tileX, int32 tileY, const SurfaceList& surfaces, const rgb_color& background)
{
	BRect tileRect(tileX << TILE_SHIFT, tileY << TILE_SHIFT,
		((tileX + 1) << TILE_SHIFT) - 1, ((tileY + 1) << TILE_SHIFT) - 1);
	
	// Top-to-Bottom occlusion pass to find first visible opaque index
	int32 firstVisible = -1;
	for (int32 i = (int32)surfaces.size() - 1; i >= 0; i--) {
		const Surface& s = surfaces[i];
		if (tileX < s.tileX0 || tileX > s.tileX1 || tileY < s.tileY0 || tileY > s.tileY1)
			continue;
		
		if (s.isOpaque && s.alpha >= 1.0f && _RegionContains(s.opaqueRegion, tileRect)) {
			firstVisible = i;
			break;
		}
	}

	if (firstVisible == -1) {
		_ClearRegion(dst, tileRect, background);
		firstVisible = 0;
	}

	// Bottom-to-Top rendering pass starting from firstVisible
	for (int32 i = firstVisible; i < (int32)surfaces.size(); i++) {
		const Surface& s = surfaces[i];
		
		if (tileX < s.tileX0 || tileX > s.tileX1 || tileY < s.tileY0 || tileY > s.tileY1)
			continue;

		BRect intersect = s.bounds & tileRect;
		if (!intersect.IsValid())
			continue;
		
		// Occlusion test (Top-to-Bottom)
		bool occluded = false;
		for (int32 j = (int32)surfaces.size() - 1; j > i; j--) {
			const Surface& upper = surfaces[j];
			if (tileX < upper.tileX0 || tileX > upper.tileX1 || tileY < upper.tileY0 || tileY > upper.tileY1)
				continue;

			if (upper.isOpaque && upper.alpha >= 1.0f && _RegionContains(upper.opaqueRegion, intersect)) {
				occluded = true;
				break;
			}
		}
		
		if (occluded || s.skipDrawing)
			continue;

		BRegion tileRegion(intersect);
		RenderingBuffer* src = s.buffer;
		BPoint translation = s.translation;

		// Fallback to backbuffer if no private buffer (e.g. allocation failed)
		if (src == NULL) {
			src = &backbuffer;
			translation = BPoint(0, 0);
		}

		if (s.isOpaque && s.alpha >= 1.0f) {
			_CopyRegionTranslated(dst, *src, tileRegion, translation);
		} else {
			_BlendRegionTranslated(dst, *src, tileRegion, s.alpha, translation);
		}
	}
}


void
Compositor::_ClearRegion(RenderingBuffer& dst, const BRegion& region, const rgb_color& color)
{
	uint32 value = (color.alpha << 24) | (color.red << 16) | (color.green << 8) | color.blue;
	uint8* bits = (uint8*)dst.Bits();
	uint32 bpr = dst.BytesPerRow();

	for (int32 i = 0; i < region.CountRects(); i++) {
		clipping_rect r = region.RectAtInt(i);
		for (int32 y = r.top; y <= r.bottom; y++) {
			uint32* row = (uint32*)(bits + y * bpr + r.left * 4);
			int32 width = r.right - r.left + 1;
			int32 x = 0;
#ifdef __SSE2__
			if (width >= 4) {
				__m128i val = _mm_set1_epi32(value);
				while (x <= width - 4) {
					_mm_storeu_si128((__m128i*)&row[x], val);
					x += 4;
				}
			}
#endif
			for (; x < width; x++) {
				row[x] = value;
			}
		}
	}
}


void
Compositor::_ClearRegion(RenderingBuffer& dst, const BRect& rect, const rgb_color& color)
{
	BRegion region(rect);
	_ClearRegion(dst, region, color);
}


void
Compositor::_CopyRegionTranslated(RenderingBuffer& dst, RenderingBuffer& src,
	const BRegion& region, const BPoint& translation)
{
	uint8* dstBits = (uint8*)dst.Bits();
	uint32 dstBPR = dst.BytesPerRow();
	uint8* srcBits = (uint8*)src.Bits();
	uint32 srcBPR = src.BytesPerRow();

	for (int32 i = 0; i < region.CountRects(); i++) {
		clipping_rect r = region.RectAtInt(i);
		
		int32 srcLeft = (int32)(r.left - translation.x);
		int32 srcTop = (int32)(r.top - translation.y);

		int32 width = r.right - r.left + 1;
		int32 height = r.bottom - r.top + 1;

		if (srcLeft < 0 || srcTop < 0)
			continue;

		if (srcLeft + width > (int32)src.Width())
			width = (int32)src.Width() - srcLeft;
		if (srcTop + height > (int32)src.Height())
			height = (int32)src.Height() - srcTop;

		if (width <= 0 || height <= 0)
			continue;

		for (int32 y = 0; y < height; y++) {
			uint8* dstRow = dstBits + (r.top + y) * dstBPR + r.left * 4;
			uint8* srcRow = srcBits + (srcTop + y) * srcBPR + srcLeft * 4;
			memcpy(dstRow, srcRow, width * 4);
		}
	}
}


void
Compositor::_BlendRegionTranslated(RenderingBuffer& dst, RenderingBuffer& src,
	const BRegion& region, float alpha, const BPoint& translation)
{
	uint8* dstBits = (uint8*)dst.Bits();
	uint32 dstBPR = dst.BytesPerRow();
	uint8* srcBits = (uint8*)src.Bits();
	uint32 srcBPR = src.BytesPerRow();
	uint32 alphaI = (uint32)(alpha * 255.0f);

	if (alphaI == 0)
		return;
	if (alphaI >= 255) {
		_CopyRegionTranslated(dst, src, region, translation);
		return;
	}

	for (int32 i = 0; i < region.CountRects(); i++) {
		clipping_rect r = region.RectAtInt(i);
		
		int32 srcLeft = (int32)(r.left - translation.x);
		int32 srcTop = (int32)(r.top - translation.y);

		int32 width = r.right - r.left + 1;
		int32 height = r.bottom - r.top + 1;

		if (srcLeft < 0 || srcTop < 0)
			continue;

		if (srcLeft + width > (int32)src.Width())
			width = (int32)src.Width() - srcLeft;
		if (srcTop + height > (int32)src.Height())
			height = (int32)src.Height() - srcTop;

		if (width <= 0 || height <= 0)
			continue;

		for (int32 y = 0; y < height; y++) {
			uint32* dstRow = (uint32*)(dstBits + (r.top + y) * dstBPR + r.left * 4);
			uint32* srcRow = (uint32*)(srcBits + (srcTop + y) * srcBPR + srcLeft * 4);
			
			int32 x = 0;
#ifdef __SSE2__
			if (width >= 4) {
				__m128i zero = _mm_setzero_si128();
				__m128i add32 = _mm_set1_epi32(128);

				for (; x <= width - 4; x += 4) {
					__m128i d4 = _mm_loadu_si128((__m128i*)&dstRow[x]);
					__m128i s4 = _mm_loadu_si128((__m128i*)&srcRow[x]);

					// Use 32-bit to avoid overflow in (src * a + dst * (255 - a))
					// dLo = [d0, d1], dHi = [d2, d3] as 16-bit shorts
					__m128i dLo = _mm_unpacklo_epi8(d4, zero);
					__m128i dHi = _mm_unpackhi_epi8(d4, zero);
					__m128i sLo = _mm_unpacklo_epi8(s4, zero);
					__m128i sHi = _mm_unpackhi_epi8(s4, zero);

					// Further unpack to 32-bit
					__m128i d01 = _mm_unpacklo_epi16(dLo, zero);
					__m128i d23 = _mm_unpackhi_epi16(dLo, zero);
					__m128i d45 = _mm_unpacklo_epi16(dHi, zero);
					__m128i d67 = _mm_unpackhi_epi16(dHi, zero);

					__m128i s01 = _mm_unpacklo_epi16(sLo, zero);
					__m128i s23 = _mm_unpackhi_epi16(sLo, zero);
					__m128i s45 = _mm_unpacklo_epi16(sHi, zero);
					__m128i s67 = _mm_unpackhi_epi16(sHi, zero);

					__m128i a32 = _mm_set1_epi32(alphaI);
					__m128i na32 = _mm_set1_epi32(255 - alphaI);

					d01 = _mm_srli_epi32(_mm_add_epi32(_mm_add_epi32(_mm_mullo_epi32(s01, a32), _mm_mullo_epi32(d01, na32)), add32), 8);
					d23 = _mm_srli_epi32(_mm_add_epi32(_mm_add_epi32(_mm_mullo_epi32(s23, a32), _mm_mullo_epi32(d23, na32)), add32), 8);
					d45 = _mm_srli_epi32(_mm_add_epi32(_mm_add_epi32(_mm_mullo_epi32(s45, a32), _mm_mullo_epi32(d45, na32)), add32), 8);
					d67 = _mm_srli_epi32(_mm_add_epi32(_mm_add_epi32(_mm_mullo_epi32(s67, a32), _mm_mullo_epi32(d67, na32)), add32), 8);

					__m128i resLo = _mm_packs_epi32(d01, d23);
					__m128i resHi = _mm_packs_epi32(d45, d67);
					__m128i final = _mm_packus_epi16(resLo, resHi);
					
					final = _mm_or_si128(final, _mm_set1_epi32(0xFF000000));
					_mm_storeu_si128((__m128i*)&dstRow[x], final);
				}
			}
#endif
			for (; x < width; x++) {
				uint32 d = dstRow[x];
				uint32 s = srcRow[x];
				
				uint32 dr = (d >> 16) & 0xFF;
				uint32 dg = (d >> 8) & 0xFF;
				uint32 db = d & 0xFF;
				
				uint32 sr = (s >> 16) & 0xFF;
				uint32 sg = (s >> 8) & 0xFF;
				uint32 sb = s & 0xFF;
				
				uint32 rr = (sr * alphaI + dr * (255 - alphaI) + 128) >> 8;
				uint32 rg = (sg * alphaI + dg * (255 - alphaI) + 128) >> 8;
				uint32 rb = (sb * alphaI + db * (255 - alphaI) + 128) >> 8;
				
				dstRow[x] = (0xFF << 24) | (rr << 16) | (rg << 8) | rb;
			}
		}
	}
}
