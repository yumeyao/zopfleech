/* zopgz.c - minimal C CLI for ECT's ZopfliGzip() (gzip-only compressor)
 *
 * Build (example):
 *   cc -O2 -std=c99 zopgz.c -o zopgz \
 *      <...link against ECT objects/libs that provide ZopfliGzip...>
 *
 * Usage:
 *   zopgz [options] input_file [output_file]
 *   zopgz [options]                # read from stdin, write to stdout
 *
 * Options:
 *   -1 .. -9           set compression level (maps to Zopfli mode)
 *   -n, --no-name      omit filename in gzip header
 *   -N, --name         store input filename in gzip header
 *   -N <name>          store <name> in gzip header
 *   --name=<name>      store <name> in gzip header
 *   -f, --force        allow writing gzip output to a terminal (stdout)
 *   -h, --help         show help
 *
 * Notes:
 *   - This is a compressor (not an optimizer). It always writes a new .gz.
 *   - When no input_file is given, it reads from stdin and writes to stdout.
 *   - In stdin mode, we pass "" for both input and output filenames to ZopfliGzip().
 *   - On Windows, stdin/stdout are switched to binary mode in stdin mode.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(_WIN32)
#  include <io.h>
#  include <fcntl.h>
#  define ISATTY _isatty
#  define FILENO _fileno
#else
#  include <unistd.h>
#  define ISATTY isatty
#  define FILENO fileno
#endif

/* --- Forward declaration. ----------------------- */
extern int ZopfliGzip(const char* infilename, const char* outfilename, unsigned mode, const char* gzip_name, unsigned time);

/* --- Globals (simple CLI style) ----------------------------------------- */
static unsigned g_level = 9;         /* default level */
static int      g_store_name = 0;    /* 0 = omit; 1 = store */
static const char* g_name_override = NULL;
static const char* g_input_path = NULL;
static const char* g_output_path = NULL;
static int      g_force_terminal = 0;/* allow writing to terminal */

/* --- Helpers ------------------------------------------------------------- */

static void die(const char* msg) {
    fprintf(stderr, "zopgz: %s\n", msg);
    exit(2);
}

static void usage(FILE* out) {
    fprintf(out,
        "Usage:\n"
        "  zopgz [options] input_file [output_file]\n"
        "  zopgz [options]                # read from stdin, write to stdout\n"
        "\n"
        "Options:\n"
        "  -1 .. -9           set compression level (maps to Zopfli mode)\n"
        "  -n, --no-name      omit filename in gzip header\n"
        "  -N, --name         store input filename in gzip header\n"
        "  -N <name>          store <name> in gzip header\n"
        "  --name=<name>      store <name> in gzip header\n"
        "  -f, --force        allow writing gzip output to a terminal (stdout)\n"
        "  -h, --help         show this help\n"
        "\n"
        "Notes:\n"
        "  - Compressor only (not an optimizer). Always produces a new .gz.\n"
        "  - No explicit pipe flags needed: omit input_file to read stdin and write stdout.\n"
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

static char* make_default_out(const char* in) {
    size_t n = strlen(in);
    const char* suffix = ".gz";
    size_t m = n + strlen(suffix) + 1;
    char* out = (char*)malloc(m);
    if (!out) return NULL;
    memcpy(out, in, n);
    memcpy(out + n, suffix, strlen(suffix) + 1);
    return out;
}

/* Parse -N value from either "-N name" or "--name=name" */
static int parse_name_option(int argc, char** argv, int i) {
    const char* a = argv[i];

    if (strcmp(a, "-N") == 0 || strcmp(a, "--name") == 0) {
        g_store_name = 1;
        /* If next is present and not an option, treat as explicit name */
        if (i + 1 < argc && argv[i + 1][0] != '-') {
            g_name_override = argv[i + 1];
            return 2; /* consumed current and next */
        }
        return 1; /* consumed only current */
    }

    if (strncmp(a, "--name=", 7) == 0) {
        g_store_name = 1;
        g_name_override = a + 7;
        return 1;
    }

    return 0; /* not a name option */
}

static void parse_args(int argc, char** argv) {
    int i = 1;
    for (; i < argc; ++i) {
        const char* a = argv[i];

        if (a[0] != '-') {
            /* first non-option = input; second non-option = output */
            if (!g_input_path) {
                g_input_path = a;
            } else if (!g_output_path) {
                g_output_path = a;
            } else {
                die("too many positional arguments");
            }
            continue;
        }

        /* long options */
        if (strcmp(a, "--help") == 0) {
            usage(stdout);
            exit(0);
        }
        if (strcmp(a, "--no-name") == 0) {
            g_store_name = 0;
            continue;
        }
        if (strcmp(a, "--force") == 0) {
            g_force_terminal = 1;
            continue;
        }
        if (strncmp(a, "--name", 6) == 0) {
            int consumed = parse_name_option(argc, argv, i);
            if (!consumed) die("invalid --name option");
            i += (consumed - 1);
            continue;
        }

        /* short options & levels */
        if (strcmp(a, "-h") == 0) {
            usage(stdout);
            exit(0);
        }
        if (strcmp(a, "-n") == 0) {
            g_store_name = 0;
            continue;
        }
        if (strcmp(a, "-f") == 0) {
            g_force_terminal = 1;
            continue;
        }
        if (a[0] == '-' && a[1] >= '1' && a[1] <= '9' && a[2] == '\0') {
            g_level = (unsigned)(a[1] - '0');
            continue;
        }
        if (strcmp(a, "-N") == 0) {
            int consumed = parse_name_option(argc, argv, i);
            i += (consumed - 1);
            continue;
        }

        /* Unknown option */
        fprintf(stderr, "zopgz: unknown option: %s\n", a);
        usage(stderr);
        exit(2);
    }

    /* If stdin mode (no input path), user must not provide an output filename. */
    if (!g_input_path && g_output_path) {
        die("when reading from stdin, do not specify output_file (writes to stdout)");
    }

    if (g_input_path && !g_output_path) {
        char* def = make_default_out(g_input_path);
        if (!def) die("out of memory");
        g_output_path = def; /* freed at process end by OS */
    }
}

/* --- Main ---------------------------------------------------------------- */

int main(int argc, char** argv) {
    parse_args(argc, argv);

    const int stdin_mode = (g_input_path == NULL);

    /* If we will write to stdout (stdin mode), guard against terminal unless -f. */
    if (stdin_mode && !g_force_terminal && ISATTY(FILENO(stdout))) {
        fprintf(stderr,
            "zopgz: refusing to write compressed data to a terminal.\n"
            "       use -f to force, or redirect the output.\n\n");
        usage(stderr);
        return 2;
    }

#if defined(_WIN32)
    if (stdin_mode) {
        _setmode(_fileno(stdin),  _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
    }
#endif

    /* gzip header filename field */
    const char* gzip_name = "";
    if (g_store_name) {
        if (stdin_mode) {
            /* no natural basename in stdin mode; only use explicit override if provided */
            gzip_name = (g_name_override && g_name_override[0]) ? g_name_override : "";
        } else {
            gzip_name = (g_name_override && g_name_override[0]) ? g_name_override
                                                                : path_basename(g_input_path);
        }
    }

    /* Fixed per request */
    unsigned multithreading = 0;
    unsigned ZIP = 0;
    unsigned char isGZ = 0; /* 0 = compress to gzip; 1 would mean optimize existing gzip */

    /* Pass "" to indicate stdin/stdout when in stdin mode */
    const char* infilename  = stdin_mode ? 0 : g_input_path;
    const char* outfilename = stdin_mode ? 0 : g_output_path;

    int rc = ZopfliGzip(infilename, outfilename, g_level, gzip_name, 0);

    if (rc != 0) {
        fprintf(stderr, "zopgz: compression failed (code %d)\n", rc);
        return rc ? rc : 1;
    }

    return 0;
}