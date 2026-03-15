# Haiku Compositor Layer

Internal CPU-based compositor for `app_server`.

## Architecture: The Surface-Centric Model
The Haiku compositor uses a **Surface-Centric** architecture. This decouples the rendering logic from the high-level Window Manager, allowing the compositor to treat windows, cursors, and effects as uniform compositable units.

### Pipeline
**Applications** -> **Window Drawing** -> **WindowSnapshots** -> **SurfaceManager** -> **SurfaceList** -> **Compositor** -> **Framebuffer**

1.  **Desktop/WM:** Tracks window state and damage, generating `WindowSnapshots`.
2.  **SurfaceManager:** Translates snapshots into a z-sorted `SurfaceList`. It resolves cursors and system overlays into standard surfaces.
3.  **Compositor:** Operates purely on `Surface` objects. It is unaware of `ServerWindow` or `Desktop` logic.

## Core Abstractions

### Surface
A lightweight descriptor representing a compositable layer of content.
- **Buffer:** Pointer to pixel data (can be individual window buffers or a shared backbuffer).
- **Bounds:** Global screen-space coordinates.
- **Alpha:** Global transparency (0.0 to 1.0).
- **isOpaque:** Fast-path flag for windows that fully cover the pixels behind them.
- **Damage:** The specific region of the surface that needs to be redrawn in the current frame.

### SurfaceManager
The translation layer responsible for:
- Building the `SurfaceList` from snapshots.
- Injecting the **Cursor Surface** at the highest Z-order.
- Sorting surfaces for correct Painter's algorithm order.

## Damage Propagation & Composition
The compositor uses a two-pass algorithm to minimize CPU work:

1.  **Pass 1: Top-Down Occlusion Culling (Damage Pruning)**
    - Iterates from top to bottom.
    - Each surface intersects the current `propagatedDamage` with its own `bounds` to determine its target `damage` region.
    - If a surface is **opaque**, it `Excludes` its bounds from the `propagatedDamage` passed to the layers below.
    - This ensures that pixels hidden by opaque windows (like the Taskbar or a maximized window) are never processed.

2.  **Pass 2: Bottom-Up Rendering**
    - Iterates from bottom to top.
    - Renders only the calculated `damage` region for each surface.
    - Uses **SSE2 SIMD** for high-performance alpha blending and solid fills.

## Features
- **CPU-based alpha blending:** SSE2 SIMD optimized for 32-bit (RGBA) buffers.
- **Window Drop Shadows:** Rendered as expanded surface damage (Normal: 15px radius, Floating: 8px radius).
- **Blur-behind:** Cached Gaussian blur for translucent system surfaces.
- **Surgical Cursor Redraws:** Cursor movement only recomposes the union of the old and new cursor frames.
- **Triple-Buffering:** Ensures thread safety between the Window Manager thread and the Present Thread.

## Settings
Persistent settings are stored in `/boot/home/config/settings/system/app_server/compositor_settings`.

### Debug Controls (`debug_controls` bool)
Enables/disables the `Ctrl+Shift+Option` + Mouse Wheel shortcut to manually adjust window transparency.

### System Alpha (`system_alpha` float)
Controls the persistent transparency level for system elements (Deskbar, Notifications). Default is `0.85`.

## Performance Notes
- **Occlusion Culling:** The Top-Down pass is critical for CPU efficiency. Moving a translucent window over an opaque one will not trigger redraws for the hidden pixels.
- **SSE2:** Required for optimal performance. The system falls back to C++ if SSE2 is unavailable.

## Implementation Files
- `src/servers/app/Compositor.cpp/h`: Core surface composition and SIMD loops.
- `src/servers/app/SurfaceManager.h`: Logic for converting snapshots to surfaces.
- `src/servers/app/drawing/HWInterface.cpp/h`: Triple-buffered state and PresentThread loop.
- `src/servers/app/Desktop.cpp/h`: High-level damage tracking.

## Changelog

### Phase 1: Surface-Centric Model (Implemented)
- Migrated from `WindowSnapshot` based rendering to a unified `SurfaceList` architecture.
- Decoupled window frame logic from rendering logic, allowing for visual-only translations.
- Introduced `SurfaceManager` to handle the translation of window state to compositable layers.

### Phase 2: Zero-Latency Interactivity (Implemented)
- Added `fVisualTranslation` to the `Window` class, enabling immediate visual feedback during dragging.
- Logical `MoveBy` operations are now deferred until the drag operation is committed, reducing app-server message traffic.

### Phase 3: Advanced Damage Propagation (Implemented)
- Implemented a two-pass composition algorithm (Top-Down Occlusion followed by Bottom-Up Rendering).
- Optimized occlusion culling to ignore pixels hidden by opaque window content (but not decorators).
- Integrated **SSE2 SIMD** loops for high-performance alpha blending and solid fills.

### Phase 4: Thread-Safe Triple Buffering (Implemented)
- Introduced a 3-slot state exchange (`fCompositorStates`) in `HWInterface` to safely pass surface lists between the Desktop and Present threads.
- Eliminated locking contention between rendering and state updates.

### Phase 5: Gaussian Blur & Shadow Optimization (Implemented)
- Added support for cached Gaussian blur (Blur-behind) using a surface-token based cache.
- Implemented **True Shadows** (real Gaussian blur) as an optional compositor feature.
- Optimized cursor redraws by treating the cursor as a high-priority compositor surface.

### Phase 6: Opaque Fast Path (Implemented)
- Added an optimized rendering path for cases where the top-most window is fully opaque and covers the dirty region.
- Skips alpha blending and uses direct memory copies to reduce CPU overhead.

## Future Phases (TODO)

### Phase 7: Retained Surface Management
- Implement lifecycle tracking for window buffers (`RetainedSurfaceMetadata`).
- Cache pre-rendered surface components (like shadows or blurred backgrounds) across frames.

### Phase 8: Shared Memory Buffer Sharing
- Optimize window buffer allocation to use shared memory where possible, reducing the need for intermediate copies.

### Phase 9: Hardware Acceleration Path
- offload heavy composition tasks (blur, alpha blending) to the GPU using the Accelerant API.

