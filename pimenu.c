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
#include <sys/time.h>

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
static int config_load(const char *path);

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

static struct emulator *emulators = NULL;
static struct gamecard *gamecards = NULL;
static int card_count = 0;
static int emulator_count = 0;
static int previous_card = 0;
static int selected_card = 0;
static int exit_down = 0;
static int kiosk_timeout = 0;

static int launch_button = 0;
static int exit_button = 1;
static struct timeval exit_press_time;
static struct timeval last_input_event;

static int exit_press_duration = 2;

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

const static struct anim_theme *anim_theme = &anim_themes[1];

int pim_quit = 0;
static int exit_code = 0;

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
		if (gc->load_status == 0) {
			// A race condition is possible, but the bitmap loader will check
			// again in thread-safe fashion
			add_to_queue(&gamecards[card_index]);
		}
	}
}

static void handle_event(SDL_Event *event)
{
	struct timeval now;
	gettimeofday(&now, NULL);

	switch (event->type) {
	case SDL_USEREVENT:
		switch (event->user.code) {
		case EVENT_RESET_GC: {
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
	case SDL_KEYDOWN: {
			// FIXME
			SDL_KeyboardEvent *keyEvent = (SDL_KeyboardEvent *)event;
			if (keyEvent->keysym.sym == SDLK_LEFT) {
				go_to(GO_PREVIOUS);
				last_input_event = now;
			} else if (keyEvent->keysym.sym == SDLK_RIGHT) {
				go_to(GO_NEXT);
				last_input_event = now;
			} else if (keyEvent->keysym.sym == SDLK_SPACE) {
				if (selected_card >= 0) {
					launch(&gamecards[selected_card]);
				}
				last_input_event = now;
			} else if (keyEvent->keysym.sym == SDLK_F12) {
				last_input_event = now;
				exit_code = 0;
				pim_quit = 1;
			}
		}
		break;
	case SDL_JOYBUTTONDOWN: {
			SDL_JoyButtonEvent *joyEvent = (SDL_JoyButtonEvent *)event;
			if (joyEvent->which == 0) {
				if (joyEvent->button == launch_button) {
					if (selected_card >= 0) {
						launch(&gamecards[selected_card]);
					}
					last_input_event = now;
				} else if (joyEvent->button == exit_button) {
					gettimeofday(&exit_press_time, NULL);
					last_input_event = now;
					exit_down = 1;
				}
			}
		}
		break;
	case SDL_JOYBUTTONUP: {
			SDL_JoyButtonEvent *joyEvent = (SDL_JoyButtonEvent *)event;
			if (joyEvent->which == 0) {
				if (joyEvent->button == exit_button) {
					exit_down = 0;
				}
			}
		}
		break;
	case SDL_JOYAXISMOTION: {
			SDL_JoyAxisEvent *joyEvent = (SDL_JoyAxisEvent *)event;
			if (joyEvent->which == 0) {
				if (joyEvent->axis == 0) {
					if (joyEvent->value < -JOY_DEADZONE) {
						go_to(GO_PREVIOUS);
						last_input_event = now;
						exit_down = 0;
					} else if (joyEvent->value > JOY_DEADZONE) {
						go_to(GO_NEXT);
						last_input_event = now;
						exit_down = 0;
					}
				} else if (joyEvent->axis == 1) {
					if (joyEvent->value < -JOY_DEADZONE) {
						// Up
						last_input_event = now;
						exit_down = 0;
					} else if (joyEvent->value > JOY_DEADZONE) {
						// Down
						last_input_event = now;
						exit_down = 0;
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
	fprintf(stderr, "Launching %s...\n", gc->archive);

	FILE *out = fopen("launch.sh", "w");
	if (out != NULL) {
		const struct emulator *e = gc->emulator;

		fprintf(out, "# %s\n", gc->archive);
		fprintf(out, "cd %s\n", e->path);
		fprintf(out, "./%s %s %s %s\n", e->exe,
			e->args != NULL ? e->args : "",
			gc->args != NULL ? gc->args : "",
			gc->archive);
		fprintf(out, "exit $?\n");
		fclose(out);
	}

	exit_code = 1;
	pim_quit = 1;

	return 1;
}

static int compare_gamecards(const void *a, const void *b)
{
	struct gamecard *gca = (struct gamecard *) a;
	struct gamecard *gcb = (struct gamecard *) b;

	if (gca->title == NULL) {
		return gcb->title == NULL ? 0 : 1;
	} else if (gcb->title == NULL) {
		return gca->title == NULL ? 0 : -1;
	}

	return strcasecmp(gca->title, gcb->title);
}

static int config_load(const char *path)
{
	int ret_val = 1;
	int gc_size = sizeof(struct gamecard);

	char *contents = glob_file(path);
	if (contents) {
		cJSON *root = cJSON_Parse(contents);
		if (root) {
			cJSON *emus_node = cJSON_GetObjectItem(root, "emulators");
			if (emus_node != NULL) {
				emulator_count = cJSON_GetArraySize(emus_node);
				if (emulator_count > 0) {
					emulators = (struct emulator *)calloc(emulator_count, sizeof(struct emulator));
					if (emulators != NULL) {
						int ei;
						struct emulator *e;
						for (ei = 0, e = emulators; ei < emulator_count; ei++, e++) {
							cJSON *emu_node = cJSON_GetArrayItem(emus_node, ei);
							if (emu_node != NULL) {
								emulator_init(e);

								cJSON *node = cJSON_GetObjectItem(emu_node, "dir");
								if (node != NULL) {
									e->path = strdup(node->valuestring);
								}
								node = cJSON_GetObjectItem(emu_node, "exe");
								if (node != NULL) {
									e->exe = strdup(node->valuestring);
								}
								node = cJSON_GetObjectItem(emu_node, "args");
								if (node != NULL) {
									e->args = strdup(node->valuestring);
								}

								cJSON *sets_node = cJSON_GetObjectItem(emu_node, "sets");
								if (sets_node != NULL) {
									int array_size = cJSON_GetArraySize(sets_node);
									if (array_size > 0) {
										struct gamecard *new_cards = (struct gamecard *)realloc(gamecards,
											(card_count + array_size) * gc_size);
										if (new_cards != NULL) {
											int ci;
											struct gamecard *gc;
											gamecards = new_cards;
											for (ci = 0, gc = gamecards + card_count; ci < array_size; ci++, gc++) {
												cJSON *set_node = cJSON_GetArrayItem(sets_node, ci);
												if (set_node != NULL) {
													gamecard_init(gc);

													gc->emulator = e;
													// gc->id = ci; // Compute post-sort

													node = cJSON_GetObjectItem(set_node, "archive");
													if (node) {
														gc->archive = strdup(node->valuestring);

														int length = snprintf(NULL, 0, SCREENSHOT_TEMPLATE, gc->archive);
														gc->screenshot_path = (char *)malloc(sizeof(char) * (length + 1));
														sprintf(gc->screenshot_path, SCREENSHOT_TEMPLATE, gc->archive);
													}
													node = cJSON_GetObjectItem(set_node, "title");
													if (node) {
														gc->title = strdup(node->valuestring);
													}
													node = cJSON_GetObjectItem(set_node, "args");
													if (node) {
														gc->args = strdup(node->valuestring);
													}
												}
											}
											card_count += array_size;
										}
									}
								}
							}
						}
					}
				}
			}

			cJSON *controls_node = cJSON_GetObjectItem(root, "controls");
			if (controls_node != NULL) {
				cJSON *node = cJSON_GetObjectItem(controls_node, "startButton");
				if (node != NULL) {
					launch_button = node->valueint;
				}
				node = cJSON_GetObjectItem(controls_node, "exitButton");
				if (node != NULL) {
					exit_button = node->valueint;
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
	gettimeofday(&last_input_event, NULL);
	srand(time(NULL));
	
	int i;
	int autolaunch = 0;
	for (i = 1; i < argc; i++) {
		if (*argv[i] == '-') {
			if (strcasecmp(argv[i] + 1, "-launch-next") == 0) {
				autolaunch = 1;
			} else if (strcasecmp(argv[i] + 1, "k") == 0) {
				if (++i < argc) {
					int secs = atoi(argv[i]);
					if (secs > 0) {
						kiosk_timeout = secs;
						printf("Kiosk mode enabled (%d seconds)\n", secs);
					}
				}
			}
		}
	}

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

	if (!autolaunch) {
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
	}

	state_load(&state, STATE_FILE);

	qsort(gamecards, card_count, sizeof(struct gamecard), compare_gamecards);

	selected_card = 0;
	for (i = 0; i < card_count; i++) {
		gamecards[i].id = i;
		if (state.last_selected && strcmp(gamecards[i].archive, state.last_selected) == 0) {
			selected_card = i;
			// break;
		}
	}

	if (autolaunch) {
		if (++selected_card >= card_count) {
			selected_card = 0;
		}
		launch(&gamecards[selected_card]);
	} else { // if (!autolaunch)
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

			struct timeval now;
			gettimeofday(&now, NULL);

			if (exit_down) {
				if (now.tv_sec - exit_press_time.tv_sec >= exit_press_duration) {
					pim_quit = 1;
					exit_code = 2;
					exit_down = 0;
				}
			}

			if (kiosk_timeout > 0) {
				if (now.tv_sec - last_input_event.tv_sec >= kiosk_timeout) {
					fprintf(stderr, "Kiosk mode - %ds timeout exceeded\n", kiosk_timeout);
					// Pick a random title and launch it
					selected_card = rand() % card_count;
					launch(&gamecards[selected_card]);
					exit_code = 3;
				}
			}

			draw();
		}

		destroy_threads();
		destroy_video();
		SDL_Quit();
	}

	if (selected_card >= 0) {
		state_set_last_selected(&state, gamecards[selected_card].archive);
	}

	struct gamecard *gc;
	for (i = 0, gc = gamecards; i < card_count; i++, gc++) {
		gamecard_free(gc);
	}
	free(gamecards);

	struct emulator *e;
	for (i = 0, e = emulators; i < emulator_count; i++, e++) {
		emulator_free(e);
	}
	free(emulators);

	state_save(&state, STATE_FILE);
	state_destroy(&state);

	return exit_code;
}
