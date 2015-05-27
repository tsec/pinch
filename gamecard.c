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
	gc->lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
}

void gamecard_free(struct gamecard *gc)
{
	pthread_mutex_destroy(&gc->lock);
	free(gc->archive); gc->archive = NULL;
	free(gc->screenshot_path); gc->screenshot_path = NULL;
	free(gc->screenshot_bitmap); gc->screenshot_bitmap = NULL;
}

void gamecard_set_bitmap(struct gamecard *gc,
	int width, int height, void *bmp)
{
	free(gc->screenshot_bitmap);

	gc->screenshot_width = width;
	gc->screenshot_height = height;
	gc->screenshot_bitmap = bmp;
	gc->status = STATUS_LOADED;
}
