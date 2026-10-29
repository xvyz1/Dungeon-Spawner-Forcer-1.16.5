#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "build.h"
#include "finder.h"
#include "inputs.h"
#include "plan.h"
#include "search.h"
#include "selftest.h"
#include "terrain.h"
#include "threads.h"

static const char* const MOBS[3] = {"SKELETON", "ZOMBIE", "SPIDER"};

typedef struct {
    int64_t seed;
    int pref, target, results, threads, output;
    int64_t w, h, cx, cz;
} FindOpts;

static int get_find_opts(FindOpts* o) {
    int64_t v;
    if (!getI64Number("numeric world seed", &o->seed, 1, 0, INT64_MIN, INT64_MAX)) return 0;
    if (!getIntEnum("mob", &o->pref, MOBS, 3)) return 0;
    if (!getI64Number("spawners of that mob in range of one spot (2-8)", &v, 1, 8, 2, 8)) return 0;
    o->target = (int) v;
    static const char* const areas[2] = {"SQUARE", "RECTANGLE"};
    int area;
    if (!getIntEnum("area type", &area, areas, 2)) return 0;
    if (area == 1) {
        if (!getI64Number("rectangle width (X) in blocks", &o->w, 0, 0, 1, 60000000)) return 0;
        if (!getI64Number("rectangle length (Z) in blocks", &o->h, 0, 0, 1, 60000000)) return 0;
    } else {
        if (!getI64Number("square radius in blocks", &v, 0, 0, 1, 30000000)) return 0;
        o->w = o->h = 2 * v + 1;
    }
    if (!getI64Number("search center X", &o->cx, 1, 0, -30000000, 30000000)) return 0;
    if (!getI64Number("search center Z", &o->cz, 1, 0, -30000000, 30000000)) return 0;
    if (!getI64Number("number of results to show", &v, 1, 20, 1, 1000)) return 0;
    o->results = (int) v;
    int cpus = ds_cpu_count();
    if (!getI64Number("number of threads", &v, 1, cpus < 1024 ? cpus : 1024, 1, 1024)) return 0;
    o->threads = (int) v;
    static const char* const yn[2] = {"YES", "NO"};
    if (!getIntEnumDefault("build commands output", &o->output, yn, 2, 1)) return 0;
    o->output = o->output == 0;
    return 1;
}

static void show(FILE* f, int rank, const Hit* h, const Room* plan, int nplan, int px, int py, int pz, int pref, int n) {
    fprintf(f, "#%d  chunk %d %d, %d %s spawners from one spot at /tp @p %d.5 %d.5 %d.5\n", rank, h->cx, h->cz, n, MOBS[pref], px, py, pz);
    for (int i = 0; i < nplan; i++) {
        int inr = plan[i].type == pref && room_dsq(&plan[i], px - h->cx * 16, py, pz - h->cz * 16) < 256;
        fprintf(f, "    %s %d %d %d%s\n", MOBS[plan[i].type], h->cx * 16 + plan[i].x, plan[i].y, h->cz * 16 + plan[i].z, inr ? "" : "   (outside the sphere)");
    }
}

typedef struct { Hit h; ChunkPlan cp; } Result;

static void show_result(FILE* f, int rank, const Hit* h, const ChunkPlan* cp, int pref) {
    show(f, rank, h, cp->plan, cp->nplan, h->cx * 16 + cp->px, cp->py, h->cz * 16 + cp->pz, pref, cp->n);
}

static int check_chunk(Terrain* tr, Scan* sc, Planner* pl, unsigned char* mask, unsigned char* excl, const FindOpts* o, const Hit* h, ChunkPlan* cp) {
    if (!terrain_chunk_ex(tr, h->cx, h->cz, mask, excl)) return -1;
    scan_mask(sc, mask);
    scan_exclude(sc, excl);
    planner_chunk(pl, (uint64_t) o->seed, h->cx, h->cz, mask);
    SearchSpec ss;
    memset(&ss, 0, sizeof ss);
    ss.seed = (uint64_t) o->seed; ss.cx = h->cx; ss.cz = h->cz; ss.pref = o->pref;
    ss.minN = o->target; ss.maxN = o->target; ss.tlimit = 120;
    chunk_search(pl, sc, &ss, cp);
    return cp->found;
}

static FILE* open_output(const FindOpts* o, char* name, size_t cap) {
    snprintf(name, cap, "%" PRId64 "_%s_%d.txt", o->seed, MOBS[o->pref], o->target);
    for (int i = 2; i < 1000; i++) {
        FILE* t = fopen(name, "rb");
        if (!t) break;
        fclose(t);
        snprintf(name, cap, "%" PRId64 "_%s_%d_%d.txt", o->seed, MOBS[o->pref], o->target, i);
    }
    return fopen(name, "w");
}

static int run_find(void) {
    FindOpts o;
    memset(&o, 0, sizeof o);
    if (!get_find_opts(&o)) return 1;
    Area ar = area_make(o.cx, o.cz, o.w, o.h);
    printf("\nSearching %" PRId64 " chunks with %d threads...\nInitiating...\n", ar.chunks, o.threads);
    double t0 = ds_time();
    Hits hits;
    find_single((uint64_t) o.seed, o.pref, o.target, ar, o.threads, &hits);
    printf("Found %d candidate chunk%s in %.1f s, checking them on the terrain model\n\n", hits.n, hits.n == 1 ? "" : "s", ds_time() - t0);
    char name[256] = "";
    FILE* of = o.output ? open_output(&o, name, sizeof name) : NULL;
    if (o.output && !of) printf("Cannot write the output file\n");
    unsigned char* mask = (unsigned char*) malloc(65536);
    unsigned char* excl = (unsigned char*) malloc(65536);
    Scan* sc = scan_new();
    Planner* pl = planner_new();
    Terrain* tr = terrain_new((uint64_t) o.seed);
    Result* res = (Result*) calloc((size_t) o.results, sizeof(Result));
    int shown = 0, ocean = 0, unverified = 0;
    for (int i = 0; i < hits.n && shown < o.results; i++) {
        ChunkPlan cp;
        int r = check_chunk(tr, sc, pl, mask, excl, &o, &hits.v[i], &cp);
        if (r < 0) { ocean++; continue; }
        if (!r) continue;
        res[shown].h = hits.v[i];
        res[shown].cp = cp;
        shown++;
        show_result(stdout, shown, &hits.v[i], &cp, o.pref);
    }
    printf("\n%d result%s", shown, shown == 1 ? "" : "s");
    if (ocean) printf(" (%d candidate%s in ocean chunks not checked)", ocean, ocean == 1 ? "" : "s");
    printf(", closest to the search center first\n");
    if (of) {
        fprintf(of, "%d result%s, closest to the search center first\n\n", shown, shown == 1 ? "" : "s");
        for (int i = 0; i < shown; i++) show_result(of, i + 1, &res[i].h, &res[i].cp, o.pref);
        for (int i = 0; i < shown; i++) {
            ChunkPlan cp;
            fprintf(of, "\n==========\n\n#%d  chunk %d %d  build commands\n\n", i + 1, res[i].h.cx, res[i].h.cz);
            check_chunk(tr, sc, pl, mask, excl, &o, &res[i].h, &cp);
            if (!build_write(of, pl, tr, o.pref, res[i].h.cx, res[i].h.cz, &cp)) unverified++;
        }
        fclose(of);
        printf("Results and build commands saved to %s%s\n", name, unverified ? " (some layouts could not be verified and have no commands)" : "");
    }
    free(res);
    terrain_free(tr);
    free(mask); free(excl); scan_free(sc); planner_free(pl); hits_free(&hits);
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "selftest") == 0) return selftest_run();
    return run_find();
}
