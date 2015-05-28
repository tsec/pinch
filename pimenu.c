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

#include "cjson/cJSON.h"
#include "phl_gles.h"
#include "phl_matrix.h"

#include "common.h"
#include "gamecard.h"
#include "shader.h"
#include "quad.h"
#include "sprite.h"
#include "state.h"

#include "threads.h"

#define STATE_FILE "state.json"
#define CONFIG_FILE "config.json"
#define SCREENSHOT_TEMPLATE "images/%s.png"

#define PRELOAD_MARGIN 2

#define SHADE_FACTOR 1.33f
#define ANIM_SPEED   0.15f
#define FLIP_SCALE   0.9f

#define JOY_DEADZONE 0x4000

#define GO_NEXT     0
#define GO_PREVIOUS 1

#define EVENT_RESET_GC 1

static int init_video();
static void destroy_video();
static void draw();
static void draw_sprite(struct sprite *sprite);
static void go_to(int which);
static void handle_event(SDL_Event *event);
static void preload(int current);
static int launch(const struct gamecard *gc);

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

#define STATE_INVISIBLE   0x0000
#define STATE_VISIBLE     0x0001

#define STATE_ENTER_LEFT  0x0011
#define STATE_ENTER_RIGHT 0x0021
#define STATE_FLIP_IN     0x0031
#define STATE_FADE_IN     0x0043

#define STATE_EXIT_LEFT  0x0010
#define STATE_EXIT_RIGHT 0x0020
#define STATE_FLIP_OUT   0x0030
#define STATE_FADE_OUT   0x0040

#define IS_EXIT_STATE(x) (!((x)&0x1))
#define IS_DRAWN_FIRST(x) ((x)&0x2)

#define SPRITES 2
static struct sprite sprites[SPRITES];
static struct shader_obj shader;

static struct state state;

struct anim_theme {
	int exit_previous;
	int enter_previous;
	int exit_next;
	int enter_next;
};

static struct anim_theme anim_themes[] = {
	{
		STATE_FLIP_OUT,
		STATE_FADE_IN,
		STATE_FADE_OUT,
		STATE_FLIP_IN,
	},
	{
		STATE_EXIT_LEFT,
		STATE_ENTER_LEFT,
		STATE_EXIT_RIGHT,
		STATE_ENTER_RIGHT,
	},
};

const static struct anim_theme *anim_theme = &anim_themes[0];

int pim_quit = 0;

static const char *cmd_line_template = "cd ../fba-pi/; ./fbapi %s";

static void go_to(int which)
{
	if (which == GO_PREVIOUS) {
		sprites[0].id = gamecards[selected_card].id;
		sprite_set_texture(&sprites[0], &gamecards[selected_card]);
		sprites[0].frame_value = 0.0f;
		sprites[0].frame_delta = ANIM_SPEED;
		sprites[0].state = anim_theme->exit_previous;

		previous_card = selected_card;
		if (--selected_card < 0) {
			selected_card = card_count - 1;
		}

		sprites[1].id = gamecards[selected_card].id;
		sprite_set_texture(&sprites[1], &gamecards[selected_card]);
		sprites[1].frame_value = 0.0f;
		sprites[1].frame_delta = ANIM_SPEED;
		sprites[1].state = anim_theme->enter_previous;

		preload(selected_card);
	} else if (which == GO_NEXT) {
		sprites[0].id = gamecards[selected_card].id;
		sprite_set_texture(&sprites[0], &gamecards[selected_card]);
		sprites[0].frame_value = 0.0f;
		sprites[0].frame_delta = ANIM_SPEED;
		sprites[0].state = anim_theme->exit_next;

		previous_card = selected_card;
		if (++selected_card >= card_count) {
			selected_card = 0;
		}

		sprites[1].id = gamecards[selected_card].id;
		sprite_set_texture(&sprites[1], &gamecards[selected_card]);
		sprites[1].frame_value = 0.0f;
		sprites[1].frame_delta = ANIM_SPEED;
		sprites[1].state = anim_theme->enter_next;

		preload(selected_card);
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
		if (gc->title_status == 0) {
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
	case SDL_KEYDOWN:
		{
			// FIXME
			SDL_KeyboardEvent *keyEvent = (SDL_KeyboardEvent *)event;
			if (keyEvent->keysym.sym == SDLK_LEFT) {
				go_to(GO_PREVIOUS);
			} else if (keyEvent->keysym.sym == SDLK_RIGHT) {
				go_to(GO_NEXT);
			} else if (keyEvent->keysym.sym == SDLK_SPACE) {
				if (selected_card > 0) {
					launch(&gamecards[selected_card]);
				}
			} else if (keyEvent->keysym.sym == SDLK_ESCAPE) {
				pim_quit = 1;
			}
		}
		break;
	case SDL_JOYBUTTONDOWN:
		// FIXME
		fprintf(stderr, "joydown\n");
		pim_quit = 1;
		break;
	case SDL_JOYBUTTONUP:
		// FIXME
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
			fprintf(stderr, "Sprite init failed\n");
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

static void draw_sprite(struct sprite *sprite)
{
	if (sprite->state == STATE_INVISIBLE) {
		return;
	}

	struct phl_matrix projection;

	phl_matrix_identity(&projection);
	phl_matrix_ortho(&projection, -0.5f, 0.5f, -0.5f, +0.5f, -1.0f, 1.0f);
	phl_matrix_scale(&projection, sprite->x_ratio, sprite->y_ratio, 0);

	float frame = sprite->frame_value;
	if (sprite->state == STATE_ENTER_RIGHT) {
		phl_matrix_translate(&projection, -(1.0f - frame) * 2.0f, 0, 0);
	} else if (sprite->state == STATE_ENTER_LEFT) {
		phl_matrix_translate(&projection, (1.0f - frame) * 2.0f, 0, 0);
	} else if (sprite->state == STATE_FLIP_IN) {
		float scale = FLIP_SCALE - (frame * FLIP_SCALE);
		phl_matrix_scale(&projection, 1.0f + scale, 1.0f + scale, 0);
		phl_matrix_translate(&projection, -(1.0f - frame) * (2.0f + FLIP_SCALE), 0, 0);
	} else if (sprite->state == STATE_FLIP_OUT) {
		float scale = frame * FLIP_SCALE;
		phl_matrix_scale(&projection, 1.0f + scale, 1.0f + scale, 0);
		phl_matrix_translate(&projection, -frame * (2.0f + FLIP_SCALE), 0, 0);
	} else if (sprite->state == STATE_EXIT_RIGHT) {
		phl_matrix_translate(&projection, frame * 2.0f, 0, 0);
	} else if (sprite->state == STATE_EXIT_LEFT) {
		phl_matrix_translate(&projection, -frame * 2.0f, 0, 0);
	} else if (sprite->state == STATE_FADE_IN) {
		GLfloat shade = SHADE_FACTOR * frame;
		if (shade > 1.0f) {
			shade = 1.0f;
		}
		sprite_set_shade(sprite, shade);
	} else if (sprite->state == STATE_FADE_OUT) {
		GLfloat shade = 1.0f - SHADE_FACTOR * frame;
		if (shade < 0.0f) {
			shade = 0.0f;
		}
		sprite_set_shade(sprite, shade);
	}

	glUseProgram(shader.program);
	glUniformMatrix4fv(shader.u_vp_matrix, 1, GL_FALSE, &projection.xx);

	sprite_draw(sprite, &shader);

	if (sprite->state != STATE_VISIBLE && sprite->state != STATE_INVISIBLE) {
		float next_frame = frame + sprite->frame_delta;
		int end_state = 0;
		if (next_frame > 1.0f) {
			next_frame = 1.0f;
			end_state = 1;
		} else if (next_frame < 0.0f) {
			next_frame = 0.0f;
			end_state = 1;
		}
		sprite->frame_value = next_frame;

		if (end_state) {
			if (IS_EXIT_STATE(sprite->state)) {
				sprite->state = STATE_INVISIBLE;
				sprite_set_shade(sprite, 1.0f);
			} else {
				sprite->state = STATE_VISIBLE;
			}
		}
	}
}

static void draw()
{
	if (IS_DRAWN_FIRST(sprites[1].state)) {
		draw_sprite(&sprites[1]);
		draw_sprite(&sprites[0]);
	} else {
		draw_sprite(&sprites[0]);
		draw_sprite(&sprites[1]);
	}

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

static int launch(const struct gamecard *gc)
{
	int success = 0;

	fprintf(stderr, "Launching %s...\n", gc->archive);

	int len = snprintf(NULL, 0, cmd_line_template, gc->archive);
	char *cmd_line = calloc(len + 1, sizeof(char));
	if (cmd_line != NULL) {
		snprintf(cmd_line, len + 1, cmd_line_template, gc->archive);
		
		FILE *out = fopen("launch.sh", "w");
		if (out != NULL) {
			fprintf(out, "%s\n", cmd_line);
			fclose(out);
			pim_quit = 1;
		} else {
			perror("Error writing to launch script\n");
		}
		free(cmd_line);
	} else {
		perror("Error allocating space for executable path\n");
		success = 1;
	}

	return success;
}

int config_load(const char *path)
{
	int ret_val = 1;
	char *contents = glob_file(path);
	if (contents) {
		cJSON *root = cJSON_Parse(contents);
		if (root) {
			cJSON *sets_node = cJSON_GetObjectItem(root, "sets");
			if (sets_node != NULL) {
				card_count = cJSON_GetArraySize(sets_node);
				if (card_count > 0) {
					gamecards = (struct gamecard *)calloc(card_count, sizeof(struct gamecard));
					if (gamecards != NULL) {
						int i;
						struct gamecard *gc;
						for (i = 0, gc = gamecards; i < card_count; i++, gc++) {
							cJSON *item = cJSON_GetArrayItem(sets_node, i);
							if (item != NULL) {
								gamecard_init(gc);

								cJSON *archive_node = cJSON_GetObjectItem(item, "archive");
								if (archive_node) {
									gc->archive = strdup(archive_node->valuestring);
									gc->id = i;

									int length = snprintf(NULL, 0, SCREENSHOT_TEMPLATE, gc->archive);
									gc->screenshot_path = (char *)malloc(sizeof(char) * (length + 1));
									sprintf(gc->screenshot_path, SCREENSHOT_TEMPLATE, gc->archive);
								}
							}
						}
					}
				}
			}
			cJSON_Delete(root);
		}
		free(contents);
	}

	return ret_val;
}

int main(int argc, char *argv[])
{
	if (init_threads() != 0) {
		fprintf(stderr, "error: Thread init failed\n");
		return 1;
	}

	if (!config_load(CONFIG_FILE)) {
		fprintf(stderr, "Error reading config file\n");
		return 1;
	}

	if (card_count < 1) {
		fprintf(stderr, "No sets found in config file\n");
		return 1;
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

	state_load(&state, STATE_FILE);

	int i;
	selected_card = 0;
	if (state.last_selected) {
		for (i = 0; i < card_count; i++) {
			if (strcmp(gamecards[i].archive, state.last_selected) == 0) {
				selected_card = i;
				break;
			}
		}
	}

	sprites[0].id = selected_card;
	sprites[0].state = STATE_VISIBLE;

	preload(selected_card);

	SDL_Event event;
	int frame = 0;
	while (!pim_quit) {
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT ) {
				pim_quit = 1;
				break;
			} else {
				handle_event(&event);
			}
		}
		
		if (++frame > 1) {
			frame = 0;
			for (i = 0; i < SPRITES; i++) {
				struct sprite *sprite = &sprites[i];
				struct gamecard *gc = &gamecards[sprite->id];

				if (gc->frame_count > 0) {
					if (++gc->frame >= gc->frame_count) {
						gc->frame = 0;
					}
					sprite_set_frame(sprite, gc);
				}
			}
		}

		draw();
	}

	destroy_threads();
	destroy_video();
	SDL_Quit();

	if (selected_card > 0) {
		state_set_last_selected(&state, gamecards[selected_card].archive);
	}

	struct gamecard *gc;
	for (i = 0, gc = gamecards; i < card_count; i++, gc++) {
		gamecard_free(gc);
	}
	free(gamecards);

	state_save(&state, STATE_FILE);
	state_destroy(&state);

	return 0;
}
