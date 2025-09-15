/* ungzlib.h */
#ifndef UNGZLIB_H
#define UNGZLIB_H

#include <stdio.h>
#include <time.h>
#include <zlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Step 1) Initialize a decompression stream from an existing file handle.
   - fin: An open file handle (e.g., from fopen, or stdin).
   - On success, returns a z_stream* and takes ownership of the FILE*, stored to opaque.
   - On failure, returns NULL and the caller retains ownership of the FILE*. */
z_stream* ungzlib_init(FILE* fin);

/* Step 1.1) Open a file/stdin and initialize a decompression stream.
   - path: path to input file, or NULL for stdin.
   - This is a convenience wrapper around fopen() and ungzlib_init(). */
z_stream* ungzlib_open(const char* path);

/* Step 2) Optionally peek/skip header to get gzip FNAME/MTIME.
   - Must be called at the start of a member.
   - Z_OK: header parsed successfully (outpath/mtime filled if requested).
   - Z_STREAM_END: not a gzip/zlib member (caller decides if it's an error).
   - Other: fatal parse error (Z_DATA_ERROR, Z_MEM_ERROR, Z_ERRNO, etc.). */
int ungzlib_parse_header(z_stream* strm, char** outpath, time_t* time_out);

/* Step 3) Decompress a single gzip/zlib member.
   - Z_OK: decompression succeeded and input stream is at EOF.
   - Z_STREAM_END: member successfully decompressed, more input data remains.
   - Other: fatal decompression error. */
int ungzlib_process_member(z_stream* strm, FILE* fout);

/* Step 3.1) Decompress all concatenated gzip/zlib members from a stream.
   - Z_OK: all members decompressed successfully and input is at EOF.
   - Z_STREAM_END: one or more members decompressed, followed by non-gzip and non-zlib data.
   - Other: fatal decompression error (possibily with one or more members decompressed). */
int ungzlib_process_all(z_stream* strm, FILE* fout);

/* Step 3.2) Wrapper for ungzlib_process_all() that handles opening/closing the output file.
   - outfile: path to output file, or NULL for stdout. */
int ungzlib_extract_to(z_stream* strm, const char* outfile);

/* Step 4) Close the stream and free all resources, including the owned FILE*. */
void ungzlib_close(z_stream* strm);

/* Example usage: (The loop here presents the logic inside ungzlib_process_all())

   z_stream* s = ungzlib_init(fin);
   char* name = NULL; time_t mtime = 0;

   int r = ungzlib_parse_header(s, &name, &mtime);
   while (1) {
       if (r == Z_STREAM_END) {          // No valid gzip/zlib header.
           // Can be an error on the first member, or just trailing garbage later.
       } else if (r == Z_OK) {           // Gzip/zlib header detected.
           r = ungzlib_process_member(s, fout);
           if (r == Z_STREAM_END) {      // Member ended, but more input remains.
               r = ungzlib_parse_header(s, NULL, NULL); // Check for the next header.
               continue;
           }
       }
       break;                            // Done: clean EOF (Z_OK) or a fatal error.
   };

   free(name);
   ungzlib_close(s);
*/

#ifdef __cplusplus
}
#endif

#endif /* UNGZLIB_H */
