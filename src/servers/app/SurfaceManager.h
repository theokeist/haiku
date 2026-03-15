/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#ifndef SURFACE_MANAGER_H
#define SURFACE_MANAGER_H

#include <vector>
#include <algorithm>
#include "Compositor.h"
#include "Window.h"
#include "ServerWindow.h"
#include "TileDamageTracker.h"

class SurfaceManager {
public:
	static SurfaceList CreateSurfaceList(const std::vector<WindowSnapshot>& snapshots)
	{
		SurfaceList list;
		for (size_t i = 0; i < snapshots.size(); i++) {
			const WindowSnapshot& snap = snapshots[i];
			Surface s;
			
			s.bounds = snap.fullFootprintFrame;
			s.translation = snap.translation;
			
			s.bufferRef = snap.buffer;
			s.buffer = s.bufferRef.IsSet() ? s.bufferRef->Buffer() : NULL;
			s.alpha = snap.alpha;
			s.isOpaque = snap.opaqueFastPath;
			s.zOrder = (int32)i;
			s.skipDrawing = false;

			// Handle BDirectWindow: if it's currently accessing the frame buffer,
			// the compositor MUST NOT draw over it. We treat it as an opaque
			// occluder that we skip drawing for.
			if (snap.isDirect) {
				s.isOpaque = true;
				s.alpha = 1.0f;
				s.skipDrawing = true;
				s.opaqueRegion.Set(s.bounds);
			} else if (s.isOpaque && s.alpha >= 1.0f) {
				// Occlusion: logical content frame only
				s.opaqueRegion.Set(snap.contentFrame);
			} else {
				s.opaqueRegion.MakeEmpty();
			}
			
			// The damage region provided by the snapshot is in global coordinates
			s.damage = snap.visible;
			
			s.surfaceToken = snap.serverToken;
			s.bufferGeneration = snap.bufferGeneration;
			
			// Content changed if current generation != last known generation
			s.contentChanged = snap.bufferGeneration != snap.retained.surfaceGeneration;

			s.blurEnabled = snap.blurEnabled;
			s.blurRadius = snap.blurRadius;
			s.blurRect = snap.blurRect;
			s.shadowRadius = snap.shadowRadius;
			s.shadowOffset = snap.shadowOffset;
			s.shadowOpacity = snap.shadowOpacity;

			// Pre-calculate tile bounds (Stage 8)
			BRect visualFrame = s.bounds;
			s.tileX0 = std::max(0, (int32)visualFrame.left >> TILE_SHIFT);
			s.tileY0 = std::max(0, (int32)visualFrame.top >> TILE_SHIFT);
			s.tileX1 = (int32)visualFrame.right >> TILE_SHIFT;
			s.tileY1 = (int32)visualFrame.bottom >> TILE_SHIFT;

			list.push_back(s);
		}
		return list;
	}
};

#endif // SURFACE_MANAGER_H
