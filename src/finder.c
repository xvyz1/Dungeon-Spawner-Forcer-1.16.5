#include "finder.h"
#include "threads.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STONE_TOP 60
#define BLOCK 2048

static int64_t fdiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

Area area_make(int64_t centerX, int64_t centerZ, int64_t w, int64_t h) {
    Area a;
    int64_t xmin = centerX - w / 2, xmax = xmin + w - 1;
    int64_t zmin = centerZ - h / 2, zmax = zmin + h - 1;
    a.cx0 = -fdiv(-(xmin - 8), 16);
    a.cx1 = fdiv(xmax - 8, 16);
    a.cz0 = -fdiv(-(zmin - 8), 16);
    a.cz1 = fdiv(zmax - 8, 16);
    if (a.cx1 < a.cx0) a.cx1 = a.cx0;
    if (a.cz1 < a.cz0) a.cz1 = a.cz0;
    a.chunks = (a.cx1 - a.cx0 + 1) * (a.cz1 - a.cz0 + 1);
    a.cenx = centerX;
    a.cenz = centerZ;
    return a;
}

void hits_free(Hits* h) { free(h->v); h->v = NULL; h->n = h->cap = 0; }

static void hits_add(Hits* h, const Hit* x) {
    if (h->n == h->cap) { h->cap = h->cap ? h->cap * 2 : 64; h->v = (Hit*) realloc(h->v, sizeof(Hit) * h->cap); }
    h->v[h->n++] = *x;
}

typedef struct {
    uint64_t seed;
    int pref, target;
    Area ar;
    int64_t width;
    volatile int64_t next, done;
    double t0;
    int lastStep;
    Hits* per;
} Ctx;

typedef struct { int found; Hit hit; int target, pref; } Leaf;

static int on_leaf(void* user, const Room* rooms, int count) {
    Leaf* l = (Leaf*) user;
    int px, py, pz, n;
    if (!stand_best(rooms, count, l->pref, l->target, &px, &py, &pz, &n) || n < l->target) return 0;
    l->found = 1;
    l->hit.n = n; l->hit.nrooms = count;
    memcpy(l->hit.rooms, rooms, sizeof(Room) * count);
    l->hit.px = px; l->hit.py = py; l->hit.pz = pz;
    return 1;
}

static void progress(Ctx* c) {
    double el = ds_time() - c->t0;
    if (el < 10.0) return;
    int64_t d = ds_atomic_get(&c->done);
    if (d <= 0) return;
    double frac = (double) d / (double) c->ar.chunks, est = el / frac;
    int step = est < 60 ? 10 : est < 300 ? 5 : 1;
    int pct = (int) (frac * 100);
    if (pct / step <= c->lastStep) return;
    c->lastStep = pct / step;
    int left = (int) (est - el);
    printf("%d%% done, %d:%02d elapsed, about %d:%02d left\n", pct, (int) el / 60, (int) el % 60, left / 60, left % 60);
    fflush(stdout);
}

static void worker(void* arg, int w) {
    Ctx* c = (Ctx*) arg;
    Scan* sc = scan_new();
    scan_stone(sc, STONE_TOP);
    for (;;) {
        int64_t end = ds_atomic_add(&c->next, BLOCK);
        int64_t start = end - BLOCK;
        if (start >= c->ar.chunks) break;
        if (end > c->ar.chunks) end = c->ar.chunks;
        for (int64_t k = start; k < end; k++) {
            int cx = (int) (c->ar.cx0 + k % c->width), cz = (int) (c->ar.cz0 + k / c->width);
            Leaf l;
            l.found = 0; l.target = c->target; l.pref = c->pref;
            scan_chunk(sc, c->seed, cx, cz, c->pref, c->target, on_leaf, &l);
            if (l.found) { l.hit.cx = cx; l.hit.cz = cz; hits_add(&c->per[w], &l.hit); }
        }
        ds_atomic_add(&c->done, end - start);
        if (w == 0) progress(c);
    }
    scan_free(sc);
}

static int64_t sortCx, sortCz;

static int64_t sdist(const Hit* h) {
    int64_t dx = (int64_t) h->cx * 16 + 8 - sortCx, dz = (int64_t) h->cz * 16 + 8 - sortCz;
    return dx * dx + dz * dz;
}

static int hit_cmp(const void* a, const void* b) {
    const Hit* x = (const Hit*) a; const Hit* y = (const Hit*) b;
    int64_t dx = sdist(x), dy = sdist(y);
    if (dx != dy) return dx < dy ? -1 : 1;
    if (x->cz != y->cz) return x->cz < y->cz ? -1 : 1;
    return x->cx < y->cx ? -1 : x->cx > y->cx;
}

void find_single(uint64_t seed, int pref, int target, Area ar, int threads, Hits* out) {
    Ctx c;
    memset(&c, 0, sizeof c);
    c.seed = seed; c.pref = pref; c.target = target; c.ar = ar;
    c.width = ar.cx1 - ar.cx0 + 1;
    c.t0 = ds_time();
    c.per = (Hits*) calloc(threads, sizeof(Hits));
    ds_parallel(threads, worker, &c);
    memset(out, 0, sizeof *out);
    for (int i = 0; i < threads; i++) {
        for (int k = 0; k < c.per[i].n; k++) hits_add(out, &c.per[i].v[k]);
        free(c.per[i].v);
    }
    free(c.per);
    sortCx = ar.cenx;
    sortCz = ar.cenz;
    qsort(out->v, out->n, sizeof(Hit), hit_cmp);
}
