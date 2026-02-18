/*
 * Copyright 2025, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#ifndef _EXPERIMENTAL_COMPOSITOR_PROTOCOL_H
#define _EXPERIMENTAL_COMPOSITOR_PROTOCOL_H

#include <SupportDefs.h>

#define B_EXPERIMENTAL_COMPOSITOR_PROTOCOL_VERSION 1

static const char* const kExperimentalCompositorServiceName
	= "x-vnd.haiku-experimental-compositor";

enum {
	B_EXPERIMENTAL_COMPOSITOR_CONNECT		= 'excc',
	B_EXPERIMENTAL_COMPOSITOR_DISCONNECT	= 'excd',
	B_EXPERIMENTAL_COMPOSITOR_SET_EFFECT	= 'exef',
	B_EXPERIMENTAL_COMPOSITOR_SET_TEXTURE	= 'extx',
	B_EXPERIMENTAL_COMPOSITOR_QUERY_STATUS	= 'exqs'
};

static const char* const kExperimentalCompositorEffectName = "effect";
static const char* const kExperimentalCompositorTextureData = "texture:data";
static const char* const kExperimentalCompositorTextureWidth = "texture:width";
static const char* const kExperimentalCompositorTextureHeight = "texture:height";
static const char* const kExperimentalCompositorLastError = "error";

#endif	// _EXPERIMENTAL_COMPOSITOR_PROTOCOL_H
