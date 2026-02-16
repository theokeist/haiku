/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <GraphicsDefs.h>
#include <Region.h>

#include <vector>

#include "CompositorDebugOptions.h"

class RenderingBuffer;

struct WindowSnapshot {
	BRegion			visible;
	float			alpha;
	uint8			blurRadius;
	bool			blurBehind;
	bool			opaqueFastPath;
};

struct ComposeStats {
	int32			dirtyRects;
	int64			dirtyPixels;
	int32			windowsComposed;
	int32			alphaWindows;
	int32			blurredWindows;
	int64			blurredPixels;
	int32			cacheHits;
	int32			cacheMisses;
	bigtime_t		blurTime;
	bigtime_t		composeTime;
};

class Compositor {
public:
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
	void			_BlurRegion(RenderingBuffer& dst, const BRegion& region,
						uint8 radius, int64& _pixelCount,
						bigtime_t& _elapsed) const;
	void			_DrawOverlay(RenderingBuffer& dst, const BRegion& dirty,
						const BRegion& blurRegion) const;
};

#endif // COMPOSITOR_H
