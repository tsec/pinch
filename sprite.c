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

#include "phl_gles.h"
#include "shader.h"
#include "quad.h"
#include "gamecard.h"

#include "sprite.h"

#define TEXTURE_WIDTH 512
#define TEXTURE_HEIGHT 512
#define TEXTURE_BPP 3

static const GLfloat quad_vertices[] = {
	-0.5f, -0.5f, 0.0f,
	+0.5f, -0.5f, 0.0f,
	+0.5f, +0.5f, 0.0f,
	-0.5f, +0.5f, 0.0f,
};

int sprite_init(struct sprite *sprite)
{
	memset(sprite, 0, sizeof(struct sprite));

	glGenTextures(1, &sprite->texture);
	if (glGetError() != GL_NO_ERROR) {
		fprintf(stderr, "glGenTextures() failed\n");
		return 1;
	}

	if (quad_init(&sprite->quad) != 0) {
		fprintf(stderr, "quad_init() failed\n");
		glDeleteTextures(1, &sprite->texture);
		return 1;
	}

	sprite->texture_pitch = TEXTURE_WIDTH * TEXTURE_BPP;
	if ((sprite->row = malloc(sprite->texture_pitch)) == NULL) {
		fprintf(stderr, "sprite row malloc failed\n");
		glDeleteTextures(1, &sprite->texture);
		quad_destroy(&sprite->quad);
		return 1;
	}

	quad_set_vertices(&sprite->quad, quad_vertices);

	GLfloat colors[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	quad_set_all_vertex_colors(&sprite->quad, colors);

	glBindTexture(GL_TEXTURE_2D, sprite->texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TEXTURE_WIDTH, TEXTURE_HEIGHT,
		0, GL_RGB, GL_UNSIGNED_BYTE, NULL);

	return 0;
}

void sprite_destroy(struct sprite *sprite)
{
	glDeleteTextures(1, &sprite->texture);
	quad_destroy(&sprite->quad);
	free(sprite->row); sprite->row = NULL;
}

int sprite_set_texture(struct sprite *sprite, struct gamecard *gc)
{
	memset(sprite->row, 0, sprite->texture_pitch);
	unsigned char *src = (unsigned char *)gc->screenshot_bitmap;

	int bitmap_pitch = gc->screenshot_width * 3; // 3 == BPP
	int copy_pitch = (bitmap_pitch < sprite->texture_pitch)
		? bitmap_pitch : sprite->texture_pitch;

	glBindTexture(GL_TEXTURE_2D, sprite->texture);

	int i, h;
	for (i = 0, h = gc->screenshot_height; i < h; i++) {
		memcpy(sprite->row, src, copy_pitch);
		src += bitmap_pitch;
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, i, TEXTURE_WIDTH, 1,
			GL_RGB, GL_UNSIGNED_BYTE, sprite->row);
	}

	float wr = (float)gc->screenshot_width / TEXTURE_WIDTH;
	float hr = (float)gc->screenshot_height / TEXTURE_HEIGHT;

	quad_resize(&sprite->quad, wr, hr);

	sprite->x_ratio = 1.0f;
	sprite->y_ratio = 1.0f;

	float a = 1.0f;
	if (phl_gles_screen_height > 0) {
		a = (float)phl_gles_screen_width / (float)phl_gles_screen_height;
	}
	float a0 = 1.0f;
	if (gc->screenshot_height > 0) {
		a0 = (float)gc->screenshot_width / (float)gc->screenshot_height;
	}

	if (a > a0) {
		sprite->x_ratio = a0 / a;
	} else {
		sprite->y_ratio = a / a0;
	}

	return 0;
}

int sprite_set_frame(struct sprite *sprite, struct gamecard *gc)
{
	if (gc->frame > gc->frame_count) {
		return 1;
	}

	memset(sprite->row, 0, sprite->texture_pitch);
	unsigned char *src = (unsigned char *)gc->frames[gc->frame];

	int bitmap_pitch = gc->screenshot_width * 3; // 3 == BPP
	int copy_pitch = (bitmap_pitch < sprite->texture_pitch)
		? bitmap_pitch : sprite->texture_pitch;

	glBindTexture(GL_TEXTURE_2D, sprite->texture);

	int i, h;
	for (i = 0, h = gc->screenshot_height; i < h; i++) {
		memcpy(sprite->row, src, copy_pitch);
		src += bitmap_pitch;
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, i, TEXTURE_WIDTH, 1,
			GL_RGB, GL_UNSIGNED_BYTE, sprite->row);
	}

	return 0;
}

void sprite_set_shade(struct sprite *sprite, GLfloat shade)
{
	GLfloat colors[] = { shade, shade, shade, 1.0f };
	quad_set_all_vertex_colors(&sprite->quad, colors);
}

void sprite_draw(struct sprite *sprite, struct shader_obj *shader)
{
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, sprite->texture);

	quad_draw(&sprite->quad, shader);
}
