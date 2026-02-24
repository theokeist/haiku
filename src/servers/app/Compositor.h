/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <GraphicsDefs.h>
#include <Rect.h>
#include <Region.h>

#include <vector>

class Window;
#include "CompositorDebugOptions.h"

class RenderingBuffer;

// Metadata placeholder for future retained-surface lifecycle management.
// This is intentionally non-functional for now; it allows plumbing stable
// identifiers through snapshot flow without changing current rendering output.
struct RetainedSurfaceMetadata {
	int32			surfaceToken;
	uint64			surfaceGeneration;
	int32			lastKnownDamageRects;
	bool			valid;
};

// Snapshot of window state consumed by compositor for a compose pass.
struct WindowSnapshot {
	BRegion			visible;
	float			alpha;
	bool			opaqueFastPath;
	bool			animActive;
	bool			blurEnabled;
	float			blurRadius;
	BRect			blurRect;
	Window*			window;
	bool			blurBehind;
	RetainedSurfaceMetadata	retained;
};

// Per-frame instrumentation emitted by compositing/present logging.
struct ComposeStats {
	int32			dirtyRects;
	int64			dirtyPixels;
	int32			windowsComposed;
	int32			copyPathWindows;
	int32			blendPathWindows;
	int32			alphaWindows;
	int32			blurredWindows;
	int64			blurredPixels;
	int32			blurCacheHits;
	int32			blurCacheMisses;
	BRegion			overlayRects;
	int32			cacheHits;
	int32			cacheMisses;
	bigtime_t		composeTime;
};

class Compositor {
public:
				Compositor();
	void			SetLogLevel(int32 logLevel);
	void			SetDebugOptions(bool showOverlay, bool logTimings,
						bool stressInvalidate);
	ComposeStats	Compose(RenderingBuffer& dst, RenderingBuffer& src,
						const BRegion& dirty,
						const std::vector<WindowSnapshot>& snapshots,
						const rgb_color& background) const;

private:
	struct BlurCacheEntry {
		BlurCacheEntry();

		Window*			window;
		BRect			rect;
		std::vector<uint32>
						pixels;
		int64			generation;
		int32			width;
		int32			height;
		int32			radius;
	};

	ComposeStats	Compose(RenderingBuffer& dst, RenderingBuffer& src,
						const BRegion& dirty,
						const std::vector<WindowSnapshot>& snapshots,
						const rgb_color& background,
						const CompositorDebugOptions& options) const;

private:
	void			_ClearRegion(RenderingBuffer& dst, const BRegion& dirty,
						const rgb_color& background) const;
	void			_CopyRegion(RenderingBuffer& dst, RenderingBuffer& src,
						const BRegion& region) const;
	void			_BlendRegion(RenderingBuffer& dst, RenderingBuffer& src,
						const BRegion& region, float alpha) const;
	void			_BlurRegionCached(RenderingBuffer& dst, const BRect& rect,
						Window* window, bool backgroundChanged,
						int32 radius, ComposeStats& stats) const;
	void			_BlurRegion(RenderingBuffer& dst, const BRect& rect,
						std::vector<uint32>& output, int32 radius) const;
	void			_DrawDebugOverlay(RenderingBuffer& dst, const BRegion& dirty,
						const std::vector<WindowSnapshot>& snapshots,
						ComposeStats& stats) const;
	void			_DrawRectOutline(RenderingBuffer& dst, const BRect& rect,
						uint32 color, BRegion& overlayRects) const;
	void			_DrawText(RenderingBuffer& dst, int32 x, int32 y,
						const char* text, uint32 color,
						BRegion& overlayRects) const;

	mutable std::vector<BlurCacheEntry>
						fBlurCache;
	mutable int64		fBackgroundGeneration;
	int32				fLogLevel;
	bool				fShowOverlay;
	bool				fLogTimings;
	bool				fStressInvalidate;
	mutable bool		fLoggedSnapshotOrder;
	void			_BlurRegion(RenderingBuffer& dst, const BRegion& region,
						uint8 radius, int64& _pixelCount,
						bigtime_t& _elapsed) const;
	void			_DrawOverlay(RenderingBuffer& dst, const BRegion& dirty,
						const BRegion& blurRegion) const;
};

#endif // COMPOSITOR_H
