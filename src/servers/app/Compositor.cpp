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
	BRegion allBlurRegions;

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
			allBlurRegions.Include(&region);
			int64 blurPixels = 0;
			bigtime_t blurTime = 0;
			_BlurRegion(dst, region, it->blurRadius, blurPixels, blurTime);
			stats.blurredPixels += blurPixels;
			stats.blurTime += blurTime;
			stats.cacheMisses++;
		}

		if (it->opaqueFastPath)
			_CopyRegion(dst, src, region);
		else
			_BlendRegion(dst, src, region, it->alpha);
	}

	if (options.showOverlay)
		_DrawOverlay(dst, dirty, allBlurRegions);

	stats.composeTime = system_time() - start;
	return stats;
}


void
Compositor::_BlurRegion(RenderingBuffer& dst, const BRegion& region,
	uint8 radius, int64& _pixelCount, bigtime_t& _elapsed) const
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
		_pixelCount += int64(width) * height;

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

	_elapsed += system_time() - start;
}


void
Compositor::_DrawOverlay(RenderingBuffer& dst, const BRegion& dirty,
	const BRegion& blurRegion) const
{
	if (dst.ColorSpace() != B_RGBA32 && dst.ColorSpace() != B_RGB32)
		return;

	uint8* bits = (uint8*)dst.Bits();
	uint32 bpr = dst.BytesPerRow();

	auto drawFrame = [&](const BRegion& region, uint8 b, uint8 g, uint8 r) {
		BRegion clipped(region);
		BRegion bounds;
		bounds.Set((BRect)dst.Bounds());
		clipped.IntersectWith(&bounds);
		int32 count = clipped.CountRects();
		for (int32 i = 0; i < count; i++) {
			BRect rect = clipped.RectAt(i);
			int32 left = (int32)rect.left;
			int32 right = (int32)rect.right;
			int32 top = (int32)rect.top;
			int32 bottom = (int32)rect.bottom;
			if (right < left || bottom < top)
				continue;

			for (int32 x = left; x <= right; x++) {
				uint8* pTop = bits + top * bpr + x * 4;
				uint8* pBottom = bits + bottom * bpr + x * 4;
				pTop[0] = b; pTop[1] = g; pTop[2] = r; pTop[3] = 255;
				pBottom[0] = b; pBottom[1] = g; pBottom[2] = r; pBottom[3] = 255;
			}
			for (int32 y = top; y <= bottom; y++) {
				uint8* pLeft = bits + y * bpr + left * 4;
				uint8* pRight = bits + y * bpr + right * 4;
				pLeft[0] = b; pLeft[1] = g; pLeft[2] = r; pLeft[3] = 255;
				pRight[0] = b; pRight[1] = g; pRight[2] = r; pRight[3] = 255;
			}
		}
	};

	drawFrame(dirty, 0, 255, 255);
	drawFrame(blurRegion, 0, 140, 255);
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
