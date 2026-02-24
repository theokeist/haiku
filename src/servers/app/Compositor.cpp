/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#include "Compositor.h"

#include <inttypes.h>
#include <stdio.h>
#include <OS.h>
#include <string.h>
#include <String.h>
#include <vector>
#include "GaussianBlur.h"
#include "RenderingBuffer.h"

Compositor::BlurCacheEntry::BlurCacheEntry()
	:
	window(NULL),
	rect(),
	generation(-1),
	width(0),
	height(0),
	radius(0)
{
}

Compositor::Compositor()
	:
	fBackgroundGeneration(0),
	fLogLevel(0),
	fShowOverlay(false),
	fLogTimings(false),
	fStressInvalidate(false),
	fLoggedSnapshotOrder(false)
{
}


void
Compositor::SetLogLevel(int32 logLevel)
{
	fLogLevel = logLevel;
	if (fLogLevel < 2)
		fLoggedSnapshotOrder = false;
}


void
Compositor::SetDebugOptions(bool showOverlay, bool logTimings,
	bool stressInvalidate)
{
	fShowOverlay = showOverlay;
	fLogTimings = logTimings;
	fStressInvalidate = stressInvalidate;
}

namespace {

static inline bool
_IsSupported32BitColorSpace(color_space colorSpace)
{
	// Current compositor math is 4-byte pixel oriented; guard unsupported
	// formats before entering copy/blend/blur paths.
	return colorSpace == B_RGBA32 || colorSpace == B_RGB32;
}


static inline bool
_IsSupported32BitBuffer(const RenderingBuffer& buffer)
{
	return _IsSupported32BitColorSpace(buffer.ColorSpace());
}

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
	if (fBackgroundGeneration == 0)
		fBackgroundGeneration = 1;

	ComposeStats stats = {};
	stats.dirtyRects = dirty.CountRects();
	stats.dirtyPixels = _RegionPixelCount(dirty);

	_ClearRegion(dst, dirty, background);

	if (fLogLevel >= 2 && !fLoggedSnapshotOrder) {
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
				" alpha=%.2f blur=%s radius=%u opaque=%s\n", i,
				snapshots[i].alpha, snapshots[i].blurBehind ? "yes" : "no",
				snapshots[i].blurRadius,
				snapshots[i].opaqueFastPath ? "yes" : "no");
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
		fLoggedSnapshotOrder = true;
	}

	if (dirty.CountRects() > 0)
		fBackgroundGeneration++;

	for (std::vector<WindowSnapshot>::const_iterator it = snapshots.begin();
			it != snapshots.end(); ++it) {
		BRegion region(it->visible);
		region.IntersectWith(&dirty);
		if (region.CountRects() == 0)
			continue;

		stats.windowsComposed++;
		if (!it->opaqueFastPath)
			stats.alphaWindows++;

		if (it->blurEnabled && it->blurRadius > 0.0f) {
			bool backgroundChanged = dirty.Intersects(it->blurRect);
			int32 radius = (int32)(it->blurRadius + 0.5f);
			if (radius < 1)
				radius = 1;
			stats.blurredWindows++;
			stats.blurredPixels += _RegionPixelCount(BRegion(it->blurRect));
			_BlurRegionCached(dst, it->blurRect, it->window, backgroundChanged,
				radius, stats);
		}

		if (it->opaqueFastPath) {
			stats.copyPathWindows++;
			_CopyRegion(dst, src, region);
		} else {
			stats.blendPathWindows++;
			_BlendRegion(dst, src, region, it->alpha);
		}
	}

	if (fShowOverlay)
		_DrawDebugOverlay(dst, dirty, snapshots, stats);

	if (fLogTimings && stats.blurTime > 0) {
		debug_printf("compositor: blur windows=%" B_PRId32
			" pixels=%" B_PRId64 " cache hit=%" B_PRId32
			" miss=%" B_PRId32 " blurTime=%" B_PRId64 "us\n",
			stats.blurredWindows, stats.blurredPixels, stats.blurCacheHits,
			stats.blurCacheMisses, stats.blurTime);
	}
	if (options.showOverlay)
		_DrawOverlay(dst, dirty, allBlurRegions);

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
Compositor::_BlurRegion(RenderingBuffer& dst, const BRegion& region,
	uint8 radius, int64& _pixelCount, bigtime_t& _elapsed) const
{
	bigtime_t start = system_time();
	if (radius == 0)
		return;
	if (!_IsSupported32BitBuffer(dst))
		return;

	BRegion clipped(region);
	BRegion dstBounds;
	dstBounds.Set((BRect)dst.Bounds());
	clipped.IntersectWith(&dstBounds);
	if (clipped.CountRects() == 0)
		return;

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
Compositor::_BlurRegionCached(RenderingBuffer& dst, const BRect& rect,
	Window* window, bool backgroundChanged, int32 radius, ComposeStats& stats) const
{
	if (dst.ColorSpace() != B_RGBA32 && dst.ColorSpace() != B_RGB32)
		return;

	BRect bounds = dst.Bounds();
	BRect clipped = rect & bounds;
	if (!clipped.IsValid())
		return;

	int32 left = (int32)clipped.left;
	int32 top = (int32)clipped.top;
	int32 right = (int32)clipped.right;
	int32 bottom = (int32)clipped.bottom;
	int32 width = right - left + 1;
	int32 height = bottom - top + 1;

	BlurCacheEntry* entry = NULL;
	for (size_t i = 0; i < fBlurCache.size(); i++) {
		if (fBlurCache[i].window == window
			&& fBlurCache[i].rect == clipped
			&& fBlurCache[i].radius == radius) {
			entry = &fBlurCache[i];
			break;
		}
	}

	if (entry == NULL) {
		fBlurCache.push_back(BlurCacheEntry());
		entry = &fBlurCache.back();
		entry->window = window;
		entry->rect = clipped;
	}

	if (entry->width != width || entry->height != height) {
		entry->width = width;
		entry->height = height;
		entry->pixels.clear();
		entry->pixels.resize(width * height);
		entry->generation = -1;
	}

	entry->radius = radius;
	if (fStressInvalidate)
		entry->generation = -1;

	if (backgroundChanged || entry->generation != fBackgroundGeneration) {
		bigtime_t blurStart = system_time();
		_BlurRegion(dst, clipped, entry->pixels, radius);
		stats.blurTime += system_time() - blurStart;
		stats.blurCacheMisses++;
		entry->generation = fBackgroundGeneration;
	} else {
		stats.blurCacheHits++;
	}

	uint8* dstBits = (uint8*)dst.Bits();
	uint32 dstBPR = dst.BytesPerRow();
	const uint32* src = entry->pixels.data();
	for (int32 y = 0; y < height; y++) {
		uint32* row = (uint32*)(dstBits + (top + y) * dstBPR + left * 4);
		memcpy(row, src + y * width, width * sizeof(uint32));
	}
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
Compositor::_BlurRegion(RenderingBuffer& dst, const BRect& rect,
	std::vector<uint32>& output, int32 radius) const
{
	int32 left = (int32)rect.left;
	int32 top = (int32)rect.top;
	int32 right = (int32)rect.right;
	int32 bottom = (int32)rect.bottom;
	int32 width = right - left + 1;
	int32 height = bottom - top + 1;
	if (width <= 0 || height <= 0)
		return;

	if (radius < 1)
		radius = 1;
	if (radius > 32)
		radius = 32;

	std::vector<uint32> input(width * height);
	std::vector<uint32> temp;
	if ((int32)output.size() != width * height)
		output.resize(width * height);

	uint8* dstBits = (uint8*)dst.Bits();
	uint32 dstBPR = dst.BytesPerRow();
	for (int32 y = 0; y < height; y++) {
		const uint32* row = (const uint32*)(dstBits
			+ (top + y) * dstBPR + left * 4);
		memcpy(&input[y * width], row, width * sizeof(uint32));
	}

	const GaussianKernel& kernel = GaussianBlurLibrary::KernelForRadius(radius);
	GaussianBlurLibrary::BlurRGBA32(&input[0], &output[0], width, height, kernel,
		temp);
Compositor::_DrawOverlay(RenderingBuffer& dst, const BRegion& dirty,
	const BRegion& blurRegion) const
{
	if (!_IsSupported32BitBuffer(dst))
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
Compositor::_DrawRectOutline(RenderingBuffer& dst, const BRect& rect,
	uint32 color, BRegion& overlayRects) const
{
	BRect clipped = rect & dst.Bounds();
	if (!clipped.IsValid())
		return;

	overlayRects.Include(clipped);
	uint8* bits = (uint8*)dst.Bits();
	uint32 bpr = dst.BytesPerRow();
	int32 left = (int32)clipped.left;
	int32 right = (int32)clipped.right;
	int32 top = (int32)clipped.top;
	int32 bottom = (int32)clipped.bottom;

	for (int32 x = left; x <= right; x++) {
		((uint32*)(bits + top * bpr))[x] = color;
		((uint32*)(bits + bottom * bpr))[x] = color;
	}
	for (int32 y = top; y <= bottom; y++) {
		((uint32*)(bits + y * bpr))[left] = color;
		((uint32*)(bits + y * bpr))[right] = color;
	}
}


void
Compositor::_DrawText(RenderingBuffer& dst, int32 x, int32 y,
	const char* text, uint32 color, BRegion& overlayRects) const
{
	if (text == NULL)
		return;

	uint8* bits = (uint8*)dst.Bits();
	uint32 bpr = dst.BytesPerRow();
	int32 cursor = x;
	for (const char* c = text; *c != '\0'; c++) {
		if (*c == ' ') {
			cursor += 4;
			continue;
		}
		for (int32 py = 0; py < 6; py++) {
			int32 yy = y + py;
			if (yy < 0 || yy > (int32)dst.Bounds().bottom)
				continue;
			for (int32 px = 0; px < 3; px++) {
				int32 xx = cursor + px;
				if (xx < 0 || xx > (int32)dst.Bounds().right)
					continue;
				if (((*c + px + py) & 1) == 0)
					((uint32*)(bits + yy * bpr))[xx] = color;
			}
		}
		overlayRects.Include(BRect(cursor, y, cursor + 2, y + 5));
		cursor += 4;
	}
}


void
Compositor::_DrawDebugOverlay(RenderingBuffer& dst, const BRegion& dirty,
	const std::vector<WindowSnapshot>& snapshots, ComposeStats& stats) const
{
	const uint32 dirtyColor = 0xff00ff00;
	const uint32 blurColor = 0xffff8000;
	const uint32 textColor = 0xffffffff;

	for (int32 i = 0; i < dirty.CountRects(); i++)
		_DrawRectOutline(dst, dirty.RectAt(i), dirtyColor, stats.overlayRects);

	for (size_t i = 0; i < snapshots.size(); i++) {
		if (snapshots[i].blurEnabled)
			_DrawRectOutline(dst, snapshots[i].blurRect, blurColor,
				stats.overlayRects);
	}

	BString text;
	text.SetToFormat("D%" B_PRId32 " B%" B_PRId32 " H%" B_PRId32 " M%" B_PRId32
		" T%" B_PRId64, stats.dirtyRects, stats.blurredWindows,
		stats.blurCacheHits, stats.blurCacheMisses, stats.blurTime);
	_DrawText(dst, 8, 8, text.String(), textColor, stats.overlayRects);
Compositor::_ClearRegion(RenderingBuffer& dst, const BRegion& dirty,
	const rgb_color& background) const
{
	if (!_IsSupported32BitBuffer(dst))
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
	if (!_IsSupported32BitBuffer(dst))
		return;
	if (!_IsSupported32BitBuffer(src))
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

	int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++) {
		BRect rect = region.RectAt(i);
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
	if (!_IsSupported32BitBuffer(dst))
		return;
	if (!_IsSupported32BitBuffer(src))
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
	uint8 invAlpha = 255 - alphaByte;

	int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++) {
		BRect rect = region.RectAt(i);
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
				dstRow[0] = ((dstRow[0] * invAlpha + 255) >> 8)
					+ ((srcRow[0] * alphaByte + 255) >> 8);
				dstRow[1] = ((dstRow[1] * invAlpha + 255) >> 8)
					+ ((srcRow[1] * alphaByte + 255) >> 8);
				dstRow[2] = ((dstRow[2] * invAlpha + 255) >> 8)
					+ ((srcRow[2] * alphaByte + 255) >> 8);
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
