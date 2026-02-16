/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#include "Compositor.h"

#include <inttypes.h>
#include <stdio.h>
#include <OS.h>
#include <string.h>

#include <vector>

#include "RenderingBuffer.h"

namespace {

static inline int64
_RegionPixelCount(const BRegion& region)
{
	int64 pixels = 0;
	int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++) {
		BRect rect = region.RectAt(i);
		int32 width = rect.IntegerWidth() + 1;
		int32 height = rect.IntegerHeight() + 1;
		pixels += int64(width) * height;
	}
	return pixels;
}

} // namespace


ComposeStats
Compositor::Compose(RenderingBuffer& dst, RenderingBuffer& src,
	const BRegion& dirty, const std::vector<WindowSnapshot>& snapshots,
	const rgb_color& background, const CompositorDebugOptions& options) const
{
	bigtime_t start = system_time();
	static bool sLoggedSnapshotOrder = false;

	ComposeStats stats = {};
	stats.dirtyRects = dirty.CountRects();
	stats.dirtyPixels = _RegionPixelCount(dirty);
	stats.blurredWindows = 0;
	stats.blurredPixels = 0;
	stats.blurCacheHits = 0;
	stats.blurCacheMisses = 0;
	stats.blurTime = 0;

	BRegion blurRegion;

	_ClearRegion(dst, dirty, background);

	if (!sLoggedSnapshotOrder) {
		size_t count = snapshots.size();
		debug_printf("compositor: snapshot order (bottom->top) count=%"
			B_PRId32 "\n", (int32)count);
		size_t maxToLog = 5;
		for (size_t i = 0; i < count && i < maxToLog; i++) {
			BRect frame = snapshots[i].visible.Frame();
			debug_printf("compositor: snapshot[%zu] frame=(%.1f, %.1f, %.1f, %.1f)"
				" alpha=%.2f blur=%s radius=%u opaque=%s\n", i,
				frame.left, frame.top, frame.right, frame.bottom,
				snapshots[i].alpha, snapshots[i].blurBehind ? "yes" : "no",
				snapshots[i].blurRadius,
				snapshots[i].opaqueFastPath ? "yes" : "no");
		}
		if (count > maxToLog) {
			size_t start = count > maxToLog ? count - maxToLog : 0;
			for (size_t i = start; i < count; i++) {
				BRect frame = snapshots[i].visible.Frame();
				debug_printf("compositor: snapshot[%zu] frame=(%.1f, %.1f, %.1f, %.1f)"
					" alpha=%.2f blur=%s radius=%u opaque=%s\n", i,
					frame.left, frame.top, frame.right, frame.bottom,
					snapshots[i].alpha, snapshots[i].blurBehind ? "yes" : "no",
					snapshots[i].blurRadius,
					snapshots[i].opaqueFastPath ? "yes" : "no");
			}
		}
		sLoggedSnapshotOrder = true;
	}

	for (std::vector<WindowSnapshot>::const_iterator it = snapshots.begin();
			it != snapshots.end(); ++it) {
		BRegion region(it->visible);
		region.IntersectWith(&dirty);
		if (region.CountRects() == 0)
			continue;

		stats.windowsComposed++;
		if (!it->opaqueFastPath)
			stats.alphaWindows++;

		if (it->blurBehind && it->blurRadius > 0) {
			stats.blurredWindows++;
			stats.blurCacheMisses++;
			blurRegion.Include(&region);
			_BlurRegion(dst, region, it->blurRadius, &stats.blurredPixels,
				&stats.blurTime);
		}

		if (it->opaqueFastPath)
			_CopyRegion(dst, src, region);
		else
			_BlendRegion(dst, src, region, it->alpha);
	}

	stats.composeTime = system_time() - start;

	if (options.showOverlay)
		_DrawDebugOverlay(dst, dirty, blurRegion, stats);

	if (options.logTimings) {
		debug_printf("compositor: compose=%" B_PRId64 "us blur=%" B_PRId64
			"us blurredWindows=%" B_PRId32 " blurredPixels=%" B_PRId64
			" dirtyRects=%" B_PRId32 " dirtyPixels=%" B_PRId64 "\n",
			stats.composeTime, stats.blurTime, stats.blurredWindows,
			stats.blurredPixels, stats.dirtyRects, stats.dirtyPixels);
	}
	return stats;
}


void
Compositor::_BlurRegion(RenderingBuffer& dst, const BRegion& region,
	uint8 radius, int64* _blurredPixels, bigtime_t* _blurTime) const
{
	bigtime_t start = system_time();
	if (radius == 0)
		return;
	if (dst.ColorSpace() != B_RGBA32 && dst.ColorSpace() != B_RGB32)
		return;

	BRegion clipped(region);
	BRegion dstBounds;
	dstBounds.Set((BRect)dst.Bounds());
	clipped.IntersectWith(&dstBounds);
	if (clipped.CountRects() == 0)
		return;

	uint8* dstBits = (uint8*)dst.Bits();
	uint32 dstBPR = dst.BytesPerRow();

	int32 count = clipped.CountRects();
	for (int32 i = 0; i < count; i++) {
		BRect rect = clipped.RectAt(i);
		int32 left = (int32)rect.left;
		int32 right = (int32)rect.right;
		int32 top = (int32)rect.top;
		int32 bottom = (int32)rect.bottom;
		int32 width = right - left + 1;
		int32 height = bottom - top + 1;
		if (width <= 0 || height <= 0)
			continue;

		if (_blurredPixels != NULL)
			*_blurredPixels += int64(width) * height;

		std::vector<uint8> source(width * height * 4);
		for (int32 y = 0; y < height; y++) {
			uint8* src = dstBits + (top + y) * dstBPR + left * 4;
			memcpy(&source[y * width * 4], src, width * 4);
		}

		for (int32 y = 0; y < height; y++) {
			uint8* out = dstBits + (top + y) * dstBPR + left * 4;
			for (int32 x = 0; x < width; x++) {
				int32 y0 = y - radius;
				if (y0 < 0)
					y0 = 0;
				int32 y1 = y + radius;
				if (y1 >= height)
					y1 = height - 1;
				int32 x0 = x - radius;
				if (x0 < 0)
					x0 = 0;
				int32 x1 = x + radius;
				if (x1 >= width)
					x1 = width - 1;

				uint32 sumB = 0;
				uint32 sumG = 0;
				uint32 sumR = 0;
				uint32 samples = 0;

				for (int32 by = y0; by <= y1; by++) {
					const uint8* sample = &source[(by * width + x0) * 4];
					for (int32 bx = x0; bx <= x1; bx++) {
						sumB += sample[0];
						sumG += sample[1];
						sumR += sample[2];
						sample += 4;
						samples++;
					}
				}

				out[0] = (uint8)(sumB / samples);
				out[1] = (uint8)(sumG / samples);
				out[2] = (uint8)(sumR / samples);
				out[3] = 255;
				out += 4;
			}
		}
	}

	if (_blurTime != NULL)
		*_blurTime += system_time() - start;
}


void
Compositor::_DrawDebugOverlay(RenderingBuffer& dst, const BRegion& dirty,
	const BRegion& blurRegion, const ComposeStats& stats) const
{
	if (dst.ColorSpace() != B_RGBA32 && dst.ColorSpace() != B_RGB32)
		return;

	uint8* bits = (uint8*)dst.Bits();
	uint32 bpr = dst.BytesPerRow();

	int32 count = dirty.CountRects();
	for (int32 i = 0; i < count; i++) {
		BRect rect = dirty.RectAt(i);
		int32 left = (int32)rect.left;
		int32 right = (int32)rect.right;
		int32 top = (int32)rect.top;
		int32 bottom = (int32)rect.bottom;
		if (right < left || bottom < top)
			continue;

		for (int32 x = left; x <= right; x++) {
			uint8* t = bits + top * bpr + x * 4;
			uint8* b = bits + bottom * bpr + x * 4;
			t[2] = 255; t[1] = 0; t[0] = 0; t[3] = 255;
			b[2] = 255; b[1] = 0; b[0] = 0; b[3] = 255;
		}
		for (int32 y = top; y <= bottom; y++) {
			uint8* l = bits + y * bpr + left * 4;
			uint8* r = bits + y * bpr + right * 4;
			l[2] = 255; l[1] = 0; l[0] = 0; l[3] = 255;
			r[2] = 255; r[1] = 0; r[0] = 0; r[3] = 255;
		}
	}

	int32 blurCount = blurRegion.CountRects();
	for (int32 i = 0; i < blurCount; i++) {
		BRect rect = blurRegion.RectAt(i);
		int32 left = (int32)rect.left;
		int32 right = (int32)rect.right;
		int32 top = (int32)rect.top;
		int32 bottom = (int32)rect.bottom;
		if (right < left || bottom < top)
			continue;

		for (int32 x = left; x <= right; x++) {
			uint8* t = bits + top * bpr + x * 4;
			uint8* b = bits + bottom * bpr + x * 4;
			t[2] = 0; t[1] = 255; t[0] = 255; t[3] = 255;
			b[2] = 0; b[1] = 255; b[0] = 255; b[3] = 255;
		}
		for (int32 y = top; y <= bottom; y++) {
			uint8* l = bits + y * bpr + left * 4;
			uint8* r = bits + y * bpr + right * 4;
			l[2] = 0; l[1] = 255; l[0] = 255; l[3] = 255;
			r[2] = 0; r[1] = 255; r[0] = 255; r[3] = 255;
		}
	}

	int32 barLeft = 8;
	int32 barTop = 8;
	int32 barWidth = 120;
	int32 barHeight = 6;
	int32 fill = stats.blurredWindows > 0 ? barWidth : 0;
	for (int32 y = 0; y < barHeight; y++) {
		for (int32 x = 0; x < barWidth; x++) {
			uint8* p = bits + (barTop + y) * bpr + (barLeft + x) * 4;
			if (x < fill) {
				p[2] = 0; p[1] = 255; p[0] = 255; p[3] = 255;
			} else {
				p[2] = 32; p[1] = 32; p[0] = 32; p[3] = 255;
			}
		}
	}
}


void
Compositor::_ClearRegion(RenderingBuffer& dst, const BRegion& dirty,
	const rgb_color& background) const
{
	if (dst.ColorSpace() != B_RGBA32 && dst.ColorSpace() != B_RGB32)
		return;

	BRegion clipped(dirty);
	BRegion clipBounds;
	clipBounds.Set((BRect)dst.Bounds());
	clipped.IntersectWith(&clipBounds);
	if (clipped.CountRects() == 0)
		return;

	uint32 color = (uint32(255) << 24)
		| (uint32(background.red) << 16)
		| (uint32(background.green) << 8)
		| uint32(background.blue);

	uint8* dstBits = (uint8*)dst.Bits();
	uint32 dstBPR = dst.BytesPerRow();

	int32 count = clipped.CountRects();
	for (int32 i = 0; i < count; i++) {
		BRect rect = clipped.RectAt(i);
		int32 left = (int32)rect.left;
		int32 right = (int32)rect.right;
		int32 top = (int32)rect.top;
		int32 bottom = (int32)rect.bottom;
		int32 width = right - left + 1;
		if (width <= 0 || bottom < top)
			continue;

		for (int32 y = top; y <= bottom; y++) {
			uint32* dstRow = (uint32*)(dstBits + y * dstBPR + left * 4);
			for (int32 x = 0; x < width; x++)
				dstRow[x] = color;
		}
	}
}


void
Compositor::_CopyRegion(RenderingBuffer& dst, RenderingBuffer& src,
	const BRegion& region) const
{
	if (dst.ColorSpace() != B_RGBA32 && dst.ColorSpace() != B_RGB32)
		return;
	if (src.ColorSpace() != B_RGBA32 && src.ColorSpace() != B_RGB32)
		return;

	BRegion clipped(region);
	BRegion dstBounds;
	dstBounds.Set((BRect)dst.Bounds());
	clipped.IntersectWith(&dstBounds);
	BRegion srcBounds;
	srcBounds.Set((BRect)src.Bounds());
	clipped.IntersectWith(&srcBounds);
	if (clipped.CountRects() == 0)
		return;

	uint8* dstBits = (uint8*)dst.Bits();
	uint8* srcBits = (uint8*)src.Bits();
	uint32 dstBPR = dst.BytesPerRow();
	uint32 srcBPR = src.BytesPerRow();

	int32 count = clipped.CountRects();
	for (int32 i = 0; i < count; i++) {
		BRect rect = clipped.RectAt(i);
		int32 left = (int32)rect.left;
		int32 right = (int32)rect.right;
		int32 top = (int32)rect.top;
		int32 bottom = (int32)rect.bottom;
		int32 width = right - left + 1;
		int32 bytes = width * 4;
		if (width <= 0 || bottom < top)
			continue;

		for (int32 y = top; y <= bottom; y++) {
			uint8* dstRow = dstBits + y * dstBPR + left * 4;
			uint8* srcRow = srcBits + y * srcBPR + left * 4;
			memcpy(dstRow, srcRow, bytes);
		}
	}
}


void
Compositor::_BlendRegion(RenderingBuffer& dst, RenderingBuffer& src,
	const BRegion& region, float alpha) const
{
	if (dst.ColorSpace() != B_RGBA32 && dst.ColorSpace() != B_RGB32)
		return;
	if (src.ColorSpace() != B_RGBA32 && src.ColorSpace() != B_RGB32)
		return;

	if (alpha <= 0.0f)
		return;

	if (alpha > 1.0f)
		alpha = 1.0f;

	BRegion clipped(region);
	BRegion dstBounds;
	dstBounds.Set((BRect)dst.Bounds());
	clipped.IntersectWith(&dstBounds);
	BRegion srcBounds;
	srcBounds.Set((BRect)src.Bounds());
	clipped.IntersectWith(&srcBounds);
	if (clipped.CountRects() == 0)
		return;

	uint8* dstBits = (uint8*)dst.Bits();
	uint8* srcBits = (uint8*)src.Bits();
	uint32 dstBPR = dst.BytesPerRow();
	uint32 srcBPR = src.BytesPerRow();
	uint8 alphaByte = (uint8)(alpha * 255.0f);

	int32 count = clipped.CountRects();
	for (int32 i = 0; i < count; i++) {
		BRect rect = clipped.RectAt(i);
		int32 left = (int32)rect.left;
		int32 right = (int32)rect.right;
		int32 top = (int32)rect.top;
		int32 bottom = (int32)rect.bottom;
		int32 width = right - left + 1;
		if (width <= 0 || bottom < top)
			continue;

		for (int32 y = top; y <= bottom; y++) {
			uint8* dstRow = dstBits + y * dstBPR + left * 4;
			uint8* srcRow = srcBits + y * srcBPR + left * 4;
			for (int32 x = 0; x < width; x++) {
				uint8 srcAlpha = srcRow[3];
				if (srcAlpha == 0) {
					dstRow += 4;
					srcRow += 4;
					continue;
				}

				uint16 combinedAlpha = (uint16(srcAlpha) * alphaByte + 127) / 255;
				uint16 invAlpha = 255 - combinedAlpha;
				dstRow[0] = (uint8)((srcRow[0] * combinedAlpha
					+ dstRow[0] * invAlpha + 127) / 255);
				dstRow[1] = (uint8)((srcRow[1] * combinedAlpha
					+ dstRow[1] * invAlpha + 127) / 255);
				dstRow[2] = (uint8)((srcRow[2] * combinedAlpha
					+ dstRow[2] * invAlpha + 127) / 255);
				dstRow[3] = 255;

				dstRow += 4;
				srcRow += 4;
			}
		}
	}
}
