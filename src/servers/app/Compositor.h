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
#include "TileDamageTracker.h"
#include "CompositorBuffer.h"

class RenderingBuffer;

// Metadata for retained-surface lifecycle management.
struct RetainedSurfaceMetadata {
	int32			surfaceToken;
	int32			surfaceGeneration;
	int32			lastKnownDamageRects;
	bool			valid;
};

// Represents a compositable layer of content.
struct Surface {
	BReference<CompositorBuffer> bufferRef;
	RenderingBuffer*	buffer;      // Source pixels (cached from bufferRef)
	BRect				bounds;      // Position in global screen space
	BPoint				translation; // Dynamic offset for movement
	float				alpha;       // Global opacity (0.0 to 1.0)
	bool				isOpaque;    // true if alpha == 1.0 and content is fully opaque
	int32				zOrder;      // Depth (higher is on top)
	
	// Damage/Visibility
	BRegion				damage;      // Calculated target region for this pass
	BRegion				opaqueRegion;// Area that occludes layers below
	bool				skipDrawing; // true for occluders like direct windows
	
	// Metadata for caching and effects
	uint32				surfaceToken;// Unique ID for this surface
	int32				bufferGeneration; // Content version
	bool				contentChanged;   // true if buffer changed since last pass
	
	bool				blurEnabled;
	float				blurRadius;
	BRect				blurRect;
	
	// Shadow properties
	float				shadowRadius;
	BPoint				shadowOffset;
	float				shadowOpacity;

	// Tile coordinates (Stage 8)
	int32				tileX0;
	int32				tileY0;
	int32				tileX1;
	int32				tileY1;
};

typedef std::vector<Surface> SurfaceList;

// Snapshot of window state used by SurfaceManager to build Surface objects.
struct WindowSnapshot {
	BRegion			visible;
	BPoint			translation;
	float			alpha;
	bool			opaqueFastPath;
	bool			animActive;
	bool			blurEnabled;
	float			blurRadius;
	BRect			blurRect;
	bool			blurBehind;
	int32			bufferGeneration;
	
	BRect			fullFootprintFrame;
	BRect			contentFrame;
	BReference<CompositorBuffer> buffer;
	uint32			serverToken;

	// Shadow properties
	float			shadowRadius;
	BPoint			shadowOffset;
	float			shadowOpacity;

	bool			isDirect; // true if window is in BDirectWindow mode

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
	bigtime_t		blurTime;
};

class Compositor {
public:
				Compositor();
				~Compositor();
	void			SetLogLevel(int32 logLevel);
	void			SetDebugOptions(bool showOverlay, bool logTimings,
						bool stressInvalidate);
	void			Compose(RenderingBuffer& dst, RenderingBuffer& backbuffer,
						const BRegion& dirty,
						const SurfaceList& surfaces,
						const rgb_color& background,
						bool trueShadows = false);

	void			ComposeTileGrid(RenderingBuffer& dst,
						RenderingBuffer& backbuffer,
						const SurfaceList& surfaces,
						TileDamageTracker* tracker,
						const rgb_color& background);

private:
	struct BlurCacheEntry {
		BlurCacheEntry();
		~BlurCacheEntry();

		uint32			surfaceToken;
		BRect			rect;
		int32*			pixels;
		int32			generation;
		int32			width;
		int32			height;
		int32			radius;
		RenderingBuffer* buffer;
	};

	struct ShadowCacheEntry {
		ShadowCacheEntry();

		uint32			surfaceToken;
		int32			bufferGeneration;
		float			radius;
		int32			width;
		int32			height;
		std::vector<uint8>
						mask;
	};

	void			_ComposeSingleTile(RenderingBuffer& dst,
						RenderingBuffer& backbuffer,
						int32 tx, int32 ty, const SurfaceList& surfaces,
						const rgb_color& background);

private:
	void					_ClearRegion(RenderingBuffer& dst, const BRegion& dirty,
											const rgb_color& background);
	void					_ClearRegion(RenderingBuffer& dst, const BRect& rect,
											const rgb_color& background);
	void			_CopyRegionTranslated(RenderingBuffer& dst, RenderingBuffer& src,
						const BRegion& region, const BPoint& translation);
	void			_BlendRegionTranslated(RenderingBuffer& dst, RenderingBuffer& src,
						const BRegion& region, float alpha,
						const BPoint& translation);

	int32				fLogLevel;
	bool				fShowOverlay;
	bool				fLogTimings;
	bool				fStressInvalidate;
};

#endif // COMPOSITOR_H
