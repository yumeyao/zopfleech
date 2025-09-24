# zopfleech

Zopfleech provides a high-quality [deflate] compression library and a `gzip`-compatible command-line tool. The core algorithm is a powerful variant of [zopfli], first seen in [ECT], which blends key ideas from [7-zip].

[deflate]: https://en.wikipedia.org/wiki/Deflate
[zopfli]: https://github.com/google/zopfli
[7-zip]: https://7-zip.org
[ECT]: https://github.com/fhanau/Efficient-Compression-Tool

## About the Project

The name "**zopfleech**" explains the tributes in the scene:
- It's a **variant of [zopfli]** compression algorithm, which is known for its excellent compression ratio with extremely slow speed.
- The core improvement comes from the [**Efficient Compression Tool (ECT)**](https://github.com/fhanau/Efficient-Compression-Tool), whose author ***"leeched"*** the [Binary Tree (BT) match finder from 7-zip](https://github.com/yumeyao/7zDeflate/blob/master/DEFLATE.md#implementations-comparison) to replace the inefficient one in zopfli.  
    *(Notably, 7-zip's finder is so effective that its deflate encoder can, in some cases, beat vanilla Zopfli even in ratio, all while remaining a practical and reasonable speed.)* 
- The result is an algorithm that **usually beats zopfli in both speed and ratio**. This project, in turn, ***leeches*** ECT's refined implementation, turning it into a more focused C library and command-line tool.

### Key Differences from ECT
- **Performance:** Up to ~30% faster on AVX (level `-9`), the higher the level and the bigger the file, the greater the speed improvement. ([PR contributed back](https://github.com/fhanau/Efficient-Compression-Tool/pull/146))
- **Reusability:** A focused, reusable library with more developer controls vs. an integrated component.
- **Goal:** Dedicated lib and `gzip`-replacement archiver with full control vs. a multi-purpose optimizer.

### Comparison Chart
The following tests are run on Ubuntu 24.04 WSL @AMD 9950x, All commands ran **single-threaded**.  
zopgz produces byte-identical output to ect 0.95, differing only in the filename stored in the gzip header (ect enforces saving it, whereas zopgz uses the same switch `-n` as used in `gzip` to bypass it).

| Command       | Compressed Size(Ratio) | Time  | Compressed Size(Ratio) | Time  |
| ------------- |-------------:| -----:|-------------:| -----:|
| Uncompressed Data | `629186560(100.0%)` | [Ubuntu 1604 wsl install.tar](https://aka.ms/wsl-ubuntu-1604) | `917544960(100.0%)` | [gcc-15.1.0.tar](https://ftp.gnu.org/gnu/gcc/gcc-15.1.0/gcc-15.1.0.tar.gz)
| `gzip -9`| `206517627(32.82%)` |   49.61s | `171315772(18.67%)` | 28.71s |
| `zopfli --i1` | `198128222(31.49%)` | 585.28s | `163697780(17.84%)` | 564.55s |
| `zopfli` (`--i15`) | `197070543(31.32%)` | 1342.16s | `162743248(17.74%)` | 1869.04s |
| `zopgz -2` | `195803804(31.12%)` | 61.50s | `163162496(17.78%)` | 85.63s |
| `7z -tgzip -mx9 -mfb258` | `195779873(31.12%)` | 383.01s | `162981178(17.76%)` | 679.82s |
| `ect -gzip -4` | `194976663(30.99%)` | 114.19s | `162624789(17.72%)` | 124.48s |
| `zopgz -4n` | `194976653(30.99%)` | 110.86s | `162624774(17.72%)` | 110.08s |
| `ect -gzip -9` | `193833758(30.81%)` | 1304.69s | `162143097(17.67%)` | 1610.46s |
| `zopgz -9n` | `193833748(30.81%)` | 940.74s | `162143082(17.67%)` | 1215.89s |

As shown, `zopgz` at its highest setting (`-9`) provides the best compression ratio while being **25-30% faster** than the original ECT implementation.

## Features

### lib

- **Fully in C** (relaxed ANSI C) for max reusability and portability.
  - In-memory and FILE* APIs.
  - Compressing into gzip/zlib/raw deflate streams.
  - No coroutine-style streaming API (feed by chunks).
- **Compression Levels**: 2-9 (same as upstream ECT project).
- **Dependency-Free**: The compression functions are self-contained and have no external dependencies (not even zlib).
- **No Decpomression**: Decompression code is provided as a reusable module within the CLI source for those who need it.

### CLI
- `gzip`-compatible, near-complete replacement.
  - support most switches and syntaxes, like `zopgz -9nkf foo.tar` (level `9`, not saving file`n`ame, `k`eep original file, `f`orce overwriting existing `foo.tar.gz` and `f`ollow links)
  - pipe (stdin/stdout), restoring file permissions and timestamps, concatenated multi-streams decompression handling, decompression honoring (or discarding) `FNAME` in the header with taking care of path leak attacks, etc. Almost every usual or unusual feature/behavior you can imagine on `gzip`.
- `-r` or `--recursive` unimplemented on purpose: behavior odds on complex scenarios (not human-understandable) can't really rely on. Should use `find . -type f -exec zopgz {} +` for a reliable and predictable behavior and file-level parallel processing.
- `--rsyncable` unimplemented. The benefits of `gzip --rsyncable` are often misunderstood and only apply under **very specific** conditions (not a simple "I use rsync, I benefit from --rsyncable" way).
- `-v`, `-t`, `-l` not implemented yet.

## Building

The project uses a clean and flexible CMake-based build system.

#### Prerequisites:
- CMake 3.5 or higher with a C compiler.
- zlib (if you want the CLI tool to handle decompression)

#### Standard Build:
Build both the CLI Tool and a static lib.

```
mkdir build
cd build
cmake ..
make
```

In addition, you can add the following arguments to the cmake call:
- `-DBUILD_SHARED_LIBS=ON`: Build a shared lib. (CLI still links against static lib)
- `-DZOPFLEECH_MIN_CPU=AVX2`: (x86/x64 only) Build with AVX2 accelerated code. Possibe values are `AVX2`, `AVX`, `SSE4.2` (the default), `SSE2`, or an empty string "".

#### Lib name explained:
While this project is named **zopfleech**, it's API and source code layout is close to upstream **zopfli** (an ancient version later modded by ECT a lot), and this project is a perfect **replacement of zopfli**, so the names in APIs and lib still use **zopfli**.

#### Alternative Builds:
- The CMake build system is modular. Both `src/` and `src/zopfli` can act as the top-level CMake entry to build CLI tool or lib only.
