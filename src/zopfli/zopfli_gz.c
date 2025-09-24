#include "util.h"
#include "zopfli_lib.h"
#include "gzip_container.h"

/* The functions doesn't match what in the header of the same filename on purpose. */
/* gcc/clang defaults -ffunction-sections to off, so unused functions will be linked together increasing binary size */

/*
 Loads a file into a memory array.
 */
static int LoadFile(const char* filename, unsigned char** out, size_t* outsize) {
  if (!filename) {
    return ZopfliLoadPipe(stdin, out, outsize);
  }
  FILE* file;

  file = fopen(filename, "rb");
  if (!file) return 0;

  int ret = ZopfliLoadFile(file, out, outsize);

  fclose(file);
  return ret;
}

/*
Saves a file from a memory array, overwriting the file if it existed.
*/
static int SaveFile(const char* filename, const unsigned char* in, size_t insize) {
  FILE* file = filename ? fopen(filename, "wb") : stdout;
  if (file == NULL) {
      return 0;
  }
  int ret = ZopfliSaveFile(file, in, insize);
  if (filename) { fclose(file); }
  return ret;
}

/*
 outfilename: filename to write output to, or 0 to write to stdout instead
 */
int ZopfliGzip(const char* infilename, const char* outfilename, unsigned level, const char* gzip_name, unsigned time) {
  unsigned char* in = 0;
  size_t insize = 0;
  unsigned char* out = 0;
  size_t outsize = 0;

  ZopfliOptions options;
  ZopfliInitOptions(&options, level, 0);
  if (options.numiterations == -1) {
    /* the level actually can be 2-9, 10002-10009, ... */
    /* unsupported level */
    return -2; /* Z_STREAM_ERROR - input param error */
  }

  if (!LoadFile(infilename, &in, &insize)) {
    /* fprintf(stderr, "Invalid input: %s\n", infilename); */
    return -3; /* Z_DATA_ERROR - input data error */
  }

  ZopfliGzipCompressEx(&options, in, insize, &out, &outsize, time, gzip_name);
  free(in);

  if (!SaveFile(outfilename, out, outsize)) {
    /* fprintf(stderr, "Can't write to file %s\n", outfilename); */
    return -1; /* Z_ERRNO - output file io error */
  }

  free(out);
  return 0; /* Z_OK */
}