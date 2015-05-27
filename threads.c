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

#include "gamecard.h"
#include "threadqueue.h"
#include "threads.h"

static int buffer_memory_alloced = 0;
static pthread_mutex_t memory_counter_lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

static int threads_running = 0;
static pthread_mutex_t thread_counter_lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

static struct threadqueue loader_queue;
static pthread_t load_waiter_thread;

static void* load_waiter_func(void *arg);
static void* bitmap_loader_func(void *arg);
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
		pthread_t loader_thread;
		if (pthread_create(&loader_thread, NULL, bitmap_loader_func, gc) != 0) {
			perror("thread_create(bitmap_loader_func) returned an error\n");
			pim_quit = 1;
			break;
		}
	}

	threads_running_incr(-1);

	return NULL;
}

static void* bitmap_loader_func(void *arg)
{
	threads_running_incr(+1);

	struct gamecard *gc = (struct gamecard *)arg;
	pthread_mutex_lock(&gc->lock);

	if (gc->status == 0) {
		gc->status = STATUS_LOADING;

		int w, h, size;
		void *bmp = load_bitmap(gc->screenshot_path, &w, &h, &size);
		if (bmp) {
			gamecard_set_bitmap(gc, w, h, bmp);
			buffer_memory_incr(size);
		} else {
			gc->status = STATUS_ERROR;
			// FIXME: error loading bitmap - use a placeholder
		}
	}

	pthread_mutex_unlock(&gc->lock);
	bitmap_loaded_callback(gc);

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
