#pragma once
#include <stdint.h>
#include "dungeon.h"

typedef struct { int64_t cx0, cx1, cz0, cz1; int64_t chunks; int64_t cenx, cenz; } Area;

typedef struct {
    int cx, cz, n;
    int nrooms;
    Room rooms[8];
    int px, py, pz;
} Hit;

typedef struct { Hit* v; int n, cap; } Hits;

Area area_make(int64_t centerX, int64_t centerZ, int64_t w, int64_t h);
void find_single(uint64_t seed, int pref, int target, Area ar, int threads, Hits* out);
void hits_free(Hits* h);
