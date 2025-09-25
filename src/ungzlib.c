#include <stdlib.h>
#include <string.h>
#include "ungzlib.h"

#define Z_STREAM_IBUF_SIZE ((1 << 15) - sizeof(z_stream))

typedef struct {
    z_stream z;
    unsigned char ibuf[Z_STREAM_IBUF_SIZE];
} z_stream_ex;

/* Step 1) Initialize a decompression stream from an existing file handle. */
z_stream* ungzlib_init(FILE* fin) {
    z_stream* strm = (z_stream*)malloc(sizeof(z_stream_ex));
    if (!strm) return NULL;

    memset(strm, 0, sizeof(*strm)); /* no need to memset ibuf */

    /* auto-detect zlib or gzip */
    if (inflateInit2(strm, 32 + MAX_WBITS) != Z_OK) {
        free(strm);
        return NULL;
    }

    strm->opaque = (voidpf)fin;  /* stash FILE* for process/parse */
    strm->next_in = ((z_stream_ex*)strm)->ibuf;
    strm->avail_in = 0;

    return strm;
}

/* Step 1.1) Open a file/stdin and initialize a decompression stream. */
z_stream* ungzlib_open(const char* path) {
    FILE* fin = path ? fopen(path, "rb") : stdin;
    if (!fin) return NULL;

    z_stream* strm = ungzlib_init(fin);
    if (!strm) {
        if (fin != stdin) fclose(fin);
        return NULL;
    }
    return strm;
}

static int buffer_fetch(z_stream* z, size_t need) {
    z_stream_ex* strm = (z_stream_ex*)z;
    if (strm->z.avail_in >= need) return Z_OK;
    if (need > sizeof(strm->ibuf)) return Z_BUF_ERROR;  /* impossible to happen */

    size_t cap = (size_t)((strm->ibuf + sizeof(strm->ibuf)) - (strm->z.next_in + strm->z.avail_in));
    need = need ? need : 1;
    if (cap < need || strm->z.avail_in == 0) { /* if fully consumed, carriage return for max capacity */
        if (strm->z.avail_in) memmove(strm->ibuf, strm->z.next_in, strm->z.avail_in);
        strm->z.next_in = strm->ibuf;
        cap = sizeof(strm->ibuf) - strm->z.avail_in;
    }
    FILE* fin = (FILE*)strm->z.opaque;

    do {
        size_t n = fread((unsigned char*)strm->z.next_in + strm->z.avail_in, 1, cap, fin);
        if (n == 0) return feof(fin) ? Z_DATA_ERROR : Z_ERRNO; /* EOF - corrupted. ferror() - Z_ERRNO */
        strm->z.avail_in += (uInt)n;
        cap -= n;
    } while (strm->z.avail_in < need);

    return Z_OK;
}

static int feed_header(z_stream* z, uInt n) {
    uInt old_avail = z->avail_in;
    z->avail_in = n;  /* user guarantees n <= old_avail */
    int ret = inflate(z, Z_NO_FLUSH);
    /* this is only used to restore the state gracefully, if not fully consumed, that means an error */
    uInt consumed = n - z->avail_in;
    z->avail_in = old_avail - consumed;
    return (z->avail_out == 0 || z->avail_in != 0) ? Z_DATA_ERROR : ret;
}

/* 2) Optionally peek/skip header to get gzip FNAME/MTIME. */
int ungzlib_parse_header(z_stream* strm, char** outpath, time_t* time_out) {
    /* during header parse, we provide a 1-byte dummy out */
    unsigned char dummy_out[1];
    strm->next_out  = dummy_out;
    strm->avail_out = 1;

    int ret = buffer_fetch(strm, 2); /* otherwise definitely not gzip/zlib */
    if (ret != Z_OK) return ret == Z_ERRNO ? ret : Z_STREAM_END; /* pop ferror(), otherwise garbage data with Z_STREAM_END */

    unsigned char* hdr = (unsigned char*)strm->next_in;
    if (hdr[0] == 0x1F && hdr[1] == 0x8B) { /* GZIP */
        ret = buffer_fetch(strm, 10);
        if (ret != Z_OK) return ret; /* an obvious gzip header with less than 10 bytes is considered as a hard error */
        hdr = (unsigned char*)strm->next_in;
        unsigned char flg = hdr[3];

        uLong mtime = (uLong)hdr[4] | ((uLong)hdr[5] << 8) | ((uLong)hdr[6] << 16) | ((uLong)hdr[7] << 24);
        if (time_out) *time_out = (time_t)mtime;
        if (outpath) *outpath = NULL;
        if (!outpath || !(flg & 0x08)) return Z_OK; /* no need to do the heavy parse and let zlib do the rest. */

        uInt xlen;
        /* FEXTRA: we must feed the 10-byte header and maybe xlen too(12-byte in total), then xlen bytes */
        if (flg & 0x04) {
            ret = buffer_fetch(strm, 12);
            if (ret != Z_OK) return ret;
            xlen = (uInt)strm->next_in[10] | ((uInt)strm->next_in[11] << 8);
        }
        ret = feed_header(strm, (flg & 0x04) ? 12 : 10);
        if (ret != Z_OK) return ret;

        /* If FEXTRA present, feed xlen bytes possibly larger than buffer */
        if (flg & 0x04) {
            while (xlen > 0) {
                ret = buffer_fetch(strm, 1);
                if (ret != Z_OK) return ret;
                /* feed up to current avail or remaining xlen */
                uInt chunk = strm->avail_in;
                chunk = chunk <= xlen ? chunk : xlen;
                ret = feed_header(strm, chunk);
                if (ret != Z_OK) return ret;
                xlen -= chunk;
            }
        }

        /* if (flg & 0x08) - FNAME already ensured, otherwise we returned early. */
        char* fname = NULL;
        size_t len = 0, capacity = 0;
        while (1) {
            size_t maxlen = strm->avail_in;
            size_t buflen = strnlen((const char*)strm->next_in, maxlen);

            size_t cap_req = len + buflen + 1;
            if (cap_req > capacity) {
                if (buflen != maxlen) capacity = cap_req;
                else capacity = capacity ? 2 * capacity : 256;
                char* tmp = realloc(fname, capacity);
                if (!tmp) { ret = Z_MEM_ERROR; goto fname_fail; }
                fname = tmp;
            }
            memcpy(fname + len, strm->next_in, buflen);
            len += buflen;
            fname[len] = '\0';

            size_t consumed = buflen + (buflen != maxlen ? 1 : 0);
            strm->next_in += consumed, strm->avail_in -= consumed;
            if (buflen != maxlen) break;

            ret = buffer_fetch(strm, 1);
            if (ret != Z_OK) goto fname_fail;
        }

        Bytef* old_next = (Bytef*)strm->next_in;
        uInt old_avail = strm->avail_in;
        strm->next_in = (Bytef*)fname;
        strm->avail_in = (uInt)len + 1;
        ret = feed_header(strm, len + 1);
        strm->next_in = old_next;
        strm->avail_in = old_avail;
        if (ret != Z_OK) goto fname_fail;
        *outpath = fname;
        return Z_OK;

fname_fail:
        free(fname);
        return ret;
    } else if (hdr[0] != 0x78) { /* not GZIP and not ZLIB */
        return Z_STREAM_END;
    }

    return ret;
}

/* Step 3) Decompress a single gzip/zlib member. */
int ungzlib_process_member(z_stream* strm, FILE* fout) {
    unsigned char obuf[1 << 15];
    int ret = Z_OK;

    while (1) {
        if (strm->avail_in == 0) {
            int fetchret = buffer_fetch(strm, 1);
            if (fetchret == Z_DATA_ERROR && ret == Z_STREAM_END) {
                ret = Z_OK; /* true end of the input stream */
                break;
            } else if (fetchret != Z_OK) {
                ret = fetchret;
                break;
            } else if (ret == Z_STREAM_END) {
                break; /* one stream ends with remaining data in input */
            }
        }

        strm->next_out  = obuf;
        strm->avail_out = (uInt)sizeof(obuf);

        ret = inflate(strm, Z_NO_FLUSH);

        size_t produced = sizeof(obuf) - strm->avail_out;
        if (produced) {
            if (fwrite(obuf, 1, produced, fout) != produced) {
                ret = Z_ERRNO; break;
            }
        }

        if (ret == Z_OK) continue;
        if (ret == Z_STREAM_END) {
            if (strm->avail_in) break; /* one stream ends but maybe more */
            else continue; /* fetch to check if more */
        }
        if (ret == Z_BUF_ERROR) {
            /* output buffer filled while input remains */
            continue;
        }

        /* Z_DATA_ERROR / Z_MEM_ERROR / Z_NEED_DICT or other hard errors */
        break;
    }

    /* reset the zlib internal state so the user can continue with further streams */
    if (ret == Z_STREAM_END) {
        uLong total_in = strm->total_in, total_out = strm->total_out;
        (void)inflateReset(strm);
        strm->total_in = total_in, strm->total_out = total_out;
    }

    return ret;
}

/* Step 3.1) Decompress all concatenated gzip/zlib members from a stream. */
int ungzlib_process_all(z_stream* strm, FILE* fout) {
    int ret = Z_OK;
    do {
        ret = ungzlib_process_member(strm, fout);

        if (ret == Z_STREAM_END) { /* we have at least one member succeeded */
            ret = ungzlib_parse_header(strm, NULL, NULL); /* Z_OK -> continue, otherwise garbage or broken */
        } else {
            break; /* Z_OK -> fully decompressed, or hard error */
        }
    } while (ret == Z_OK);

    return ret;
}

/* Step 3.2) Wrapper for ungzlib_process_all() that handles opening/closing the output file. */
int ungzlib_extract_to(z_stream* strm, const char* outfile) {
    FILE* fout = stdout;
    if (outfile) {
        fout = fopen(outfile, "wb");
        if (!fout) return Z_ERRNO;
    }

    int ret = ungzlib_process_all(strm, fout);

    if (fout != stdout) {
        fclose(fout);
    }
    return ret;
}

/* Step 4) Close the stream and free all resources, including the owned FILE*. */
void ungzlib_close(z_stream* strm) {
    if (!strm) return;
    FILE* fin = (FILE*)strm->opaque;
    inflateEnd(strm);
    if (fin && fin != stdin) {
        fclose(fin);
    }
    free(strm);
}

#if 0 /* we reset it inside ungzlib_process() */
#if defined(__ELF__) && (defined(__GNUC__) || defined(__clang__)) && !defined(__APPLE__)
/* Export a public alias symbol that resolves to zlib's inflateReset for really zero overhead. */
__asm__(".globl ungzlib_reinit\n"
        ".type  ungzlib_reinit,@function\n"
        "ungzlib_reinit = inflateReset\n");
#else
int ungzlib_reinit(z_stream* strm) {
    return inflateReset(strm);
}
#endif
#endif

