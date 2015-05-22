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

#ifndef SHADER_H
#define SHADER_H

struct shader_obj {
	GLuint program;
	GLint a_position;
	GLint a_texcoord;
	GLint u_vp_matrix;
	GLint u_texture;
};

int shader_init(struct shader_obj *shader, const char *vs_src, const char *fs_src);
void shader_destroy(struct shader_obj *shader);

#endif // SHADER_H
