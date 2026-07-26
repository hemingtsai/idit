/**
 * gen_ascii — Generate a random ASCII text file with random line breaks.
 *
 * Build:
 *   g++ -std=c++17 -O3 -march=native -o gen_ascii gen_ascii.cpp
 *
 * Usage:
 *   ./gen_ascii -o out.txt -s 50G
 *   ./gen_ascii -o out.txt -s 500M --min-line 20 --max-line 300 --seed 42
 */

#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Fast PRNG: xoroshiro128++  (period 2^128 - 1, very fast, good quality)
// ---------------------------------------------------------------------------
struct Rng {
    uint64_t s[2];

    Rng(uint64_t seed) {
        // SplitMix64 seeding
        uint64_t z = seed + 0x9E3779B97F4A7C15ULL;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        s[0] = z ^ (z >> 31);
        z = s[0] + 0x9E3779B97F4A7C15ULL;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        s[1] = z ^ (z >> 31);
    }

    uint64_t next() {
        uint64_t s0 = s[0];
        uint64_t s1 = s[1];
        uint64_t result = s0 + s1;
        s1 ^= s0;
        s[0] = ((s0 << 24) | (s0 >> 40)) ^ s1 ^ (s1 << 16);
        s[1] = (s1 << 37) | (s1 >> 27);
        return result;
    }

    // Uniform in [0, n).  n must be > 0.
    uint64_t range64(uint64_t n) {
        // For small n (our case: line lengths), simple modulo has negligible bias.
        return next() % n;
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static double now_sec() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<double>(tv.tv_sec) +
           static_cast<double>(tv.tv_usec) * 1e-6;
}

static void fmt_size(uint64_t n, char* buf, size_t sz) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    double v = static_cast<double>(n);
    while (v >= 1024.0 && i < 4) { v /= 1024.0; i++; }
    snprintf(buf, sz, "%.1f %s", v, units[i]);
}

static uint64_t parse_size(const char* s) {
    char* end;
    double val = strtod(s, &end);
    if (val < 0) return 0;
    uint64_t mult = 1;
    if (strcasecmp(end, "T") == 0 || strcasecmp(end, "TB") == 0)
        mult = 1024ULL * 1024 * 1024 * 1024;
    else if (strcasecmp(end, "G") == 0 || strcasecmp(end, "GB") == 0)
        mult = 1024ULL * 1024 * 1024;
    else if (strcasecmp(end, "M") == 0 || strcasecmp(end, "MB") == 0)
        mult = 1024ULL * 1024;
    else if (strcasecmp(end, "K") == 0 || strcasecmp(end, "KB") == 0)
        mult = 1024ULL;
    else if (*end != '\0')
        return 0;
    return static_cast<uint64_t>(val * mult);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    // ---- parse args ----
    const char* outpath = nullptr;
    uint64_t target_size = 0;
    int min_line = 40, max_line = 200;
    uint64_t seed = 0;
    size_t buf_mb = 16; // write buffer in MiB

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) &&
            i + 1 < argc) {
            outpath = argv[++i];
        } else if ((strcmp(argv[i], "-s") == 0 ||
                    strcmp(argv[i], "--size") == 0) &&
                   i + 1 < argc) {
            target_size = parse_size(argv[++i]);
            if (target_size == 0) {
                fprintf(stderr, "Invalid size: %s\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "--min-line") == 0 && i + 1 < argc) {
            min_line = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--max-line") == 0 && i + 1 < argc) {
            max_line = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = strtoull(argv[++i], nullptr, 0);
        } else if (strcmp(argv[i], "--buf") == 0 && i + 1 < argc) {
            buf_mb = static_cast<size_t>(atoi(argv[++i]));
            if (buf_mb < 1) buf_mb = 1;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            printf(
                "Usage: %s -o <file> -s <size> [options]\n"
                "\n"
                "Options:\n"
                "  -o, --output FILE   Output file path (required)\n"
                "  -s, --size   SIZE   Target size: 50G, 500M, 10K, etc. "
                "(required)\n"
                "  --min-line   N      Minimum line length (default: 40)\n"
                "  --max-line   N      Maximum line length (default: 200)\n"
                "  --seed       N      Random seed (default: time-based)\n"
                "  --buf        N      Write buffer in MiB (default: 16)\n"
                "  -h, --help          Show this help\n"
                "\n"
                "Examples:\n"
                "  %s -o data.txt -s 50G\n"
                "  %s -o data.txt -s 1G --min-line 20 --max-line 500\n",
                argv[0], argv[0], argv[0]);
            return 0;
        }
    }

    if (!outpath || target_size == 0) {
        fprintf(stderr, "Usage: %s -o <file> -s <size> [options]\n"
                        "Try -h for help.\n",
                argv[0]);
        return 1;
    }

    if (min_line < 1) min_line = 1;
    if (max_line < min_line) max_line = min_line;

    // ---- init ----
    if (seed == 0) {
        seed = static_cast<uint64_t>(time(nullptr)) ^
               (static_cast<uint64_t>(getpid()) << 32);
    }
    Rng rng(seed);

    size_t buf_capacity = buf_mb * 1024 * 1024;
    char* buf = static_cast<char*>(malloc(buf_capacity));
    if (!buf) {
        perror("malloc");
        return 1;
    }

    int fd = open(outpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror(outpath);
        free(buf);
        return 1;
    }

    // ---- generate ----
    const int ASCII_MIN = 32;
    const int ASCII_COUNT = 95; // 32..126 = 95 printable chars (no \r \n \t)

    uint64_t written = 0;
    double t_start = now_sec(), t_last = t_start;
    size_t buf_pos = 0;
    int line_len_range = max_line - min_line + 1;

    while (written < target_size) {
        uint64_t remaining = target_size - written;

        // Random line length
        uint64_t line_len = static_cast<uint64_t>(min_line) +
                            rng.range64(static_cast<uint64_t>(line_len_range));
        if (line_len + 1 > remaining) {
            line_len = (remaining > 1) ? remaining - 1 : 0;
        }

        // Fill line with random printable ASCII
        uint64_t i = 0;

        // Fast path: 4 chars per PRNG call using remainders
        while (i + 4 <= line_len && buf_pos + 4 <= buf_capacity) {
            uint64_t r = rng.next();
            buf[buf_pos++] = static_cast<char>(ASCII_MIN + (r % ASCII_COUNT));
            r /= ASCII_COUNT;
            buf[buf_pos++] = static_cast<char>(ASCII_MIN + (r % ASCII_COUNT));
            r /= ASCII_COUNT;
            buf[buf_pos++] = static_cast<char>(ASCII_MIN + (r % ASCII_COUNT));
            r /= ASCII_COUNT;
            buf[buf_pos++] = static_cast<char>(ASCII_MIN + (r % ASCII_COUNT));
            i += 4;
        }

        // Slow path: remaining chars (< 4)
        while (i < line_len) {
            if (buf_pos >= buf_capacity) {
                ssize_t n = write(fd, buf, buf_capacity);
                if (n < 0) {
                    perror("write");
                    goto cleanup;
                }
                buf_pos = 0;
            }
            buf[buf_pos++] =
                static_cast<char>(ASCII_MIN + rng.range64(ASCII_COUNT));
            i++;
        }

        // Newline
        if (buf_pos >= buf_capacity) {
            ssize_t n = write(fd, buf, buf_capacity);
            if (n < 0) {
                perror("write");
                goto cleanup;
            }
            buf_pos = 0;
        }
        buf[buf_pos++] = '\n';

        // Flush when buffer is getting full
        if (buf_pos >= buf_capacity - 1024) {
            ssize_t n = write(fd, buf, buf_pos);
            if (n < 0) {
                perror("write");
                goto cleanup;
            }
            buf_pos = 0;
        }

        written += line_len + 1;

        // Progress report every 2 seconds
        double t_now = now_sec();
        if (t_now - t_last >= 2.0) {
            double elapsed = t_now - t_start;
            double rate = written / elapsed;
            char sz_w[32], sz_t[32], sz_r[32];
            fmt_size(written, sz_w, sizeof(sz_w));
            fmt_size(target_size, sz_t, sizeof(sz_t));
            fmt_size(static_cast<uint64_t>(rate), sz_r, sizeof(sz_r));
            fprintf(stderr, "\r  %s / %s (%.1f%%)  %s/s  ETA %.0fs   ",
                    sz_w, sz_t, 100.0 * written / target_size, sz_r,
                    (target_size - written) / rate);
            t_last = t_now;
        }
    }

    // Final flush
    if (buf_pos > 0) {
        ssize_t n = write(fd, buf, buf_pos);
        if (n < 0) {
            perror("write");
            goto cleanup;
        }
    }

    // Done
    {
        double elapsed = now_sec() - t_start;
        char sz_w[32], sz_r[32];
        fmt_size(written, sz_w, sizeof(sz_w));
        fmt_size(static_cast<uint64_t>(written / elapsed), sz_r, sizeof(sz_r));
        fprintf(stderr, "\r  Done: %s in %.1fs  (%s/s)                    \n",
                sz_w, elapsed, sz_r);
    }

cleanup:
    free(buf);
    close(fd);
    return 0;
}
