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
	uint8			blurRadius;
	bool			blurBehind;
	bool			opaqueFastPath;
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
	int32			cacheHits;
	int32			cacheMisses;
	bigtime_t		blurTime;
	bigtime_t		composeTime;
};

class Compositor {
public:
	// Composes all window snapshots intersecting dirty into dst.
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
