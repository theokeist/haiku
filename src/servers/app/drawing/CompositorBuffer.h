/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#ifndef COMPOSITOR_BUFFER_H
#define COMPOSITOR_BUFFER_H

#include <Referenceable.h>
#include "MallocBuffer.h"

class CompositorBuffer : public BReferenceable {
public:
	CompositorBuffer(int32 width, int32 height)
		: fBuffer(width, height)
	{
	}

	virtual ~CompositorBuffer()
	{
	}

	RenderingBuffer* Buffer() { return &fBuffer; }
	status_t InitCheck() const { return fBuffer.InitCheck(); }

private:
	MallocBuffer fBuffer;
};

#endif // COMPOSITOR_BUFFER_H
