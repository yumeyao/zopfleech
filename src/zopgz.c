/* zopgz.c - gzip-only compressor/decompressor front-end
 *
 * Build (example):
 *   cc -O2 -std=c99 zopgz.c ectungz.c -o zopgz \
 *      <...link against ECT objects/libs that provide ZopfliGzip...>
 *
 * Usage:
 *   zopgz [options] [files...]
 *   (files are compressed in-place with suffix; no input files -> stdin)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#ifndef strncasecmp
#define strncasecmp strnicmp
#endif
#endif /* glibc, BSD (MacOS) already have it in string.h */
#include <time.h>

#if defined(_WIN32)
#  include <io.h>
#  include <fcntl.h>
#  define ISATTY _isatty
#  define FILENO _fileno
#  include <windows.h>
#else
#  include <unistd.h>
#  define ISATTY isatty
#  define FILENO fileno
#  include <sys/stat.h>
#  include <utime.h>
#endif

#include "ungzlib.h"

extern int ZopfliGzip(const char* infilename, const char* outfilename, unsigned mode, const char* gzip_name, unsigned time);

/* Globals */
static unsigned g_level = 9;
static int g_store_name = 0;
static int g_store_time = 0; /* mirrors g_store_name */
static int g_force = 0;
static int g_quiet = 0;
static int g_write_stdout = 0;
/* scan backward on decompression if the user specified g_suffix is not present. */
/* .taz .tgz first (index number is more obvious) for replacement with .tar instead of strip */
#define known_suffixes_gz known_suffixes[6]
static const char* known_suffixes[] = {".taz", ".tgz", "-z", "_z", "-gz", ".z", ".gz"};
static const char* g_suffix = NULL;
static size_t g_suffix_len = 0;
static int g_use_stdin = -1;  /* -1: undecided; 0: filenames; 1: stdin */
static int g_keep_input = 0;
static int g_recursive = 0;   /* parsed for compatibility; error after parsing */
static int g_decompress = 0;

/* Helpers */
static void usage(FILE* out) {
    fprintf(out,
        "Usage:\n"
        "  zopgz [options] [files...]\n"
        "  (files are compressed in-place with suffix; no input files -> stdin)\n"
        "\n"
        "Mandatory arguments to long options are mandatory for short options too.\n"
        "\n"
        "  -1 .. -9           compression level, the higher the better but slower\n"
        "  --fast, --best     aliases for -1 and -9 (discouraged)\n"
        "  -d, --decompress   decompress (instead of compress)\n"
        "  -n, --no-name      omit/ignore filename (and mtime)\n"
        "  -N, --name         store/restore filename (and mtime)\n"
        "  -S, --suffix=SUF   use suffix SUF on compressed files (default .gz)\n"
        "  -c, --stdout       write to stdout (for all inputs)\n"
        "  -k, --keep         keep input files (default is to delete on success)\n"
        "  -f, --force        force overwrite of output file and compress links\n"
        "  -q, --quiet        suppress warnings\n"
        "  -h, --help         show this help\n"
    );
}

static const char* path_basename(const char* p) {
    if (!p) return "";
    const char* slash = strrchr(p, '/');
#if defined(_WIN32)
    const char* bslash = strrchr(p, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
    return slash ? (slash + 1) : p;
}

static char* make_joint_path(const char* path, size_t path_len, const char* suffix, size_t suffix_len) {
    char* out = (char*)malloc(path_len + suffix_len + 1);
    if (!out) {
        fprintf(stderr, "zopgz: out of memory\n");
        return NULL;
    }
    memcpy(out, path, path_len);
    memcpy(out + path_len, suffix, suffix_len);
    out[path_len + suffix_len] = '\0';
    return out;
}

static char* make_outname_with_suffix(const char* in, const char* suffix) {
    size_t n = strlen(in);
    size_t s = strlen(suffix);
    return make_joint_path(in, n, suffix, s);
}

static void parse_args(int argc, char** argv) {
    int end_of_opts = 0;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];

        if (a[0] != '-' || end_of_opts) {
            if (g_use_stdin == 1) {
                fprintf(stderr, "zopgz: use exactly one '-' and no other files to read from stdin\n");
                exit(2);
            }
            g_use_stdin = 0; /* saw at least one filename */
            continue;
        }

        if (a[1] == '\0') {
            if (g_use_stdin != -1) {
                fprintf(stderr, "zopgz: use exactly one '-' and no other files to read from stdin\n");
                exit(2);
            }
            g_use_stdin = 1; /* user forced stdin */
            continue;
        }

        if (a[1] == '-' && a[2] == '\0') {
            end_of_opts = 1; /* -- indicates end of args */
            continue;
        }

        /* long options */
        if (strcmp(a, "--help") == 0) { usage(stdout); exit(0); }
        if (strcmp(a, "--force") == 0) { g_force = 1; continue; }
        if (strcmp(a, "--quiet") == 0) { g_quiet = 1; continue; }
        if (strcmp(a, "--stdout") == 0) { g_write_stdout = 1; continue; }
        if (strcmp(a, "--keep") == 0) { g_keep_input = 1; continue; }
        if (strcmp(a, "--fast") == 0) { g_level = 1; continue; }
        if (strcmp(a, "--best") == 0) { g_level = 9; continue; }
        if (strcmp(a, "--no-name") == 0) { g_store_name = 0; g_store_time = 0; continue; }
        if (strcmp(a, "--name") == 0) { g_store_name = 1; g_store_time = 1; continue; }
        if (strcmp(a, "--recursive") == 0) { g_recursive = 1; continue; }
        if (strcmp(a, "--decompress") == 0) { g_decompress = 1; continue; }
        if (strncmp(a, "--suffix", 8) == 0) {
            if (a[8] == '=' && a[9] != '\0') { g_suffix = a + 9; continue; }
            if (a[8] == '=' || a[8] == '\0') {
                fprintf(stderr, "zopgz: --suffix requires a value, use --suffix=SUF\n");
                exit(2);
            }
            /* fallthrough to unknown option */
        }

        if (a[1] == '-') {
            fprintf(stderr, "zopgz: unknown option: %s\n", a);
            usage(stderr);
            exit(2);
        }

        /* short options / clusters */
        for (int j = 1; a[j] != '\0'; ++j) {
            char c = a[j];
            if (c >= '1' && c <= '9') { g_level = (unsigned)(c - '0'); continue; }
            switch (c) {
                case 'h': usage(stdout); exit(0);
                case 'f': g_force = 1; break;
                case 'q': g_quiet = 1; break;
                case 'c': g_write_stdout = 1; break;
                case 'k': g_keep_input = 1; break;
                case 'r': g_recursive = 1; break;
                case 'n': g_store_name = 0; g_store_time = 0; break;
                case 'N': g_store_name = 1; g_store_time = 1; break;
                case 'd': g_decompress = 1; break;
                case 'S': {
                    const char* val = (a[j+1] ? &a[j+1] : (i+1<argc ? argv[++i] : NULL));
                    if (!val || *val == '\0') {
                        fprintf(stderr, "zopgz: -S requires a non-empty suffix\n");
                        exit(2);
                    }
                    g_suffix = val;
                    j = (int)strlen(a) - 1; /* if inline */
                    break;
                }
                default:
                    fprintf(stderr, "zopgz: unknown option: -%c\n", c);
                    usage(stderr);
                    exit(2);
            }
        }
    }

    /* at the end, set g_write_stdout to 1 if use stdin */
    if (g_use_stdin) g_write_stdout = 1;
    if (g_write_stdout) g_store_name = g_store_time = 0;  /* gzip behavior */
    g_suffix_len = 3; /* unconditionally for .gz first */
    if (g_suffix) g_suffix_len = strlen(g_suffix);
    else if (!g_decompress) g_suffix = known_suffixes_gz; /* .gz */
    if (g_recursive) {
        fprintf(stderr, "zopgz: recursive mode is not supported. consider: find DIR -type f -exec zopgz {} \\;\n");
        exit(2);
    }
}

static int file_exists(const char* path) {
    if (!path || !*path) return 0;
#if defined(_WIN32)
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path, &st) == 0;
#endif
}

/* Return 1 = yes, 0 = no. Reads one char from stdin (TTY) and drains rest of line. */
static int prompt_yesno_overwrite(const char* outpath) {
    fprintf(stderr, "zopgz: %s already exists; replace? (y/N) ", outpath);
    fflush(stderr);
    int ch = getchar();
    int yes = (ch == 'y' || ch == 'Y');
    while (ch != '\n' && ch != EOF) ch = getchar();
    return yes;
}

#if defined(_WIN32)
#define FILE_STAT WIN32_FILE_ATTRIBUTE_DATA
static time_t mtime_from_stat(FILE_STAT* pstat) {
    /* Convert FILETIME (100ns ticks since 1601-01-01) to time_t (seconds since 1970-01-01). */
    ULONGLONG ft = ((ULONGLONG)pstat->ftLastWriteTime.dwHighDateTime << 32) |
                    (ULONGLONG)pstat->ftLastWriteTime.dwLowDateTime;
    const ULONGLONG TICKS_PER_SEC = 10000000ULL;
    const ULONGLONG EPOCH_DIFF    = 11644473600ULL; /* seconds between 1601 and 1970 */
    return (time_t)((ft / TICKS_PER_SEC) - EPOCH_DIFF);  /* auto wrap */
}
#else
#define FILE_STAT struct stat
#define mtime_from_stat(pstat) ((pstat)->st_mtime)
#endif

/* 0 for regular files, 1 for symlink, 2 for directory, 3 for 1 + 2 */
static int probe_path(const char* path, FILE_STAT* pstat) {
    int ret = 0;
#if defined(_WIN32)
    /* Symlinks/junctions are reparse points; DIRECTORY bit often reflects target kind. */
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, pstat)) return 0;
    DWORD attr = pstat->dwFileAttributes;
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) ret |= 1;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)     ret |= 2;
#else
    if (lstat(path, pstat) != 0) return ret;
    if (S_ISLNK(pstat->st_mode)) {
        ret |= 1;
        if (stat(path, pstat) == 0 && S_ISDIR(pstat->st_mode)) ret |= 2;
    } else if (S_ISDIR(pstat->st_mode)) {
        ret |= 2;
    }
#endif
    return ret;
}

/* Copy source mode (POSIX) and timestamps (all platforms) onto outpath. */
static void copystat(const char* outpath, const FILE_STAT* src) {
#if defined(_WIN32)
    HANDLE h = CreateFileA(outpath,
                           FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        SetFileTime(h, &src->ftCreationTime, &src->ftLastAccessTime, &src->ftLastWriteTime);
        CloseHandle(h);
    }
    /* minimal chmod on Windows */
    if (src->dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
        DWORD dst = GetFileAttributesA(outpath);
        SetFileAttributesA(outpath, dst | FILE_ATTRIBUTE_READONLY);
    }
#else
    chmod(outpath, src->st_mode & 07777);

    struct utimbuf tb;
    tb.actime  = src->st_atime;
    tb.modtime = src->st_mtime;
    utime(outpath, &tb);
#endif
}

/* Prepare outpath for writing: prompt/refuse/remove as needed. */
static int prepare_out_for_write(const char* outpath) {
    if (!file_exists(outpath)) return 0;
    FILE_STAT dst_st;
    int info = probe_path(outpath, &dst_st);
    if (info == 2) {
        fprintf(stderr, "zopgz: %s is a directory; cannot overwrite\n", outpath);
        return 1;
    }
    if (!g_force) {
        if (ISATTY(FILENO(stdin))) {
            if (!prompt_yesno_overwrite(outpath)) {
                if (!g_quiet) fprintf(stderr, "zopgz: not overwritten: %s\n", outpath);
                return 1;
            }
        } else {
            fprintf(stderr, "zopgz: %s already exists; use -f to overwrite\n", outpath);
            return 1;
        }
    }
#if defined(_WIN32)
    {
        DWORD a = GetFileAttributesA(outpath);
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY)) {
            SetFileAttributesA(outpath, a & ~FILE_ATTRIBUTE_READONLY);
        }
    }
#endif
    remove(outpath);
    return 0;
}

static void z_stream_cleanup(z_stream* strm) {
    inflateEnd(strm);
    free(strm);
}

static char* decide_outpath(const char* inpath, z_stream* strm, time_t* hdr_time) {
    char* hdr_name = NULL;
    int ret = ungzlib_parse_header(strm, &hdr_name, hdr_time);
    if (ret != Z_OK) {
        fprintf(stderr, "zopgz: bad gzip/zlib header in %s\n", inpath ? inpath : "<stdin>");
        return NULL;
    }

    char* outpath = NULL;
    const char* base_name = path_basename(inpath);
    size_t path_len = (size_t)(base_name - inpath);
    size_t base_len = 0;
    int is_tgz = 0;

    if (g_store_name && hdr_name && *hdr_name) {
        const char* safe_name = path_basename(hdr_name);
        base_len = strlen(safe_name);
        if (base_name == inpath) {
            if (safe_name != hdr_name) memmove(hdr_name, safe_name, base_len + 1);
            outpath = hdr_name; /* takes the ownership after memmove. */
            hdr_name = NULL;
        } else {
            /* construct path/to/base_name with base_name replaced with hdr_name */
            base_name = safe_name;
        }
    } else {
        base_len = strlen(base_name);
        size_t suffix_len = g_suffix_len;
        const char** suffix = g_suffix ? &g_suffix : &known_suffixes_gz;
        do {
            if (base_len > suffix_len && strncasecmp(base_name + base_len - suffix_len, *suffix, suffix_len) == 0) {
                if (!g_suffix && (suffix == &known_suffixes[0] || suffix == &known_suffixes[1])) {
                    /* replace ".tgz" or ".taz" with ".tar" */
                    path_len = path_len + base_len - 4;
                    base_name = ".tar", base_len = 4;
                } else {
                    base_len = base_len - suffix_len;
                }
                goto found_suffix;
            }
        } while (!g_suffix && suffix-- != &known_suffixes[0] && (suffix_len = strlen(*suffix)));
        if (g_suffix) fprintf(stderr, "zopgz: cannot derive output name for %s with suffix %s\n", inpath, g_suffix);
        else fprintf(stderr, "zopgz: unknown suffix of %s for decompression\n", inpath);
        z_stream_cleanup(strm);
        goto fail;
    }
    if (!outpath) {
found_suffix:
        outpath = make_joint_path(inpath, path_len, base_name, base_len);
    }
fail:
    if (hdr_name) free(hdr_name);
    return outpath;
}

/* Compress/decompress one path (NULL => stdin) to file or stdout */
static int process_one(const char* inpath) {
    int info;
    FILE_STAT src_st;
    /* Skip symbolic links without -f */
    if (inpath && (info = probe_path(inpath, &src_st))) {
        if (info & 1 && !g_force) {
            fprintf(stderr, "zopgz: %s is a symbolic link -- skipping\n", inpath);
            return 1;
        }
        if (info & 2) {
            if (!g_quiet) fprintf(stderr, "zopgz: %s is a %sdirectory -- ignored\n",
                                  inpath, (info & 1) ? "symlink to " : "");
            return 1;
        }
        /* else: -f and target is a file; proceed */
    }

    int ret = 1;
    char* outpath = NULL;

    union {
        z_stream* strm;
        const char* gzip_name;
    } ctx;
    time_t mtime = 0;
    if (g_decompress) {
        ctx.strm = ungzlib_open(inpath);
        if (!ctx.strm) {
            fprintf(stderr, "zopgz: cannot open input for decompression: %s\n", inpath ? inpath : "<stdin>");
            return 1;
        }

        if (!g_write_stdout) {
            outpath = decide_outpath(inpath, ctx.strm, &mtime);
            /* decide_outpath() already printed an error and cleaned up on header failure */
        }
    } else {
        if (inpath && g_store_time) mtime = mtime_from_stat(&src_st);
        ctx.gzip_name = (g_store_name && inpath) ? path_basename(inpath) : "";

        if (!g_write_stdout) {
            outpath = make_outname_with_suffix(inpath, g_suffix);
        }
    }

    if (!g_write_stdout && !outpath) goto fail;
    if (!g_write_stdout && prepare_out_for_write(outpath)) goto fail;

    if (g_decompress) {
        ret = ungzlib_extract_to(ctx.strm, outpath);
    } else {
        ret = ZopfliGzip(inpath, outpath, g_level, ctx.gzip_name, mtime);
    }
    if (ret == 0 /* Z_OK or 0 */ || (g_decompress && Z_STREAM_END)) {
        if (!g_write_stdout && inpath) {
            if (g_decompress && g_store_time && mtime != 0) {
#if defined(_WIN32)
                /* Convert time_t (seconds since 1970) -> FILETIME (100ns since 1601). */
                const ULONGLONG TICKS_PER_SEC = 10000000ULL;
                const ULONGLONG EPOCH_DIFF    = 11644473600ULL;

                ULONGLONG secs  = (ULONGLONG)(unsigned long)mtime + EPOCH_DIFF;
                ULONGLONG ticks = secs * TICKS_PER_SEC;

                FILETIME ft;
                ft.dwLowDateTime  = (DWORD)(ticks & 0xFFFFFFFFu);
                ft.dwHighDateTime = (DWORD)(ticks >> 32);

                src_st.ftLastWriteTime = ft;
#else
                src_st.st_mtime = mtime;
#endif
            }
            copystat(outpath, &src_st);
            if (!g_keep_input && remove(inpath) != 0) {
                if (!g_quiet) fprintf(stderr, "zopgz: warning: could not remove '%s'\n", inpath);
            }
            if (ret == Z_STREAM_END) {
                if (!g_quiet) fprintf(stderr, "zopgz: %s: decompression OK, trailing garbage ignored\n", inpath ? inpath : "<stdin>");
            }
        }
    } else {
        fprintf(stderr, "zopgz: %s failed for %s (code %d)\n",
                g_decompress ? "decompression" : "compression",
                inpath ? inpath : "<stdin>", ret);
    }
fail:
    if (g_decompress && ctx.strm) ungzlib_close(ctx.strm);
    if (outpath) free(outpath);
    return ret;
}

int main(int argc, char** argv) {
    /* Pass 1: options + presence of filenames */
    parse_args(argc, argv);

    /* Only refuse writing *compressed* data to a terminal. Decompression to
       a terminal is fine (text/zcat). */
    if (!g_decompress && g_write_stdout && !g_force && ISATTY(FILENO(stdout))) {
        fprintf(stderr,
            "zopgz: refusing to write compressed data to a terminal\n"
            "       use -f to force, or redirect the output\n\n");
        usage(stderr);
        return 2;
    }

#if defined(_WIN32)
    if (g_use_stdin) { _setmode(_fileno(stdin),  _O_BINARY); }
    if (g_write_stdout) { _setmode(_fileno(stdout), _O_BINARY); }
#endif

    /* stdin -> stdout (no filenames or sole "-") */
    if (g_use_stdin) {
        int rc = process_one(NULL);
        return rc ? rc : 0;
    }

    /* compress file operands (non-recursive) */
    int exit_rc = 0;
    int end_of_opts = 0;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!end_of_opts) {
            if (strcmp(a, "--") == 0) { end_of_opts = 1; continue; }
            if (a[0] == '-') {
                /* ignore options; special-handle -S to skip its value */
                if (strcmp(a, "-S") == 0) {
                    if (i + 1 < argc) i++; /* skip suffix value */
                }
                continue;
            }
        }
        int rc = process_one(a);
        if (rc > exit_rc) exit_rc = rc; /* continue with other files */
    }
    return exit_rc;
}
