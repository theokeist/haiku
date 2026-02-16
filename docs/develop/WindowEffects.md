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
