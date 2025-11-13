#pragma once
#ifndef CSV_H
#define CSV_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

typedef struct {
    char* str;
} Basename;

static inline Basename getFilepathBasename(const char* path) {
    Basename b = {0};
    if (!path) return b;

    const char* last_slash = strrchr(path, '/');
#ifdef _WIN32
    const char* last_backslash = strrchr(path, '\\');
    if (!last_slash || (last_backslash && last_backslash > last_slash))
        last_slash = last_backslash;
#endif
    const char* base = last_slash ? last_slash + 1 : path;

    const char* dot = strrchr(base, '.');
    size_t name_len = dot ? (size_t)(dot - base) : strlen(base);
    char* clean = (char*)malloc(name_len + 1);
    if (!clean) return b;

    for (size_t i = 0; i < name_len; i++) {
        unsigned char c = (unsigned char)base[i];
        clean[i] = (isalnum(c) || c == '-' || c == '_') ? c : '_';
    }
    clean[name_len] = '\0';
    b.str = clean;
    return b;
}

static inline void freeBasename(Basename* b) {
    if (b && b->str) free(b->str);
    if (b) b->str = NULL;
}

static inline int save_csv_reward_accum_by_episode(const char* filename, float* rewards, size_t n) {
    if (!filename || !rewards) return -1;
    FILE* f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "[ERROR] cannot open '%s': %s\n", filename, strerror(errno));
        return -1;
    }
    fprintf(f, "episode,reward\n");
    for (size_t i = 0; i < n; i++)
        fprintf(f, "%zu,%.6f\n", i, rewards[i]);
    fclose(f);
    return 0;
}

#endif
