#pragma once
#include <stdint.h>
#include "dungeon.h"
#include "plan.h"

typedef struct {
    int found;
    int n;
    int nplan;
    Room plan[8];
    int px, py, pz;
    int upper;
    int exhaustive;
    int designs, skips;
    double seconds;
} ChunkPlan;

typedef struct {
    uint64_t seed;
    int cx, cz;
    int pref;
    int fixedP, px, py, pz;
    int minN, maxN;
    double tlimit;
} SearchSpec;

void chunk_search(Planner* pl, Scan* sc, const SearchSpec* spec, ChunkPlan* out);
double wall_time(void);
