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
#include <pthread.h>
#include <png.h>

#include "common.h"
#include "phl_gles.h"

void gamecard_dump(const struct gamecard *gc)
{
	fprintf(stderr, "[%s,%s,%s,%dx%d]\n", gc->archive, gc->screenshot_path,
		gc->screenshot_bitmap ? "(bitmap)" : "",
		gc->screenshot_width, gc->screenshot_height);
}

void gamecard_init(struct gamecard *gc)
{
	gc->lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
}

void gamecard_free(struct gamecard *gc)
{
	pthread_mutex_destroy(&gc->lock);
	free(gc->archive); gc->archive = NULL;
	free(gc->screenshot_path); gc->screenshot_path = NULL;
	free(gc->screenshot_bitmap); gc->screenshot_bitmap = NULL;
}

void gamecard_set_bitmap(struct gamecard *gc,
	int width, int height, void *bmp)
{
	free(gc->screenshot_bitmap);

	gc->screenshot_width = width;
	gc->screenshot_height = height;
	gc->screenshot_bitmap = bmp;
	gc->status = STATUS_LOADED;
	gc->x_scale = 1.0f;
	gc->y_scale = 1.0f;

	// Screen aspect ratio adjustment
	float a = (float)phl_gles_screen_width / (float)phl_gles_screen_height;
	float a0 = (float)width / (float)height;
	if (a > a0) {
		gc->x_scale = a0 / a;
	} else {
		gc->y_scale = a / a0;
	}
}

// http://stackoverflow.com/questions/11296644/loading-png-textures-to-opengl-with-libpng-only
void* load_bitmap(const char *path, int *width, int *height, int *size)
{
	png_byte header[8];

	FILE *fp = fopen(path, "rb");
	if (fp == 0) {
		perror(path);
		return NULL;
	}

	// read the header
	fread(header, 1, 8, fp);

	if (png_sig_cmp(header, 0, 8)) {
		fprintf(stderr, "error: %s is not a PNG.\n", path);
		fclose(fp);
		return NULL;
	}

	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr) {
		fprintf(stderr, "error: png_create_read_struct returned 0.\n");
		fclose(fp);
		return NULL;
	}

	// create png info struct
	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		fprintf(stderr, "error: png_create_info_struct returned 0.\n");
		png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
		fclose(fp);
		return NULL;
	}

	// create png info struct
	png_infop end_info = png_create_info_struct(png_ptr);
	if (!end_info) {
		fprintf(stderr, "error: png_create_info_struct returned 0.\n");
		png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp) NULL);
		fclose(fp);
		return NULL;
	}

	// the code in this if statement gets called if libpng encounters an error
	if (setjmp(png_jmpbuf(png_ptr))) {
		fprintf(stderr, "error from libpng\n");
		png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
		fclose(fp);
		return NULL;
	}

	// init png reading
	png_init_io(png_ptr, fp);

	// let libpng know you already read the first 8 bytes
	png_set_sig_bytes(png_ptr, 8);

	// read all the info up to the image data
	png_read_info(png_ptr, info_ptr);

	// variables to pass to get info
	int bit_depth, color_type;
	png_uint_32 temp_width, temp_height;

	// get info about png
	png_get_IHDR(png_ptr, info_ptr, &temp_width, &temp_height,
		&bit_depth, &color_type, NULL, NULL, NULL);

	// Update the png info struct.
	png_read_update_info(png_ptr, info_ptr);

	// Row size in bytes.
	int rowbytes = png_get_rowbytes(png_ptr, info_ptr);

	// glTexImage2d requires rows to be 4-byte aligned
	rowbytes += 3 - ((rowbytes-1) % 4);

	// Allocate the image_data as a big block, to be given to opengl
	int bitmap_size = rowbytes * temp_height * sizeof(png_byte) + 15;
	png_byte *bitmap = malloc(bitmap_size);
	if (bitmap == NULL) {
		fprintf(stderr, "error: could not allocate memory for PNG image data\n");
		png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
		fclose(fp);
		return NULL;
	}

	// row_pointers is for pointing to image_data for reading the png with libpng
	png_bytep *row_pointers = malloc(temp_height * sizeof(png_bytep));
	if (row_pointers == NULL) {
		fprintf(stderr, "error: could not allocate memory for PNG row pointers\n");
		png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
		free(bitmap);
		fclose(fp);
		return NULL;
	}

	// set the individual row_pointers to point at the correct offsets of image_data
	int i;
	for (i = 0; i < temp_height; i++) {
		row_pointers[temp_height - 1 - i] = bitmap + i * rowbytes;
	}

	// read the png into image_data through row_pointers
	png_read_image(png_ptr, row_pointers);

	*width = temp_width;
	*height = temp_height;
	*size = bitmap_size;

	// clean up
	png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
	free(row_pointers);
	fclose(fp);

	return bitmap;
}
