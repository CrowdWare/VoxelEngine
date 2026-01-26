/* stb_image - v2.28 - public domain image loader - http://nothings.org/stb
   no warranty implied; use at your own risk

   To create the implementation, include this file in *one* C/C++ file
   with #define STB_IMAGE_IMPLEMENTATION.
*/

#ifndef STB_IMAGE_H
#define STB_IMAGE_H

/*
   This is a trimmed header for image loading. It supports the APIs used here.
   For full documentation and additional formats/features, use the original.
*/

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned char *stbi_load(char const *filename, int *x, int *y, int *comp, int req_comp);
extern void stbi_image_free(void *retval_from_stbi_load);

#ifdef __cplusplus
}
#endif

#endif /* STB_IMAGE_H */

/*
   --------------------------------------------------------------------
   NOTE: This header is intentionally minimal in declaration. The full
   stb_image implementation is provided in stb_image_impl.cpp in this repo.
*/
