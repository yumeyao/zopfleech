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

/* --- Forward declaration. ----------------------- */
extern int ZopfliGzip(const char* infilename, const char* outfilename, unsigned mode, const char* gzip_name, unsigned time);

/* Globals */
static unsigned g_level = 9;
static int g_store_name = 0;
static int g_store_time = 0; /* mirrors g_store_name */
static int g_force = 0;
static int g_quiet = 0;
static int g_write_stdout = 0;
static const char* g_suffix = ".gz";
static int g_use_stdin = -1;  /* -1: undecided; 0: filenames; 1: stdin */
static int g_keep_input = 0;
static int g_recursive = 0;   /* parsed for compatibility; error after parsing */

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
        "  -n, --no-name      omit filename (and mtime) in gzip header\n"
        "  -N, --name         store input filename (and mtime) in gzip header\n"
        "  -S, --suffix=SUF   set output suffix when auto-naming (default .gz)\n"
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

static int path_is_dir(const char* p) {
#if defined(_WIN32)
  DWORD a = GetFileAttributesA(p);
  return (a != INVALID_FILE_ATTRIBUTES) && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat st; return (stat(p, &st) == 0) && S_ISDIR(st.st_mode);
#endif
}

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
        /* Try to classify link target only if we can resolve it. */
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
        /* Mirror all three timestamps from source. */
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

/* Compress one path (NULL => stdin) to file or stdout */
static int compress_one(const char* inpath) {
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

    time_t mtime = 0;
    if (inpath && g_store_time) mtime = mtime_from_stat(&src_st);

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
            return 3;
        }
        /* Overwrite policy: if exists and not forced, prompt (TTY) or refuse (non-tty). */
        if (file_exists(outpath)) {
            if (path_is_dir(outpath)) {
                fprintf(stderr, "zopgz: %s is a directory; cannot overwrite\n", outpath);
                free(outpath);
                return 1;
            }
            if (!g_force) {
                /* No -f: prompt on TTY, refuse otherwise */
                if (ISATTY(FILENO(stdin))) {
                    if (!prompt_yesno_overwrite(outpath)) {
                        if (!g_quiet) fprintf(stderr, "zopgz: not overwritten: %s\n", outpath);
                        free(outpath);
                        return 1; /* non-zero like gzip */
                    }
                } else {
                    fprintf(stderr, "zopgz: %s already exists; use -f to overwrite\n", outpath);
                    free(outpath);
                    return 1;
                }
            }
#if defined(_WIN32)
            DWORD a = GetFileAttributesA(outpath);
            if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY)) {
                SetFileAttributesA(outpath, a & ~FILE_ATTRIBUTE_READONLY);
            }
#endif
            remove(outpath);
        }
    }

    int rc = ZopfliGzip(inpath, outpath, g_level, gzip_name, mtime);

    if (rc == 0) {
        /* delete input file if we created a file and user didn't request keep */
        if (!g_write_stdout && inpath) {
            copystat(outpath, &src_st);
            if (!g_keep_input && remove(inpath) != 0) {
                if (!g_quiet) fprintf(stderr, "zopgz: warning: could not remove '%s'\n", inpath);
            }
        }
    } else {
        fprintf(stderr, "zopgz: compression failed for %s (code %d)\n",
                inpath ? inpath : "<stdin>", rc);
    }

    if (outpath) free(outpath);
    return rc;
}

int main(int argc, char** argv) {
    /* Pass 1: options + presence of filenames */
    parse_args(argc, argv);

    if (g_write_stdout && !g_force && ISATTY(FILENO(stdout))) {
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
        int rc = compress_one(NULL);
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
        int rc = compress_one(a);
        if (rc > exit_rc) exit_rc = rc; /* continue with other files */
    }
    return exit_rc;
}
