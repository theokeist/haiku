/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#include "PresentQueue.h"

#include <inttypes.h>
#include <OS.h>
#include <string.h>

#include "HWInterface.h"
#include "MallocBuffer.h"


PresentQueue::PresentQueue(int32 width, int32 height, color_space format)
	:
	fRenderIndex(0),
	fReadyIndex(-1),
	fBufferCount(0),
	fLogLevel(0),
	fLock("present queue lock")
{
	_AllocateBuffers(width, height, format);
}


status_t
PresentQueue::InitCheck() const
{
	return fBufferCount > 0 ? B_OK : B_NO_MEMORY;
}


status_t
PresentQueue::Resize(int32 width, int32 height, color_space format)
{
	BAutolock _(fLock);
	return _AllocateBuffers(width, height, format);
}


RenderingBuffer*
PresentQueue::AcquireForRender()
{
	BAutolock _(fLock);
	if (fBufferCount == 0)
		return NULL;

	int32 chosenIndex = -1;
	for (int32 i = 0; i < fBufferCount; i++) {
		int32 index = (fRenderIndex + i) % fBufferCount;
		if (!fEntries[index].ready) {
			chosenIndex = index;
			break;
		}
	}

	if (chosenIndex < 0) {
		// All buffers are ready; latest wins, so reuse the render index.
		chosenIndex = fRenderIndex;
		if (fLogLevel >= 2)
			debug_printf("present queue: all buffers busy, reusing buffer\n");
	}

	fRenderIndex = chosenIndex;
	fEntries[fRenderIndex].ready = false;
	fEntries[fRenderIndex].dirty.MakeEmpty();
	return fEntries[fRenderIndex].buffer.Get();
}


void
PresentQueue::Submit(RenderingBuffer* buffer, const BRegion& dirty)
{
	BAutolock _(fLock);
	if (fBufferCount == 0 || buffer == NULL)
		return;

	int32 pendingBefore = fPendingDirty.CountRects();
	fPendingDirty.Include(&dirty);
	if (fPendingDirty.CountRects() < pendingBefore && fLogLevel >= 2) {
		debug_printf("present queue: pending dirty shrank unexpectedly "
			"(before=%" B_PRId32 " after=%" B_PRId32 ")\n",
			pendingBefore, fPendingDirty.CountRects());
	}

	fEntries[fRenderIndex].dirty = dirty;
	fEntries[fRenderIndex].ready = true;
	// Latest-ready frame wins while dirty accumulates until present.
	if (fReadyIndex >= 0 && fReadyIndex != fRenderIndex)
		fEntries[fReadyIndex].ready = false;
	fReadyIndex = fRenderIndex;
	fRenderIndex = (fRenderIndex + 1) % fBufferCount;
}


bigtime_t
PresentQueue::PresentNext(HWInterface& interface, bool vsync)
{
	RenderingBuffer* buffer = NULL;
	BRegion dirty;
	{
		BAutolock _(fLock);
		if (fBufferCount == 0 || fReadyIndex < 0)
			return 0;

		Entry& entry = fEntries[fReadyIndex];
		if (!entry.ready)
			return 0;

		buffer = entry.buffer.Get();
		dirty = fPendingDirty;
		fPendingDirty.MakeEmpty();
		entry.ready = false;
		fReadyIndex = -1;
	}

	if (dirty.CountRects() == 0 && fLogLevel >= 2)
		debug_printf("present queue: presenting with empty dirty region\n");

	bigtime_t start = system_time();
	if (vsync)
		interface.WaitForRetrace(0);
	interface.PresentBuffer(buffer, dirty);
	return system_time() - start;
}


void
PresentQueue::SetLogLevel(int32 logLevel)
{
	BAutolock _(fLock);
	fLogLevel = logLevel;
}


status_t
PresentQueue::_AllocateBuffers(int32 width, int32 height, color_space format)
{
	for (int32 i = 0; i < 3; i++) {
		fEntries[i].buffer.Unset();
		fEntries[i].dirty.MakeEmpty();
		fEntries[i].ready = false;
	}
	fBufferCount = 0;
	fRenderIndex = 0;
	fReadyIndex = -1;
	fPendingDirty.MakeEmpty();

	for (int32 i = 0; i < 3; i++) {
		fEntries[i].buffer.SetTo(new(std::nothrow) MallocBuffer(width, height));
		if (!fEntries[i].buffer.IsSet())
			break;

		status_t status = fEntries[i].buffer->InitCheck();
		if (status < B_OK) {
			fEntries[i].buffer.Unset();
			break;
		}

		memset(fEntries[i].buffer->Bits(), 255,
			fEntries[i].buffer->BitsLength());
		fBufferCount++;
	}

	return fBufferCount > 0 ? B_OK : B_NO_MEMORY;
}
