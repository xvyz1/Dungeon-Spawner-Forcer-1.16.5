#include "dungeon.h"
#include "jrand.h"
#include <stdlib.h>
#include <string.h>

#define BW 24
#define NBLK (BW * BW * 256)

struct Scan {
    unsigned char blocks[NBLK];
    int stoneTop;
    int pref, minTyped;
    JRand rnd;
    Room sp[8];
    LeafFn fn;
    void* user;
    int stop;
    int changedAll[8][1024], nAll[8];
    unsigned char excl[65536];
    int hasExcl;
    int changedChest[8][8], nChest[8];
};

static inline int bidx(int x, int y, int z) { return (x + 4) + ((z + 4) + y * BW) * BW; }

static inline int getb(Scan* c, int x, int y, int z) {
    if (x < -4 || x > 19 || z < -4 || z > 19) return 0;
    if (y < 0 || y > 255) return 2;
    return c->blocks[bidx(x, y, z)];
}

static inline int isSolid(Scan* c, int x, int y, int z) { return getb(c, x, y, z) & 1; }
static inline int isAir(Scan* c, int x, int y, int z) { return (getb(c, x, y, z) & 2) != 0; }
static inline int isChest(Scan* c, int x, int y, int z) { return (getb(c, x, y, z) & 4) != 0; }
static inline int isSpawner(Scan* c, int x, int y, int z) { return (getb(c, x, y, z) & 8) != 0; }
static inline int inside(int x, int y, int z) { return x >= 0 && x <= 15 && z >= 0 && z <= 15 && y >= 0 && y <= 255; }

static inline void setAndAdd(Scan* c, int* list, int* n, int x, int y, int z, int v) {
    if (x < -4 || x > 19 || z < -4 || z > 19 || y < 0 || y > 255) return;
    int i = bidx(x, y, z);
    int prev = c->blocks[i];
    c->blocks[i] = (unsigned char) v;
    if (prev != v) list[(*n)++] = (prev << 24) | i;
}

#define BLK(c, x, y, z) ((c)->blocks[bidx((x), (y), (z))])

static inline void setFast(Scan* c, int* list, int* n, int x, int y, int z, int v) {
    int i = bidx(x, y, z);
    int prev = c->blocks[i];
    c->blocks[i] = (unsigned char) v;
    if (prev != v) list[(*n)++] = (prev << 24) | i;
}

static inline void undo(Scan* c, int* list, int* n) {
    for (int k = *n - 1; k >= 0; k--) c->blocks[list[k] & 0xFFFFFF] = (unsigned char) (list[k] >> 24);
    *n = 0;
}

int room_dsq(const Room* a, int x, int y, int z) {
    int dx = a->x - x, dy = a->y - y, dz = a->z - z;
    return dx * dx + dy * dy + dz * dz;
}

int stand_best(const Room* r, int count, int pref, int need, int* px, int* py, int* pz, int* typedInRange) {
    int typed[8], nt = 0;
    for (int i = 0; i < count; i++) if (pref < 0 || r[i].type == pref) typed[nt++] = i;
    if (nt < need || !nt) return 0;
    if (need > 1) {
        int ok = 0;
        for (int i = 0; i < nt && !ok; i++) {
            int near = 0;
            for (int j = 0; j < nt; j++) {
                int dx = r[typed[i]].x - r[typed[j]].x, dy = r[typed[i]].y - r[typed[j]].y, dz = r[typed[i]].z - r[typed[j]].z;
                if (dx * dx + dy * dy + dz * dz < 1024) near++;
            }
            if (near >= need) ok = 1;
        }
        if (!ok) return 0;
    }
    int x0 = 99999, x1 = -99999, y0 = 99999, y1 = -99999, z0 = 99999, z1 = -99999;
    for (int j = 0; j < nt; j++) {
        const Room* s = &r[typed[j]];
        if (s->x < x0) x0 = s->x;
        if (s->x > x1) x1 = s->x;
        if (s->y < y0) y0 = s->y;
        if (s->y > y1) y1 = s->y;
        if (s->z < z0) z0 = s->z;
        if (s->z > z1) z1 = s->z;
    }
    int bestN = -1, bestMax = 1 << 30, bx = 0, by = 0, bz = 0;
    for (int x = x0 - 1; x <= x1 + 1; x++)
        for (int y = y0 - 1; y <= y1 + 1; y++)
            for (int z = z0 - 1; z <= z1 + 1; z++) {
                int cnt = 0, mx = 0;
                for (int j = 0; j < nt; j++) {
                    int d = room_dsq(&r[typed[j]], x, y, z);
                    if (d < 256) { cnt++; if (d > mx) mx = d; }
                }
                if (cnt > bestN || (cnt == bestN && mx < bestMax)) { bestN = cnt; bestMax = mx; bx = x; by = y; bz = z; }
            }
    *px = bx; *py = by; *pz = bz; *typedInRange = bestN;
    return 1;
}

static void rec(Scan* c, int i, int count, int prefCount) {
    if (c->stop) return;
    if (i == 8) {
        if (c->fn(c->user, c->sp, count)) c->stop = 1;
        return;
    }
    if (c->minTyped > 0 && prefCount + (8 - i) < c->minTyped) return;
    uint64_t saved = c->rnd.s;
    JRand* r = &c->rnd;
    int x = jr_int(r, 16), z = jr_int(r, 16), y = jr_int(r, 256);
    int xr = jr_int(r, 2) + 2, zr = jr_int(r, 2) + 2;
    if (y < 1 || y > 251) {
        rec(c, i + 1, count, prefCount);
        r->s = saved;
        return;
    }
    int v10 = xr + 1, v11 = zr + 1;
    int x0 = x - v10 > 0 ? x - v10 : 0, x1 = x + v10 < 15 ? x + v10 : 15;
    int z0 = z - v11 > 0 ? z - v11 : 0, z1 = z + v11 < 15 ? z + v11 : 15;
    if (c->hasExcl) {
        for (int px = x0; px <= x1; px++)
            for (int pz = z0; pz <= z1; pz++)
                for (int yy = y - 2; yy <= y + 4; yy++)
                    if (c->excl[px + pz * 16 + yy * 256]) {
                        r->s = saved;
                        return;
                    }
    }
    int ok = 1;
    for (int px = x0; ok && px <= x1; px++)
        for (int pz = z0; ok && pz <= z1; pz++)
            if (!(BLK(c, px, y - 1, pz) & 1) || !(BLK(c, px, y + 4, pz) & 1)) ok = 0;
    int exits = 0;
    if (ok) {
        for (int dx = -v10; dx <= v10 && exits <= 5; dx++) {
            int px = x + dx;
            if (px < 0 || px > 15) continue;
            for (int dz = -v11; dz <= v11; dz++) {
                if (dx != v10 && dx != -v10 && dz != v11 && dz != -v11) continue;
                int pz = z + dz;
                if (pz < 0 || pz > 15) continue;
                if ((BLK(c, px, y, pz) & 2) && (BLK(c, px, y + 1, pz) & 2)) {
                    exits++;
                    if (exits > 5) break;
                }
            }
        }
    }
    int v18 = (2 * xr + 3) * (2 * zr + 3) - (x1 - x0 + 1) * (z1 - z0 + 1);
    int valid = ok && exits <= 5 && (exits >= 1 || v18 != 0);
    if (!valid) {
        rec(c, i + 1, count, prefCount);
        r->s = saved;
        return;
    }
    if (y == 1) v18 = 0;
    int* la = c->changedAll[i];
    int* na = &c->nAll[i];
    for (int dx = -v10; dx <= v10; dx++) {
        int px = x + dx;
        if (px < 0 || px > 15) continue;
        for (int dz = -v11; dz <= v11; dz++) {
            int pz = z + dz;
            if (pz < 0 || pz > 15) continue;
            for (int dy = 3; dy >= -1; dy--) {
                int yy = y + dy;
                int cur = BLK(c, px, yy, pz);
                if (dx != -v10 && dx != v10 && dz != -v11 && dz != v11 && dy != -1) {
                    if (!(cur & 12)) setFast(c, la, na, px, yy, pz, 2);
                } else if (yy < 1 || !(BLK(c, px, yy - 1, pz) & 1)) {
                    setFast(c, la, na, px, yy, pz, 2);
                } else if (dy == -1 && !(cur & 4)) {
                    setFast(c, la, na, px, yy, pz, 1);
                    jr_int(r, 4);
                }
            }
        }
    }
    Room* sp = &c->sp[count];
    uint64_t base = r->s;
    int seenT = 0;
    for (int i24 = 0; i24 <= v18; i24++) {
        int* lc = c->changedChest[i];
        int* nc = &c->nChest[i];
        for (int l = 0; l < 2; l++) {
            for (int j = 0; j < 3; j++) {
                int wx = x + jr_int(r, 2 * xr + 1) - xr;
                int wz = z + jr_int(r, 2 * zr + 1) - zr;
                int in = wx >= 0 && wx <= 15 && wz >= 0 && wz <= 15;
                if (!in || (BLK(c, wx, y, wz) & 2)) {
                    int n = (BLK(c, wx + 1, y, wz) & 1) + (BLK(c, wx - 1, y, wz) & 1) + (BLK(c, wx, y, wz + 1) & 1) + (BLK(c, wx, y, wz - 1) & 1);
                    if (n == 1) {
                        if (in) {
                            setFast(c, lc, nc, wx, y, wz, 5);
                            jr_long(r);
                        }
                        break;
                    }
                }
            }
        }
        sp->x = x; sp->y = y; sp->z = z; sp->xr = xr; sp->zr = zr;
        sp->type = jr_mob(r);
        setFast(c, lc, nc, x, y, z, 9);
        int doRec = 1;
        if (i == 7) {
            if (seenT & (1 << sp->type)) doRec = 0;
            else seenT |= 1 << sp->type;
        }
        if (doRec) rec(c, i + 1, count + 1, prefCount + (sp->type == c->pref));
        undo(c, lc, nc);
        r->s = base;
        jr_int(r, 4);
        base = r->s;
        if (i == 7 && seenT == 7) break;
        if (c->stop) break;
    }
    undo(c, la, na);
    r->s = saved;
}

static void init_blocks(Scan* c, const unsigned char* mask, int stoneTop) {
    memset(c->blocks, 0, NBLK);
    for (int y = 0; y < 256; y++)
        for (int z = 0; z < 16; z++)
            for (int x = 0; x < 16; x++)
                c->blocks[bidx(x, y, z)] = mask ? mask[x + z * 16 + y * 256] : (y <= stoneTop ? 1 : 2);
}

Scan* scan_new(void) {
    Scan* s = (Scan*) calloc(1, sizeof(Scan));
    s->stoneTop = -2;
    return s;
}

void scan_free(Scan* s) { free(s); }

void scan_stone(Scan* s, int top) {
    s->hasExcl = 0;
    if (s->stoneTop == top) return;
    init_blocks(s, NULL, top);
    s->stoneTop = top;
}

void scan_mask(Scan* s, const unsigned char* mask) {
    s->hasExcl = 0;
    init_blocks(s, mask, 0);
    s->stoneTop = -2;
}

void scan_chunk(Scan* s, uint64_t seed, int cx, int cz, int pref, int minTyped, LeafFn fn, void* user) {
    s->pref = pref; s->minTyped = minTyped;
    s->fn = fn; s->user = user; s->stop = 0;
    jr_chunk(&s->rnd, seed, cx, cz);
    memset(s->nAll, 0, sizeof s->nAll);
    memset(s->nChest, 0, sizeof s->nChest);
    rec(s, 0, 0, 0);
}

void scan_exclude(Scan* s, const unsigned char* excl) {
    if (!excl) { s->hasExcl = 0; return; }
    memcpy(s->excl, excl, 65536);
    s->hasExcl = 1;
}
