/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <GraphicsDefs.h>
#include <Region.h>

#include <vector>

class RenderingBuffer;

struct WindowSnapshot {
	BRegion			visible;
	float			alpha;
	bool			opaqueFastPath;
};

struct ComposeStats {
	int32			dirtyRects;
	int64			dirtyPixels;
	int32			windowsComposed;
	int32			alphaWindows;
	bigtime_t		composeTime;
};

class Compositor {
public:
	ComposeStats	Compose(RenderingBuffer& dst, RenderingBuffer& src,
						const BRegion& dirty,
						const std::vector<WindowSnapshot>& snapshots,
						const rgb_color& background) const;

private:
	void			_ClearRegion(RenderingBuffer& dst, const BRegion& dirty,
						const rgb_color& background) const;
	void			_CopyRegion(RenderingBuffer& dst, RenderingBuffer& src,
						const BRegion& region) const;
	void			_BlendRegion(RenderingBuffer& dst, RenderingBuffer& src,
						const BRegion& region, float alpha) const;
};

#endif // COMPOSITOR_H
