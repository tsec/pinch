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

static GLuint create_shader(GLenum type, const char *shader_src);
static GLuint create_program(const char *vs_src, const char *fs_src);

int shader_init(struct shader_obj *shader, const char *vs_src, const char *fs_src)
{
	int ret = 1;

	memset(shader, 0, sizeof(struct shader_obj));
	if ((shader->program = create_program(vs_src, fs_src))) {
		shader->a_position  = glGetAttribLocation(shader->program, "a_position");
		shader->a_texcoord  = glGetAttribLocation(shader->program, "a_texcoord");
		shader->a_color     = glGetAttribLocation(shader->program, "a_color");
		shader->u_vp_matrix = glGetUniformLocation(shader->program, "u_vp_matrix");
		shader->u_texture   = glGetUniformLocation(shader->program, "u_texture");
fprintf(stderr, "pos: %d\ntex: %d\ncol: %d\nmat: %d\ntext: %d\n", 
shader->a_position ,
shader->a_texcoord ,
shader->a_color    ,
shader->u_vp_matrix,
shader->u_texture  	);
		ret = 0;
	}

	return ret;
}

void shader_destroy(struct shader_obj *shader)
{
	glDeleteProgram(shader->program);
}

static GLuint create_shader(GLenum type, const char *shader_src)
{
	GLuint shader = glCreateShader(type);
	if (!shader) {
		fprintf(stderr, "glCreateShader() failed: %d\n", glGetError());
		return 0;
	}

	// Load and compile the shader source
	glShaderSource(shader, 1, &shader_src, NULL);
	glCompileShader(shader);

	// Check the compile status
	GLint compiled = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if (!compiled) {
		GLint infoLen = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
		if (infoLen > 1) {
			char* infoLog = (char *)malloc(sizeof(char) * infoLen);
			glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
			fprintf(stderr, "Error compiling shader:\n%s\n", infoLog);
			free(infoLog);
		}

		glDeleteShader(shader);
		return 0;
	}

	return shader;
}

static GLuint create_program(const char *vs_src, const char *fs_src)
{
	GLuint vertex_shader = create_shader(GL_VERTEX_SHADER, vs_src);
	if (!vertex_shader) {
		fprintf(stderr, "create_shader(GL_VERTEX_SHADER) failed\n");
		return 0;
	}

	GLuint fragment_shader = create_shader(GL_FRAGMENT_SHADER, fs_src);
	if (!fragment_shader) {
		fprintf(stderr, "create_shader(GL_FRAGMENT_SHADER) failed\n");
		glDeleteShader(vertex_shader);
		return 0;
	}

	GLuint program_object = glCreateProgram();
	if (!program_object) {
		fprintf(stderr, "glCreateProgram() failed: %d\n", glGetError());
		return 0;
	}

	glAttachShader(program_object, vertex_shader);
	glAttachShader(program_object, fragment_shader);

	// Link the program
	glLinkProgram(program_object);

	// Check the link status
	GLint linked = 0;
	glGetProgramiv(program_object, GL_LINK_STATUS, &linked);
	if (!linked) {
		GLint infoLen = 0;
		glGetProgramiv(program_object, GL_INFO_LOG_LENGTH, &infoLen);
		if (infoLen > 1) {
			char *infoLog = (char *)malloc(infoLen);
			glGetProgramInfoLog(program_object, infoLen, NULL, infoLog);
			fprintf(stderr, "Error linking program: %s\n", infoLog);
			free(infoLog);
		}

		glDeleteProgram(program_object);
		return 0;
	}

	// Delete these here because they are attached to the program object.
	glDeleteShader(vertex_shader);
	glDeleteShader(fragment_shader);

	return program_object;
}
