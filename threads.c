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

#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>

#include "gamecard.h"
#include "threadqueue.h"
#include "threads.h"

#define PATH_MAX   512
// at 30 fps, I figure 5 seconds of animation is enough. for now.
#define FRAMES_MAX 150

#define TITLE_FMT "images/%s.png"
#define FRAME_FMT "mov/%s-%04d.png"

static int buffer_memory_alloced = 0;
static pthread_mutex_t memory_counter_lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

static int threads_running = 0;
static pthread_mutex_t thread_counter_lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

static struct threadqueue loader_queue;
static pthread_t load_waiter_thread;

static void* load_waiter_func(void *arg);
static void* loader_func(void *arg);
static void threads_running_incr(int delta);
static void buffer_memory_incr(int delta);

int init_threads()
{
	fprintf(stderr, "Initializing threads\n");

	thread_queue_init(&loader_queue);
	if (pthread_create(&load_waiter_thread, NULL, load_waiter_func, NULL) != 0) {
		return 1;
	}

	return 0;
}

void destroy_threads()
{
	fprintf(stderr, "Stopping threads... ");

	thread_queue_add(&loader_queue, NULL, 0);
	pthread_join(load_waiter_thread, NULL);
	pthread_mutex_destroy(&thread_counter_lock);
	thread_queue_cleanup(&loader_queue, 0);

	fprintf(stderr, "OK\n");
}

void system_status()
{
	fprintf(stderr, "Threads: %d - RAM: %dMB (%d)kB\n",
		threads_running,
		buffer_memory_alloced / (1024*1024), buffer_memory_alloced / 1024);
}

void add_to_queue(struct gamecard *gc)
{
	thread_queue_add(&loader_queue, gc, 0);
}

static void* load_waiter_func(void *arg)
{
	threads_running_incr(+1);

	struct threadmsg message;
	while (!pim_quit) {
		if (thread_queue_get(&loader_queue, NULL, &message) != 0) {
			fprintf(stderr, "error: thread_queue_get returned an error\n");
			pim_quit = 1;
			break;
		}

		if (!message.data) {
			// NULL data is a quit signal
			break;
		}

		struct gamecard *gc = (struct gamecard *)message.data;
		pthread_t anim_thread;
		if (pthread_create(&anim_thread, NULL, loader_func, gc) != 0) {
			perror("thread_create(loader_func) returned an error\n");
			pim_quit = 1;
			break;
		}
	}

	threads_running_incr(-1);

	return NULL;
}

static void* loader_func(void *arg)
{
	threads_running_incr(+1);

	int load = 0;
	int success = 0;
	struct gamecard *gc = (struct gamecard *)arg;

	// Check and update the status
	pthread_mutex_lock(&gc->load_lock);
	if (gc->load_status == 0) {
		gc->load_status = STATUS_LOADING;
		load = 1;
	}
	pthread_mutex_unlock(&gc->load_lock);

	if (!load) {
		goto done;
	}

	int w, h, size;
	void *bmp;
	char path[PATH_MAX];
    struct stat st;

	snprintf(path, PATH_MAX - 1, TITLE_FMT, gc->archive);
	if (stat(path, &st) == 0) {
		// Found title card - load it
		if ((bmp = load_bitmap(path, &w, &h, &size)) != NULL) {
			gc->screenshot_width = w;
			gc->screenshot_height = h;
			gc->screenshot_bitmap = bmp;

			buffer_memory_incr(size);
			bitmap_loaded_callback(gc);

			fprintf(stderr, "%s: loaded title\n", gc->archive);
			success = 1; // at least we have a title
		}
	}

	// Check to see if the first frame is available
	snprintf(path, PATH_MAX - 1, FRAME_FMT, gc->archive, 0);
	if (stat(path, &st) == 0) {
		void *temp[FRAMES_MAX];
		int i, found = 0, total_size = 0;
		for (i = 1; i < FRAMES_MAX; i++) {
			void *bmp = load_bitmap(path, &w, &h, &size);
			if (bmp == NULL) {
				break;
			}

			if (gc->screenshot_width == 0 || gc->screenshot_height == 0) {
				gc->screenshot_width = w;
				gc->screenshot_height = h;
			}

			temp[found++] = bmp;
			total_size += size;

			snprintf(path, PATH_MAX - 1, FRAME_FMT, gc->archive, i);
		}

		if (found > 0) {
			if ((gc->frames = (void **)calloc(found, sizeof(void *))) == NULL) {
				for (i = 0; i < found; i++) {
					free(temp[i]);
				}
				goto cleanup;
			}

			for (i = 0; i < found; i++) {
				gc->frames[i] = temp[i];
			}

			buffer_memory_incr(total_size);
			gc->frame_count = found;

			success = 1;
			fprintf(stderr, "%s: loaded %d frames\n",
				gc->archive, gc->frame_count);

			bitmap_loaded_callback(gc);
		}
	}

cleanup:
	pthread_mutex_lock(&gc->load_lock);
	gc->load_status = success ? STATUS_LOADED : STATUS_ERROR;
	pthread_mutex_unlock(&gc->load_lock);
done:
	threads_running_incr(-1);

	return NULL;
}

static void threads_running_incr(int delta)
{
	pthread_mutex_lock(&thread_counter_lock);
	threads_running += delta;
	pthread_mutex_unlock(&thread_counter_lock);
	// FIXME
	system_status();
}

static void buffer_memory_incr(int delta)
{
	pthread_mutex_lock(&memory_counter_lock);
	buffer_memory_alloced += delta;
	pthread_mutex_unlock(&memory_counter_lock);
	// FIXME
	system_status();
}
