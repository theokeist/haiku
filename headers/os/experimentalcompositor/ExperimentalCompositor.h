/*
 * Copyright 2025, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#ifndef _EXPERIMENTAL_COMPOSITOR_H
#define _EXPERIMENTAL_COMPOSITOR_H

#include <String.h>
#include <SupportDefs.h>

class BExperimentalCompositor {
public:
							BExperimentalCompositor();
							~BExperimentalCompositor();

	status_t				Connect();
	void					Disconnect();
	status_t				SetEffect(const char* name);
	status_t				SetTexture(const uint8* data, size_t size,
								int32 width, int32 height);

	bool					IsConnected() const;
	const BString&		LastError() const;

private:
	status_t				_UpdateState(const char* action, status_t status);

	bool					fConnected;
	BString					fLastError;
};

#endif	// _EXPERIMENTAL_COMPOSITOR_H
