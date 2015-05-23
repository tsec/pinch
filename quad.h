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

#ifndef QUAD_H
#define QUAD_H

struct quad_obj {
	GLuint buf_uvs;
	GLuint buf_vertices;
	GLuint buf_indices;
	GLuint buf_color;
};

int phl_gl_closest_power_of_two(int n);

int quad_init(struct quad_obj *quad);
void quad_set_vertices(const struct quad_obj *quad, const GLfloat *vertices);
void quad_set_all_vertex_colors(const struct quad_obj *quad, const GLfloat *c);
void quad_resize(const struct quad_obj *quad, GLfloat maxU, GLfloat maxV);
void quad_draw(const struct quad_obj *quad, const struct shader_obj *shader);
void quad_destroy(struct quad_obj *quad);

#endif // QUAD_H
