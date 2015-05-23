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
#define SHADE_FACTOR   1.33f

#define TEXTURE_COUNT 3
#define TEXTURE_WIDTH 512
#define TEXTURE_HEIGHT 512
#define TEXTURE_BPP 3

#define BUFFER_COUNT 6

#define JOY_DEADZONE 0x4000

static int init_video();
static void destroy_video();
static void reset_bounds(struct gamecard *p, struct gamecard *n);
static void draw_shit(struct gamecard *p, struct gamecard *n);
static void handle_event(SDL_Event *event);

static GLuint textures[TEXTURE_COUNT];
static void *texture_bitmaps[TEXTURE_COUNT];

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
static struct shader_obj shader;

int pim_quit = 0;

#define ANIM_NONE 0
#define ANIM_L2R  1
#define ANIM_R2L  2
static const float frame_speed = 0.01f;

static int current_anim = ANIM_NONE;
static float current_frame = 0.0f;
static float frame_incr;

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
		// switch (event->user.code) {
		// }
		break;
	case SDL_KEYUP:
		{
			// FIXME
			SDL_KeyboardEvent *keyEvent = (SDL_KeyboardEvent *)event;
			if (keyEvent->keysym.sym == SDLK_LEFT) {
				// Left
				previous_card = selected_card;
				if (--selected_card < 0) {
					selected_card = card_count - 1;
				}
				current_anim = ANIM_R2L;
				current_frame = 0.0f;
				frame_incr = frame_speed;

				reset_bounds(&gamecards[previous_card],
					&gamecards[selected_card]);
			} else if (keyEvent->keysym.sym == SDLK_RIGHT) {
				// Right
				previous_card = selected_card;
				if (++selected_card >= card_count) {
					selected_card = 0;
				}

				current_anim = ANIM_L2R;
				current_frame = 0.0f;
				frame_incr = frame_speed;

				reset_bounds(&gamecards[previous_card],
					&gamecards[selected_card]);
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
						// Left
						previous_card = selected_card;
						if (--selected_card < 0) {
							selected_card = card_count - 1;
						}
						current_anim = ANIM_R2L;
						current_frame = 0.0f;
						frame_incr = frame_speed;

						reset_bounds(&gamecards[previous_card],
							&gamecards[selected_card]);
					} else if (joyEvent->value > JOY_DEADZONE) {
						// Right
						previous_card = selected_card;
						if (++selected_card >= card_count) {
							selected_card = 0;
						}

						current_anim = ANIM_L2R;
						current_frame = 0.0f;
						frame_incr = frame_speed;

						reset_bounds(&gamecards[previous_card],
							&gamecards[selected_card]);
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

	int i;
	for (i = 0; i < QUADS; i++) {
		if (quad_init(&quads[i]) != 0) {
			shader_destroy(&shader);
			phl_gles_shutdown();
			return 1;
		}
		quad_set_vertices(&quads[i], quad_vertices);
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

	SDL_SetVideoMode(0,0,0,0);

	return 0;
}

static void destroy_video()
{
	fprintf(stderr, "Destroying video... ");

	shader_destroy(&shader);

	int i;
	for (i = 0; i < QUADS; i++) {
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
	if (current_anim != ANIM_NONE) {
		phl_matrix_identity(&projection);
		phl_matrix_ortho(&projection, -0.5f, 0.5f, -0.5f, +0.5f, -1.0f, 1.0f);
		phl_matrix_scale(&projection, p->x_scale, p->y_scale, 0);

		glUseProgram(shader.program);
		glUniformMatrix4fv(shader.u_vp_matrix, 1, GL_FALSE, &projection.xx);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, textures[0]);

		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEXTURE_WIDTH, TEXTURE_HEIGHT,
			GL_RGB, GL_UNSIGNED_BYTE, texture_bitmaps[0]);

		GLfloat shade = 1.0f - SHADE_FACTOR * current_frame;
		if (shade < 0) {
			shade = 0;
		}

		GLfloat colors[] = { shade, shade, shade, 1.0f };
		quad_set_all_vertex_colors(&quads[0], colors);

		quad_draw(&quads[0], &shader);
	}

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

	glUseProgram(shader.program);
	glUniformMatrix4fv(shader.u_vp_matrix, 1, GL_FALSE, &projection.xx);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, textures[1]);

	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEXTURE_WIDTH, TEXTURE_HEIGHT,
		GL_RGB, GL_UNSIGNED_BYTE, texture_bitmaps[1]);

	quad_draw(&quads[1], &shader);

	phl_gles_swap_buffers();
}

void bitmap_loaded_callback(struct gamecard *gc)
{
	if (gc == &gamecards[selected_card]) {
		// reset_bounds(gc);
		// draw_shit(gc);
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

		if (current_anim != ANIM_NONE) {
			float next_frame = current_frame + frame_incr;
			if (next_frame > 1.0f) {
				next_frame = 1.0f;
				current_anim = ANIM_NONE;
			} else if (next_frame < 0.0f) {
				next_frame = 0.0f;
				current_anim = ANIM_NONE;
			}

			current_frame = next_frame;
		}

		// FIXME: not needed at every frame!
		preload(selected_card);
		draw_shit(&gamecards[previous_card], &gamecards[selected_card]);
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
