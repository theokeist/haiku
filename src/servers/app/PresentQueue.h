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

class PresentQueue {
public:
								PresentQueue(int32 width, int32 height,
									color_space format);

			status_t			InitCheck() const;
			status_t			Resize(int32 width, int32 height,
									color_space format);

			RenderingBuffer*	AcquireForRender();
			void				Submit(RenderingBuffer* buffer,
									const BRegion& dirty);
			bigtime_t			PresentNext(HWInterface& interface,
									bool vsync);
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
			BLocker				fLock;
};

#endif // PRESENT_QUEUE_H
