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

#ifndef GAMECARD_H
#define GAMECARD_H

struct emulator {
	char *path;
	char *exe;
	char *args;
};

void emulator_init(struct emulator *e);
void emulator_free(struct emulator *e);

#define STATUS_LOADING 1
#define STATUS_LOADED  2
#define STATUS_ERROR   3

struct gamecard {
	int id;
	char *archive;
	char *args;
	char *screenshot_path;
	char *title;
	void *screenshot_bitmap;
	int screenshot_width;
	int screenshot_height;
	int load_status;
	pthread_mutex_t load_lock;
	void **frames;
	int frame_count;
	int frame;
	const struct emulator *emulator;
};

void gamecard_init(struct gamecard *gc);
void gamecard_free(struct gamecard *gc);
void gamecard_dump(const struct gamecard *gc);

#endif // GAMECARD_H
