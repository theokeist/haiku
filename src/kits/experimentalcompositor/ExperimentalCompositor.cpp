/*
 * Copyright 2025, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#include <experimentalcompositor/ExperimentalCompositor.h>

#include <experimentalcompositor/ExperimentalCompositorProtocol.h>
#include <Message.h>
#include <Messenger.h>
#include <String.h>

#include <cstdio>
#include <cstring>

#undef TRACE
#define TRACE(prefix, fmt, ...) \
	printf("[%s] " fmt "\n", prefix, ##__VA_ARGS__)

BExperimentalCompositor::BExperimentalCompositor()
	:	fConnected(false)
{
}

BExperimentalCompositor::~BExperimentalCompositor()
{
	Disconnect();
}

status_t
BExperimentalCompositor::Connect()
{
	if (fConnected)
		return B_OK;

	BMessenger messenger(kExperimentalCompositorServiceName);
	if (!messenger.IsValid()) {
		return _UpdateState("connect", B_NAME_NOT_FOUND);
	}

	BMessage reply;
	status_t status = messenger.SendMessage(
		B_EXPERIMENTAL_COMPOSITOR_CONNECT, &reply);
	if (status != B_OK)
		return _UpdateState("connect", status);

	status = reply.GetInt32(kExperimentalCompositorLastError, B_OK);
	if (status != B_OK)
		return _UpdateState("connect", status);

	fConnected = true;
	fLastError = "";
	TRACE("ExperimentalCompositor", "Connected");
	return B_OK;
}

void
BExperimentalCompositor::Disconnect()
{
	if (!fConnected)
		return;

	BMessenger messenger(kExperimentalCompositorServiceName);
	if (messenger.IsValid())
		messenger.SendMessage(B_EXPERIMENTAL_COMPOSITOR_DISCONNECT);

	TRACE("ExperimentalCompositor", "Disconnected");
	fConnected = false;
}

status_t
BExperimentalCompositor::SetEffect(const char* name)
{
	if (name == NULL || name[0] == '\0')
		return _UpdateState("set effect", B_BAD_VALUE);
	if (!fConnected)
		return _UpdateState("set effect", B_NOT_CONNECTED);

	BMessenger messenger(kExperimentalCompositorServiceName);
	if (!messenger.IsValid())
		return _UpdateState("set effect", B_NAME_NOT_FOUND);

	BMessage message(B_EXPERIMENTAL_COMPOSITOR_SET_EFFECT);
	message.AddString(kExperimentalCompositorEffectName, name);

	BMessage reply;
	status_t status = messenger.SendMessage(&message, &reply);
	if (status != B_OK)
		return _UpdateState("set effect", status);

	status = reply.GetInt32(kExperimentalCompositorLastError, B_OK);
	if (status != B_OK)
		return _UpdateState("set effect", status);

	TRACE("ExperimentalCompositor", "Effect set to %s", name);
	return B_OK;
}

status_t
BExperimentalCompositor::SetTexture(const uint8* data, size_t size,
	int32 width, int32 height)
{
	if (data == NULL || size == 0 || width <= 0 || height <= 0)
		return _UpdateState("set texture", B_BAD_VALUE);
	if (!fConnected)
		return _UpdateState("set texture", B_NOT_CONNECTED);

	BMessenger messenger(kExperimentalCompositorServiceName);
	if (!messenger.IsValid())
		return _UpdateState("set texture", B_NAME_NOT_FOUND);

	BMessage message(B_EXPERIMENTAL_COMPOSITOR_SET_TEXTURE);
	message.AddData(kExperimentalCompositorTextureData, B_RAW_TYPE, data, size);
	message.AddInt32(kExperimentalCompositorTextureWidth, width);
	message.AddInt32(kExperimentalCompositorTextureHeight, height);

	BMessage reply;
	status_t status = messenger.SendMessage(&message, &reply);
	if (status != B_OK)
		return _UpdateState("set texture", status);

	status = reply.GetInt32(kExperimentalCompositorLastError, B_OK);
	if (status != B_OK)
		return _UpdateState("set texture", status);

	TRACE("ExperimentalCompositor", "Texture set (%" B_PRId32 "x%" B_PRId32 ")",
		width, height);
	return B_OK;
}

bool
BExperimentalCompositor::IsConnected() const
{
	return fConnected;
}

const BString&
BExperimentalCompositor::LastError() const
{
	return fLastError;
}

status_t
BExperimentalCompositor::_UpdateState(const char* action, status_t status)
{
	if (status == B_OK)
		return B_OK;

	fConnected = false;
	fLastError.SetToFormat("%s failed (%s)", action, strerror(status));
	TRACE("ExperimentalCompositor", "%s", fLastError.String());
	return status;
}
