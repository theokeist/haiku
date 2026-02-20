/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */

#include <Application.h>
#include <Message.h>
#include <Messenger.h>

#include <private/app/ServerProtocol.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void
print_usage(const char* name)
{
	fprintf(stderr, "usage: %s <window_token> <alpha>\n", name);
	fprintf(stderr, "\twindow_token: int32 server window token\n");
	fprintf(stderr, "\talpha: float in range 0..1\n");
}


int
main(int argc, char** argv)
{
	if (argc != 3) {
		print_usage(argv[0]);
		return 1;
	}

	char* end = NULL;
	errno = 0;
	long windowToken = strtol(argv[1], &end, 10);
	if (errno != 0 || end == argv[1] || *end != '\0') {
		fprintf(stderr, "invalid window token: %s\n", argv[1]);
		return 1;
	}

	errno = 0;
	float alpha = strtof(argv[2], &end);
	if (errno != 0 || end == argv[2] || *end != '\0') {
		fprintf(stderr, "invalid alpha: %s\n", argv[2]);
		return 1;
	}

	BApplication app("application/x-vnd.Haiku-setwindowalpha");

	BMessenger messenger("application/x-vnd.Haiku-app_server");
	if (!messenger.IsValid()) {
		fprintf(stderr, "failed to contact app_server\n");
		return 1;
	}

	// Alpha is a normalized opacity scalar: 0 = transparent, 1 = opaque.
	// Message routing is handled in app_server/Desktop so window locking rules
	// remain centralized.
	BMessage message(AS_INTERNAL_SET_WINDOW_ALPHA);
	message.AddInt32("window", (int32)windowToken);
	message.AddFloat("alpha", alpha);

	status_t status = messenger.SendMessage(&message);
	if (status != B_OK) {
		fprintf(stderr, "failed to send alpha message: %s\n",
			strerror(status));
		return 1;
	}

	return 0;
}
