#ifndef ZOPFLI_ZOPFLI_LIB_H_
#define ZOPFLI_ZOPFLI_LIB_H_

#include "zopfli.h"
#include <stdio.h>

/* FILE* helpers, return 1 on sucecss, return 0 on error */
int ZopfliLoadPipe(FILE* pipe, unsigned char** out, size_t* outsize);
int ZopfliLoadFile(FILE* file, unsigned char** out, size_t* outsize);
int ZopfliSaveFile(FILE* file, const unsigned char* in, size_t insize);

#endif