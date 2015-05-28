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
#include <pthread.h>

#include "gamecard.h"

void gamecard_dump(const struct gamecard *gc)
{
	fprintf(stderr, "[%s,%s,%s,%dx%d]\n", gc->archive, gc->screenshot_path,
		gc->screenshot_bitmap ? "(bitmap)" : "",
		gc->screenshot_width, gc->screenshot_height);
}

void gamecard_init(struct gamecard *gc)
{
	memset(gc, 0, sizeof(gc));

	gc->title_lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	gc->frame_lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
}

void gamecard_free(struct gamecard *gc)
{
	free(gc->archive); gc->archive = NULL;
	free(gc->screenshot_path); gc->screenshot_path = NULL;
	free(gc->screenshot_bitmap); gc->screenshot_bitmap = NULL;

	int i;
	for (i = 0; i < gc->frame_count; i++) {
		free(gc->frames[i]);
	}
	free(gc->frames); gc->frames = NULL;

	pthread_mutex_destroy(&gc->title_lock);
	pthread_mutex_destroy(&gc->frame_lock);
}
