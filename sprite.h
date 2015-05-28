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

#ifndef SPRITE_H
#define SPRITE_H

struct sprite {
	int id;
	GLuint texture;
	struct quad_obj quad;
	float frame_value;
	float frame_delta;
	int state;
	float x_ratio;
	float y_ratio;
	unsigned int texture_pitch;
	void *row; // scratch area
};

int sprite_init(struct sprite *sprite);
int sprite_set_frame(struct sprite *sprite, struct gamecard *gc);
int sprite_set_texture(struct sprite *sprite, struct gamecard *gc);
void sprite_set_shade(struct sprite *sprite, GLfloat shade);
void sprite_draw(struct sprite *sprite, struct shader_obj *shader);
void sprite_destroy(struct sprite *sprite);

#endif // SPRITE_H
