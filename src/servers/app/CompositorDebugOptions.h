/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#ifndef COMPOSITOR_DEBUG_OPTIONS_H
#define COMPOSITOR_DEBUG_OPTIONS_H

#include <SupportDefs.h>

struct CompositorDebugOptions {
	bool	forceBlurAll;
	float	forceOpacity;
	bool	showOverlay;
	bool	logTimings;
	bool	stressInvalidate;

	CompositorDebugOptions()
		:
		forceBlurAll(false),
		forceOpacity(-1.0f),
		showOverlay(false),
		logTimings(false),
		stressInvalidate(false)
	{
	}
};

#endif // COMPOSITOR_DEBUG_OPTIONS_H
