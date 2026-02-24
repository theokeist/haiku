# Present Queue / Compositor risk checklist

The recent app_server rendering changes are **not** risk-free. They can still break in three broad ways:

1. **Build-time integration**
   - New fields/methods crossing `PresentQueue`, `HWInterface`, and `Compositor` can drift out of sync.
   - New CLI/UI plumbing (`setwindowalpha`, Appearance settings, app_server messages) can break if message constants or includes diverge.

2. **Runtime synchronization**
   - The `PresentQueue::PresentNext()` lock-snapshot/present split reduces lock hold time, but can still regress if buffer lifetime assumptions change.
   - Coalesced invalidation (`fPendingInvalidate`) depends on wakeup/flush ordering; missed signals can stall visual updates.

3. **Behavior/performance regressions**
   - 32-bit memcpy fast-path and forced opaque clear alpha can change blending/composition results for edge cases.
   - Triple-buffer and compositor paths staying side-by-side can regress if one path stops being exercised.

## Does this branch make future Gaussian effects harder?

**Short answer: slightly yes, but not fundamentally.**

What helps:
- Compositing is now centralized in `Compositor::Compose()`, so blur/effects logic has a single insertion point.
- Dirty-region coalescing means expensive effects can run once per burst instead of once per invalidation.

What makes Gaussian support harder:
- The current blur path is region-local and copies each dirty rect into a temporary buffer before filtering. True Gaussian kernels are neighborhood-sensitive across rect borders, so this tiling model can cause seams unless rects are expanded/padded.
- `PresentQueue` keeps only the latest ready frame while unioning dirty regions. That is fine for correctness, but it limits opportunities for temporal reuse/history-based optimizations often used by heavier blur pipelines.
- Forcing output alpha to 255 in clear/blend paths is correct for opaque scanout, but it removes alpha history that some multi-pass effect pipelines may want to preserve internally.

Net: the branch is a good base for adding Gaussian effects, but you will likely need a larger offscreen working area (dirty + kernel radius padding), and possibly a small effect-cache/history layer above the current per-burst compose flow.

## Window-surface model vs. Quartz-style compositing (where we are now)

- **Current model (this branch):** app_server composes from snapshots into an offscreen render target, then presents dirty regions via `PresentQueue`.
- **Quartz-style ideal:** retain explicit per-window surfaces/layers, then run a deterministic composition graph (effects/animations/opacity) over those retained surfaces each frame.

### What is good already
- We already have snapshot-driven composition and a dedicated present queue, which is a strong foundation for smooth moves and future effects.
- Opaque windows now have a fast-path candidate (`opaqueFastPath`) so simple window movement can stay on copy/memcpy paths instead of full blending.

### What is still missing for "Quartz-like" behavior
- A formal per-window retained-surface lifecycle (allocation, damage tracking, eviction, reuse policy).
- A frame graph / scheduler for transitions (fade/slide) rather than ad-hoc debug options.
- Explicit timeline-driven animation sampling synchronized with present cadence.

### Practical next step
- Keep current compositing path as baseline, and add optional retained-surface metadata + per-window damage history first; build animation/effect scheduling on that instead of bypassing the queue.

## Minimum validation before merge

Run at least:

- Build checks for affected targets (`app_server`, `appearance`, `setwindowalpha`).
- Smoke test window moves/resizes/occlusion with rapid invalidations.
- Toggle alpha-debug setting and verify UI + decorator tint changes propagate to all desktops.
- Verify present-rate logging is sane and no stalled frames occur after bursts.

If these checks pass, confidence improves significantly, but there is still no proof of "cannot break"; only reduced risk.
