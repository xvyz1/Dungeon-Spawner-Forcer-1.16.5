#include "selftest.h"
#include "dungeon.h"
#include "finder.h"
#include "plan.h"
#include "terrain.h"
#include "threads.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define EXPECT_SCAN_COUNT 4
#define EXPECT_SCAN_HASH 5856508837917955743ULL
#define EXPECT_PLAN_BUILD 52
#define EXPECT_PLAN_HASH 9087850136834376958ULL
#define EXPECT_TERRAIN_HASH 13335217250094406287ULL

static uint64_t fnv(uint64_t h, uint64_t v) {
    h ^= v;
    h *= 1099511628211ULL;
    return h;
}

static uint64_t fnv_str(uint64_t h, const char* s) {
    while (*s) h = fnv(h, (unsigned char) *s++);
    return h;
}

static int check_scan(void) {
    Area ar = area_make(0, 0, 25601, 25601);
    Hits h;
    find_single(48, 0, 8, ar, ds_cpu_count(), &h);
    uint64_t d = fnv(1469598103934665603ULL, (uint64_t) h.n);
    for (int i = 0; i < h.n; i++) {
        d = fnv(d, (uint64_t) (uint32_t) h.v[i].cx); d = fnv(d, (uint64_t) (uint32_t) h.v[i].cz); d = fnv(d, (uint64_t) h.v[i].n);
        d = fnv(d, (uint64_t) h.v[i].px); d = fnv(d, (uint64_t) h.v[i].py); d = fnv(d, (uint64_t) h.v[i].pz);
    }
    int ok = h.n == EXPECT_SCAN_COUNT && d == EXPECT_SCAN_HASH;
    if (!ok) printf("scan: %d hits, digest %llu\n", h.n, (unsigned long long) d);
    hits_free(&h);
    return ok;
}

static int check_plan(void) {
    unsigned char* mask = (unsigned char*) malloc(65536);
    for (int y = 0; y < 256; y++) memset(mask + y * 256, y <= 60 ? 1 : 2, 256);
    static const int P[8][5] = {{15, 51, 9, 3, 2}, {15, 30, 4, 3, 2}, {13, 8, 15, 3, 2}, {13, 42, 2, 2, 3}, {15, 40, 15, 2, 2}, {0, 9, 8, 2, 3}, {0, 52, 5, 2, 3}, {7, 43, 15, 3, 3}};
    static const int T[8] = {0, 0, 0, 0, 0, 2, 0, 0};
    Room plan[8];
    for (int i = 0; i < 8; i++) { plan[i].x = P[i][0]; plan[i].y = P[i][1]; plan[i].z = P[i][2]; plan[i].xr = P[i][3]; plan[i].zr = P[i][4]; plan[i].type = T[i]; }
    Planner* pl = planner_new();
    planner_chunk(pl, 48, -11, -11, mask);
    DesignInfo info;
    int ok = planner_design(pl, plan, 8, &info) && planner_verify(pl, plan, 8);
    CmdSet cs = {0};
    uint64_t d = 1469598103934665603ULL;
    int nb = 0;
    if (ok && planner_commands(pl, plan, 8, 6, &cs)) {
        char buf[256];
        for (int i = 0; i < cs.nclear; i++) { cmd_format(&cs.clear[i], -176, -176, buf, sizeof buf); d = fnv_str(d, buf); }
        for (int i = 0; i < cs.nbuild; i++) { cmd_format(&cs.build[i], -176, -176, buf, sizeof buf); d = fnv_str(d, buf); }
        nb = cs.nbuild;
        cmdset_free(&cs);
    } else ok = 0;
    if (ok && (nb != EXPECT_PLAN_BUILD || d != EXPECT_PLAN_HASH)) { printf("plan: %d build commands, digest %llu\n", nb, (unsigned long long) d); ok = 0; }
    planner_free(pl);
    free(mask);
    return ok;
}
static int check_terrain(void) {
    static const int C[4][2] = {{694, 188}, {-987, 341}, {113, 120}, {64, -473}};
    Terrain* t = terrain_new(48);
    unsigned char* m = (unsigned char*) malloc(65536);
    unsigned char* e = (unsigned char*) malloc(65536);
    uint64_t d = 1469598103934665603ULL;
    for (int i = 0; i < 4; i++) {
        terrain_chunk_ex(t, C[i][0], C[i][1], m, e);
        for (int k = 0; k < 65536; k++) { d = fnv(d, m[k]); d = fnv(d, e[k]); }
    }
    free(m);
    free(e);
    terrain_free(t);
    int ok = d == EXPECT_TERRAIN_HASH;
    if (!ok) printf("terrain: digest %llu\n", (unsigned long long) d);
    return ok;
}

int selftest_run(void) {
    int b = check_scan(), c = check_plan(), d = check_terrain();
    printf("area scan %s, planner %s, terrain model %s\n", b ? "ok" : "FAILED", c ? "ok" : "FAILED", d ? "ok" : "FAILED");
    printf(b && c && d ? "selftest: PASSED\n" : "selftest: FAILED\n");
    return b && c && d ? 0 : 1;
}
