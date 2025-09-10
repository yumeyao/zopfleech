/* zopgz.c - minimal C CLI for ECT's ZopfliGzip() (gzip-only compressor)
 *
 * Build (example):
 *   cc -O2 -std=c99 zopgz.c -o zopgz \
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
#endif

/* --- Forward declaration. ----------------------- */
extern int ZopfliGzip(const char* infilename, const char* outfilename, unsigned mode, const char* gzip_name, unsigned time);

/* Globals */
static unsigned g_level = 9;
static int g_store_name = 0;
static int g_store_time = 0; /* mirrors g_store_name */
static int g_force_terminal = 0;
static int g_quiet = 0;
static int g_write_stdout = 0;
static const char* g_suffix = ".gz";
static int g_use_stdin = -1;
static int g_keep_input = 0;

/* Helpers */
static void usage(FILE* out) {
    fprintf(out,
        "Usage:\n"
        "  zopgz [options] [files...]\n"
        "  (files are compressed in-place with suffix; no input files -> stdin)\n"
        "\n"
        "Mandatory arguments to long options are mandatory for short options too.\n"
        "\n"
        "  -1 .. -9           set compression level (maps to Zopfli mode)\n"
        "  --fast, --best     aliases for -1 and -9 (discouraged)\n"
        "  -n, --no-name      omit filename (and mtime) in gzip header\n"
        "  -N, --name         store input filename (and mtime) in gzip header\n"
        "  -S, --suffix=SUF   set output suffix when auto-naming (default .gz)\n"
        "  -c, --stdout       write to stdout (for all inputs)\n"
        "  -k, --keep         keep input files (default is to delete on success)\n"
        "  -f, --force        allow writing gzip output to a terminal (stdout)\n"
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

static char* make_default_out_with_suffix(const char* in, const char* suffix) {
    size_t n = strlen(in);
    size_t s = strlen(suffix ? suffix : "");
    char* out = (char*)malloc(n + s + 1);
    if (!out) return NULL;
    memcpy(out, in, n);
    memcpy(out + n, suffix, s + 1);
    return out;
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
            g_use_stdin = 0; /* remember that we saw at least one filename */
            continue;
        }

        if (a[1] == '\0') {
            if (g_use_stdin != -1) {
                fprintf(stderr, "zopgz: use exactly one '-' and no other files to read from stdin\n");
                exit(2);
            }
            g_use_stdin = 1; /* remember that user forced stdin */
            continue;
        }

        if (a[1] == '-' && a[2] == '\0') {
            end_of_opts = 1; /* -- indicates end of args */
            continue;
        }

        /* long options */
        if (strcmp(a, "--help") == 0) { usage(stdout); exit(0); }
        if (strcmp(a, "--force") == 0) { g_force_terminal = 1; continue; }
        if (strcmp(a, "--quiet") == 0) { g_quiet = 1; continue; }
        if (strcmp(a, "--stdout") == 0) { g_write_stdout = 1; continue; }
        if (strcmp(a, "--keep") == 0) { g_keep_input = 1; continue; }
        if (strcmp(a, "--fast") == 0) { g_level = 1; continue; }
        if (strcmp(a, "--best") == 0) { g_level = 9; continue; }
        if (strcmp(a, "--no-name") == 0) { g_store_name = 0; g_store_time = 0; continue; }
        if (strcmp(a, "--name") == 0) { g_store_name = 1; g_store_time = 1; continue; }
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
                case 'f': g_force_terminal = 1; break;
                case 'q': g_quiet = 1; break;
                case 'c': g_write_stdout = 1; break;
                case 'k': g_keep_input = 1; break;
                case 'n': g_store_name = 0; g_store_time = 0; break;
                case 'N': g_store_name = 1; g_store_time = 1; break;
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
}

static int is_symlink_path(const char* path) {
    if (!path) return 0;
#if defined(_WIN32)
    /* Best-effort: treat any reparse point as “symlink-like” and skip. */
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return 0;
    return (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    return S_ISLNK(st.st_mode);
#endif
}

/* Compress one path (NULL => stdin) to file or stdout */
static int compress_one(const char* inpath) {
    /* Skip symbolic links (do not follow), like gzip */
    if (inpath && is_symlink_path(inpath)) {
        if (!g_quiet) fprintf(stderr, "zopgz: %s is a symbolic link -- skipping\n", inpath);
        return 0; /* not an error */
    }

    /* gzip header filename only for file input with -N/--name */
    const char* gzip_name = "";
    if (g_store_name && inpath) {
        gzip_name = path_basename(inpath);
    }

    /* determine outfilename */
    char* outpath = NULL;
    if (!g_write_stdout) {
        outpath = make_default_out_with_suffix(inpath, g_suffix);
        if (!outpath) {
            fprintf(stderr, "zopgz: out of memory\n");
            return 2;
        }
    }

    int rc = ZopfliGzip(inpath, outpath, g_level, gzip_name, 0);

    if (outpath) free(outpath);

    if (rc == 0) {
        /* delete input file if we created a file and user didn't request keep */
        if (!g_write_stdout && inpath && !g_keep_input) {
            if (remove(inpath) != 0) {
                if (!g_quiet) fprintf(stderr, "zopgz: warning: could not remove '%s'\n", inpath);
            }
        }
    } else {
        fprintf(stderr, "zopgz: compression failed for %s (code %d)\n",
                inpath ? inpath : "<stdin>", rc);
    }
    return rc;
}

int main(int argc, char** argv) {
    /* Pass 1: options + presence of filenames */
    parse_args(argc, argv);

    if (g_write_stdout && !g_force_terminal && ISATTY(FILENO(stdout))) {
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

    /* Pass 2: compress inputs (or stdin if none) */
    if (g_use_stdin) {
        /* stdin -> stdout */
        int rc = compress_one(NULL);
        return rc ? rc : 0;
    }

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
        int rc = compress_one(a);
        if (rc != 0) exit_rc = rc; /* continue with other files */
    }
    return exit_rc;
}
