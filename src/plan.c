#include "plan.h"
#include "hset.h"
#include "jrand.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define S_ 1
#define A_ 2
#define C_ 3
#define P_ 4
#define L_ 5

#define OWID 32
#define OLO (-8)
#define NCELL (OWID * OWID * 256)
#define MAXREQ 1024
#define MAXROOMS 8
#define MAXJ 65536

typedef struct { int idx; unsigned char want; } Req;

typedef struct {
    Req req[MAXREQ]; int nreq;
    int x, y, z, xr, zr, f;
    int hx, hz, hasHole;
} RoomRec;

struct Planner {
    uint64_t seed;
    int cx, cz;
    const unsigned char* mask;
    unsigned char over[NCELL];
    unsigned char touched[NCELL];
    unsigned char init[NCELL];
    int stamp[NCELL];
    int epoch;
    int jidx[MAXJ];
    unsigned char jold[MAXJ];
    int nj;
    int logging;
    RoomRec rooms[MAXROOMS]; int nrooms;
    Req reqbuf[MAXREQ];
    unsigned char scratch[NCELL];
};

static inline int oidx(int x, int y, int z) { return (x - OLO) + (z - OLO) * OWID + y * (OWID * OWID); }
static inline int is_solid(int s) { return s == S_ || s == C_ || s == P_; }
static inline int in_chunk(int x, int z) { return x >= 0 && x < 16 && z >= 0 && z < 16; }
static inline int cell_ok(int x, int y, int z) { return y >= 0 && y <= 255 && x >= OLO && x < OLO + OWID && z >= OLO && z < OLO + OWID; }

static inline int pget(const Planner* p, int x, int y, int z) {
    if (!cell_ok(x, y, z)) return A_;
    int v = p->over[oidx(x, y, z)];
    if (v) return v;
    if (in_chunk(x, z)) {
        int m = p->mask[x + z * 16 + y * 256];
        return m == 1 ? S_ : m == 2 ? A_ : L_;
    }
    return A_;
}

static inline void pput(Planner* p, int x, int y, int z, int s) {
    if (!cell_ok(x, y, z)) return;
    int i = oidx(x, y, z);
    if (p->logging && p->nj < MAXJ) { p->jidx[p->nj] = i; p->jold[p->nj] = p->over[i]; p->nj++; }
    p->over[i] = (unsigned char) s;
}

static void rollback(Planner* p, int mark) {
    for (int k = p->nj - 1; k >= mark; k--) p->over[p->jidx[k]] = p->jold[k];
    p->nj = mark;
}

Planner* planner_new(void) {
    Planner* p = (Planner*) calloc(1, sizeof(Planner));
    return p;
}

void planner_free(Planner* p) { free(p); }

void planner_chunk(Planner* p, uint64_t seed, int cx, int cz, const unsigned char* mask) {
    p->seed = seed; p->cx = cx; p->cz = cz; p->mask = mask;
}

static int valid_room(const Planner* p, int x, int y, int z, int xr, int zr) {
    int exits = 0;
    for (int dx = -xr - 1; dx <= xr + 1; dx++)
        for (int dy = -1; dy <= 4; dy++)
            for (int dz = -zr - 1; dz <= zr + 1; dz++) {
                int s = pget(p, x + dx, y + dy, z + dz);
                if ((dy == -1 || dy == 4) && !is_solid(s)) return 0;
                if (dy == 0 && (dx == -xr - 1 || dx == xr + 1 || dz == -zr - 1 || dz == zr + 1) && s == A_ && pget(p, x + dx, y + 1, z + dz) == A_) exits++;
            }
    return exits >= 1 && exits <= 5;
}

static int generate_room(Planner* p, JRand* r, int x, int y, int z, int xr, int zr) {
    for (int dx = -xr - 1; dx <= xr + 1; dx++)
        for (int dy = 3; dy >= -1; dy--)
            for (int dz = -zr - 1; dz <= zr + 1; dz++) {
                int cx = x + dx, cy = y + dy, cz = z + dz;
                int s = pget(p, cx, cy, cz);
                int interior = dx != -xr - 1 && dx != xr + 1 && dz != -zr - 1 && dz != zr + 1 && dy != -1;
                if (interior) {
                    if (s != C_ && s != P_) pput(p, cx, cy, cz, A_);
                } else if (cy >= 0 && !is_solid(pget(p, cx, cy - 1, cz))) {
                    pput(p, cx, cy, cz, A_);
                } else if (is_solid(s) && s != C_) {
                    if (dy == -1) jr_int(r, 4);
                    pput(p, cx, cy, cz, S_);
                }
            }
    for (int a = 0; a < 2; a++) {
        for (int b = 0; b < 3; b++) {
            int px = x + jr_int(r, xr * 2 + 1) - xr;
            int pz = z + jr_int(r, zr * 2 + 1) - zr;
            if (pget(p, px, y, pz) == A_) {
                int n = is_solid(pget(p, px + 1, y, pz)) + is_solid(pget(p, px - 1, y, pz)) + is_solid(pget(p, px, y, pz + 1)) + is_solid(pget(p, px, y, pz - 1));
                if (n == 1) {
                    pput(p, px, y, pz, C_);
                    jr_long(r);
                    break;
                }
            }
        }
    }
    pput(p, x, y, z, P_);
    return jr_mob(r);
}

static void attempt_draw(JRand* r, int* x, int* y, int* z, int* xr, int* zr) {
    *x = jr_int(r, 16); *z = jr_int(r, 16); *y = jr_int(r, 256);
    *xr = jr_int(r, 2) + 2; *zr = jr_int(r, 2) + 2;
}

static void room_requirements(int x, int y, int z, int xr, int zr, int f, int hasHole, int hx, int hz, int sides,
                              Req* req, int* nreq, int out_floor[][2], int* nout, int ring[][2], int* nring) {
    int nr = 0, no = 0, ng = 0;
    for (int dx = -xr - 1; dx <= xr + 1; dx++)
        for (int dz = -zr - 1; dz <= zr + 1; dz++) {
            int cxx = x + dx, czz = z + dz;
            int isring = dx == -xr - 1 || dx == xr + 1 || dz == -zr - 1 || dz == zr + 1;
            if (in_chunk(cxx, czz)) continue;
            if (cell_ok(cxx, y - 1, czz)) { req[nr].idx = oidx(cxx, y - 1, czz); req[nr].want = S_; nr++; }
            if (cell_ok(cxx, y + 4, czz)) { req[nr].idx = oidx(cxx, y + 4, czz); req[nr].want = S_; nr++; }
            out_floor[no][0] = cxx; out_floor[no][1] = czz; no++;
            if (isring) {
                ring[ng][0] = cxx; ring[ng][1] = czz; ng++;
                int hole = hasHole && hx == cxx && hz == czz;
                int side = (dx == -xr - 1 ? 1 : 0) | (dx == xr + 1 ? 2 : 0) | (dz == -zr - 1 ? 4 : 0) | (dz == zr + 1 ? 8 : 0);
                if (cell_ok(cxx, y, czz)) { req[nr].idx = oidx(cxx, y, czz); req[nr].want = ((side & sides) && !hole) ? S_ : A_; nr++; }
                if (cell_ok(cxx, y + 1, czz)) { req[nr].idx = oidx(cxx, y + 1, czz); req[nr].want = hole ? A_ : S_; nr++; }
            } else {
                for (int dy = 0; dy < 4; dy++) if (cell_ok(cxx, y + dy, czz)) { req[nr].idx = oidx(cxx, y + dy, czz); req[nr].want = A_; nr++; }
            }
        }
    for (int i = 0; i < no; i++) {
        if (cell_ok(out_floor[i][0], y - 2, out_floor[i][1])) { req[nr].idx = oidx(out_floor[i][0], y - 2, out_floor[i][1]); req[nr].want = i < f ? S_ : A_; nr++; }
    }
    *nreq = nr; *nout = no; *nring = ng;
}

static int attempt_eq(int x, int y, int z, int xr, int zr, const Room* q) {
    return q->x == x && q->y == y && q->z == z && q->xr == xr && q->zr == zr;
}

int planner_design(Planner* p, const Room* plan, int n, DesignInfo* info) {
    memset(p->over, 0, NCELL);
    memset(p->touched, 0, NCELL);
    memset(p->init, 0, NCELL);
    p->nj = 0; p->logging = 0;
    p->nrooms = 0;
    info->ok = 0; info->failPi = -1; info->failNext = 0;
    JRand r;
    jr_chunk(&r, p->seed, p->cx, p->cz);
    int pi = 0;
    Req* req = p->reqbuf;
    int outf[160][2], ring[160][2];
    for (int att = 0; att < 8; att++) {
        int x, y, z, xr, zr;
        attempt_draw(&r, &x, &y, &z, &xr, &zr);
        int planned = pi < n && attempt_eq(x, y, z, xr, zr, &plan[pi]);
        if (!planned) {
            if (valid_room(p, x, y, z, xr, zr)) generate_room(p, &r, x, y, z, xr, zr);
            continue;
        }
        int want_type = plan[pi].type;
        int nat_exits = 0;
        for (int dx = -xr - 1; dx <= xr + 1; dx++)
            for (int dz = -zr - 1; dz <= zr + 1; dz++) {
                if (dx == -xr - 1 || dx == xr + 1 || dz == -zr - 1 || dz == zr + 1) {
                    if (in_chunk(x + dx, z + dz) && pget(p, x + dx, y, z + dz) == A_ && pget(p, x + dx, y + 1, z + dz) == A_) nat_exits++;
                }
            }
        int nh = nat_exits < 1 ? 1 : 0;
        int nreq0, nout0, nring0;
        room_requirements(x, y, z, xr, zr, 0, 0, 0, 0, 0, req, &nreq0, outf, &nout0, ring, &nring0);
        int nsets = nh == 0 ? 1 : nring0;
        int why_next = 0;
        int found = 0;
        for (int sides = 0; sides < 16 && !found; sides++) {
            for (int hs = 0; hs < nsets && !found; hs++) {
                int hasHole = nh == 1, hx = hasHole ? ring[hs][0] : 0, hz = hasHole ? ring[hs][1] : 0;
                for (int f = 0; f <= nout0; f++) {
                    int nreq1, nout1, nring1;
                    room_requirements(x, y, z, xr, zr, f, hasHole, hx, hz, sides, req, &nreq1, outf, &nout1, ring, &nring1);
                    int bad = 0;
                    for (int q = 0; q < nreq1 && !bad; q++) {
                        int ci = req[q].idx;
                        if (p->touched[ci] || p->init[ci]) {
                            int have = p->over[ci] ? p->over[ci] : A_;
                            if (req[q].want == S_ ? !is_solid(have) : have != A_) bad = 1;
                        }
                    }
                    if (bad) continue;
                    int mark = p->nj;
                    p->logging = 1;
                    for (int q = 0; q < nreq1; q++) {
                        int ci = req[q].idx;
                        if (!p->touched[ci] && !p->init[ci]) {
                            if (p->nj < MAXJ) { p->jidx[p->nj] = ci; p->jold[p->nj] = p->over[ci]; p->nj++; }
                            p->over[ci] = req[q].want;
                        }
                    }
                    if (!valid_room(p, x, y, z, xr, zr)) { rollback(p, mark); p->logging = 0; continue; }
                    JRand r2 = r;
                    int mob = generate_room(p, &r2, x, y, z, xr, zr);
                    if (mob != want_type) { rollback(p, mark); p->logging = 0; continue; }
                    if (pi + 1 < n) {
                        JRand r3 = r2;
                        int nx, ny, nz, nxr, nzr;
                        attempt_draw(&r3, &nx, &ny, &nz, &nxr, &nzr);
                        if (!attempt_eq(nx, ny, nz, nxr, nzr, &plan[pi + 1])) { why_next++; rollback(p, mark); p->logging = 0; continue; }
                    }
                    p->logging = 0;
                    found = 1;
                    RoomRec* rr = &p->rooms[p->nrooms];
                    rr->nreq = nreq1;
                    memcpy(rr->req, req, sizeof(Req) * nreq1);
                    rr->x = x; rr->y = y; rr->z = z; rr->xr = xr; rr->zr = zr; rr->f = f;
                    rr->hasHole = hasHole; rr->hx = hx; rr->hz = hz;
                    for (int q = 0; q < nreq1; q++) {
                        int ci = req[q].idx;
                        if (!p->touched[ci] && !p->init[ci]) p->init[ci] = req[q].want;
                    }
                    p->epoch++;
                    for (int k = mark; k < p->nj; k++) {
                        int ci = p->jidx[k];
                        if (p->stamp[ci] == p->epoch) continue;
                        p->stamp[ci] = p->epoch;
                        if (p->over[ci] != p->jold[k]) p->touched[ci] = 1;
                    }
                    r = r2;
                    break;
                }
            }
        }
        if (!found) {
            info->failPi = pi;
            info->failNext = why_next > 0;
            return 0;
        }
        p->nrooms++;
        pi++;
    }
    info->ok = pi == n;
    return info->ok;
}

static int simulate(Planner* p, const unsigned char* cells, Room* out) {
    memcpy(p->over, cells, NCELL);
    p->logging = 0;
    JRand r;
    jr_chunk(&r, p->seed, p->cx, p->cz);
    int n = 0;
    for (int a = 0; a < 8; a++) {
        int x, y, z, xr, zr;
        attempt_draw(&r, &x, &y, &z, &xr, &zr);
        if (valid_room(p, x, y, z, xr, zr)) {
            int mob = generate_room(p, &r, x, y, z, xr, zr);
            out[n].x = x; out[n].y = y; out[n].z = z; out[n].xr = xr; out[n].zr = zr; out[n].type = mob; n++;
        }
    }
    return n;
}

static int rooms_equal(const Room* a, int na, const Room* b, int nb) {
    if (na != nb) return 0;
    for (int i = 0; i < na; i++)
        if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z || a[i].xr != b[i].xr || a[i].zr != b[i].zr || a[i].type != b[i].type) return 0;
    return 1;
}

int planner_verify(Planner* p, const Room* plan, int n) {
    unsigned char* cells = p->scratch;
    memcpy(cells, p->init, NCELL);
    Room out[8];
    int m = simulate(p, cells, out);
    return rooms_equal(out, m, plan, n);
}

typedef struct { int x0, x1, z0, z1; } Rect;

typedef struct { Box* v; int n, cap; } BoxList;

static void bl_add(BoxList* b, Box x) {
    if (b->n == b->cap) { b->cap = b->cap ? b->cap * 2 : 64; b->v = (Box*) realloc(b->v, sizeof(Box) * b->cap); }
    b->v[b->n++] = x;
}

#define GW 24
#define GLO (-4)

static inline int gok(int x, int z) { return x >= GLO && x < GLO + GW && z >= GLO && z < GLO + GW; }

typedef struct { unsigned char (*layer)[GW * GW]; } Grid;

static int rect_eq(Rect a, Rect b) { return a.x0 == b.x0 && a.x1 == b.x1 && a.z0 == b.z0 && a.z1 == b.z1; }

static int box_cmp(const void* a, const void* b) {
    const Box* x = (const Box*) a; const Box* y = (const Box*) b;
    if (x->y0 != y->y0) return x->y0 < y->y0 ? -1 : 1;
    if (x->z0 != y->z0) return x->z0 < y->z0 ? -1 : 1;
    if (x->x0 != y->x0) return x->x0 < y->x0 ? -1 : 1;
    return 0;
}

static void cover_boxes(unsigned char (*cells)[GW * GW], BoxList* out) {
    out->n = 0;
    typedef struct { Rect r; int ys, ye; } Open;
    Open open_[256]; int nopen = 0;
    for (int y = 0; y < 256; y++) {
        Rect cur[256]; int ncur = 0;
        int any = 0;
        for (int i = 0; i < GW * GW; i++) if (cells[y][i]) { any = 1; break; }
        if (any) {
            unsigned char remaining[GW * GW];
            memcpy(remaining, cells[y], sizeof remaining);
            for (;;) {
                int z0 = 0, x0 = 0, found = 0;
                for (int z = GLO; z < GLO + GW && !found; z++)
                    for (int x = GLO; x < GLO + GW; x++)
                        if (remaining[(x - GLO) + (z - GLO) * GW]) { z0 = z; x0 = x; found = 1; break; }
                if (!found) break;
                int x1 = x0;
                for (;;) {
                    int nx = x1 + 1;
                    if (gok(nx, z0) && (in_chunk(nx, z0) || cells[y][(nx - GLO) + (z0 - GLO) * GW])) x1 = nx; else break;
                }
                int z1 = z0;
                for (;;) {
                    int nz = z1 + 1, allok = 1;
                    for (int x = x0; x <= x1; x++) if (!(gok(x, nz) && (in_chunk(x, nz) || cells[y][(x - GLO) + (nz - GLO) * GW]))) { allok = 0; break; }
                    if (allok) z1 = nz; else break;
                }
                Rect rr = {x0, x1, z0, z1};
                if (ncur < 256) cur[ncur++] = rr;
                for (int x = x0; x <= x1; x++) for (int z = z0; z <= z1; z++) remaining[(x - GLO) + (z - GLO) * GW] = 0;
            }
        }
        Open nxt[256]; int nn = 0;
        for (int k = 0; k < nopen; k++) {
            int hit = -1;
            for (int c = 0; c < ncur; c++) if (rect_eq(cur[c], open_[k].r)) { hit = c; break; }
            Rect r = open_[k].r;
            long vol = (long) (r.x1 - r.x0 + 1) * (r.z1 - r.z0 + 1) * (open_[k].ye - open_[k].ys + 2);
            if (hit >= 0 && open_[k].ye == y - 1 && vol <= 32768) {
                nxt[nn].r = r; nxt[nn].ys = open_[k].ys; nxt[nn].ye = y; nn++;
                cur[hit] = cur[--ncur];
            } else {
                Box b = {r.x0, open_[k].ys, r.z0, r.x1, open_[k].ye, r.z1, 0};
                bl_add(out, b);
            }
        }
        for (int c = 0; c < ncur; c++) { nxt[nn].r = cur[c]; nxt[nn].ys = y; nxt[nn].ye = y; nn++; }
        memcpy(open_, nxt, sizeof(Open) * nn);
        nopen = nn;
    }
    for (int k = 0; k < nopen; k++) {
        Rect r = open_[k].r;
        Box b = {r.x0, open_[k].ys, r.z0, r.x1, open_[k].ye, r.z1, 0};
        bl_add(out, b);
    }
    qsort(out->v, out->n, sizeof(Box), box_cmp);
}

static unsigned char (*grid_alloc(void))[GW * GW] { return (unsigned char (*)[GW * GW]) calloc(256, GW * GW); }

static void apply_boxes(const Box* b, int n, unsigned char (*g)[GW * GW]) {
    memset(g, 0, 256 * GW * GW);
    for (int k = 0; k < n; k++)
        for (int x = b[k].x0; x <= b[k].x1; x++)
            for (int y = b[k].y0; y <= b[k].y1; y++)
                for (int z = b[k].z0; z <= b[k].z1; z++) {
                    if (in_chunk(x, z) || !gok(x, z) || y < 0 || y > 255) continue;
                    g[y][(x - GLO) + (z - GLO) * GW] = b[k].air ? 0 : 1;
                }
}

static int grid_eq(unsigned char (*a)[GW * GW], unsigned char (*b)[GW * GW]) { return memcmp(a, b, 256 * GW * GW) == 0; }

static void compose_grid(const RoomRec* rr, int nrooms, unsigned char (*g)[GW * GW]) {
    memset(g, 0, 256 * GW * GW);
    for (int k = 0; k < nrooms; k++)
        for (int q = 0; q < rr[k].nreq; q++) {
            if (rr[k].req[q].want != S_) continue;
            int i = rr[k].req[q].idx;
            int x = i % OWID + OLO, z = (i / OWID) % OWID + OLO, y = i / (OWID * OWID);
            if (gok(x, z)) g[y][(x - GLO) + (z - GLO) * GW] = 1;
        }
}

static int count_boxes(unsigned char (*g)[GW * GW]) {
    BoxList bl = {0};
    cover_boxes(g, &bl);
    int n = bl.n;
    free(bl.v);
    return n;
}

static void grid_to_cells(unsigned char (*g)[GW * GW], unsigned char* cells) {
    memset(cells, 0, NCELL);
    for (int y = 0; y < 256; y++)
        for (int z = GLO; z < GLO + GW; z++)
            for (int x = GLO; x < GLO + GW; x++)
                if (g[y][(x - GLO) + (z - GLO) * GW] && cell_ok(x, y, z)) cells[oidx(x, y, z)] = S_;
}

static void optimize_under(Planner* p, const Room* plan, int n, RoomRec* rr, unsigned char (*bestg)[GW * GW]) {
    unsigned char (*trial)[GW * GW] = grid_alloc();
    unsigned char* cells = p->scratch;
    compose_grid(rr, p->nrooms, bestg);
    int best_n = count_boxes(bestg);
    for (int k = 0; k < p->nrooms; k++) {
        int x = rr[k].x, y = rr[k].y, z = rr[k].z, xr = rr[k].xr, zr = rr[k].zr, f = rr[k].f;
        int outside[160][2], no = 0;
        for (int dx = -xr - 1; dx <= xr + 1; dx++)
            for (int dz = -zr - 1; dz <= zr + 1; dz++)
                if (!in_chunk(x + dx, z + dz)) { outside[no][0] = x + dx; outside[no][1] = z + dz; no++; }
        if (f == 0 || f >= no) continue;
        typedef struct { int area; int x0, x1, z0, z1; } Cand;
        Cand* cands = NULL; int nc = 0, cc = 0;
        for (int x0 = x - xr - 1; x0 <= x + xr + 1; x0++)
            for (int x1 = x0; x1 <= x + xr + 1; x1++)
                for (int z0 = z - zr - 1; z0 <= z + zr + 1; z0++)
                    for (int z1 = z0; z1 <= z + zr + 1; z1++) {
                        int cnt = 0;
                        for (int q = 0; q < no; q++) if (outside[q][0] >= x0 && outside[q][0] <= x1 && outside[q][1] >= z0 && outside[q][1] <= z1) cnt++;
                        if (cnt == f) {
                            if (nc == cc) { cc = cc ? cc * 2 : 64; cands = (Cand*) realloc(cands, sizeof(Cand) * cc); }
                            Cand c = {-(x1 - x0 + 1) * (z1 - z0 + 1), x0, x1, z0, z1};
                            cands[nc++] = c;
                        }
                    }
        for (int i = 1; i < nc; i++) { Cand v = cands[i]; int j = i - 1; while (j >= 0 && cands[j].area > v.area) { cands[j + 1] = cands[j]; j--; } cands[j + 1] = v; }
        int lim = nc < 60 ? nc : 60;
        for (int ci = 0; ci < lim; ci++) {
            Cand c = cands[ci];
            RoomRec* saved = (RoomRec*) malloc(sizeof(RoomRec));
            memcpy(saved, &rr[k], sizeof(RoomRec));
            int w = 0;
            for (int q = 0; q < rr[k].nreq; q++) {
                int i = rr[k].req[q].idx;
                if (i / (OWID * OWID) == y - 2) continue;
                rr[k].req[w++] = rr[k].req[q];
            }
            for (int q = 0; q < no; q++) {
                int inside_u = outside[q][0] >= c.x0 && outside[q][0] <= c.x1 && outside[q][1] >= c.z0 && outside[q][1] <= c.z1;
                if (cell_ok(outside[q][0], y - 2, outside[q][1])) { rr[k].req[w].idx = oidx(outside[q][0], y - 2, outside[q][1]); rr[k].req[w].want = inside_u ? S_ : A_; w++; }
            }
            rr[k].nreq = w;
            compose_grid(rr, p->nrooms, trial);
            int n2 = count_boxes(trial);
            int accept = 0;
            if (n2 < best_n) {
                grid_to_cells(trial, cells);
                Room out[8];
                int m = simulate(p, cells, out);
                if (rooms_equal(out, m, plan, n)) accept = 1;
            }
            if (accept) {
                memcpy(bestg, trial, 256 * GW * GW);
                best_n = n2;
                free(saved);
                break;
            }
            memcpy(&rr[k], saved, sizeof(RoomRec));
            free(saved);
        }
        free(cands);
    }
    free(trial);
}

int planner_commands(Planner* p, const Room* plan, int n, int margin, CmdSet* out) {
    unsigned char (*solid)[GW * GW] = grid_alloc();
    optimize_under(p, plan, n, p->rooms, solid);
    BoxList plain = {0};
    cover_boxes(solid, &plain);
    unsigned char (*ringg)[GW * GW] = grid_alloc();
    for (int k = 0; k < p->nrooms; k++) {
        RoomRec* r = &p->rooms[k];
        for (int dx = -r->xr - 1; dx <= r->xr + 1; dx++)
            for (int dz = -r->zr - 1; dz <= r->zr + 1; dz++)
                if ((dx == -r->xr - 1 || dx == r->xr + 1 || dz == -r->zr - 1 || dz == r->zr + 1) && gok(r->x + dx, r->z + dz) && r->y + 1 <= 255)
                    ringg[r->y + 1][(r->x + dx - GLO) + (r->z + dz - GLO) * GW] = 1;
    }
    unsigned char (*rest)[GW * GW] = grid_alloc();
    for (int y = 0; y < 256; y++) for (int i = 0; i < GW * GW; i++) rest[y][i] = solid[y][i] && !ringg[y][i];
    BoxList fancy = {0};
    cover_boxes(rest, &fancy);
    for (int k = 0; k < p->nrooms; k++) {
        RoomRec* r = &p->rooms[k];
        Box a = {r->x - r->xr - 1, r->y + 1, r->z - r->zr - 1, r->x + r->xr + 1, r->y + 1, r->z + r->zr + 1, 0};
        bl_add(&fancy, a);
        Box b = {r->x - r->xr, r->y + 1, r->z - r->zr, r->x + r->xr, r->y + 1, r->z + r->zr, 1};
        bl_add(&fancy, b);
        if (r->hasHole) {
            Box c = {r->hx, r->y + 1, r->hz, r->hx, r->y + 1, r->hz, 1};
            bl_add(&fancy, c);
        }
    }
    unsigned char (*chk)[GW * GW] = grid_alloc();
    BoxList* use = &plain;
    if (fancy.n < plain.n) {
        apply_boxes(fancy.v, fancy.n, chk);
        unsigned char (*sol2)[GW * GW] = grid_alloc();
        for (int y = 0; y < 256; y++) for (int i = 0; i < GW * GW; i++) {
            int x = i % GW + GLO, z = i / GW + GLO;
            sol2[y][i] = in_chunk(x, z) ? 0 : solid[y][i];
        }
        if (grid_eq(chk, sol2)) use = &fancy;
        free(sol2);
    }
    apply_boxes(use->v, use->n, chk);
    unsigned char (*sol3)[GW * GW] = grid_alloc();
    for (int y = 0; y < 256; y++) for (int i = 0; i < GW * GW; i++) {
        int x = i % GW + GLO, z = i / GW + GLO;
        sol3[y][i] = in_chunk(x, z) ? 0 : solid[y][i];
    }
    int good = grid_eq(chk, sol3);
    free(sol3);
    int ok = 0;
    if (good) {
        unsigned char* cells = p->scratch;
        grid_to_cells(chk, cells);
        Room res[8];
        int m = simulate(p, cells, res);
        ok = rooms_equal(res, m, plan, n);
    }
    if (ok) {
        BoxList clear = {0};
        int M = margin;
        int rects[4][4] = {{-M, 15 + M, -M, -1}, {-M, 15 + M, 16, 15 + M}, {-M, -1, 0, 15}, {16, 15 + M, 0, 15}};
        for (int q = 0; q < 4; q++) {
            int y = 0;
            while (y <= 255) {
                int area = (rects[q][1] - rects[q][0] + 1) * (rects[q][3] - rects[q][2] + 1);
                int per = 30000 / area;
                if (per < 1) per = 1;
                int h = y + per - 1;
                if (h > 255) h = 255;
                Box b = {rects[q][0], y, rects[q][2], rects[q][1], h, rects[q][3], 1};
                bl_add(&clear, b);
                y = h + 1;
            }
        }
        out->clear = clear.v; out->nclear = clear.n;
        out->build = (Box*) malloc(sizeof(Box) * (use->n ? use->n : 1));
        memcpy(out->build, use->v, sizeof(Box) * use->n);
        out->nbuild = use->n;
    }
    free(plain.v); free(fancy.v); free(solid); free(ringg); free(rest); free(chk);
    return ok;
}

void cmdset_free(CmdSet* c) { free(c->clear); free(c->build); c->clear = c->build = NULL; c->nclear = c->nbuild = 0; }

int cmd_format(const Box* b, int ox, int oz, char* out, int cap) {
    return snprintf(out, cap, "fill %d %d %d %d %d %d %s", ox + b->x0, b->y0, oz + b->z0, ox + b->x1, b->y1, oz + b->z1, b->air ? "air" : "stone");
}

int blobs_make(char** lines, int n, int limit, int drop, char*** blobs) {
    char head[512], tail[16] = "]}]}]}";
    int hl = snprintf(head, sizeof head, "summon falling_block ~ ~%d ~ {BlockState:{Name:\"minecraft:redstone_block\"},Time:1,Passengers:[{id:\"minecraft:armor_stand\",Health:0,Passengers:[{id:\"minecraft:falling_block\",BlockState:{Name:\"minecraft:activator_rail\"},Time:1,Passengers:[", drop);
    int tl = (int) strlen(tail);
    char** res = NULL; int nres = 0;
    char* cur = (char*) malloc(limit + 4096); int cl = 0, count = 0, size = hl + tl;
    memcpy(cur, head, hl); cl = hl;
    for (int i = 0; i < n; i++) {
        char piece[2048];
        int pl = snprintf(piece, sizeof piece, "{id:\"minecraft:command_block_minecart\",Command:\"%s\"}", lines[i]);
        if (size + pl + 1 > limit && count > 0) {
            memcpy(cur + cl, tail, tl); cl += tl; cur[cl] = 0;
            res = (char**) realloc(res, sizeof(char*) * (nres + 1)); res[nres++] = cur;
            cur = (char*) malloc(limit + 4096); memcpy(cur, head, hl); cl = hl; count = 0; size = hl + tl;
        }
        size += pl + 1;
        if (count > 0) cur[cl++] = ',';
        memcpy(cur + cl, piece, pl); cl += pl; count++;
    }
    if (count > 0) {
        memcpy(cur + cl, tail, tl); cl += tl; cur[cl] = 0;
        res = (char**) realloc(res, sizeof(char*) * (nres + 1)); res[nres++] = cur;
    } else free(cur);
    *blobs = res;
    return nres;
}

struct Memo { HSet any, nxt; };

static uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 31;
    return h;
}

static uint64_t room_key(const Room* r, int withType) {
    uint64_t v = (uint64_t) r->x | ((uint64_t) r->z << 8) | ((uint64_t) r->y << 16) | ((uint64_t) r->xr << 24) | ((uint64_t) r->zr << 28);
    if (withType) v |= (uint64_t) r->type << 32;
    return v;
}

uint64_t plan_hash(const Room* plan, int n) {
    uint64_t h = 1469598103934665603ULL ^ (uint64_t) n;
    for (int i = 0; i < n; i++) h = mix(h, room_key(&plan[i], 1));
    return h | 1;
}

Memo* memo_new(void) { return (Memo*) calloc(1, sizeof(Memo)); }
void memo_free(Memo* m) { if (m) { hs_free(&m->any); hs_free(&m->nxt); free(m); } }

int memo_blocked(const Memo* m, const Room* plan, int n) {
    uint64_t h = 1469598103934665603ULL;
    for (int j = 0; j < n; j++) {
        h = mix(h, room_key(&plan[j], 1));
        if (hs_has(&m->any, h)) return 1;
        if (hs_has(&m->nxt, mix(h, j + 1 < n ? room_key(&plan[j + 1], 0) + 1 : 0))) return 1;
    }
    return 0;
}

void memo_record(Memo* m, const Room* plan, int n, const DesignInfo* info) {
    if (info->failPi < 0) return;
    uint64_t h = 1469598103934665603ULL;
    for (int j = 0; j <= info->failPi; j++) h = mix(h, room_key(&plan[j], 1));
    if (info->failNext) hs_add(&m->nxt, mix(h, info->failPi + 1 < n ? room_key(&plan[info->failPi + 1], 0) + 1 : 0));
    else hs_add(&m->any, h);
}
