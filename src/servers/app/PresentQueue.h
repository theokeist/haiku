/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#ifndef PRESENT_QUEUE_H
#define PRESENT_QUEUE_H

#include <AutoDeleter.h>
#include <Locker.h>
#include <Region.h>

class HWInterface;
class MallocBuffer;
class RenderingBuffer;

// Thread-safe, small ring queue for compositor output buffers.
// Producers acquire/submit render targets while the present thread consumes
// the latest ready frame and presents coalesced dirty regions.
class PresentQueue {
public:
			struct PressureMetrics {
				int64	acquireReuseCount;
				int64	readyOverwriteCount;
				int64	unknownSubmitCount;
			};

							PresentQueue(int32 width, int32 height,
								color_space format);

			status_t			InitCheck() const;
			status_t			Resize(int32 width, int32 height,
									color_space format);

			// Returns a writable back buffer for the next compose pass.
			RenderingBuffer*	AcquireForRender();
			// Marks a rendered buffer ready and unions its dirty region.
			void				Submit(RenderingBuffer* buffer,
									const BRegion& dirty);
			// Presents the latest ready buffer and returns present duration in us.
			bigtime_t			PresentNext(HWInterface& interface,
									bool vsync);
			// Snapshot queue-pressure counters for logging/diagnostics.
			PressureMetrics		GetPressureMetrics();
			void				SetLogLevel(int32 logLevel);

			int32				BufferCount() const { return fBufferCount; }

private:
			struct Entry {
				ObjectDeleter<MallocBuffer>	buffer;
				BRegion						dirty;
				bool						ready;
			};

			status_t			_AllocateBuffers(int32 width, int32 height,
									color_space format);

			Entry				fEntries[3];
			int32				fRenderIndex;
			int32				fReadyIndex;
			int32				fBufferCount;
			int32				fLogLevel;
			BRegion				fPendingDirty;
			int64				fAcquireReuseCount;
			int64				fReadyOverwriteCount;
			int64				fUnknownSubmitCount;
			BLocker				fLock;
};

#endif // PRESENT_QUEUE_H
