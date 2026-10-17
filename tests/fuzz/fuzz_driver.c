/* usage: fuzz_driver <corpus-dir> [iterations] [seed]   |   fuzz_driver <crash-file>   (replay) */
#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#define MAX_SEEDS 256
#define FUZZ_MAX_INPUT 32768

static uint8_t *seed_data[MAX_SEEDS];
static size_t   seed_len[MAX_SEEDS];
static int      n_seeds;
static uint8_t  cur[FUZZ_MAX_INPUT];
static volatile size_t cur_len;

static void on_abort(int sig) {                 /* save the input that caused the crash */
    (void)sig;
    int fd = open("fuzz-crash.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) { ssize_t w = write(fd, cur, cur_len); (void)w; close(fd); }
    _exit(1);
}

static uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *buf = malloc(FUZZ_MAX_INPUT);
    *len = buf ? fread(buf, 1, FUZZ_MAX_INPUT, f) : 0;
    fclose(f);
    return buf;
}

static void load_seeds(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) { perror(dir); exit(2); }
    struct dirent *e;
    while ((e = readdir(d)) && n_seeds < MAX_SEEDS) {
        if (e->d_name[0] == '.') continue;
        char path[1024]; size_t len;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        uint8_t *buf = read_file(path, &len);
        if (buf && len) { seed_data[n_seeds] = buf; seed_len[n_seeds++] = len; } else free(buf);
    }
    closedir(d);
    if (n_seeds == 0) { fprintf(stderr, "no seeds in %s\n", dir); exit(2); }
}

static const uint8_t TOKENS[] = { '\r', '\n', ':', ' ', '\t', 0, '?', '%', '/', '.', 0x7f, 0xff, '0', '9', '-' };

static size_t mutate(uint8_t *buf, size_t len) {
    for (int rounds = 1 + rand() % 4; rounds > 0; rounds--) {
        switch (rand() % 6) {
        case 0: if (len) buf[rand() % len] ^= (uint8_t)(1u << (rand() % 8)); break;   /* flip a bit */
        case 1: if (len) buf[rand() % len] = (uint8_t)rand(); break;                  /* random byte */
        case 2: if (len < FUZZ_MAX_INPUT) {                                                /* insert a token */
                    size_t p = (size_t)rand() % (len + 1);
                    memmove(buf + p + 1, buf + p, len - p);
                    buf[p] = TOKENS[(size_t)rand() % sizeof TOKENS];
                    len++;
                } break;
        case 3: if (len > 1) {                                                        /* delete a byte */
                    size_t p = (size_t)rand() % len;
                    memmove(buf + p, buf + p + 1, len - p - 1);
                    len--;
                } break;
        case 4: if (len) {                                                            /* duplicate a chunk */
                    size_t p = (size_t)rand() % len, n = 1 + (size_t)rand() % (len - p);
                    if (len + n <= FUZZ_MAX_INPUT) { memmove(buf + p + n, buf + p, len - p); len += n; }
                } break;
        default: {                                                                    /* splice in another seed */
                    int s = rand() % n_seeds;
                    size_t p = (size_t)rand() % (len + 1), n = seed_len[s];
                    if (p + n > FUZZ_MAX_INPUT) n = FUZZ_MAX_INPUT - p;
                    memcpy(buf + p, seed_data[s], n);
                    len = p + n;
                }
        }
    }
    return len;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <corpus-dir|crash-file> [iterations] [seed]\n", argv[0]); return 2; }
    struct stat st;
    if (stat(argv[1], &st) != 0) { perror(argv[1]); return 2; }

    if (S_ISREG(st.st_mode)) {                            /* replay a single input */
        size_t len; uint8_t *buf = read_file(argv[1], &len);
        if (!buf) return 2;
        LLVMFuzzerTestOneInput(buf, len);
        puts("replay finished without a crash");
        return 0;
    }

    long iterations = argc > 2 ? atol(argv[2]) : 1000000;
    unsigned seed = argc > 3 ? (unsigned)atol(argv[3]) : (unsigned)time(NULL);
    printf("seed %u, %ld iterations\n", seed, iterations);
    srand(seed);
    load_seeds(argv[1]);
    signal(SIGABRT, on_abort);

    for (long i = 0; i < iterations; i++) {
        int s = rand() % n_seeds;
        memcpy(cur, seed_data[s], seed_len[s]);
        cur_len = mutate(cur, seed_len[s]);
        LLVMFuzzerTestOneInput(cur, cur_len);
        if (i && i % 200000 == 0) printf("  %ld runs\n", i);
    }
    puts("done: no crashes");
    return 0;
}