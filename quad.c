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
#include <stdlib.h>
#include <string.h>
#include <GLES2/gl2.h>

#include "shader.h"
#include "quad.h"

#define BUFFER_COUNT 4

static const int quad_vertex_count = 4;
static const int quad_index_count = 6;

static const GLushort indices[] = {
	0, 1, 2,
	0, 2, 3,
};

int phl_gl_closest_power_of_two(int n)
{
    int rv = 1;
    while (rv < n) {
    	rv *= 2;
    }
    return rv;
}

int quad_init(struct quad_obj *quad)
{
	glGenBuffers(BUFFER_COUNT, &quad->buf_uvs);
	return 0;
}

void quad_set_vertices(struct quad_obj *quad, int count, const GLfloat *vertices)
{
	int i = 0, j;
	if (count > 0 && count <= 12) {
		for (i = 0; i < count; i++) {
			quad->vertices[i] = vertices[i];
		}
	}

	for (j = i; j < 12; j++) {
		quad->vertices[j] = 0.0f;
	}
}

void quad_resize(const struct quad_obj *quad, GLfloat maxU, GLfloat maxV)
{
	static const GLfloat minU = 0.0f;
	static const GLfloat minV = 0.0f;

	GLfloat uvs[] = {
		minU, minV,
		maxU, minV,
		maxU, maxV,
		minU, maxV,
	};

	GLfloat fixme[] = { 
		1.0f, 1.0f, 1.0f, 1.0f,
		1.0f, 1.0f, 1.0f, 1.0f,
		1.0f, 1.0f, 1.0f, 1.0f,
		1.0f, 1.0f, 1.0f, 1.0f,
	 };

	glBindBuffer(GL_ARRAY_BUFFER, quad->buf_vertices);
	glBufferData(GL_ARRAY_BUFFER,
		quad_vertex_count * sizeof(GLfloat) * 3, quad->vertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ARRAY_BUFFER, quad->buf_uvs);
	glBufferData(GL_ARRAY_BUFFER,
		quad_vertex_count * sizeof(GLfloat) * 2, uvs, GL_STATIC_DRAW);

	glBindBuffer(GL_ARRAY_BUFFER, quad->buf_color);
	glBufferData(GL_ARRAY_BUFFER,
		quad_vertex_count * sizeof(GLfloat) * 4, fixme, GL_STATIC_DRAW);

	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quad->buf_indices);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,
		quad_index_count * sizeof(GL_UNSIGNED_SHORT), indices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void quad_draw(const struct quad_obj *quad, const struct shader_obj *shader)
{
	glUniform1i(shader->u_texture, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glBindBuffer(GL_ARRAY_BUFFER, quad->buf_vertices);
	glVertexAttribPointer(shader->a_position, 3, GL_FLOAT,
		GL_FALSE, 3 * sizeof(GLfloat), NULL);
	glEnableVertexAttribArray(shader->a_position);

	glBindBuffer(GL_ARRAY_BUFFER, quad->buf_uvs);
	glVertexAttribPointer(shader->a_texcoord, 2, GL_FLOAT,
		GL_FALSE, 2 * sizeof(GLfloat), NULL);
	glEnableVertexAttribArray(shader->a_texcoord);

	glBindBuffer(GL_ARRAY_BUFFER, quad->buf_color);
	glVertexAttribPointer(shader->a_color, 4, GL_FLOAT,
		GL_FALSE, 4 * sizeof(GLfloat), NULL);
	glEnableVertexAttribArray(shader->a_color);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quad->buf_indices);

	glDrawElements(GL_TRIANGLES, quad_index_count, GL_UNSIGNED_SHORT, 0);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void quad_destroy(struct quad_obj *quad)
{
	glDeleteBuffers(BUFFER_COUNT, &quad->buf_uvs);
}
