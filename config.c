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

#include "config.h"

static char* glob_file(const char *path);

int config_load(struct config *config, const char *path)
{
	int ret_val = 1;
	config->last_selected = NULL;

	char *contents = glob_file(path);
	if (contents) {
		cJSON *root = cJSON_Parse(contents);
		if (root) {
			cJSON *last_selected_node;
			if ((last_selected_node = cJSON_GetObjectItem(root, "lastSelected"))) {
				if (last_selected_node->valuestring) {
					config->last_selected = strdup(last_selected_node->valuestring);
					ret_val = 0;
				}
			}
			cJSON_Delete(root);
		}
		free(contents);
	}

	return ret_val;
}

int config_save(struct config *config, const char *path)
{
	int ret_val = 1;

	cJSON *root = cJSON_CreateObject();
	if (config->last_selected) {
		cJSON_AddItemToObject(root, "lastSelected",
			cJSON_CreateString(config->last_selected));
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

void config_set_last_selected(struct config *config, const char *value)
{
	free(config->last_selected);
	config->last_selected = NULL;

	if (value) {
		config->last_selected = strdup(value);
	}
}

void config_destroy(struct config *config)
{
	free(config->last_selected);
	config->last_selected = NULL;
}

static char* glob_file(const char *path)
{
	char *contents = NULL;
	
	FILE *file = fopen(path,"r");
	if (file) {
		// Determine size
		fseek(file, 0L, SEEK_END);
		long size = ftell(file);
		rewind(file);
	
		// Allocate memory
		contents = (char *)calloc(size + 1, 1);
		if (contents) {
			// Read contents
			if (fread(contents, size, 1, file) != 1) {
				free(contents);
				contents = NULL;
			}
		}

		fclose(file);
	}
	
	return contents;
}
