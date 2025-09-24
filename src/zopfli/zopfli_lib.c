/*
Copyright 2011 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Author: lode.vandevenne@gmail.com (Lode Vandevenne)
Author: jyrki.alakuijala@gmail.com (Jyrki Alakuijala)
*/

#include "zopfli.h"

#include "deflate.h"
#include "gzip_container.h"
#include "zlib_container.h"
#include <stdio.h>

/*
 Loads stdin into a memory array.
 */
int LoadPipe(unsigned char** out, size_t* outsize) {
  const size_t CHUNK = 1 << 20; /* 1 MiB chunks; */

  *out = 0;
  *outsize = -1;

  unsigned char* buf = NULL;
  size_t cap = 0;
  size_t size = 0;

  for (;;) {
    /* ensure capacity for the next read */
    if (cap - size < CHUNK) {
      size_t newcap = cap ? (cap * 2) : (CHUNK * 2);
      unsigned char* p = (unsigned char*)realloc(buf, newcap);
      if (!p) {
        free(buf);
        return 0;
      }
      buf = p;
      cap = newcap;
    }

    size_t n = fread(buf + size, 1, CHUNK, stdin);
    if (n > 0) {
      size += n;
    }
    if (n < CHUNK) {
      /* EOF or error */
      if (!feof(stdin)) {
        free(buf);
        return 0;
      }
      break;
    }
  }

  *out = buf;
  *outsize = size;
  return 1;
}

/*
 Loads a file into a memory array.
 */
int LoadFile(const char* filename, unsigned char** out, size_t* outsize) {
  if (!filename) {
    return LoadPipe(out, outsize);
  }
  FILE* file;

  *out = 0;
  *outsize = 0;
  file = fopen(filename, "rb");
  if (!file) return 0;

  fseek(file , 0 , SEEK_END);
  *outsize = ftell(file);
  if(*outsize > 2147483647) {
    fclose(file);
    return 0;
  }
  rewind(file);

  *out = (unsigned char*)malloc(*outsize);
  if (!*out && *outsize){
    fclose(file);
    return 0;
  }
  if (*outsize) {
    size_t testsize = fread(*out, 1, *outsize, file);
    if (testsize != *outsize) {
      /* It could be a directory */
      free(*out);
      *out = 0;
      *outsize = 0;
      fclose(file);
      return 0;
    }
  }

  fclose(file);
  return 1;
}

/*
Saves a file from a memory array, overwriting the file if it existed.
*/
int SaveFile(const char* filename, const unsigned char* in, size_t insize) {
  FILE* file = filename ? fopen(filename, "wb") : stdout;
  if (file == NULL) {
      return 0;
  }
  fwrite((char*)in, 1, insize, file);
  if (filename) { fclose(file); }
  return 1;
}

/*
 outfilename: filename to write output to, or 0 to write to stdout instead
 */
int ZopfliGzip(const char* infilename, const char* outfilename, unsigned mode, const char* gzip_name, unsigned time) {
  unsigned char* in = 0;
  size_t insize = 0;
  unsigned char* out = 0;
  size_t outsize = 0;

  if (!LoadFile(infilename, &in, &insize)) {
    /* fprintf(stderr, "Invalid input: %s\n", infilename); */
    return -3;
  }

  ZopfliOptions options;
  ZopfliInitOptions(&options, mode, 0);
  ZopfliGzipCompressEx(&options, in, insize, &out, &outsize, time, gzip_name);
  free(in);

  if (!SaveFile(outfilename, out, outsize)) {
    /* fprintf(stderr, "Can't write to file %s\n", outfilename); */
    return -1;
  }

  free(out);
  return EXIT_SUCCESS;
}

