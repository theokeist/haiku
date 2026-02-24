wd# Haiku Composition & Windowing System Overview

**Last Updated:** February 2026  
**Scope:** Desktop rendering, window management, compositor architecture  
**Audience:** Developers working on graphics, windowing, or desktop features

---

## Architecture Overview

Haiku's windowing system follows a **server-centric architecture** where a central `app_server` daemon manages all windows, rendering, and composition. This design separates graphics rendering from application logic and enables sophisticated window management features.

### System Stack

```
┌──────────────────────────────────────────────────────────────┐
│ User Applications (BApplication + BWindow)                   │
├──────────────────────────────────────────────────────────────┤
│ Client-side messaging (via Ports & BMessenger)               │
├──────────────────────────────────────────────────────────────┤
│ app_server (AppServer daemon: src/servers/app/)              │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐ │
│  │ Desktop (per display/screen)                           │ │
│  │  ├─ Window management & focus                          │ │
│  │  ├─ Event dispatch (keyboard, mouse)                   │ │
│  │  ├─ Workspace management (4-32 virtual desktops)      │ │
│  │  ├─ Decorator management (window chrome)              │ │
│  │  └─ Compositor (new rendering pipeline)               │ │
│  └────────────────────────────────────────────────────────┘ │
│                                                              │
│  Per-Application Objects:                                   │
│  ├─ ServerApp (connection to client app)                   │ │
│  └─ ServerWindow (per-window state & drawing)             │ │
│                                                              │
│  Global Services:                                          │ │
│  ├─ BitmapManager                                         │ │
│  ├─ GlobalFontManager                                     │ │
│  ├─ ScreenManager (multi-monitor)                         │ │
│  └─ InputManager (keyboard/mouse hardware events)         │ │
├──────────────────────────────────────────────────────────────┤
│ Hardware Abstraction (HWInterface, DrawingEngine)            │
│  ├─ Framebuffer/VRAM access                                 │
│  ├─ Accelerator drivers                                     │
│  └─ Cursor management                                       │
├──────────────────────────────────────────────────────────────┤
│ Kernel (IPC ports, memory areas, interrupts)                 │
└──────────────────────────────────────────────────────────────┘
```

---

## Key Components

### 1. AppServer (Main Server Process)

**File:** [src/servers/app/AppServer.cpp](src/servers/app/AppServer.cpp)

Initializes and manages the global app_server singleton:
- **Lifecycle**: Registers with system, loads compositor/alpha debug settings  
- **Multi-Desktop support**: Can run multiple `Desktop` objects (one per user/screen)  
- **Compositor settings**: Loads from `system/app_server` preferences  
- **Debug infrastructure**: Alpha transparency debug mode, compositor debug overlay

```cpp
AppServer {
  BObjectList<Desktop>  fDesktops;           // per-display desktops
  CompositorSettings    fCompositorSettings; // global render config
  BMessageRunner*       fCompositorDebugRunner;
}
```

**Key Methods:**
- `_CreateDesktop()` — Creates a new Desktop for a user/screen combination
- `_FindDesktop()` — Looks up existing Desktop by user ID
- `ApplyCompositorSettings()` — Applies render settings to all desktops
- `SetCompositorDebugOptions()` — Enables debug overlay/timing logs

### 2. Desktop (Per-Display Manager)

**File:** [src/servers/app/Desktop.h/cpp](src/servers/app/Desktop.h)

Central hub for a single display's window management:
- **Window lists**: All windows, focus stack, per-workspace windows  
- **Event dispatch**: Routes keyboard/mouse events to focused window  
- **Workspace support**: Manages 1–32 virtual desktops  
- **Rendering**: Coordinates with Compositor for screen updates  
- **Decoration**: Manages window borders/title bars via DecorManager  

```cpp
Desktop : public MessageLooper {
  WindowList              fAllWindows;        // all windows on this desktop
  WindowList              fFocusList;         // Z-order stack
  ::Workspace::Private    fWorkspaces[32];    // virtual workspaces
  int32                   fCurrentWorkspace;  // active workspace
  ::Compositor            fCompositor;        // rendering pipeline
  DecorManager*          fDecorManager;       // window borders
  EventDispatcher        fEventDispatcher;    // input event routing
  ::VirtualScreen        fVirtualScreen;      // multi-monitor support
}
```

**Key Methods:**
- `AddWindow()` / `RemoveWindow()` — Window lifecycle  
- `SetWindowFocus()` — Updates input focus & Z-order  
- `Redraw()` — Triggers compositor to update screen  
- `BringWindowToFront()` / `SendWindowToBack()` — Z-order modification  
- `SwitchWorkspace()` — Changes active virtual desktop

### 3. ServerApp (Per-Application State)

**File:** [src/servers/app/ServerApp.h/cpp](src/servers/app/ServerApp.h)

Manages connection to a single client application:
- **Window list**: Tracks all windows created by this app  
- **Resource management**: Bitmaps, cursors, fonts  
- **Message routing**: Forwards window messages to client  
- **Memory allocator**: Tracks app's shared memory usage  

```cpp
ServerApp {
  Desktop*              fDesktop;           // parent display
  BObjectList<ServerWindow> fWindowList;   // windows owned by this app
  ServerApp*            fAppCursor;         // per-app cursor override
  port_id               fMessagePort;       // receives messages from client
  team_id               fClientTeam;        // client process ID
}
```

**Client Connection Protocol:**
- **Client-side**: `BApplication` creates a connection to `app_server`  
- **Message exchange**: Uses Haiku's port-based IPC (high-performance)  
- **MemoryAllocator**: Shared memory for bitmap/font data  

### 4. ServerWindow (Per-Window State)

**File:** [src/servers/app/ServerWindow.h/cpp](src/servers/app/ServerWindow.h)

Represents a single window from the server's perspective:
- **Window content**: Tracks what needs redrawing (dirty regions)  
- **View hierarchy**: Server-side representation of BView tree  
- **Message dispatch**: Handles drawing commands from client  
- **Event target**: Routes mouse/keyboard events from Desktop  

```cpp
ServerWindow : public MessageLooper {
  ::Window*          fHaikuWindow;      // server-side Window object
  ::Desktop*         fDesktop;          // parent display
  ::ServerApp*       fServerApp;        // parent application
  BMessages*         fMessagePort;      // receives drawing commands
  ::DrawingEngine*   fDrawingEngine;    // rendering backend
}
```

### 5. Window (Rendering & Display State)

**File:** [src/servers/app/Window.h](src/servers/app/Window.h)

Encapsulates rendering state for a single window:
- **Frame & clipping**: Window position, size, and visible region  
- **Z-order & workspaces**: Window stacking and visibility  
- **Dirty region**: Tracks what part of framebuffer needs updating  
- **Decoration**: Borders/title bar managed by Decorator  
- **Effect properties**: Alpha, blur, animation state  

```cpp
class Window {
  BRect             fFrame;           // window position/size
  BRegion           fVisibleRegion;   // actually visible pixels (after clipping)
  float             fAlpha;           // transparency [0.0..1.0]
  bool              fAlphaAnimActive; // animating alpha change
  bool              fBlurEnabled;     // background blur effect
  float             fBlurRadius;      // blur amount
  ::Decorator*      fDecorator;       // window chrome renderer
  ::DrawingEngine*  fDrawingEngine;   // framebuffer access
  View*             fTopView;         // root of client's view tree
}
```

**Key Methods:**
- `SetClipping()` — Updates visible region (called when overlaps change)  
- `ProcessDirtyRegion()` — Schedules redraw for dirty parts  
- `SetAlpha()` / `SetBlurRadius()` — Effect properties  
- `MoveBy()` / `ResizeBy()` — Updates position/size  

### 6. Compositor (Modern Rendering Pipeline)

**File:** [src/servers/app/Compositor.h](src/servers/app/Compositor.h)  
**Status:** Recent addition (2025+) — modern rendering architecture

Combines window layers into final framebuffer:
- **Layered rendering**: Composites windows in Z-order  
- **Per-window effects**: Alpha blending, blur-behind  
- **Region-based updates**: Only redraws dirty areas  
- **Optimization**: Caches blur operations, tracks statistics  
- **Debug overlay**: Shows performance metrics & window bounds  

```cpp
struct WindowSnapshot {
  BRegion     visible;        // window's visible region
  float       alpha;          // transparency
  bool        opaqueFastPath; // skip blending for opaque windows
  bool        animActive;     // animating alpha/position
  bool        blurEnabled;    // enable blur-behind
  float       blurRadius;     // blur strength
  Window*     window;         // reference to Window object
}

class Compositor {
  ComposeStats Compose(
    RenderingBuffer& dst,     // target (framebuffer)
    RenderingBuffer& src,     // temporary buffer
    const BRegion& dirty,     // dirty region to redraw
    const vector<WindowSnapshot>& snapshots,  // all windows
    const rgb_color& bg       // background color
  ) const;
  
  // Compositing operations:
  _ClearRegion();        // Fill with background color
  _CopyRegion();         // Opaque window blit
  _BlendRegion();        // Alpha-blended window
  _BlurRegion();         // Gaussian blur effect
  _DrawDebugOverlay();   // Performance visualization
}
```

**Rendering Pipeline:**
1. **Desktop requests redraw** for dirty region  
2. **Each window's drawing commands** are executed to temporary buffer  
3. **Compositor receives window snapshots** with visibility & alpha info  
4. **Composite operation:**
   - Start with background color (cleared dirty region)  
   - For each window (bottom-to-top in Z-order):  
     - If opaque & fully visible: direct copy  
     - If alpha/transparent: blend with background  
     - If blur-enabled: Gaussian blur on intersection with background  
5. **Present to framebuffer** (hardware accelerated if available)  

### 7. RenderingBuffer (Abstraction Layer)

**File:** [src/servers/app/RenderingBuffer.h](src/servers/app/RenderingBuffer.h)

Platform-neutral pixel buffer interface:
- **Color space**: Supports RGBA32, RGB16, grayscale, indexed color  
- **Stride**: Row-based access with BytesPerRow()  
- **Bits**: Direct framebuffer access for rendering  

```cpp
class RenderingBuffer {
  virtual color_space ColorSpace() const;
  virtual void*       Bits() const;              // pixel data pointer
  virtual uint32      BytesPerRow() const;       // stride
  virtual uint32      Width() const;             // pixels per line
  virtual uint32      Height() const;            // number of lines
}
```

Implementations:
- **Direct framebuffer**: Hardware video memory access  
- **Memory bitmap**: Software buffer (offscreen rendering)  
- **DirectWindow**: Full-screen app temporary buffer  

### 8. DrawingEngine (Hardware Interface)

**File:** [src/servers/app/DrawingEngine.h](src/servers/app/DrawingEngine.h)

Backend for rasterization operations:
- **BitBlt**: Copy regions between buffers  
- **Fill**: Draw rectangles, regions  
- **Fonts**: Render text  
- **Hardware acceleration**: Uses AccelerantHWInterface when available  

---

## Rendering Pipeline (Detailed)

### Window Update Flow

```
Client Application (BWindow/BView)
    ↓
    RequestUpdate() or Invalidate() message sent via port
    ↓
ServerWindow receives B_UPDATE_REQUEST
    ↓
MarkDirty() or MarkContentDirty() adds region to dirty set
    ↓
Desktop::Redraw() schedules compositor run
    ↓
Window::ProcessDirtyRegion() determines what to redraw
    ↓
Client's View::Draw() executed in server's drawing thread
    ↓
Drawing commands rendered to WindowBuffer (temp bitmap)
    ↓
Compositor::Compose() combines all window buffers
    ↓
Result blitted to hardware framebuffer
    ↓
Hardware refresh (vsync-synchronized)
```

### Multi-Monitor Support

**VirtualScreen** manages multiple physical monitors:
- **Screen objects**: Each physical monitor is a Screen  
- **Virtual coordination**: Apps can span monitors  
- **Independent desktops**: Optional per-monitor workspaces  
- **Cursor tracking**: Moves between screens seamlessly  

### Event Dispatch

```
Input Hardware (keyboard, mouse, touchpad)
    ↓
InputManager receives raw events (kernel driver)
    ↓
EventDispatcher (in Desktop) routes to focused window
    ↓
ServerWindow::_MessageLooper() queues B_MOUSE_*/ B_KEY_* messages
    ↓
Client's BWindow::DispatchMessage() processes in app thread
```

---

## Windowing Features

### Window Properties

| Property | Implementation | Notes |
|----------|---|---|
| **Look** | Window::window_look enum | Bordered, floating, modal, sheet, etc. |
| **Feel** | Window::window_feel enum | Normal, modal, floating window behavior |
| **Flags** | Window::window_flags bitmask | Resizable, closable, minimizable, Z-order |
| **Workspace** | uint32 workspace mask | Which virtual desktops show window |
| **Frame** | BRect | Position & size on screen |
| **Title** | String | Window caption/name |
| **Alpha** | float [0.0–1.0] | Transparency (0=invisible, 1=opaque) |
| **Blur** | float radius | Gaussian blur of content behind window |

### Decorators (Window Chrome)

**File:** [src/servers/app/decorator/](src/servers/app/decorator/)

Pluggable modules render window borders, title bar, buttons:
- **Default**: SATDecorator (Haiku's native look)  
- **Custom**: Can be replaced via preferences  
- **Window behavior**: DecorManager allocates WindowBehaviour for interactions  
- **Settings**: Persisted in `system/app_server/decorator_settings`  

### Workspace Management

Each Desktop supports 1–32 virtual desktops:
- **Workspace switching**: Desktop::SwitchWorkspace()  
- **Window assignment**: Windows visible on subset of workspaces  
- **Focus tracking**: Last-focused window per workspace remembered  
- **Deskbar integration**: Shows workspace switcher/preview  

### StackAndTile (Window Grouping)

**File:** [src/servers/app/StackAndTile.h](src/servers/app/StackAndTile.h)

Advanced window snapping & docking:
- **Magnetic edges**: Windows snap when dragged near others  
- **Tab groups**: Multiple windows can share title bar  
- **Tiling layouts**: Auto-arrange windows in grid/rows  
- **Persistent state**: Group layout saved/restored  

---

## Current Issues & TODOs

### Graphics Test Code (12+ TODOs)

**High-density**: [src/tests/servers/app/painter/Painter.cpp](src/tests/servers/app/painter/Painter.cpp) (12 TODOs)  
**Issues**:
- Painter test coverage incomplete  
- Redraw semantics for nested views unclear  
- Clipping test cases (newerClipping, newClipping) have unfinished assertions  
- Deadlock scenarios untested  

### Compositor Validation (Recent)

**Note**: Compositor is newly implemented (2025+), validation ongoing:
- Per-window blur caching may have edge cases  
- Multi-threaded composition locking under review  
- DirectWindow (full-screen) integration needs testing  

### Known Limitations

1. **No GPU composition** — Compositor is CPU-based  
2. **No vsync control** — Display updates not synchronized to refresh rate  
3. **No window damage tracking** — Full redraw on any update (inefficient for large windows)  
4. **Limited animation** — Alpha can animate, position/size cannot (yet)  
5. **Decorator hot-swap** — Can't change decorator without restart  

---

## Common Development Tasks

### Adding a New Window Effect

1. **Add property to Window class** (src/servers/app/Window.h)  
2. **Add getter/setter methods** with thread-safe locking  
3. **Extend WindowSnapshot** to capture effect state  
4. **Implement in Compositor::_Compose()** method  
5. **Add debug visualization** in `_DrawDebugOverlay()`  
6. **Test with test harness** (src/tests/servers/app/playground/)  

### Debugging Rendering Issues

**Enable debug output:**
```cpp
// In Desktop/AppServer code:
desktop->SetCompositorDebugOptions(CompositorDebugOptions(
  true,  // showOverlay
  true,  // logTimings
  false  // stressInvalidate
));

// In preferences: /boot/home/config/settings/system/app_server/debug_options
[Debug]
show_compositor_overlay=true
log_compositor_timings=true
```

**Overlay shows:**
- Dirty region (red)  
- Per-window bounds (colored outlines)  
- Composition stats (FPS, pixel count, blur cache hits)  

### Profiling Composition

**ComposeStats** structure captures metrics:
```cpp
dirtyRects       — number of rectangular regions redrawn
dirtyPixels      — total pixels affected
windowsComposed  — windows included in composition
alphaWindows     — windows with alpha blending
blurredWindows   — windows with blur-behind effect
blurCacheHits    — cached blur rasterizations reused
blurTime         — nanoseconds spent on blur operations
composeTime      — total composition time
```

---

## Architecture Decisions

### Why Server-Centric?

1. **Security**: Untrusted apps can't directly access hardware  
2. **Compositing**: Global window blending requires server visibility  
3. **Focus management**: Single dispatcher prevents race conditions  
4. **Resource quotas**: Server can enforce allocation limits  
5. **Hotspot debugging**: Centralized logging/profiling point  

### Why Port-Based IPC?

- **Performance**: Optimized for rapid message passing (vs network sockets)  
- **Simplicity**: No network complexity, local-only communication  
- **Synchronization**: Built-in message ordering & reply mechanisms  
- **Compatibility**: Matches BeOS/Haiku design heritage  

### Why Region-Based Invalidation?

- **Efficiency**: Only redraw changed areas, not entire window  
- **Granularity**: Can track overlapping views separately  
- **Compositing**: Compositor knows exactly what needs updating  

---

## See Also

- [Compositor Debug Options](src/servers/app/CompositorDebugOptions.h)  
- [Window Protocol](headers/private/app/ServerProtocol.h)  
- [API: BWindow, BView](src/kits/app/) — Client-side classes  
- [Decorator Reference](src/servers/app/decorator/Decorator.h)  
- [VirtualScreen Multi-Monitor](src/servers/app/VirtualScreen.h)  

