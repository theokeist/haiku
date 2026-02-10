/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */

#include <Application.h>
#include <SupportDefs.h>
#include <WindowInfo.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void
print_usage(const char* name)
{
	fprintf(stderr, "usage: %s [team_id]\n", name);
	fprintf(stderr, "\tteam_id: optional team id to filter window list\n");
}


int
main(int argc, char** argv)
{
	team_id team = -1;
	if (argc > 2) {
		print_usage(argv[0]);
		return 1;
	}

	if (argc == 2) {
		char* end = NULL;
		errno = 0;
		long parsed = strtol(argv[1], &end, 10);
		if (errno != 0 || end == argv[1] || *end != '\0') {
			fprintf(stderr, "invalid team id: %s\n", argv[1]);
			return 1;
		}
		team = static_cast<team_id>(parsed);
	}

	BApplication app("application/x-vnd.Haiku-listwindowtokens");

	int32 count = 0;
	int32* tokens = get_token_list(team, &count);
	if (tokens == NULL) {
		fprintf(stderr, "failed to fetch window list\n");
		return 1;
	}

	for (int32 i = 0; i < count; i++) {
		int32 token = tokens[i];
		client_window_info* info = get_window_info(token);
		const char* title = "";
		if (info != NULL && info->name[0] != '\0')
			title = info->name;
		printf("%" B_PRId32 "\t%s\n", token, title);
		free(info);
	}

	free(tokens);
	return 0;
}
