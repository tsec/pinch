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

#ifndef PIM_COMMON_H
#define PIM_COMMON_H

#define STATUS_LOADING 1
#define STATUS_LOADED  2
#define STATUS_ERROR   3

extern int pim_quit;

struct gamecard {
	int id;
	char *archive;
	char *screenshot_path;
	void *screenshot_bitmap;
	int screenshot_width;
	int screenshot_height;
	int status;
	pthread_mutex_t lock;
};

void gamecard_init(struct gamecard *gc);
void gamecard_free(struct gamecard *gc);
void gamecard_set_bitmap(struct gamecard *gc,
	int width, int height, void *bmp);
void gamecard_dump(const struct gamecard *gc);

void* load_bitmap(const char *path, int *width, int *height, int *size);
char* glob_file(const char *path);

#endif // PIM_COMMON_H
