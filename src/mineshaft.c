#include "mineshaft.h"
#include "jrand.h"

#include <stdlib.h>
#include <string.h>

enum { T_COR, T_CROSS, T_ROOM, T_STAIRS };
enum { D_N, D_S, D_W, D_E };
enum { X0, Y0, Z0, X1, Y1, Z1 };

#define MAXP 1024

typedef struct { int type, dir, depth, two; int bb[6]; } MsPiece;

typedef struct {
    MsPiece p[MAXP];
    int n;
    JRand r;
} Ctx;

static int rnd_int(Ctx* c, int b) { return jr_int(&c->r, b); }

static int hit(const Ctx* c, const int* b) {
    for (int i = 0; i < c->n; i++) {
        const int* q = c->p[i].bb;
        if (b[X1] >= q[X0] && b[X0] <= q[X1] && b[Z1] >= q[Z0] && b[Z0] <= q[Z1] && b[Y1] >= q[Y0] && b[Y0] <= q[Y1]) return 1;
    }
    return 0;
}

static int child(Ctx* c, int start, int x, int y, int z, int dir, int depth);

static int corridor_box(Ctx* c, int x, int y, int z, int dir, int* bb) {
    bb[X0] = x; bb[Y0] = y; bb[Z0] = z; bb[X1] = x; bb[Y1] = y + 2; bb[Z1] = z;
    int k;
    for (k = rnd_int(c, 3) + 2; k > 0; k--) {
        int len = k * 5;
        if (dir == D_N) { bb[X1] = x + 2; bb[Z0] = z - (len - 1); }
        else if (dir == D_S) { bb[X1] = x + 2; bb[Z1] = z + len - 1; }
        else if (dir == D_W) { bb[X0] = x - (len - 1); bb[Z1] = z + 2; }
        else { bb[X1] = x + len - 1; bb[Z1] = z + 2; }
        if (!hit(c, bb)) break;
    }
    return k > 0;
}

static int crossing_box(Ctx* c, int x, int y, int z, int dir, int* bb) {
    bb[X0] = x; bb[Y0] = y; bb[Z0] = z; bb[X1] = x; bb[Y1] = y + 2; bb[Z1] = z;
    if (rnd_int(c, 4) == 0) bb[Y1] += 4;
    if (dir == D_N) { bb[X0] = x - 1; bb[X1] = x + 3; bb[Z0] = z - 4; }
    else if (dir == D_S) { bb[X0] = x - 1; bb[X1] = x + 3; bb[Z1] = z + 4; }
    else if (dir == D_W) { bb[X0] = x - 4; bb[Z0] = z - 1; bb[Z1] = z + 3; }
    else { bb[X1] = x + 4; bb[Z0] = z - 1; bb[Z1] = z + 3; }
    return !hit(c, bb);
}

static int stairs_box(Ctx* c, int x, int y, int z, int dir, int* bb) {
    bb[X0] = x; bb[Y0] = y - 5; bb[Z0] = z; bb[X1] = x; bb[Y1] = y + 2; bb[Z1] = z;
    if (dir == D_N) { bb[X1] = x + 2; bb[Z0] = z - 8; }
    else if (dir == D_S) { bb[X1] = x + 2; bb[Z1] = z + 8; }
    else if (dir == D_W) { bb[X0] = x - 8; bb[Z1] = z + 2; }
    else { bb[X1] = x + 8; bb[Z1] = z + 2; }
    return !hit(c, bb);
}

static int make_piece(Ctx* c, int x, int y, int z, int dir, int depth) {
    int roll = rnd_int(c, 100);
    MsPiece pc;
    memset(&pc, 0, sizeof pc);
    pc.dir = dir;
    pc.depth = depth;
    if (roll >= 80) {
        if (!crossing_box(c, x, y, z, dir, pc.bb)) return -1;
        pc.type = T_CROSS;
        pc.two = pc.bb[Y1] - pc.bb[Y0] + 1 > 3;
    } else if (roll >= 70) {
        if (!stairs_box(c, x, y, z, dir, pc.bb)) return -1;
        pc.type = T_STAIRS;
    } else {
        if (!corridor_box(c, x, y, z, dir, pc.bb)) return -1;
        pc.type = T_COR;
        int rails = rnd_int(c, 3) == 0;
        if (!rails) rnd_int(c, 23);
    }
    if (c->n >= MAXP) return -1;
    c->p[c->n] = pc;
    return c->n++;
}

static void add_children(Ctx* c, int start, int idx);

static int child(Ctx* c, int start, int x, int y, int z, int dir, int depth) {
    if (depth > 8) return -1;
    if (abs(x - c->p[start].bb[X0]) <= 80 && abs(z - c->p[start].bb[Z0]) <= 80) {
        int k = make_piece(c, x, y, z, dir, depth + 1);
        if (k >= 0) add_children(c, start, k);
        return k;
    }
    return -1;
}

static void corridor_children(Ctx* c, int start, int idx) {
    MsPiece p = c->p[idx];
    const int* b = p.bb;
    int depth = p.depth;
    int r = rnd_int(c, 4);
    int d = p.dir;
    if (d == D_N) {
        if (r <= 1) child(c, start, b[X0], b[Y0] - 1 + rnd_int(c, 3), b[Z0] - 1, d, depth);
        else if (r == 2) child(c, start, b[X0] - 1, b[Y0] - 1 + rnd_int(c, 3), b[Z0], D_W, depth);
        else child(c, start, b[X1] + 1, b[Y0] - 1 + rnd_int(c, 3), b[Z0], D_E, depth);
    } else if (d == D_S) {
        if (r <= 1) child(c, start, b[X0], b[Y0] - 1 + rnd_int(c, 3), b[Z1] + 1, d, depth);
        else if (r == 2) child(c, start, b[X0] - 1, b[Y0] - 1 + rnd_int(c, 3), b[Z1] - 3, D_W, depth);
        else child(c, start, b[X1] + 1, b[Y0] - 1 + rnd_int(c, 3), b[Z1] - 3, D_E, depth);
    } else if (d == D_W) {
        if (r <= 1) child(c, start, b[X0] - 1, b[Y0] - 1 + rnd_int(c, 3), b[Z0], d, depth);
        else if (r == 2) child(c, start, b[X0], b[Y0] - 1 + rnd_int(c, 3), b[Z0] - 1, D_N, depth);
        else child(c, start, b[X0], b[Y0] - 1 + rnd_int(c, 3), b[Z1] + 1, D_S, depth);
    } else {
        if (r <= 1) child(c, start, b[X1] + 1, b[Y0] - 1 + rnd_int(c, 3), b[Z0], d, depth);
        else if (r == 2) child(c, start, b[X1] - 3, b[Y0] - 1 + rnd_int(c, 3), b[Z0] - 1, D_N, depth);
        else child(c, start, b[X1] - 3, b[Y0] - 1 + rnd_int(c, 3), b[Z1] + 1, D_S, depth);
    }
    if (depth < 8) {
        if (d != D_N && d != D_S) {
            for (int x = b[X0] + 3; x + 3 <= b[X1]; x += 5) {
                int q = rnd_int(c, 5);
                if (q == 0) child(c, start, x, b[Y0], b[Z0] - 1, D_N, depth + 1);
                else if (q == 1) child(c, start, x, b[Y0], b[Z1] + 1, D_S, depth + 1);
            }
        } else {
            for (int z = b[Z0] + 3; z + 3 <= b[Z1]; z += 5) {
                int q = rnd_int(c, 5);
                if (q == 0) child(c, start, b[X0] - 1, b[Y0], z, D_W, depth + 1);
                else if (q == 1) child(c, start, b[X1] + 1, b[Y0], z, D_E, depth + 1);
            }
        }
    }
}

static void crossing_children(Ctx* c, int start, int idx) {
    MsPiece p = c->p[idx];
    const int* b = p.bb;
    int depth = p.depth;
    int d = p.dir;
    if (d == D_N) {
        child(c, start, b[X0] + 1, b[Y0], b[Z0] - 1, D_N, depth);
        child(c, start, b[X0] - 1, b[Y0], b[Z0] + 1, D_W, depth);
        child(c, start, b[X1] + 1, b[Y0], b[Z0] + 1, D_E, depth);
    } else if (d == D_S) {
        child(c, start, b[X0] + 1, b[Y0], b[Z1] + 1, D_S, depth);
        child(c, start, b[X0] - 1, b[Y0], b[Z0] + 1, D_W, depth);
        child(c, start, b[X1] + 1, b[Y0], b[Z0] + 1, D_E, depth);
    } else if (d == D_W) {
        child(c, start, b[X0] + 1, b[Y0], b[Z0] - 1, D_N, depth);
        child(c, start, b[X0] + 1, b[Y0], b[Z1] + 1, D_S, depth);
        child(c, start, b[X0] - 1, b[Y0], b[Z0] + 1, D_W, depth);
    } else {
        child(c, start, b[X0] + 1, b[Y0], b[Z0] - 1, D_N, depth);
        child(c, start, b[X0] + 1, b[Y0], b[Z1] + 1, D_S, depth);
        child(c, start, b[X1] + 1, b[Y0], b[Z0] + 1, D_E, depth);
    }
    if (p.two) {
        if (jr_next(&c->r, 1) != 0) child(c, start, b[X0] + 1, b[Y0] + 4, b[Z0] - 1, D_N, depth);
        if (jr_next(&c->r, 1) != 0) child(c, start, b[X0] - 1, b[Y0] + 4, b[Z0] + 1, D_W, depth);
        if (jr_next(&c->r, 1) != 0) child(c, start, b[X1] + 1, b[Y0] + 4, b[Z0] + 1, D_E, depth);
        if (jr_next(&c->r, 1) != 0) child(c, start, b[X0] + 1, b[Y0] + 4, b[Z1] + 1, D_S, depth);
    }
}

static void room_children(Ctx* c, int start, int idx) {
    MsPiece p = c->p[idx];
    const int* b = p.bb;
    int depth = p.depth;
    int ys = b[Y1] - b[Y0] + 1, xs = b[X1] - b[X0] + 1, zs = b[Z1] - b[Z0] + 1;
    int v = ys - 3 - 1;
    if (v <= 0) v = 1;
    int pos = 0;
    while (pos < xs) {
        pos += rnd_int(c, xs);
        if (pos + 3 > xs) break;
        child(c, start, b[X0] + pos, b[Y0] + rnd_int(c, v) + 1, b[Z0] - 1, D_N, depth);
        pos += 4;
    }
    pos = 0;
    while (pos < xs) {
        pos += rnd_int(c, xs);
        if (pos + 3 > xs) break;
        child(c, start, b[X0] + pos, b[Y0] + rnd_int(c, v) + 1, b[Z1] + 1, D_S, depth);
        pos += 4;
    }
    pos = 0;
    while (pos < zs) {
        pos += rnd_int(c, zs);
        if (pos + 3 > zs) break;
        child(c, start, b[X0] - 1, b[Y0] + rnd_int(c, v) + 1, b[Z0] + pos, D_W, depth);
        pos += 4;
    }
    pos = 0;
    while (pos < zs) {
        pos += rnd_int(c, zs);
        if (pos + 3 > zs) break;
        child(c, start, b[X1] + 1, b[Y0] + rnd_int(c, v) + 1, b[Z0] + pos, D_E, depth);
        pos += 4;
    }
}

static void stairs_children(Ctx* c, int start, int idx) {
    MsPiece p = c->p[idx];
    const int* b = p.bb;
    int depth = p.depth;
    if (p.dir == D_N) child(c, start, b[X0], b[Y0], b[Z0] - 1, D_N, depth);
    else if (p.dir == D_S) child(c, start, b[X0], b[Y0], b[Z1] + 1, D_S, depth);
    else if (p.dir == D_W) child(c, start, b[X0] - 1, b[Y0], b[Z0], D_W, depth);
    else child(c, start, b[X1] + 1, b[Y0], b[Z0], D_E, depth);
}

static void add_children(Ctx* c, int start, int idx) {
    int t = c->p[idx].type;
    if (t == T_COR) corridor_children(c, start, idx);
    else if (t == T_CROSS) crossing_children(c, start, idx);
    else if (t == T_ROOM) room_children(c, start, idx);
    else stairs_children(c, start, idx);
}

static int is_mesa(int b) { return b == 37 || b == 38 || b == 39 || b == 165 || b == 166 || b == 167; }

static void start_seed(JRand* r, uint64_t seed, int cx, int cz) {
    jr_set(r, seed);
    int64_t a = jr_long(r);
    int64_t b = jr_long(r);
    jr_set(r, (uint64_t) (((int64_t) cx * a) ^ ((int64_t) cz * b) ^ (int64_t) seed));
}

static void mark(unsigned char* excl, int cx, int cz, const int* bb, int pad) {
    int x0 = bb[X0] - pad - cx * 16, x1 = bb[X1] + pad - cx * 16;
    int z0 = bb[Z0] - pad - cz * 16, z1 = bb[Z1] + pad - cz * 16;
    int y0 = bb[Y0] - pad, y1 = bb[Y1] + pad;
    if (x1 < 0 || x0 > 15 || z1 < 0 || z0 > 15) return;
    if (x0 < 0) x0 = 0;
    if (x1 > 15) x1 = 15;
    if (z0 < 0) z0 = 0;
    if (z1 > 15) z1 = 15;
    if (y0 < 0) y0 = 0;
    if (y1 > 255) y1 = 255;
    for (int y = y0; y <= y1; y++)
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++) excl[x + z * 16 + y * 256] = 1;
}

static void one_start(uint64_t seed, int sx, int sz, int mesa, int cx, int cz, unsigned char* excl) {
    static Ctx c;
    c.n = 0;
    start_seed(&c.r, seed, sx, sz);
    MsPiece room;
    memset(&room, 0, sizeof room);
    room.type = T_ROOM;
    room.dir = -1;
    int rx = sx * 16 + 2, rz = sz * 16 + 2;
    int ex = rnd_int(&c, 6), ey = rnd_int(&c, 6), ez = rnd_int(&c, 6);
    room.bb[X0] = rx; room.bb[Y0] = 50; room.bb[Z0] = rz;
    room.bb[X1] = rx + 7 + ex; room.bb[Y1] = 54 + ey; room.bb[Z1] = rz + 7 + ez;
    c.p[0] = room;
    c.n = 1;
    add_children(&c, 0, 0);
    int mn[3] = {1 << 30, 1 << 30, 1 << 30}, mx[3] = {-(1 << 30), -(1 << 30), -(1 << 30)};
    for (int i = 0; i < c.n; i++)
        for (int k = 0; k < 3; k++) {
            if (c.p[i].bb[k] < mn[k]) mn[k] = c.p[i].bb[k];
            if (c.p[i].bb[k + 3] > mx[k]) mx[k] = c.p[i].bb[k + 3];
        }
    int ysize = mx[1] - mn[1] + 1;
    int shift;
    if (mesa) shift = 63 - mx[1] + ysize / 2 + 5;
    else {
        int lim = 63 - 10;
        int top = ysize + 1;
        if (top < lim) top += rnd_int(&c, lim - top);
        shift = top - mx[1];
    }
    for (int i = 0; i < c.n; i++) {
        c.p[i].bb[Y0] += shift;
        c.p[i].bb[Y1] += shift;
        mark(excl, cx, cz, c.p[i].bb, 1);
    }
}

int mineshaft_mark(uint64_t seed, int cx, int cz, int (*biome_at)(void* user, int qx, int qz), void* user, unsigned char* excl) {
    int n = 0;
    JRand r;
    for (int sx = cx - 9; sx <= cx + 9; sx++)
        for (int sz = cz - 9; sz <= cz + 9; sz++) {
            start_seed(&r, seed, sx, sz);
            if (!(jr_double(&r) < 0.004)) continue;
            int mesa = is_mesa(biome_at(user, sx * 4 + 2, sz * 4 + 2));
            one_start(seed, sx, sz, mesa, cx, cz, excl);
            n++;
        }
    return n;
}
