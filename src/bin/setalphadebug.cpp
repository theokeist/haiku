/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */

#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void
print_usage(const char* name)
{
	fprintf(stderr, "usage: %s <on|off>\n", name);
}


static status_t
get_alpha_debug_settings_path(BPath& path)
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

	return path.Append("alpha_debug");
}


int
main(int argc, char** argv)
{
	if (argc != 2) {
		print_usage(argv[0]);
		return 1;
	}

	bool enabled = false;
	if (strcmp(argv[1], "on") == 0)
		enabled = true;
	else if (strcmp(argv[1], "off") == 0)
		enabled = false;
	else {
		print_usage(argv[0]);
		return 1;
	}

	BPath path;
	status_t status = get_alpha_debug_settings_path(path);
	if (status != B_OK) {
		fprintf(stderr, "failed to resolve settings path: %s\n",
			strerror(status));
		return 1;
	}

	BMessage settings;
	settings.AddBool("enabled", enabled);

	BFile file(path.Path(), B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
	status = file.InitCheck();
	if (status != B_OK) {
		fprintf(stderr, "failed to open settings file: %s\n",
			strerror(status));
		return 1;
	}

	status = settings.Flatten(&file);
	if (status != B_OK) {
		fprintf(stderr, "failed to write settings: %s\n",
			strerror(status));
		return 1;
	}

	return 0;
}
