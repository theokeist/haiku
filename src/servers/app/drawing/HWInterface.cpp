/*
 * Copyright 2005-2012, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 */


#include "HWInterface.h"

#include <Autolock.h>
#include <OS.h>
#include <inttypes.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <vesa/vesa_info.h>

#include "CompositorSettings.h"
#include "Compositor.h"
#include "drawing_support.h"

#include "DrawingEngine.h"
#include "PresentQueue.h"
#include "RenderingBuffer.h"
#include "SystemPalette.h"
#include "Window.h"


using std::nothrow;

namespace {

static void
_AppendStressReplayRegions(const BRegion& base, const IntRect& bounds,
	int64 frameCounter, std::vector<BRegion>& out)
{
	if (base.CountRects() == 0)
		return;

	BRegion boundsRegion;
	boundsRegion.Set((BRect)bounds);

	// Replay 1: shifted copy of current dirty region to emulate rapid moves.
	BRegion shifted(base);
	shifted.OffsetBy(8.0f, 8.0f);
	shifted.IntersectWith(&boundsRegion);
	if (shifted.CountRects() > 0)
		out.push_back(shifted);

	// Replay 2: vertical strip to emulate occlusion/unocclusion damage.
	BRect frame = bounds;
	float center = frame.left + frame.Width() / 2.0f;
	BRect strip(center - 24.0f, frame.top, center + 24.0f, frame.bottom);
	BRegion occlusionStrip(strip);
	occlusionStrip.IntersectWith(&boundsRegion);
	if (occlusionStrip.CountRects() > 0)
		out.push_back(occlusionStrip);

	// Replay 3: periodic full-screen invalidation burst.
	if ((frameCounter % 4) == 0)
		out.push_back(boundsRegion);
}

} // namespace


static inline int32
atomic_add_compat(volatile int32* value, int32 addValue)
{
	return atomic_add(const_cast<int32*>(value), addValue);
}


static inline int32
atomic_get_compat(volatile int32* value)
{
	return atomic_get(const_cast<int32*>(value));
}


static inline void
atomic_set_compat(volatile int32* value, int32 newValue)
{
	atomic_set(const_cast<int32*>(value), newValue);
}


static inline int32
atomic_test_and_set_compat(volatile int32* value, int32 newValue,
	int32 testAgainst)
{
	return atomic_test_and_set(const_cast<int32*>(value), newValue,
		testAgainst);
}


HWInterfaceListener::HWInterfaceListener()
{
}


HWInterfaceListener::~HWInterfaceListener()
{
}


// #pragma mark - HWInterface


HWInterface::HWInterface()
	:
	MultiLocker("hw interface lock"),
	fCursorAreaBackup(NULL),
	fFloatingOverlaysLock("floating overlays lock"),
	fCursor(NULL),
	fDragBitmap(NULL),
	fDragBitmapOffset(0, 0),
	fCursorAndDragBitmap(NULL),
	fCursorVisible(false),
	fCursorObscured(false),
	fHardwareCursorEnabled(false),
	fCursorLocation(0, 0),
	fTrackingRect(),
	fVGADevice(-1),
	fListeners(20),
	fCompositorBackground((rgb_color){0, 0, 0, 255}),
	fCompositorFrameCounter(0),
	fCompositorLogEveryN(0),
	fCompositorComposeAccum(0),
	fCompositorComposeCount(0),
	fCompositorAnimActive(false),
	fCompositorEnabled(true),
	fCompositorAnimationsEnabled(true),
	fCompositorBlurEnabled(false),
	fCompositorTranslucencyEnabled(false),
	fCompositorShowOverlay(false),
	fCompositorLogTimings(false),
	fCompositorStressInvalidate(false),
	fCompositorTargetFps(60),
	fCompositorLogLevel(0),
	fCompositorSettingsLock("compositor settings lock"),
	fPresentQueue(NULL),
	fCompositor(NULL),
	fWindowSnapshots(),
	fCompositorBackground((rgb_color){0, 0, 0, 255}),
	fCompositorFrameCounter(0),
	fCompositorLogEveryN(120),
	fPendingInvalidate(),
	fPresentInvalidateLock("present invalidate lock"),
	fPresentThread(-1),
	fPresentSemaphore(-1),
	fPresentScheduled(0),
	fPresentThreadRunning(0),
	fPendingInvalidations(0),
	fPresentCounter(0),
	fPresentLogTime(0)
{
}


HWInterface::~HWInterface()
{
	_StopPresentThread();
}


status_t
HWInterface::Initialize()
{
	return MultiLocker::InitCheck();
}


DrawingEngine*
HWInterface::CreateDrawingEngine()
{
	return new(std::nothrow) DrawingEngine(this);
}


EventStream*
HWInterface::CreateEventStream()
{
	return NULL;
}


status_t
HWInterface::GetAccelerantPath(BString &path)
{
	return B_ERROR;
}


status_t
HWInterface::GetDriverPath(BString &path)
{
	return B_ERROR;
}


status_t
HWInterface::GetPreferredMode(display_mode* mode)
{
	return B_NOT_SUPPORTED;
}


status_t
HWInterface::GetMonitorInfo(monitor_info* info)
{
	return B_NOT_SUPPORTED;
}


// #pragma mark -


void
HWInterface::SetCursor(ServerCursor* cursor)
{
	if (!fFloatingOverlaysLock.Lock())
		return;

	if (fCursor.Get() != cursor) {
		BRect oldFrame = _CursorFrame();

		fCursor.SetTo(cursor);

		Invalidate(oldFrame);

		_AdoptDragBitmap();
		Invalidate(_CursorFrame());
	}
	fFloatingOverlaysLock.Unlock();
}


ServerCursorReference
HWInterface::Cursor() const
{
	if (!fFloatingOverlaysLock.Lock())
		return ServerCursorReference(NULL);

	fFloatingOverlaysLock.Unlock();
	return fCursor;
}


ServerCursorReference
HWInterface::CursorAndDragBitmap() const
{
	if (!fFloatingOverlaysLock.Lock())
		return ServerCursorReference(NULL);

	fFloatingOverlaysLock.Unlock();
	return fCursorAndDragBitmap;
}


void
HWInterface::SetCursorVisible(bool visible)
{
	if (!fFloatingOverlaysLock.Lock())
		return;

	if (fCursorVisible != visible) {
		// NOTE: _CursorFrame() will
		// return an invalid rect if
		// fCursorVisible == false!
		if (visible) {
			fCursorVisible = visible;
			fCursorObscured = false;
			IntRect r = _CursorFrame();

			_DrawCursor(r);
			Invalidate(r);
		} else {
			IntRect r = _CursorFrame();
			fCursorVisible = visible;

			_RestoreCursorArea();
			Invalidate(r);
		}
	}
	fFloatingOverlaysLock.Unlock();
}


bool
HWInterface::IsCursorVisible()
{
	bool visible = true;
	if (fFloatingOverlaysLock.Lock()) {
		visible = fCursorVisible;
		fFloatingOverlaysLock.Unlock();
	}
	return visible;
}


void
HWInterface::ObscureCursor()
{
	if (!fFloatingOverlaysLock.Lock())
		return;

	if (!fCursorObscured) {
		SetCursorVisible(false);
		fCursorObscured = true;
	}
	fFloatingOverlaysLock.Unlock();
}


void
HWInterface::MoveCursorTo(float x, float y)
{
	if (!fFloatingOverlaysLock.Lock())
		return;

	BPoint p(x, y);
	if (p != fCursorLocation) {
		// unhide cursor if it is obscured only
		if (fCursorObscured) {
			SetCursorVisible(true);
		}
		IntRect oldFrame = _CursorFrame();
		fCursorLocation = p;
		if (fCursorVisible) {
			// Invalidate and _DrawCursor would not draw
			// anything if the cursor is hidden
			// (invalid cursor frame), but explicitly
			// testing for it here saves us some cycles
			if (fCursorAreaBackup.IsSet()) {
				// means we have a software cursor which we need to draw
				_RestoreCursorArea();
				_DrawCursor(_CursorFrame());
			}
			IntRect newFrame = _CursorFrame();
			if (newFrame.Intersects(oldFrame))
				Invalidate(oldFrame | newFrame);
			else {
				Invalidate(oldFrame);
				Invalidate(newFrame);
			}
		}
	}
	fFloatingOverlaysLock.Unlock();
}


BPoint
HWInterface::CursorPosition()
{
	BPoint location;
	if (fFloatingOverlaysLock.Lock()) {
		location = fCursorLocation;
		fFloatingOverlaysLock.Unlock();
	}
	return location;
}


void
HWInterface::SetDragBitmap(const ServerBitmap* bitmap,
	const BPoint& offsetFromCursor)
{
	if (fFloatingOverlaysLock.Lock()) {
		fDragBitmap.SetTo((ServerBitmap*)bitmap, false);
		fDragBitmapOffset = offsetFromCursor;
		_AdoptDragBitmap();
		fFloatingOverlaysLock.Unlock();
	}
}


// #pragma mark -


RenderingBuffer*
HWInterface::DrawingBuffer() const
{
	if (IsDoubleBuffered())
		return BackBuffer();
	return FrontBuffer();
}


/*! The object needs to be already locked!
*/
status_t
HWInterface::InvalidateRegion(const BRegion& region)
{
	bool compositorEnabled = false;
	int32 logLevel = 0;
	{
		BAutolock _(fCompositorSettingsLock);
		compositorEnabled = fCompositorEnabled;
		logLevel = fCompositorLogLevel;
	}
	if (compositorEnabled && fCompositor.IsSet() && fPresentQueue.IsSet()) {
		{
			BAutolock _(fPresentInvalidateLock);
			int32 pendingBefore = fPendingInvalidate.CountRects();
			fPendingInvalidate.Include(&region);
			if (fPendingInvalidate.CountRects() < pendingBefore
				&& logLevel >= 2) {
				debug_printf("compositor: pending invalidate shrank unexpectedly "
					"(before=%" B_PRId32 " after=%" B_PRId32 ")\n",
					pendingBefore, fPendingInvalidate.CountRects());
			}
		}
		atomic_add_compat(&fPendingInvalidations, 1);
		_SchedulePresent();
		return B_OK;
	}

	if (SupportsTripleBuffering()) {
		RenderingBuffer* frontBuffer = FrontBuffer();
		RenderingBuffer* backBuffer = BackBuffer();

		if (!backBuffer || !frontBuffer)
			return B_NO_INIT;

		BRegion clipped(region);
		IntRect bufferClip(backBuffer->Bounds());
		clipped.IntersectWith((BRect)bufferClip);
		BRegion clipRegion;
		clipRegion.Set((BRect)bufferClip);
		clipped.IntersectWith(&clipRegion);

		if (clipped.CountRects() == 0)
			return B_OK;

		IntRect area(clipped.Frame());
		if (!area.IsValid())
			return B_OK;

		bool cursorLocked = fFloatingOverlaysLock.Lock();

		if (IsDoubleBuffered())
			clipped.Exclude((clipping_rect)_CursorFrame());

		WaitForRetrace(0);

		_CopyBackToFront(clipped);
		_DrawCursor(bufferClip & area);

		if (cursorLocked)
			fFloatingOverlaysLock.Unlock();

		SwapBackBuffers();
		return B_OK;
	}

	int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++) {
		status_t result = Invalidate(region.RectAt(i));
		if (result != B_OK)
			return result;
	}

	return B_OK;
}


/*! The object needs to be already locked!
*/
status_t
HWInterface::Invalidate(const BRect& frame)
{
	if (IsDoubleBuffered())
		return CopyBackToFront(frame);

	return B_OK;
}


/*! The object must already be locked!
*/
status_t
HWInterface::CopyBackToFront(const BRect& frame)
{
	bool compositorEnabled = false;
	{
		BAutolock _(fCompositorSettingsLock);
		compositorEnabled = fCompositorEnabled;
	}
	if (compositorEnabled && fCompositor.IsSet() && fPresentQueue.IsSet()) {
		BRegion region(frame);
		return InvalidateRegion(region);
	}

	RenderingBuffer* frontBuffer = FrontBuffer();
	RenderingBuffer* backBuffer = BackBuffer();

	if (!backBuffer || !frontBuffer)
		return B_NO_INIT;

	// we need to mess with the area, but it is const
	IntRect area(frame);
	IntRect bufferClip(backBuffer->Bounds());

	if (area.IsValid() && area.Intersects(bufferClip)) {

		// make sure we don't copy out of bounds
		area = bufferClip & area;

		bool cursorLocked = fFloatingOverlaysLock.Lock();

		BRegion region((BRect)area);
		if (IsDoubleBuffered())
			region.Exclude((clipping_rect)_CursorFrame());

		if (SupportsTripleBuffering())
			WaitForRetrace(0);

		_CopyBackToFront(region);

		_DrawCursor(area);

		if (cursorLocked)
			fFloatingOverlaysLock.Unlock();

		if (SupportsTripleBuffering())
			SwapBackBuffers();

		return B_OK;
	}
	return B_BAD_VALUE;
}


bool
HWInterface::SupportsTripleBuffering() const
{
	return false;
}


void
HWInterface::SwapBackBuffers()
{
}


void
HWInterface::ConfigureCompositor(int32 width, int32 height,
	color_space format)
{
	if (!fCompositor.IsSet())
		fCompositor.SetTo(new(std::nothrow) Compositor());
	if (!fPresentQueue.IsSet())
		fPresentQueue.SetTo(new(std::nothrow) PresentQueue(width, height, format));
	else
		fPresentQueue->Resize(width, height, format);
	{
		BAutolock _(fCompositorSettingsLock);
		if (fCompositor.IsSet())
			fCompositor->SetDebugOptions(fCompositorShowOverlay,
				fCompositorLogTimings, fCompositorStressInvalidate);
		if (fCompositor.IsSet())
			fCompositor->SetLogLevel(fCompositorLogLevel);
		if (fPresentQueue.IsSet())
			fPresentQueue->SetLogLevel(fCompositorLogLevel);
	}
	_StartPresentThread();
}


void
HWInterface::ApplyCompositorSettings(const CompositorSettings& settings)
{
	int32 logLevel = settings.log_level;
	int64 logEveryN = 0;
	if (logLevel == 1)
		logEveryN = 120;
	else if (logLevel >= 2)
		logEveryN = 60;

	{
		BAutolock _(fCompositorSettingsLock);
		fCompositorEnabled = settings.enable_compositor;
		fCompositorAnimationsEnabled = settings.enable_animations;
		fCompositorBlurEnabled = settings.enable_blur;
		fCompositorTranslucencyEnabled = settings.enable_translucency;
		fCompositorShowOverlay = settings.show_overlay;
		fCompositorLogTimings = settings.log_timings;
		fCompositorStressInvalidate = settings.stress_invalidate;
		fCompositorTargetFps = settings.target_fps > 0 ? settings.target_fps : 60;
		fCompositorLogLevel = logLevel;
		fCompositorLogEveryN = logEveryN;
		fCompositorFrameCounter = 0;
		fCompositorComposeAccum = 0;
		fCompositorComposeCount = 0;
		if (!fCompositorAnimationsEnabled || !fCompositorEnabled)
			fCompositorAnimActive = false;
	}

	if (fCompositor.IsSet())
		fCompositor->SetDebugOptions(fCompositorShowOverlay,
			fCompositorLogTimings, fCompositorStressInvalidate);
	if (fCompositor.IsSet())
		fCompositor->SetLogLevel(logLevel);
	if (fPresentQueue.IsSet())
		fPresentQueue->SetLogLevel(logLevel);
}


void
HWInterface::UpdateCompositorState(const std::vector<WindowSnapshot>& snapshots,
	const rgb_color& background)
{
	fWindowSnapshots = snapshots;
	fCompositorBackground = background;
	fCompositorDebugOptions = options;

	if (fCompositor.IsSet() && fPresentQueue.IsSet()) {
		RenderingBuffer* buffer = DrawingBuffer();
		if (buffer != NULL) {
			BRegion fullBounds;
			fullBounds.Set((BRect)buffer->Bounds());
			fPendingInvalidate.Include(&fullBounds);
		}
		atomic_add_compat(&fPendingInvalidations, 1);
		_SchedulePresent();
	}
}


void
HWInterface::PresentBuffer(RenderingBuffer* buffer, const BRegion& dirty)
{
	if (buffer == NULL || FrontBuffer() == NULL)
		return;

	BRegion region(dirty);
	region.IntersectWith((BRect)buffer->Bounds());
	RenderingBuffer* front = FrontBuffer();
	if (buffer == NULL || front == NULL)
		return;

	BRegion region(dirty);
	BRegion clipRegion;
	clipRegion.Set((BRect)buffer->Bounds());
	region.IntersectWith(&clipRegion);
	BRegion frontClip;
	frontClip.Set((BRect)front->Bounds());
	region.IntersectWith(&frontClip);
	if (region.CountRects() == 0)
		return;

	bool cursorLocked = fFloatingOverlaysLock.Lock();
	if (IsDoubleBuffered())
		region.Exclude((clipping_rect)_CursorFrame());

	uint32 srcBPR = buffer->BytesPerRow();
	uint8* src = (uint8*)buffer->Bits();

	int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++) {
		clipping_rect r = region.RectAtInt(i);
		uint8* srcOffset = src + r.top * srcBPR + r.left * 4;
		_CopyToFront(srcOffset, srcBPR, r.left, r.top, r.right, r.bottom);
	}

	_DrawCursor(_CursorFrame());

	if (cursorLocked)
		fFloatingOverlaysLock.Unlock();
}


status_t
HWInterface::_StartPresentThread()
{
	if (fPresentThread >= 0)
		return B_OK;

	fPresentSemaphore = create_sem(0, "present queue sem");
	if (fPresentSemaphore < 0)
		return fPresentSemaphore;

	fPresentThreadRunning = 1;
	fPresentThread = spawn_thread(_PresentThreadEntry, "present queue thread",
		B_DISPLAY_PRIORITY, this);
	if (fPresentThread < 0) {
		delete_sem(fPresentSemaphore);
		fPresentSemaphore = -1;
		fPresentThreadRunning = 0;
		return fPresentThread;
	}

	resume_thread(fPresentThread);
	return B_OK;
}


void
HWInterface::_StopPresentThread()
{
	if (fPresentThread < 0)
		return;

	fPresentThreadRunning = 0;
	release_sem(fPresentSemaphore);

	status_t status;
	wait_for_thread(fPresentThread, &status);
	fPresentThread = -1;

	if (fPresentSemaphore >= 0) {
		delete_sem(fPresentSemaphore);
		fPresentSemaphore = -1;
	}
}


void
HWInterface::_SchedulePresent()
{
	if (fPresentSemaphore < 0)
		return;

	if (atomic_test_and_set_compat(&fPresentScheduled, 1, 0) == 0)
		release_sem(fPresentSemaphore);
}


void
HWInterface::_ProcessPendingInvalidate()
{
	BRegion pending;
	{
		BAutolock _(fPresentInvalidateLock);
		if (fPendingInvalidate.CountRects() > 0) {
			pending = fPendingInvalidate;
			fPendingInvalidate.MakeEmpty();
		}
	}

	bool compositorEnabled = false;
	bool animationsEnabled = false;
	bool translucencyEnabled = false;
	bool showOverlay = false;
	bool logTimings = false;
	bool stressInvalidate = false;
	int32 logLevel = 0;
	int64 logEveryN = 0;
	{
		BAutolock _(fCompositorSettingsLock);
		compositorEnabled = fCompositorEnabled;
		animationsEnabled = fCompositorAnimationsEnabled;
		translucencyEnabled = fCompositorTranslucencyEnabled;
		showOverlay = fCompositorShowOverlay;
		logTimings = fCompositorLogTimings;
		stressInvalidate = fCompositorStressInvalidate;
		logLevel = fCompositorLogLevel;
		logEveryN = fCompositorLogEveryN;
	}

	if (!compositorEnabled || !fPresentQueue.IsSet() || !fCompositor.IsSet())
		return;

	bigtime_t now = system_time();
	bool anyAnimActive = false;
	int32 animatingWindows = 0;
	for (size_t i = 0; i < fWindowSnapshots.size(); i++) {
		WindowSnapshot& snapshot = fWindowSnapshots[i];
		if (snapshot.window == NULL)
			continue;

		bool allowNormalEffects = translucencyEnabled;
		bool allowEffects = !snapshot.window->IsNormal() || allowNormalEffects;
		bool allowAnimations = animationsEnabled && allowEffects;
		if (allowAnimations && snapshot.window->IsAlphaAnimating()) {
			snapshot.alpha = snapshot.window->AnimatedAlpha(now);
			snapshot.animActive = true;
		} else
			snapshot.animActive = false;

		snapshot.opaqueFastPath = snapshot.alpha >= 1.0f
			&& (!snapshot.blurEnabled || snapshot.blurRadius <= 0.0f);

		if (snapshot.animActive) {
			anyAnimActive = true;
			animatingWindows++;
		}
	}

	if (pending.CountRects() > 0) {
		for (size_t i = 0; i < fWindowSnapshots.size(); i++) {
			const WindowSnapshot& snapshot = fWindowSnapshots[i];
			if (snapshot.alpha < 1.0f || snapshot.blurEnabled)
				pending.Include(&snapshot.visible);
		}
	}

	if (pending.CountRects() == 0 && !anyAnimActive)
		return;

	if (pending.CountRects() == 0 && anyAnimActive) {
		for (size_t i = 0; i < fWindowSnapshots.size(); i++) {
			const WindowSnapshot& snapshot = fWindowSnapshots[i];
			if (snapshot.animActive)
				pending.Include(&snapshot.visible);
		}
		if (pending.CountRects() == 0)
			return;
	}

	if (!LockExclusiveAccess())
		return;

	RenderingBuffer* source = DrawingBuffer();
	RenderingBuffer* renderTarget = fPresentQueue->AcquireForRender();
	if (source == NULL || renderTarget == NULL) {
		UnlockExclusiveAccess();
		return;
	}

	std::vector<BRegion> replayRegions;
	replayRegions.push_back(pending);
	if (options.stressInvalidate)
		_AppendStressReplayRegions(pending, source->Bounds(),
			fCompositorFrameCounter, replayRegions);

	ComposeStats stats = {};
	bigtime_t presentTime = 0;
	for (size_t i = 0; i < replayRegions.size(); i++) {
		RenderingBuffer* target = i == 0
			? renderTarget : fPresentQueue->AcquireForRender();
		if (target == NULL)
			break;

		ComposeStats passStats = fCompositor->Compose(*target, *source,
			replayRegions[i], snapshots, background, options);
		if (i == 0)
			stats = passStats;
		else {
			stats.dirtyRects += passStats.dirtyRects;
			stats.dirtyPixels += passStats.dirtyPixels;
			stats.windowsComposed += passStats.windowsComposed;
			stats.copyPathWindows += passStats.copyPathWindows;
			stats.blendPathWindows += passStats.blendPathWindows;
			stats.alphaWindows += passStats.alphaWindows;
			stats.blurredWindows += passStats.blurredWindows;
			stats.blurredPixels += passStats.blurredPixels;
			stats.cacheHits += passStats.cacheHits;
			stats.cacheMisses += passStats.cacheMisses;
			stats.blurTime += passStats.blurTime;
			stats.composeTime += passStats.composeTime;
		}
	if (showOverlay && stats.overlayRects.CountRects() > 0)
		pending.Include(&stats.overlayRects);
		pending, snapshots, background, options);

		fPresentQueue->Submit(target, replayRegions[i]);
		presentTime += fPresentQueue->PresentNext(*this, true);
	}
	PresentQueue::PressureMetrics pressure = fPresentQueue->GetPressureMetrics();
	UnlockExclusiveAccess();

	int32 invalidations = atomic_get_compat(&fPendingInvalidations);
	atomic_set_compat(&fPendingInvalidations, 0);

	fCompositorFrameCounter++;
	fCompositorComposeAccum += stats.composeTime;
	fCompositorComposeCount++;
	bool shouldLog = false;
	if (logEveryN > 0 && logLevel >= 1
		&& (fCompositorFrameCounter % logEveryN) == 0) {
		shouldLog = true;
	}
	if (logTimings && (fCompositorFrameCounter % 30) == 0)
		shouldLog = true;
	if (showOverlay && logLevel >= 1 && (fCompositorFrameCounter % 30) == 0)
		shouldLog = true;
	if (stressInvalidate && (fCompositorFrameCounter % 60) == 0)
		shouldLog = true;

	if (shouldLog) {
		int64 avgCompose = fCompositorComposeCount > 0
			? fCompositorComposeAccum / fCompositorComposeCount
			: 0;
		debug_printf("compositor: frame %" B_PRId64
			" invalidations=%" B_PRId32 " dirtyRects=%" B_PRId32
			" dirtyPixels=%" B_PRId64 " windows=%" B_PRId32
			" copy=%" B_PRId32 " blend=%" B_PRId32
			" alpha=%" B_PRId32 " blurred=%" B_PRId32
			" blurPixels=%" B_PRId64 " blurTime=%" B_PRId64 "us"
			" cache(h/m)=%" B_PRId32 "/%" B_PRId32
			" queue(reuse/overwrite/unknown)=%" B_PRId64 "/%" B_PRId64
			"/%" B_PRId64
			" compose=%" B_PRId64 "us"
			" present=%" B_PRId64 "us\n",
			fCompositorFrameCounter, invalidations, stats.dirtyRects,
			stats.dirtyPixels, stats.windowsComposed,
			stats.copyPathWindows, stats.blendPathWindows,
			stats.alphaWindows,
			stats.blurredWindows, stats.blurredPixels, stats.blurTime,
			stats.cacheHits, stats.cacheMisses,
			pressure.acquireReuseCount, pressure.readyOverwriteCount,
			pressure.unknownSubmitCount,
			stats.composeTime, presentTime,
			" alpha=%" B_PRId32 " animating=%" B_PRId32
			" blurred=%" B_PRId32 " blurPixels=%" B_PRId64
			" blurHits=%" B_PRId32 " blurMisses=%" B_PRId32
			" blurTime=%" B_PRId64 "us compose=%" B_PRId64
			"us avgCompose=%" B_PRId64 "us present=%" B_PRId64
			"us pendingRects=%" B_PRId32 " pendingDirty=%" B_PRId32
			" buffering=%s vsync=%s\n",
			fCompositorFrameCounter, invalidations, stats.dirtyRects,
			stats.dirtyPixels, stats.windowsComposed, stats.alphaWindows,
			animatingWindows, stats.blurredWindows, stats.blurredPixels,
			stats.blurCacheHits, stats.blurCacheMisses, stats.blurTime,
			stats.composeTime, avgCompose, presentTime,
			pending.CountRects(), pendingDirtyRects,
			fPresentQueue.IsSet() ? "intermediate" : "frontbuffer",
			"runtime");
		fCompositorComposeAccum = 0;
		fCompositorComposeCount = 0;
	}

	fPresentCounter++;
	bigtime_t now = system_time();
	if (fPresentLogTime == 0)
		fPresentLogTime = now;
	if (logLevel >= 2 && now - fPresentLogTime >= 1000000) {
		debug_printf("compositor: presents per second %" B_PRId64 "\n",
			fPresentCounter);
		fPresentCounter = 0;
		fPresentLogTime = now;
	}

	if (anyAnimActive)
		_SchedulePresent();

	fCompositorAnimActive = anyAnimActive;
}


status_t
HWInterface::_PresentThreadEntry(void* data)
{
	HWInterface* interface = static_cast<HWInterface*>(data);
	while (atomic_get_compat(&interface->fPresentThreadRunning) != 0) {
		status_t status = acquire_sem(interface->fPresentSemaphore);
		if (status != B_OK
			|| atomic_get_compat(&interface->fPresentThreadRunning) == 0) {
			continue;
		}

		while (atomic_get_compat(&interface->fPresentThreadRunning) != 0) {
			interface->_ProcessPendingInvalidate();

			// Mark scheduling slot as free. If new work arrived while composing,
			// re-arm before leaving the loop so producers don't miss a wakeup.
			atomic_set_compat(&interface->fPresentScheduled, 0);
			if (!interface->_HasPendingInvalidate())
				break;

			// New invalidations arrived while processing. Re-arm the scheduled
			// state so producers won't skip the semaphore release.
			if (atomic_test_and_set_compat(&interface->fPresentScheduled, 1, 0)
				!= 0) {
				break;

			// New invalidations arrived while processing. Re-arm the scheduled
			// state so producers won't skip the semaphore release.
			if (atomic_test_and_set(&interface->fPresentScheduled, 1, 0) != 0)
				break;
			bool animationsEnabled = false;
			int32 targetFps = 60;
			{
				BAutolock _(interface->fCompositorSettingsLock);
				animationsEnabled = interface->fCompositorAnimationsEnabled;
				targetFps = interface->fCompositorTargetFps;
			}
			if (targetFps <= 0)
				targetFps = 60;
			bigtime_t animDelay = 1000000 / targetFps;
			bigtime_t delay = (animationsEnabled
				&& interface->fCompositorAnimActive) ? animDelay : 2000;
			snooze(delay);
			if (!interface->_HasPendingInvalidate()
				&& !interface->fCompositorAnimActive) {
				BAutolock _(interface->fPresentInvalidateLock);
				if (interface->fPendingInvalidate.CountRects() == 0
					&& !interface->fCompositorAnimActive) {
					atomic_set(&interface->fPresentScheduled, 0);
					break;
				}
			}
		}
	}
	return B_OK;
}


bool
HWInterface::_HasPendingInvalidate()
{
	BAutolock _(fPresentInvalidateLock);
	return fPendingInvalidate.CountRects() > 0;
}

void
HWInterface::_CopyBackToFront(/*const*/ BRegion& region)
{
	RenderingBuffer* backBuffer = BackBuffer();

	uint32 srcBPR = backBuffer->BytesPerRow();
	uint8* src = (uint8*)backBuffer->Bits();

	int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++) {
		clipping_rect r = region.RectAtInt(i);
		// offset to left top pixel in source buffer (always B_RGBA32)
		uint8* srcOffset = src + r.top * srcBPR + r.left * 4;
		_CopyToFront(srcOffset, srcBPR, r.left, r.top, r.right, r.bottom);
	}
}


// #pragma mark -


overlay_token
HWInterface::AcquireOverlayChannel()
{
	return NULL;
}


void
HWInterface::ReleaseOverlayChannel(overlay_token token)
{
}


status_t
HWInterface::GetOverlayRestrictions(const Overlay* overlay,
	overlay_restrictions* restrictions)
{
	return B_NOT_SUPPORTED;
}


bool
HWInterface::CheckOverlayRestrictions(int32 width, int32 height,
	color_space colorSpace)
{
	return false;
}


const overlay_buffer*
HWInterface::AllocateOverlayBuffer(int32 width, int32 height, color_space space)
{
	return NULL;
}


void
HWInterface::FreeOverlayBuffer(const overlay_buffer* buffer)
{
}


void
HWInterface::ConfigureOverlay(Overlay* overlay)
{
}


void
HWInterface::HideOverlay(Overlay* overlay)
{
}


// #pragma mark -


bool
HWInterface::HideFloatingOverlays(const BRect& area)
{
	if (IsDoubleBuffered())
		return false;
	if (!fFloatingOverlaysLock.Lock())
		return false;
	if (fCursorAreaBackup.IsSet() && !fCursorAreaBackup->cursor_hidden) {
		BRect backupArea(fCursorAreaBackup->left, fCursorAreaBackup->top,
			fCursorAreaBackup->right, fCursorAreaBackup->bottom);
		if (area.Intersects(backupArea)) {
			_RestoreCursorArea();
			// do not unlock the cursor lock
			return true;
		}
	}
	fFloatingOverlaysLock.Unlock();
	return false;
}


bool
HWInterface::HideFloatingOverlays()
{
	if (IsDoubleBuffered())
		return false;
	if (!fFloatingOverlaysLock.Lock())
		return false;

	_RestoreCursorArea();
	return true;
}


void
HWInterface::ShowFloatingOverlays()
{
	if (fCursorAreaBackup.IsSet() && fCursorAreaBackup->cursor_hidden)
		_DrawCursor(_CursorFrame());

	fFloatingOverlaysLock.Unlock();
}


// #pragma mark -


bool
HWInterface::AddListener(HWInterfaceListener* listener)
{
	if (listener && !fListeners.HasItem(listener))
		return fListeners.AddItem(listener);
	return false;
}


void
HWInterface::RemoveListener(HWInterfaceListener* listener)
{
	fListeners.RemoveItem(listener);
}


// #pragma mark -


/*!	Default implementation, can be used as fallback or for software cursor.
	\param area is where we potentially draw the cursor, the cursor
		might be somewhere else, in which case this function does nothing
*/
void
HWInterface::_DrawCursor(IntRect area) const
{
	RenderingBuffer* backBuffer = DrawingBuffer();
	if (!backBuffer || !area.IsValid())
		return;

	ServerCursorReference cursor;
	IntRect cf;
	if (fFloatingOverlaysLock.Lock()) {
		cursor = fCursorAndDragBitmap;
		cf = _CursorFrame();
		fFloatingOverlaysLock.Unlock();
	}

	if (!cursor)
		return;

	// make sure we don't copy out of bounds
	area = backBuffer->Bounds() & area;

	if (cf.IsValid() && area.Intersects(cf)) {

		// clip to common area
		area = area & cf;

		int32 width = area.right - area.left + 1;
		int32 height = area.bottom - area.top + 1;

		// make a bitmap from the backbuffer
		// that has the cursor blended on top of it

		// blending buffer
		uint8* buffer = new(std::nothrow) uint8[width * height * 4];
			// TODO: cache this buffer
		if (buffer == NULL)
			return;

		// offset into back buffer
		uint8* src = (uint8*)backBuffer->Bits();
		uint32 srcBPR = backBuffer->BytesPerRow();
		src += area.top * srcBPR + area.left * 4;

		// offset into cursor bitmap
		uint8* crs = (uint8*)cursor->Bits();
		uint32 crsBPR = cursor->BytesPerRow();
		// since area is clipped to cf,
		// the diff between area.top and cf.top is always positive,
		// same for diff between area.left and cf.left
		crs += (area.top - (int32)floorf(cf.top)) * crsBPR
				+ (area.left - (int32)floorf(cf.left)) * 4;

		uint8* dst = buffer;

		if (fCursorAreaBackup.IsSet() && fCursorAreaBackup->buffer
			&& fFloatingOverlaysLock.Lock()) {
			fCursorAreaBackup->cursor_hidden = false;
			// remember which area the backup contains
			fCursorAreaBackup->left = area.left;
			fCursorAreaBackup->top = area.top;
			fCursorAreaBackup->right = area.right;
			fCursorAreaBackup->bottom = area.bottom;
			uint8* bup = fCursorAreaBackup->buffer;
			uint32 bupBPR = fCursorAreaBackup->bpr;

			// blending and backup of drawing buffer
			for (int32 y = area.top; y <= area.bottom; y++) {
				uint8* s = src;
				uint8* c = crs;
				uint8* d = dst;
				uint8* b = bup;

				for (int32 x = area.left; x <= area.right; x++) {
					*(uint32*)b = *(uint32*)s;
					// assumes backbuffer alpha = 255
					// assuming pre-multiplied cursor bitmap
					int a = 255 - c[3];
					d[0] = ((int)(b[0] * a + 255) >> 8) + c[0];
					d[1] = ((int)(b[1] * a + 255) >> 8) + c[1];
					d[2] = ((int)(b[2] * a + 255) >> 8) + c[2];

					s += 4;
					c += 4;
					d += 4;
					b += 4;
				}
				crs += crsBPR;
				src += srcBPR;
				dst += width * 4;
				bup += bupBPR;
			}
			fFloatingOverlaysLock.Unlock();
		} else {
			// blending
			for (int32 y = area.top; y <= area.bottom; y++) {
				uint8* s = src;
				uint8* c = crs;
				uint8* d = dst;
				for (int32 x = area.left; x <= area.right; x++) {
					// assumes backbuffer alpha = 255
					// assuming pre-multiplied cursor bitmap
					uint8 a = 255 - c[3];
					d[0] = ((s[0] * a + 255) >> 8) + c[0];
					d[1] = ((s[1] * a + 255) >> 8) + c[1];
					d[2] = ((s[2] * a + 255) >> 8) + c[2];

					s += 4;
					c += 4;
					d += 4;
				}
				crs += crsBPR;
				src += srcBPR;
				dst += width * 4;
			}
		}
		// copy result to front buffer
		_CopyToFront(buffer, width * 4, area.left, area.top, area.right,
			area.bottom);

		delete[] buffer;
	}
}


/*!	- source is assumed to be already at the right offset
	- source is assumed to be in B_RGBA32 format
	- location in front buffer is calculated
	- conversion from B_RGBA32 to format of front buffer is taken care of
*/
void
HWInterface::_CopyToFront(uint8* src, uint32 srcBPR, int32 x, int32 y,
	int32 right, int32 bottom) const
{
	RenderingBuffer* frontBuffer = FrontBuffer();

	uint8* dst = (uint8*)frontBuffer->Bits();
	uint32 dstBPR = frontBuffer->BytesPerRow();

	// transfer, handle colorspace conversion
	switch (frontBuffer->ColorSpace()) {
		case B_RGB32:
		case B_RGBA32:
		{
			int32 width = right - x + 1;
			int32 bytes = width * 4;

			if (bytes > 0) {
				// offset to left top pixel in dest buffer
				dst += y * dstBPR + x * 4;
				for (; y <= bottom; y++) {
					memcpy(dst, src, bytes);
					dst += dstBPR;
					src += srcBPR;
				}
			}
			break;
		}

		case B_RGB24:
		{
			// offset to left top pixel in dest buffer
			dst += y * dstBPR + x * 3;
			int32 left = x;
			// copy
			for (; y <= bottom; y++) {
				uint8* srcHandle = src;
				uint8* dstHandle = dst;
				for (x = left; x <= right; x++) {
					dstHandle[0] = srcHandle[0];
					dstHandle[1] = srcHandle[1];
					dstHandle[2] = srcHandle[2];
					dstHandle += 3;
					srcHandle += 4;
				}
				dst += dstBPR;
				src += srcBPR;
			}
			break;
		}

		case B_RGB16:
		{
			// offset to left top pixel in dest buffer
			dst += y * dstBPR + x * 2;
			int32 left = x;
			// copy
			// TODO: assumes BGR order, does this work on big endian as well?
			for (; y <= bottom; y++) {
				uint8* srcHandle = src;
				uint16* dstHandle = (uint16*)dst;
				for (x = left; x <= right; x++) {
					*dstHandle = (uint16)(((srcHandle[2] & 0xf8) << 8)
						| ((srcHandle[1] & 0xfc) << 3) | (srcHandle[0] >> 3));
					dstHandle ++;
					srcHandle += 4;
				}
				dst += dstBPR;
				src += srcBPR;
			}
			break;
		}

		case B_RGB15:
		case B_RGBA15:
		{
			// offset to left top pixel in dest buffer
			dst += y * dstBPR + x * 2;
			int32 left = x;
			// copy
			// TODO: assumes BGR order, does this work on big endian as well?
			for (; y <= bottom; y++) {
				uint8* srcHandle = src;
				uint16* dstHandle = (uint16*)dst;
				for (x = left; x <= right; x++) {
					*dstHandle = (uint16)(((srcHandle[2] & 0xf8) << 7)
						| ((srcHandle[1] & 0xf8) << 2) | (srcHandle[0] >> 3));
					dstHandle ++;
					srcHandle += 4;
				}
				dst += dstBPR;
				src += srcBPR;
			}
			break;
		}

		case B_CMAP8:
		{
			const color_map *colorMap = SystemColorMap();
			// offset to left top pixel in dest buffer
			dst += y * dstBPR + x;
			int32 left = x;
			uint16 index;
			// copy
			// TODO: assumes BGR order again
			for (; y <= bottom; y++) {
				uint8* srcHandle = src;
				uint8* dstHandle = dst;
				for (x = left; x <= right; x++) {
					index = ((srcHandle[2] & 0xf8) << 7)
						| ((srcHandle[1] & 0xf8) << 2) | (srcHandle[0] >> 3);
					*dstHandle = colorMap->index_map[index];
					dstHandle ++;
					srcHandle += 4;
				}
				dst += dstBPR;
				src += srcBPR;
			}

			break;
		}

		case B_GRAY8:
			if (frontBuffer->Width() > dstBPR) {
				// VGA 16 color grayscale planar mode
				if (fVGADevice >= 0) {
					vga_planar_blit_args args;
					args.source = src;
					args.source_bytes_per_row = srcBPR;
					args.left = x;
					args.top = y;
					args.right = right;
					args.bottom = bottom;
					if (ioctl(fVGADevice, VGA_PLANAR_BLIT, &args, sizeof(args))
							== 0)
						break;
				}

				// Since we cannot set the plane, we do monochrome output
				dst += y * dstBPR + x / 8;
				int32 left = x;

				// TODO: this is awfully slow...
				// TODO: assumes BGR order
				for (; y <= bottom; y++) {
					uint8* srcHandle = src;
					uint8* dstHandle = dst;
					uint8 current8 = dstHandle[0];
						// we store 8 pixels before writing them back

					for (x = left; x <= right; x++) {
						uint8 pixel = (308 * srcHandle[2] + 600 * srcHandle[1]
							+ 116 * srcHandle[0]) / 1024;
						srcHandle += 4;

						if (pixel > 128)
							current8 |= 0x80 >> (x & 7);
						else
							current8 &= ~(0x80 >> (x & 7));

						if ((x & 7) == 7) {
							// last pixel in 8 pixel group
							dstHandle[0] = current8;
							dstHandle++;
							current8 = dstHandle[0];
						}
					}

					if (x & 7) {
						// last pixel has not been written yet
						dstHandle[0] = current8;
					}
					dst += dstBPR;
					src += srcBPR;
				}
			} else {
				// offset to left top pixel in dest buffer
				dst += y * dstBPR + x;
				int32 left = x;
				// copy
				// TODO: assumes BGR order, does this work on big endian as well?
				for (; y <= bottom; y++) {
					uint8* srcHandle = src;
					uint8* dstHandle = dst;
					for (x = left; x <= right; x++) {
						*dstHandle = (308 * srcHandle[2] + 600 * srcHandle[1]
							+ 116 * srcHandle[0]) / 1024;
						dstHandle ++;
						srcHandle += 4;
					}
					dst += dstBPR;
					src += srcBPR;
				}
			}
			break;

		default:
			fprintf(stderr, "HWInterface::CopyBackToFront() - unsupported "
				"front buffer format! (0x%x)\n", frontBuffer->ColorSpace());
			break;
	}
}


/*!	The object must be locked
*/
IntRect
HWInterface::_CursorFrame() const
{
	IntRect frame(0, 0, -1, -1);
	if (fCursorAndDragBitmap && fCursorVisible && !fHardwareCursorEnabled) {
		frame = fCursorAndDragBitmap->Bounds();
		frame.OffsetTo(fCursorLocation - fCursorAndDragBitmap->GetHotSpot());
	}
	return frame;
}


void
HWInterface::_RestoreCursorArea() const
{
	if (fCursorAreaBackup.IsSet() && !fCursorAreaBackup->cursor_hidden) {
		_CopyToFront(fCursorAreaBackup->buffer, fCursorAreaBackup->bpr,
			fCursorAreaBackup->left, fCursorAreaBackup->top,
			fCursorAreaBackup->right, fCursorAreaBackup->bottom);

		fCursorAreaBackup->cursor_hidden = true;
	}
}


void
HWInterface::_AdoptDragBitmap()
{
	// TODO: support other colorspaces/convert bitmap
	if (fDragBitmap && !(fDragBitmap->ColorSpace() == B_RGB32
		|| fDragBitmap->ColorSpace() == B_RGBA32)) {
		fprintf(stderr, "HWInterface::_AdoptDragBitmap() - bitmap has yet "
			"unsupported colorspace\n");
		return;
	}

	_RestoreCursorArea();
	BRect oldCursorFrame = _CursorFrame();

	if (fDragBitmap != NULL && fDragBitmap->Bounds().Width() > 0 && fDragBitmap->Bounds().Height() > 0) {
		BRect bitmapFrame = fDragBitmap->Bounds();
		if (fCursor) {
			// put bitmap frame and cursor frame into the same
			// coordinate space (the cursor location is the origin)
			bitmapFrame.OffsetTo(BPoint(-fDragBitmapOffset.x, -fDragBitmapOffset.y));

			BRect cursorFrame(fCursor->Bounds());
			BPoint hotspot(fCursor->GetHotSpot());
				// the hotspot is at the origin
			cursorFrame.OffsetTo(-hotspot.x, -hotspot.y);

			BRect combindedBounds = bitmapFrame | cursorFrame;

			BPoint shift;
			shift.x = -combindedBounds.left;
			shift.y = -combindedBounds.top;

			combindedBounds.OffsetBy(shift);
			cursorFrame.OffsetBy(shift);
			bitmapFrame.OffsetBy(shift);

			fCursorAndDragBitmap.SetTo(new(std::nothrow) ServerCursor(combindedBounds,
				fDragBitmap->ColorSpace(), 0, shift), true);

			uint8* dst = fCursorAndDragBitmap ? (uint8*)fCursorAndDragBitmap->Bits() : NULL;
			if (dst == NULL) {
				// Oops, we could not allocate memory for the drag bitmap.
				// Let's show the cursor only.
				fCursorAndDragBitmap = fCursor;
			} else {
				// clear the combined buffer
				uint32 dstBPR = fCursorAndDragBitmap->BytesPerRow();

				memset(dst, 0, fCursorAndDragBitmap->BitsLength());

				// put drag bitmap into combined buffer
				uint8* src = (uint8*)fDragBitmap->Bits();
				uint32 srcBPR = fDragBitmap->BytesPerRow();

				dst += (int32)bitmapFrame.top * dstBPR
					+ (int32)bitmapFrame.left * 4;

				uint32 width = bitmapFrame.IntegerWidth() + 1;
				uint32 height = bitmapFrame.IntegerHeight() + 1;

				for (uint32 y = 0; y < height; y++) {
					memcpy(dst, src, srcBPR);
					dst += dstBPR;
					src += srcBPR;
				}

				// compose cursor into combined buffer
				dst = (uint8*)fCursorAndDragBitmap->Bits();
				dst += (int32)cursorFrame.top * dstBPR
					+ (int32)cursorFrame.left * 4;

				src = (uint8*)fCursor->Bits();
				srcBPR = fCursor->BytesPerRow();

				width = cursorFrame.IntegerWidth() + 1;
				height = cursorFrame.IntegerHeight() + 1;

				for (uint32 y = 0; y < height; y++) {
					uint8* d = dst;
					uint8* s = src;
					for (uint32 x = 0; x < width; x++) {
						// takes two semi-transparent pixels
						// with unassociated alpha (not pre-multiplied)
						// and stays within non-premultiplied color space
						if (s[3] > 0) {
							if (s[3] == 255) {
								d[0] = s[0];
								d[1] = s[1];
								d[2] = s[2];
								d[3] = 255;
							} else {
								uint8 alphaRest = 255 - s[3];
								uint32 alphaTemp
									= (65025 - alphaRest * (255 - d[3]));
								uint32 alphaDest = d[3] * alphaRest;
								uint32 alphaSrc = 255 * s[3];
								d[0] = (d[0] * alphaDest + s[0] * alphaSrc)
									/ alphaTemp;
								d[1] = (d[1] * alphaDest + s[1] * alphaSrc)
									/ alphaTemp;
								d[2] = (d[2] * alphaDest + s[2] * alphaSrc)
									/ alphaTemp;
								d[3] = alphaTemp / 255;
							}
						}
						// TODO: make sure the alpha is always upside down,
						// then it doesn't need to be done when drawing the cursor
						// (see _DrawCursor())
						//					d[3] = 255 - d[3];
						d += 4;
						s += 4;
					}
					dst += dstBPR;
					src += srcBPR;
				}

				// handle pre-multiplication with alpha
				// for faster compositing during cursor drawing
				width = combindedBounds.IntegerWidth() + 1;
				height = combindedBounds.IntegerHeight() + 1;

				dst = (uint8*)fCursorAndDragBitmap->Bits();

				for (uint32 y = 0; y < height; y++) {
					uint8* d = dst;
					for (uint32 x = 0; x < width; x++) {
						d[0] = (d[0] * d[3]) >> 8;
						d[1] = (d[1] * d[3]) >> 8;
						d[2] = (d[2] * d[3]) >> 8;
						d += 4;
					}
					dst += dstBPR;
				}
			}
		} else {
			fCursorAndDragBitmap.SetTo(new ServerCursor(fDragBitmap->Bits(),
				bitmapFrame.IntegerWidth() + 1, bitmapFrame.IntegerHeight() + 1,
				fDragBitmap->ColorSpace()), true);
			fCursorAndDragBitmap->SetHotSpot(BPoint(-fDragBitmapOffset.x, -fDragBitmapOffset.y));
		}
	} else {
		fCursorAndDragBitmap = fCursor;
	}

	Invalidate(oldCursorFrame);

	fCursorAreaBackup.Unset();

	if (!fCursorAndDragBitmap)
		return;

	if (fCursorAndDragBitmap && !IsDoubleBuffered()) {
		BRect cursorBounds = fCursorAndDragBitmap->Bounds();
		fCursorAreaBackup.SetTo(new buffer_clip(cursorBounds.IntegerWidth() + 1,
			cursorBounds.IntegerHeight() + 1));
		if (fCursorAreaBackup->buffer == NULL)
			fCursorAreaBackup.Unset();
	}
 	_DrawCursor(_CursorFrame());
}


void
HWInterface::_NotifyFrameBufferChanged()
{
	BList listeners(fListeners);
	int32 count = listeners.CountItems();
	for (int32 i = 0; i < count; i++) {
		HWInterfaceListener* listener
			= (HWInterfaceListener*)listeners.ItemAtFast(i);
		listener->FrameBufferChanged();
	}
}


void
HWInterface::_NotifyScreenChanged()
{
	BList listeners(fListeners);
	int32 count = listeners.CountItems();
	for (int32 i = 0; i < count; i++) {
		HWInterfaceListener* listener
			= (HWInterfaceListener*)listeners.ItemAtFast(i);
		listener->ScreenChanged(this);
	}
}


/*static*/ bool
HWInterface::_IsValidMode(const display_mode& mode)
{
	// TODO: more of those!
	if (mode.virtual_width < 320
		|| mode.virtual_height < 200)
		return false;

	return true;
}
