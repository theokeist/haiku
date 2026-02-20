/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */

#include <Application.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <private/app/ServerProtocol.h>


static void
print_usage(const char* name)
{
	fprintf(stderr, "usage: %s <window_token> [options]\n", name);
	fprintf(stderr, "options:\n");
	fprintf(stderr, "  --alpha <value>       Set window alpha (0..1)\n");
	fprintf(stderr, "  --blur <on|off>       Enable/disable blur\n");
	fprintf(stderr, "  --radius <value>      Set blur radius (0..50)\n");
	fprintf(stderr, "  --animate <duration>  Animate alpha change (e.g. 150ms)\n");
	fprintf(stderr, "\nexamples:\n");
	fprintf(stderr, "  %s 123 --alpha 0.7 --animate 150ms\n", name);
	fprintf(stderr, "  %s 123 --blur on --radius 8\n", name);
}


static bool
parse_bool(const char* text, bool& value)
{
	if (strcasecmp(text, "on") == 0 || strcasecmp(text, "true") == 0
		|| strcmp(text, "1") == 0) {
		value = true;
		return true;
	}
	if (strcasecmp(text, "off") == 0 || strcasecmp(text, "false") == 0
		|| strcmp(text, "0") == 0) {
		value = false;
		return true;
	}

	return false;
}


static bool
parse_float(const char* text, float& value)
{
	char* end = NULL;
	errno = 0;
	value = strtof(text, &end);
	if (errno != 0 || end == text || *end != '\0')
		return false;
	return true;
}


static bool
parse_duration(const char* text, bigtime_t& value)
{
	char* end = NULL;
	errno = 0;
	double parsed = strtod(text, &end);
	if (errno != 0 || end == text)
		return false;

	if (*end == '\0') {
		value = (bigtime_t)(parsed * 1000.0);
		return true;
	}

	if (strcasecmp(end, "ms") == 0) {
		value = (bigtime_t)(parsed * 1000.0);
		return true;
	}
	if (strcasecmp(end, "us") == 0) {
		value = (bigtime_t)parsed;
		return true;
	}
	if (strcasecmp(end, "s") == 0) {
		value = (bigtime_t)(parsed * 1000000.0);
		return true;
	}

	return false;
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
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

	bool hasAlpha = false;
	bool hasBlur = false;
	bool hasRadius = false;
	bool animate = false;
	float alpha = 1.0f;
	bool blurEnabled = false;
	float blurRadius = 0.0f;
	bigtime_t duration = 0;

	for (int32 i = 2; i < argc; i++) {
		if (strcmp(argv[i], "--alpha") == 0) {
			if (i + 1 >= argc || !parse_float(argv[i + 1], alpha)) {
				fprintf(stderr, "invalid alpha value\n");
				return 1;
			}
			hasAlpha = true;
			i++;
			continue;
		}

		if (strcmp(argv[i], "--blur") == 0) {
			if (i + 1 >= argc || !parse_bool(argv[i + 1], blurEnabled)) {
				fprintf(stderr, "invalid blur value (use on/off)\n");
				return 1;
			}
			hasBlur = true;
			i++;
			continue;
		}

		if (strcmp(argv[i], "--radius") == 0) {
			if (i + 1 >= argc || !parse_float(argv[i + 1], blurRadius)) {
				fprintf(stderr, "invalid blur radius\n");
				return 1;
			}
			hasRadius = true;
			i++;
			continue;
		}

		if (strcmp(argv[i], "--animate") == 0) {
			if (i + 1 >= argc || !parse_duration(argv[i + 1], duration)) {
				fprintf(stderr, "invalid duration (e.g. 150ms, 200000us, 1s)\n");
				return 1;
			}
			animate = true;
			i++;
			continue;
		}

		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			print_usage(argv[0]);
			return 0;
		}

		fprintf(stderr, "unknown option: %s\n", argv[i]);
		print_usage(argv[0]);
		return 1;
	}

	if (!hasAlpha && !hasBlur && !hasRadius) {
		fprintf(stderr, "no effects specified\n");
		print_usage(argv[0]);
		return 1;
	}

	BApplication app("application/x-vnd.Haiku-setwindoweffects");

	BMessenger messenger("application/x-vnd.Haiku-app_server");
	if (!messenger.IsValid()) {
		fprintf(stderr, "failed to contact app_server\n");
		return 1;
	}

	BMessage message(AS_PRIVATE_SET_WINDOW_EFFECTS);
	message.AddInt32("window", (int32)windowToken);
	if (hasAlpha)
		message.AddFloat("alpha", alpha);
	if (hasBlur)
		message.AddBool("blur", blurEnabled);
	if (hasRadius)
		message.AddFloat("blur_radius", blurRadius);
	if (animate) {
		message.AddBool("animate", true);
		message.AddInt64("duration", duration);
	}

	BMessage reply;
	status_t status = messenger.SendMessage(&message, &reply);
	if (status != B_OK) {
		fprintf(stderr, "failed to send effects message: %s\n",
			strerror(status));
		return 1;
	}

	if (reply.what != B_OK) {
		fprintf(stderr, "app_server rejected effects request\n");
		return 1;
	}

	float clampedAlpha = reply.GetFloat("alpha", alpha);
	bool clampedBlur = reply.GetBool("blur", blurEnabled);
	float clampedRadius = reply.GetFloat("blur_radius", blurRadius);
	printf("alpha=%.2f blur=%s radius=%.1f\n", clampedAlpha,
		clampedBlur ? "on" : "off", clampedRadius);

	return 0;
}
