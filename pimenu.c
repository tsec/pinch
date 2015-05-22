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

#include "shader.h"
#include "quad.h"

#include "common.h"
#include "threads.h"
#include "temp.h"

#define PRELOAD_MARGIN 2

#define TEXTURE_COUNT 3
#define TEXTURE_WIDTH 512
#define TEXTURE_HEIGHT 512
#define TEXTURE_BPP 3

#define BUFFER_COUNT 6

#define JOY_DEADZONE 0x4000

#define EVENT_UPDATE_SCREEN 1

static int init_video();
static void destroy_video();
static void reset_bounds(struct gamecard *p, struct gamecard *n);
static void draw_shit(struct gamecard *p, struct gamecard *n);
static void handle_event(SDL_Event *event);
static void post_update_screen();

static GLuint textures[TEXTURE_COUNT];
static void *texture_bitmaps[TEXTURE_COUNT];

static const char *vertex_shader_src =
	"uniform mat4 u_vp_matrix;\n"
	"attribute vec4 a_position;\n"
	"attribute vec2 a_texcoord;\n"
	"varying mediump vec2 v_texcoord;\n"
	"void main() {\n"
	"   v_texcoord = a_texcoord;\n"
	"   gl_Position = u_vp_matrix * a_position;\n"
	"}\n";

static const char *fragment_shader_src =
	"varying mediump vec2 v_texcoord;\n"
	"uniform sampler2D u_texture;\n"
	"void main() {\n"
	"   gl_FragColor = texture2D(u_texture, v_texcoord);\n"
	"}\n";

static struct phl_matrix projection;

static struct gamecard *gamecards;
static int card_count;
static int previous_card = 0;
static int selected_card = 0;

static const GLfloat quad_vertices[] = {
	-0.5f, -0.5f, 0.0f,
	+0.5f, -0.5f, 0.0f,
	+0.5f, +0.5f, 0.0f,
	-0.5f, +0.5f, 0.0f,
};

#define QUADS 2

static struct quad_obj quads[QUADS];
static struct shader_obj shaders[QUADS];

int pim_quit = 0;

#define ANIM_NONE 0
#define ANIM_L2R  1
#define ANIM_R2L  2

static float current_frame = 0.0f;
static const float frame_incr = 0.01f;
static int current_anim = ANIM_NONE;

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
			// again safely
			add_to_queue(&gamecards[card_index]);
		}
	}
}

static void post_update_screen()
{
	// FIXME
	// SDL_Event event;
	// event.type = SDL_USEREVENT;
	// event.user.code = EVENT_UPDATE_SCREEN;

	// SDL_PushEvent(&event);
}

static void handle_update_screen()
{
	preload(selected_card);
	draw_shit(&gamecards[previous_card], &gamecards[selected_card]);
}

static void handle_event(SDL_Event *event)
{
	switch (event->type) {
	case SDL_USEREVENT:
		switch (event->user.code) {
		case EVENT_UPDATE_SCREEN:
			fprintf(stderr, "Received UpdateScreen event\n");
			handle_update_screen();
			break;
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
						// Left
						previous_card = selected_card;
						if (--selected_card < 0) {
							selected_card = card_count - 1;
						}
						current_anim = ANIM_R2L;
						current_frame = 0.0f;

						reset_bounds(&gamecards[previous_card],
							&gamecards[selected_card]);
						post_update_screen();
					} else if (joyEvent->value > JOY_DEADZONE) {
						// Right
						previous_card = selected_card;
						if (++selected_card >= card_count) {
							selected_card = 0;
						}

						current_anim = ANIM_L2R;
						current_frame = 0.0f;

						reset_bounds(&gamecards[previous_card],
							&gamecards[selected_card]);
						post_update_screen();
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

	int i;
	for (i = 0; i < QUADS; i++) {
		if (quad_init(&quads[i]) != 0) {
			phl_gles_shutdown();
			return 1;
		}

		quad_set_vertices(&quads[i], 12, quad_vertices);
		if (shader_init(&shaders[i], vertex_shader_src, fragment_shader_src) != 0) {
			phl_gles_shutdown();
			return 1;
		}
	}

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_DITHER);

	// Textures
	glGenTextures(TEXTURE_COUNT, textures);

	int texture_size = TEXTURE_WIDTH * TEXTURE_HEIGHT * TEXTURE_BPP;
	for (i = 0; i < TEXTURE_COUNT; i++) {
		texture_bitmaps[i] = malloc(texture_size);
		if (!texture_bitmaps[i]) {
			phl_gles_shutdown();
			for (i = 0; i < QUADS; i++) {
				quad_destroy(&quads[i]);
			}
			return 1;
		}

		glBindTexture(GL_TEXTURE_2D, textures[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TEXTURE_WIDTH, TEXTURE_HEIGHT,
			0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	}

	glClear(GL_COLOR_BUFFER_BIT);
	glViewport(0, 0, phl_gles_screen_width, phl_gles_screen_height);

	glDisable(GL_BLEND);

	return 0;
}

static void destroy_video()
{
	fprintf(stderr, "Destroying video... ");

	int i;
	for (i = 0; i < QUADS; i++) {
		shader_destroy(&shaders[i]);
		quad_destroy(&quads[i]);
	}

	glDeleteTextures(TEXTURE_COUNT, textures);

	for (i = 0; i < TEXTURE_COUNT; i++) {
		if (texture_bitmaps[i]) {
			free(texture_bitmaps[i]);
		}
	}

	phl_gles_shutdown();

	fprintf(stderr, "OK\n");
}

static void reset_bounds(struct gamecard *p, struct gamecard *n)
{
	quad_resize(&quads[0],
		(float)p->screenshot_width / TEXTURE_WIDTH - 0.0f,
		(float)p->screenshot_height / TEXTURE_HEIGHT);

	unsigned char *src = (unsigned char *)p->screenshot_bitmap;
	unsigned char *dst = (unsigned char *)texture_bitmaps[0];

	int texture_pitch = TEXTURE_WIDTH * TEXTURE_BPP;
	int bitmap_pitch = p->screenshot_width * 3;
	int copy_pitch = (bitmap_pitch < texture_pitch)
		? bitmap_pitch : texture_pitch;

	int y;
	for (y = p->screenshot_height; y--;) {
		memcpy(dst, src, copy_pitch);
		dst += texture_pitch;
		src += bitmap_pitch;
	}

	quad_resize(&quads[1],
		(float)n->screenshot_width / TEXTURE_WIDTH - 0.0f,
		(float)n->screenshot_height / TEXTURE_HEIGHT);

	src = (unsigned char *)n->screenshot_bitmap;
	dst = (unsigned char *)texture_bitmaps[1];

	texture_pitch = TEXTURE_WIDTH * TEXTURE_BPP;
	bitmap_pitch = n->screenshot_width * 3;
	copy_pitch = (bitmap_pitch < texture_pitch)
		? bitmap_pitch : texture_pitch;

	for (y = n->screenshot_height; y--;) {
		memcpy(dst, src, copy_pitch);
		dst += texture_pitch;
		src += bitmap_pitch;
	}
}

static void draw_shit(struct gamecard *p, struct gamecard *n)
{
	phl_matrix_identity(&projection);
	phl_matrix_ortho(&projection, -0.5f, 0.5f, -0.5f, +0.5f, -1.0f, 1.0f);
	phl_matrix_scale(&projection, p->x_scale, p->y_scale, 0);

	struct shader_obj *shader = &shaders[0];
	glUseProgram(shader->program);
	glUniformMatrix4fv(shader->u_vp_matrix, 1, GL_FALSE, &projection.xx);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, textures[0]);

	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEXTURE_WIDTH, TEXTURE_HEIGHT,
		GL_RGB, GL_UNSIGNED_BYTE, texture_bitmaps[0]);

	quad_draw(&quads[0], shader);

	phl_matrix_identity(&projection);
	phl_matrix_ortho(&projection, -0.5f, 0.5f, -0.5f, +0.5f, -1.0f, 1.0f);
	phl_matrix_scale(&projection, n->x_scale, n->y_scale, 0);

	if (current_anim == ANIM_R2L) {
		float scale = 0.4f - (current_frame * 0.4f);
		phl_matrix_scale(&projection, 1.0f + scale, 1.0f + scale, 0);
		phl_matrix_translate(&projection, (1.0f - current_frame) * 2.0f, 0, 0);
	} else if (current_anim == ANIM_L2R) {
		float scale = 0.4f - (current_frame * 0.4f);
		phl_matrix_scale(&projection, 1.0f + scale, 1.0f + scale, 0);
		phl_matrix_translate(&projection, -(1.0f - current_frame) * 2.0f, 0, 0);
	}

	shader = &shaders[1];
	glUseProgram(shader->program);
	glUniformMatrix4fv(shader->u_vp_matrix, 1, GL_FALSE, &projection.xx);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, textures[1]);

	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEXTURE_WIDTH, TEXTURE_HEIGHT,
		GL_RGB, GL_UNSIGNED_BYTE, texture_bitmaps[1]);

	quad_draw(&quads[1], shader);

	phl_gles_swap_buffers();
}

void bitmap_loaded_callback(struct gamecard *gc)
{
	if (gc == &gamecards[selected_card]) {
		// FIXME reset_bounds(gc);
		// draw_shit(gc);
		// post_update_screen();
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
	for (romname = romnames, gc = gamecards; *romname; romname++, gc++) {
		gamecard_init(gc);
		gc->archive = strdup(*romname);

		int length = snprintf(NULL, 0, "images/%s.png", *romname);
		gc->screenshot_path = (char *)malloc(sizeof(char) * (length + 1));
		sprintf(gc->screenshot_path, "images/%s.png", *romname);
	}

	int i;
	for (i = 0, gc = gamecards; i < card_count; i++, gc++) {
		gamecard_dump(gc);
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

	post_update_screen();

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

		if (current_frame + frame_incr < 1.0f) {
			current_frame += frame_incr;
		} else if (current_frame > 0) {
			current_anim = ANIM_NONE;
			current_frame = 1.0f;
		}

		handle_update_screen();
	}

	destroy_threads();
	destroy_video();
	SDL_Quit();

	for (i = 0, gc = gamecards; i < card_count; i++, gc++) {
		// gamecard_dump(gc);
		gamecard_free(gc);
	}
	free(gamecards);

	return 0;
}
