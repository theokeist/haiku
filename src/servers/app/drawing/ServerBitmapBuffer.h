/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#ifndef SERVER_BITMAP_BUFFER_H
#define SERVER_BITMAP_BUFFER_H

#include "RenderingBuffer.h"
#include "ServerBitmap.h"

class ServerBitmapBuffer : public RenderingBuffer {
public:
	ServerBitmapBuffer(ServerBitmap* bitmap)
		: fBitmap(bitmap)
	{
	}

	virtual ~ServerBitmapBuffer() {}

	virtual status_t InitCheck() const
	{
		return (fBitmap != NULL && fBitmap->Bits() != NULL) ? B_OK : B_NO_INIT;
	}

	virtual color_space ColorSpace() const { return fBitmap->ColorSpace(); }
	virtual void* Bits() const { return fBitmap->Bits(); }
	virtual uint32 BytesPerRow() const { return (uint32)fBitmap->BytesPerRow(); }
	virtual uint32 Width() const { return (uint32)fBitmap->Width(); }
	virtual uint32 Height() const { return (uint32)fBitmap->Height(); }

private:
	ServerBitmap* fBitmap;
};

#endif // SERVER_BITMAP_BUFFER_H
