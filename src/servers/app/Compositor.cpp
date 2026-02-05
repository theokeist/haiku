/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#include "Compositor.h"

#include <inttypes.h>
#include <stdio.h>
#include <OS.h>
#include <string.h>

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
	const rgb_color& background) const
{
	bigtime_t start = system_time();
	static bool sLoggedSnapshotOrder = false;

	ComposeStats stats = {};
	stats.dirtyRects = dirty.CountRects();
	stats.dirtyPixels = _RegionPixelCount(dirty);

	_ClearRegion(dst, dirty, background);

	if (!sLoggedSnapshotOrder) {
		size_t count = snapshots.size();
		debug_printf("compositor: snapshot order (bottom->top) count=%"
			B_PRId32 "\n", (int32)count);
		size_t maxToLog = 5;
		for (size_t i = 0; i < count && i < maxToLog; i++) {
			BRect frame = snapshots[i].visible.Frame();
			debug_printf("compositor: snapshot[%zu] frame=(%.1f, %.1f, %.1f, %.1f)"
				" alpha=%.2f opaque=%s\n", i,
				frame.left, frame.top, frame.right, frame.bottom,
				snapshots[i].alpha, snapshots[i].opaqueFastPath ? "yes" : "no");
		}
		if (count > maxToLog) {
			size_t start = count > maxToLog ? count - maxToLog : 0;
			for (size_t i = start; i < count; i++) {
				BRect frame = snapshots[i].visible.Frame();
				debug_printf("compositor: snapshot[%zu] frame=(%.1f, %.1f, %.1f, %.1f)"
					" alpha=%.2f opaque=%s\n", i,
					frame.left, frame.top, frame.right, frame.bottom,
					snapshots[i].alpha, snapshots[i].opaqueFastPath ? "yes" : "no");
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

		if (it->opaqueFastPath)
			_CopyRegion(dst, src, region);
		else
			_BlendRegion(dst, src, region, it->alpha);
	}

	stats.composeTime = system_time() - start;
	return stats;
}


void
Compositor::_ClearRegion(RenderingBuffer& dst, const BRegion& dirty,
	const rgb_color& background) const
{
	if (dst.ColorSpace() != B_RGBA32 && dst.ColorSpace() != B_RGB32)
		return;

	uint32 color = (uint32(255) << 24)
		| (uint32(background.red) << 16)
		| (uint32(background.green) << 8)
		| uint32(background.blue);

	uint8* dstBits = (uint8*)dst.Bits();
	uint32 dstBPR = dst.BytesPerRow();

	int32 count = dirty.CountRects();
	for (int32 i = 0; i < count; i++) {
		BRect rect = dirty.RectAt(i);
		int32 left = (int32)rect.left;
		int32 right = (int32)rect.right;
		int32 top = (int32)rect.top;
		int32 bottom = (int32)rect.bottom;
		int32 width = right - left + 1;

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

	uint8* dstBits = (uint8*)dst.Bits();
	uint8* srcBits = (uint8*)src.Bits();
	uint32 dstBPR = dst.BytesPerRow();
	uint32 srcBPR = src.BytesPerRow();

	int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++) {
		BRect rect = region.RectAt(i);
		int32 left = (int32)rect.left;
		int32 right = (int32)rect.right;
		int32 top = (int32)rect.top;
		int32 bottom = (int32)rect.bottom;
		int32 width = right - left + 1;
		int32 bytes = width * 4;

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

	uint8* dstBits = (uint8*)dst.Bits();
	uint8* srcBits = (uint8*)src.Bits();
	uint32 dstBPR = dst.BytesPerRow();
	uint32 srcBPR = src.BytesPerRow();
	uint8 alphaByte = (uint8)(alpha * 255.0f);
	uint8 invAlpha = 255 - alphaByte;

	int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++) {
		BRect rect = region.RectAt(i);
		int32 left = (int32)rect.left;
		int32 right = (int32)rect.right;
		int32 top = (int32)rect.top;
		int32 bottom = (int32)rect.bottom;
		int32 width = right - left + 1;

		for (int32 y = top; y <= bottom; y++) {
			uint8* dstRow = dstBits + y * dstBPR + left * 4;
			uint8* srcRow = srcBits + y * srcBPR + left * 4;
			for (int32 x = 0; x < width; x++) {
				dstRow[0] = ((dstRow[0] * invAlpha + 255) >> 8)
					+ ((srcRow[0] * alphaByte + 255) >> 8);
				dstRow[1] = ((dstRow[1] * invAlpha + 255) >> 8)
					+ ((srcRow[1] * alphaByte + 255) >> 8);
				dstRow[2] = ((dstRow[2] * invAlpha + 255) >> 8)
					+ ((srcRow[2] * alphaByte + 255) >> 8);
				dstRow[3] = 255;

				dstRow += 4;
				srcRow += 4;
			}
		}
	}
}
