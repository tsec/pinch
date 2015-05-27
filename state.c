/**
** Copyright (C) 2015 Akop Karapetyan
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
** http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cjson/cJSON.h"
#include "common.h"

#include "state.h"

int state_load(struct state *state, const char *path)
{
	int ret_val = 1;
	state->last_selected = NULL;

	char *contents = glob_file(path);
	if (contents) {
		cJSON *root = cJSON_Parse(contents);
		if (root) {
			cJSON *last_selected_node;
			if ((last_selected_node = cJSON_GetObjectItem(root, "lastSelected"))) {
				if (last_selected_node->valuestring) {
					state->last_selected = strdup(last_selected_node->valuestring);
					ret_val = 0;
				}
			}
			cJSON_Delete(root);
		}
		free(contents);
	}

	return ret_val;
}

int state_save(struct state *state, const char *path)
{
	int ret_val = 1;

	cJSON *root = cJSON_CreateObject();
	if (state->last_selected) {
		cJSON_AddItemToObject(root, "lastSelected",
			cJSON_CreateString(state->last_selected));
	}

	char *contents = cJSON_PrintUnformatted(root);
	if (contents) {
		cJSON_Delete(root);

		FILE *file = fopen(path, "w");
		if (file) {
			fprintf(file, "%s", contents);
			fclose(file);
			ret_val = 0;
		}

		free(contents);
	}

	return ret_val;
}

void state_set_last_selected(struct state *state, const char *value)
{
	free(state->last_selected);
	state->last_selected = NULL;

	if (value) {
		state->last_selected = strdup(value);
	}
}

void state_destroy(struct state *state)
{
	free(state->last_selected);
	state->last_selected = NULL;
}
