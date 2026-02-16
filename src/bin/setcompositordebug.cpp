/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */

#include <Application.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Messenger.h>
#include <Path.h>

#include <private/app/ServerProtocol.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void
print_usage(const char* name)
{
	fprintf(stderr,
		"usage: %s [--force-all on|off] [--opacity <0..1|-1>]"
		" [--overlay on|off] [--timings on|off] [--stress on|off]\n",
		name);
}


static status_t
settings_path(BPath& path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	status = path.Append("system/app_server");
	if (status != B_OK)
		return status;
	status = create_directory(path.Path(), 0755);
	if (status != B_OK)
		return status;
	return path.Append("compositor_debug");
}


static bool
parse_bool(const char* value, bool& out)
{
	if (strcmp(value, "on") == 0) {
		out = true;
		return true;
	}
	if (strcmp(value, "off") == 0) {
		out = false;
		return true;
	}
	return false;
}


int
main(int argc, char** argv)
{
	bool forceAll = false;
	float opacity = -1.0f;
	bool overlay = false;
	bool timings = false;
	bool stress = false;

	for (int i = 1; i < argc; i += 2) {
		if (i + 1 >= argc) {
			print_usage(argv[0]);
			return 1;
		}

		const char* key = argv[i];
		const char* value = argv[i + 1];
		if (strcmp(key, "--force-all") == 0) {
			if (!parse_bool(value, forceAll)) {
				print_usage(argv[0]);
				return 1;
			}
		} else if (strcmp(key, "--opacity") == 0) {
			opacity = strtof(value, NULL);
		} else if (strcmp(key, "--overlay") == 0) {
			if (!parse_bool(value, overlay)) {
				print_usage(argv[0]);
				return 1;
			}
		} else if (strcmp(key, "--timings") == 0) {
			if (!parse_bool(value, timings)) {
				print_usage(argv[0]);
				return 1;
			}
		} else if (strcmp(key, "--stress") == 0) {
			if (!parse_bool(value, stress)) {
				print_usage(argv[0]);
				return 1;
			}
		} else {
			print_usage(argv[0]);
			return 1;
		}
	}

	BMessage settings;
	settings.AddBool("forceBlurAll", forceAll);
	settings.AddFloat("forceOpacity", opacity);
	settings.AddBool("showOverlay", overlay);
	settings.AddBool("logTimings", timings);
	settings.AddBool("stressInvalidate", stress);

	BPath path;
	if (settings_path(path) == B_OK) {
		BFile file(path.Path(), B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
		if (file.InitCheck() == B_OK)
			settings.Flatten(&file);
	}

	BApplication app("application/x-vnd.Haiku-setcompositordebug");
	BMessenger messenger("application/x-vnd.Haiku-app_server");
	settings.what = AS_INTERNAL_SET_COMPOSITOR_DEBUG_OPTIONS;
	if (!messenger.IsValid() || messenger.SendMessage(&settings) != B_OK) {
		fprintf(stderr, "warning: failed to notify app_server live\n");
		return 1;
	}

	return 0;
}
