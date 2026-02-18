# Window Effects (Experimental)

## Overview
Haiku applications draw pixels into their `BView`s. The **app_server** then
composes each window onto the screen. Window effects (alpha, blur) are applied
at that compositor stage, so no changes to `BView`, `BRect`, or your drawing
code are required.

This document describes a **private/experimental** message-based API for
setting window effects. The API may change without notice.

These effects are **opt-in**. If no private message is sent, windows behave
exactly as before and no application changes are required.

## Summary (what/why/benefits)
**What:** A small, private message API allows setting per-window alpha and
blur in `app_server` without touching `BView`/`BRect` semantics.  
**Why:** Provide a simple experimentation path for compositor effects and
debugging, without impacting existing apps.  
**Benefits:** Live updates, no app changes required, and a clear separation
between drawing (apps) and compositing (system).

## What this does
- **Alpha**: make a window translucent by blending the window with the
  background.
- **Blur**: blur a small strip (currently the titlebar area) behind the window.

These are compositor effects; they do not modify any `BView` behavior.

## Getting a server window token
The effects API uses the server window token. For experimentation you can:

- Use the experimental demo app (see below), which obtains its own token.
- List window tokens with private APIs:
  - `BPrivate::get_window_order()` (from
    `headers/private/interface/WindowInfo.h`) provides the tokens for a
    workspace.
  - `get_window_info(token)` returns `client_window_info` including the window
    name so you can match a token to a window.

> Note: these are private APIs intended for development tools.

## Message-based API (private)
Message code: `AS_PRIVATE_SET_WINDOW_EFFECTS` (`'xWef'`).

Payload fields:
- `"window"` **int32** (required): server window token.
- `"alpha"` **float** (optional): window alpha in `0..1`.
- `"blur"` **bool** (optional): enable/disable blur.
- `"blur_radius"` **float** (optional): blur radius in pixels.
- `"animate"` **bool** (optional): animate alpha changes.
- `"duration"` **int64** (optional): animation duration in microseconds.

The reply `what` is `B_OK` on success or an error status on failure. The reply
also includes the clamped values:
- `"alpha"`, `"blur"`, and `"blur_radius"`.

## CLI tool
`setwindoweffects` sends the private message to `app_server`.

Examples:
```
setwindoweffects <token> --alpha 0.7 --animate 150ms
setwindoweffects <token> --blur on --radius 8
```

If the request succeeds, the tool prints the applied (clamped) values.

## Demo app
`WindowEffectsDemo` is a small sample app with sliders and checkboxes that
control **its own** window. It uses the same private message to `app_server`.

This demo is intended for developers experimenting with compositor effects.
It is included in the live image as `/boot/system/apps/WindowEffectsDemo`.

## Diagram
```
App (BWindow/BView)
        |
        | draws pixels
        v
  app_server window buffer
        |
        | AS_PRIVATE_SET_WINDOW_EFFECTS (opt-in)
        v
  compositor (alpha/blur)
        |
        v
      display
```

## Experimental status
This API is **private and experimental**. It may change, be renamed, or be
removed in future versions of Haiku.

## Correct-by-design boundaries
The current design intentionally limits scope so experiments are safer:

- Effects are applied only in `app_server` composition and do not alter app
  drawing APIs.
- Effects are opt-in via a private message; existing apps remain unchanged.
- Values are clamped server-side (`alpha`, `blur_radius`) before use.
- Animation control is explicit (`animate`, `duration`) instead of implicit.
- Internal debug switches are separated from the private per-window API.

These boundaries make it easier to test behavior incrementally without
redefining Interface Kit contracts.

## Black gaps to close before calling this production-ready
The following gaps are expected in this experimental phase and should be tracked
explicitly during testing:

1. **API stability gap**
   - The message contract is private and can break across revisions.
   - No compatibility promises for tools using server window tokens.

2. **Security / ownership gap**
   - Token-based targeting is suitable for dev tooling, but not a hardened
     cross-team policy surface.
   - Authorization model for mutating effects on foreign windows is not defined
     as a public contract.

3. **Visual quality gap**
   - Blur scope is currently narrow (titlebar-style strip, policy-dependent),
     not a general region/effect graph pipeline.
   - Quality and kernel choices are implementation-driven and not yet exposed as
     stable quality tiers.

4. **Performance predictability gap**
   - CPU cost can scale with dirty area and blur radius.
   - Hardware acceleration path parity and fallback consistency need broader
     device coverage testing.

5. **Behavioral consistency gap**
   - Policy heuristics (for example title/feel based blur behavior) may differ
     across app/window types.
   - Multi-monitor/workspace transitions need targeted validation for animation,
     clipping and present timing.

6. **Observability / testability gap**
   - Logging and overlay exist, but structured, repeatable benchmarks and
     pass/fail thresholds are not yet codified.
   - No dedicated regression suite currently validates alpha/blur correctness
     end-to-end.

## Suggested experimental validation matrix
To turn the feature into a robust experiment, validate each release against a
minimal matrix:

- **Functional:** alpha set/animate; blur enable/disable; radius clamping;
  invalid token rejection.
- **Policy:** normal vs floating windows; Deskbar/notification heuristics;
  forced debug overrides.
- **Performance:** low/high dirty region; low/high blur radius; stress-invalidate
  on/off; target FPS at 30/60/120+.
- **Stability:** rapid toggle loops, workspace switches, screen mode changes,
  app_server restart behavior.
- **Hardware diversity:** software fallback path and accelerated path where
  available.

Record results with compositor logs and screenshots so regressions are visible
over time.

## Status vs. target architecture diagrams
Current implementation status compared with the intended architecture:

- **Implemented now:** window snapshots with per-window alpha/blur state, dirty
  region based composition, present queue buffering, and runtime debug controls.
- **Partially implemented:** blur quality/performance tuning and hardware-path
  parity across accelerants.
- **Not implemented yet:** a public stable API, strict ownership policy for
  cross-team effect mutation, and benchmark-driven pass/fail CI for visual
  correctness/performance.

Treat this as a developer experiment track: verify behavior with logs/overlay and
repeatable scenario matrices before widening scope.


### New policy controls (experimental)
The compositor policy heuristics are now configurable from the Compositor
preferences/settings file instead of being fully hardcoded:

- `enable_title_blur_policy` (bool)
- `enable_floating_untitled_blur_policy` (bool)
- `blur_policy_tokens` (comma-separated strings)

This keeps the Deskbar/notification blur behavior available while making it
possible to tune or disable fragile title-based matching for testing.

### ARGB/RGBA alpha input in preferences
Compositor preferences now accepts an optional forced-opacity color text input
for alpha channel driven testing:

- `AARRGGBB` (default alpha interpretation)
- `RRGGBBAA` when annotated as `rgba`

When valid, the parsed alpha is used as forced opacity override.


## Settings API additions (experimental)
The user settings file `~/config/settings/system/app_server/compositor_settings`
accepts the following additional keys for blur policy and alpha testing:

- `enable_title_blur_policy` (bool, default `true`)
- `enable_floating_untitled_blur_policy` (bool, default `true`)
- `blur_policy_tokens` (string, default `"deskbar,notification,notify"`)
- `force_opacity_color` (string, optional)

### `force_opacity_color` format
`force_opacity_color` is parsed only when forced opacity override is enabled.
Supported formats:

- `AARRGGBB` (default, alpha in first byte)
- `RRGGBBAA` when the text includes the marker `rgba`

Examples:

- `80FFFFFF` -> alpha `0x80` (`~0.50`)
- `FFFFFF80 rgba` -> alpha `0x80` (`~0.50`)

When parsing succeeds, the alpha channel becomes the effective forced-opacity
value used by compositor policy.

## Appearance/Colors preferences alpha entry (developer workflow)
Appearance > Colors now supports text entry for color values with alpha channel
for faster testing of translucent UI colors:

- accepts `AARRGGBB`
- accepts `RRGGBBAA` with `rgba` marker

The color picker continues to control RGB channels; alpha is preserved from the
current selected color unless updated through text input.


## UI color propagation and alpha behavior
### Does Appearance > Colors propagate to windows?
Yes. UI color changes are sent to `app_server`, applied to desktop settings,
and broadcast to application windows/clients via color-updated notifications.
This updates standard Interface Kit colors used by controls, tabs, borders,
and other UI rendering paths.

### Does changing a UI color alpha make windows translucent?
Not by itself.

- UI color alpha affects rendering only where drawing code consumes that alpha.
- Global window translucency/blur is controlled by compositor effect policy and
  per-window effect state, not automatically by changing a UI color value.
- To make a window visibly translucent behind content, use compositor effects
  (`AS_PRIVATE_SET_WINDOW_EFFECTS`, force opacity settings, or policy rules)
  with compositor translucency enabled.

### Recommended test flow
1. Change a UI color in Appearance > Colors (optionally with alpha text input).
2. Verify controls/decorator/UI elements adopt the updated color values.
3. Separately apply window alpha/blur effects via compositor APIs/tools.
4. Validate final visual result with compositor overlay/logging enabled.

## Private API tutorial (end-to-end)
This section is a practical, developer-only walkthrough for testing the new
experimental features.

### 1) Enable compositor feature flags
Open `Compositor` preferences and enable (as needed):

- `Enable blur for system windows`
- `Enable translucency for normal windows`
- optional policy/debug toggles (`force_blur_all`, overlay/log timings)

Equivalent settings file keys are in
`~/config/settings/system/app_server/compositor_settings`.

### 2) Get a target window token
Use private window-info APIs (or the demo app) to identify the server window
`token` for a target window.

### 3) Apply effects from CLI (quickest path)
```sh
setwindoweffects <token> --alpha 0.80 --animate 150ms
setwindoweffects <token> --blur on --radius 8
```

### 4) Apply effects from private C++ message API
```cpp
#include <Application.h>
#include <Message.h>
#include <Messenger.h>
#include <private/app/ServerProtocol.h>

status_t
SetEffects(int32 token)
{
    BMessenger appServer("application/x-vnd.Haiku-app_server");
    if (!appServer.IsValid())
        return B_NAME_NOT_FOUND;

    BMessage request(AS_PRIVATE_SET_WINDOW_EFFECTS);
    request.AddInt32("window", token);
    request.AddFloat("alpha", 0.80f);
    request.AddBool("blur", true);
    request.AddFloat("blur_radius", 8.0f);
    request.AddBool("animate", true);
    request.AddInt64("duration", 150000); // us

    BMessage reply;
    status_t status = appServer.SendMessage(&request, &reply);
    if (status != B_OK)
        return status;

    return (status_t)reply.what; // B_OK on success
}
```



### 4b) Integrate the private API directly in your app window
If you want to apply effects from your own app (instead of using CLI tools),
use the window's server token and send `AS_PRIVATE_SET_WINDOW_EFFECTS`.

```cpp
#include <Application.h>
#include <Message.h>
#include <Messenger.h>
#include <Window.h>

#include <private/app/ServerProtocol.h>

extern "C" status_t _safe_get_server_token_(const BLooper*, int32*);

class EffectsWindow : public BWindow {
public:
    EffectsWindow()
        : BWindow(BRect(100, 100, 460, 320), "Effects App",
            B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
          fToken(B_NULL_TOKEN),
          fAppServer("application/x-vnd.Haiku-app_server")
    {
    }

    status_t SetWindowEffects(float alpha, bool blur, float blurRadius,
        bool animate = true, bigtime_t duration = 150000)
    {
        if (!fAppServer.IsValid())
            return B_NAME_NOT_FOUND;

        if (fToken == B_NULL_TOKEN)
            _safe_get_server_token_(this, &fToken);
        if (fToken == B_NULL_TOKEN)
            return B_BAD_VALUE;

        BMessage request(AS_PRIVATE_SET_WINDOW_EFFECTS);
        request.AddInt32("window", fToken);
        request.AddFloat("alpha", alpha);
        request.AddBool("blur", blur);
        request.AddFloat("blur_radius", blurRadius);
        if (animate) {
            request.AddBool("animate", true);
            request.AddInt64("duration", duration);
        }

        BMessage reply;
        status_t status = fAppServer.SendMessage(&request, &reply);
        if (status != B_OK)
            return status;

        return (status_t)reply.what;
    }

private:
    int32      fToken;
    BMessenger fAppServer;
};
```

Notes:
- This applies effects to **this window only** (its own server token).
- Keep this behind developer/experimental flags in production apps.
- The private API can change without compatibility guarantees.

### 5) Tune policy instead of hardcoding title checks
The following settings let you tune behavior without recompiling:

- `enable_title_blur_policy`
- `enable_floating_untitled_blur_policy`
- `blur_policy_tokens`

Example value:
```text
blur_policy_tokens = "deskbar,notification,notify"
```

### 6) Test alpha-channel input in preferences
#### Compositor prefs
`force_opacity_color` accepts:

- `AARRGGBB` (default)
- `RRGGBBAA` when input includes `rgba`

Examples:
- `80FFFFFF`
- `FFFFFF80 rgba`

#### Appearance > Colors
Color text field supports the same formats for UI color alpha testing.

### 7) Validate behavior
- Visual check with normal windows + desklets.
- Enable compositor overlay/log timings for performance and cache diagnostics.
- Verify UI-color alpha changes and compositor window alpha as separate axes.

> Reminder: all APIs here are private/experimental and may change without
> compatibility guarantees.
