#include "util.h"
#include "zopfli_lib.h"

/* The functions doesn't match what in the header of the same filename on purpose. */
/* gcc/clang defaults -ffunction-sections to off, so unused functions will be linked together increasing binary size */

/*
 Loads pipe(stdin) into a memory array. Returns 1 on success.
 */
int ZopfliLoadPipe(FILE* pipe, unsigned char** out, size_t* outsize) {
  const size_t CHUNK = 1 << 20; /* 1 MiB chunks; */
  *out = 0;
  *outsize = 0;

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

    size_t n = fread(buf + size, 1, CHUNK, pipe);
    if (n > 0) {
      size += n;
    }
    if (n < CHUNK) {
      /* EOF or error */
      if (!feof(pipe)) {
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
 Loads a file into a memory array. Returns 1 on success. Fallback to LoadPipe if not seekable.
 */
int ZopfliLoadFile(FILE* file, unsigned char** out, size_t* outsize) {
  if (fseek(file , 0 , SEEK_END) != 0) {
    clearerr(file);
    return ZopfliLoadPipe(file, out, outsize);
  }
  *out = 0;
  *outsize = ftell(file);
  rewind(file);

  *out = (unsigned char*)malloc(*outsize);
  if (!*out && *outsize){
    return 0;
  }
  if (*outsize) {
    size_t testsize = fread(*out, 1, *outsize, file);
    if (testsize != *outsize) {
      /* It could be a directory */
      free(*out);
      *out = 0;
      *outsize = 0;
      return 0;
    }
  }
  return 1;
}

/*
 Saves a file from a memory array. Returns 1 on success.
*/
int ZopfliSaveFile(FILE* file, const unsigned char* in, size_t insize) {
  fwrite((char*)in, 1, insize, file);
  return ferror(file) ? 0 : 1;
}
