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
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <SDL/SDL.h>
#include <math.h>

#include "phl_gles.h"
#include "phl_matrix.h"

#include "common.h"
#include "shader.h"
#include "quad.h"
#include "sprite.h"

#include "threads.h"
#include "temp.h"

#define PRELOAD_MARGIN 2

#define SHADE_FACTOR    1.33f
#define ANIMATION_SPEED 0.01f

#define JOY_DEADZONE 0x4000

#define GO_NEXT     0
#define GO_PREVIOUS 1

#define EVENT_RESET_GC 1

static int init_video();
static void destroy_video();
static void draw();
static void go_to(int which);
static void handle_event(SDL_Event *event);
static void preload(int current);

static const char *vertex_shader_src =
	"uniform mat4 u_vp_matrix;"
	"attribute vec4 a_position;"
	"attribute vec2 a_texcoord;"
	"attribute vec4 a_color;"
	"varying mediump vec2 v_texcoord;"
	"varying lowp vec4 v_color;"
	"void main() {"
		"v_texcoord = a_texcoord;"
		"v_color = a_color;"
		"gl_Position = u_vp_matrix * a_position;"
	"}";

static const char *fragment_shader_src =
	"varying mediump vec2 v_texcoord;"
	"uniform sampler2D u_texture;"
	"varying lowp vec4 v_color;"
	"void main() {"
		"gl_FragColor = texture2D(u_texture, v_texcoord) * v_color;"
	"}";

static struct gamecard *gamecards;
static int card_count;
static int previous_card = 0;
static int selected_card = 0;

#define SPRITES 2
struct sprite sprites[SPRITES];
static struct shader_obj shader;

int pim_quit = 0;

static void go_to(int which)
{
	if (which == GO_PREVIOUS) {
		// Left
		previous_card = selected_card;
		if (--selected_card < 0) {
			selected_card = card_count - 1;
		}

		sprites[0].id = gamecards[selected_card].id;
		sprite_set_texture(&sprites[0], &gamecards[selected_card]);

		preload(selected_card);
/* FIXME
		reset_bounds(&gamecards[previous_card],
			&gamecards[selected_card]);
*/
	} else if (which == GO_NEXT) {
		// Right
		previous_card = selected_card;
		if (++selected_card >= card_count) {
			selected_card = 0;
		}

		sprites[0].id = gamecards[selected_card].id;
		sprite_set_texture(&sprites[0], &gamecards[selected_card]);

		preload(selected_card);
/* FIXME
		reset_bounds(&gamecards[previous_card],
			&gamecards[selected_card]);
*/
	}
}

static void preload(int current)
{
	int i;
	for (i = -PRELOAD_MARGIN; i <= PRELOAD_MARGIN; i++) {
		int card_index = current + i;
		if (card_index < 0) {
			card_index += card_count;
		} else if (card_index >= card_count) {
			card_index -= card_count;
		}

		struct gamecard *gc = &gamecards[card_index];
		if (gc->status == 0) {
			// A race condition is possible, but the bitmap loader will check
			// again in thread-safe fashion
			add_to_queue(&gamecards[card_index]);
		}
	}
}

static void handle_event(SDL_Event *event)
{
	switch (event->type) {
	case SDL_USEREVENT:
		switch (event->user.code) {
		case EVENT_RESET_GC:
			{
				struct gamecard *gc = (struct gamecard *)event->user.data1;
				if (gc->id == sprites[0].id) {
					sprite_set_texture(&sprites[0], gc);
				} else if (gc->id == sprites[1].id) {
					sprite_set_texture(&sprites[1], gc);
				}
			}
			break;
		}
		break;
	case SDL_KEYUP:
		{
			// FIXME
			SDL_KeyboardEvent *keyEvent = (SDL_KeyboardEvent *)event;
			if (keyEvent->keysym.sym == SDLK_LEFT) {
				go_to(GO_PREVIOUS);
			} else if (keyEvent->keysym.sym == SDLK_RIGHT) {
				go_to(GO_NEXT);
			} else if (keyEvent->keysym.sym == SDLK_ESCAPE) {
				pim_quit = 1;
			}
		}
		break;
	case SDL_JOYBUTTONDOWN:
		fprintf(stderr, "joydown\n");
		pim_quit = 1;
		break;
	case SDL_JOYBUTTONUP:
		fprintf(stderr, "joyup\n");
		break;
	case SDL_JOYAXISMOTION: 
		{
			SDL_JoyAxisEvent *joyEvent = (SDL_JoyAxisEvent *)event;
			if (joyEvent->which == 0) {
				if (joyEvent->axis == 0) {
					if (joyEvent->value < -JOY_DEADZONE) {
						go_to(GO_PREVIOUS);
					} else if (joyEvent->value > JOY_DEADZONE) {
						go_to(GO_NEXT);
					}
				} else if (joyEvent->axis == 1) {
					if (joyEvent->value < -JOY_DEADZONE) {
						// Up
					} else if (joyEvent->value > JOY_DEADZONE) {
						// Down
					}
				}
			}
		}
		break;
	}
}

static int init_video()
{
	if (!phl_gles_init()) {
		return 1;
	}

	if (shader_init(&shader, vertex_shader_src, fragment_shader_src) != 0) {
		phl_gles_shutdown();
		return 1;
	}

	int i, j;
	for (i = 0; i < SPRITES; i++) {
		if (sprite_init(&sprites[i]) != 0) {
			shader_destroy(&shader);
			phl_gles_shutdown();
			
			for (j = 0; j < i; j++) {
				sprite_destroy(&sprites[j]);
			}
			return 1;
		}
	}

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_DITHER);

	glClear(GL_COLOR_BUFFER_BIT);
	glViewport(0, 0, phl_gles_screen_width, phl_gles_screen_height);

	glDisable(GL_BLEND);

	SDL_ShowCursor(0);
	SDL_SetVideoMode(0,0,0,0);

	return 0;
}

static void destroy_video()
{
	fprintf(stderr, "Destroying video... ");

	shader_destroy(&shader);

	int i;
	for (i = 0; i < SPRITES; i++) {
		sprite_destroy(&sprites[i]);
	}

	phl_gles_shutdown();

	fprintf(stderr, "OK\n");
}

static void draw()
{
	struct sprite *sprite = &sprites[0];
	
	static struct phl_matrix projection;

	phl_matrix_identity(&projection);
	phl_matrix_ortho(&projection, -0.5f, 0.5f, -0.5f, +0.5f, -1.0f, 1.0f);
	phl_matrix_scale(&projection, sprite->x_ratio, sprite->y_ratio, 0);

	glUseProgram(shader.program);
	glUniformMatrix4fv(shader.u_vp_matrix, 1, GL_FALSE, &projection.xx);

	sprite_draw(sprite, &shader);

	phl_gles_swap_buffers();
}

void bitmap_loaded_callback(struct gamecard *gc)
{
	// This callback is called from another thread. Since we can't do
	// OpenGL-related stuff here, signal via SDL's event queue to do it on
	// the main thread
	if (gc->id == sprites[0].id || gc->id == sprites[1].id) {
		SDL_Event event;
		event.type = SDL_USEREVENT;
		event.user.code = EVENT_RESET_GC;
		event.user.data1 = gc;
		SDL_PushEvent(&event);
	}
}

int main(int argc, char *argv[])
{
	if (init_threads() != 0) {
		fprintf(stderr, "error: Thread init failed\n");
		return 1;
	}

	const char **romname;
	for (romname = romnames, card_count = 0; *romname; romname++, card_count++);

	gamecards = (struct gamecard *)calloc(card_count, sizeof(struct gamecard));
	struct gamecard *gc;
	int i = 0;
	for (romname = romnames, gc = gamecards; *romname; romname++, gc++, i++) {
		gamecard_init(gc);
		gc->archive = strdup(*romname);
		gc->id = i;

		int length = snprintf(NULL, 0, "images/%s.png", *romname);
		gc->screenshot_path = (char *)malloc(sizeof(char) * (length + 1));
		sprintf(gc->screenshot_path, "images/%s.png", *romname);
	}

	if (SDL_Init(SDL_INIT_JOYSTICK|SDL_INIT_EVENTTHREAD|SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		destroy_threads();
		return 1;
	}

	SDL_JoystickEventState(SDL_ENABLE);

	if (init_video()) {
		fprintf(stderr, "init_video() failed\n");
		destroy_threads();
		SDL_Quit();
		return 1;
	}

	if (SDL_NumJoysticks() > 0) {
		SDL_JoystickOpen(0);
	}

	// FIXME
	sprites[0].id = 0;
	sprites[1].id = 1;

	preload(selected_card);

	SDL_Event event;
	while (!pim_quit) {
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT ) {
				pim_quit = 1;
				break;
			} else {
				handle_event(&event);
			}
		}

		draw();
	}

	destroy_threads();
	destroy_video();
	SDL_Quit();

	for (i = 0, gc = gamecards; i < card_count; i++, gc++) {
		gamecard_free(gc);
	}
	free(gamecards);

	return 0;
}
