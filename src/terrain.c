#include "terrain.h"
#include "jrand.h"
#include "mineshaft.h"
#include "cubiomes/generator.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SEA 63
#define CELLS_Y 32
#define OCT_MAX 16

static float SINT[65536];
static int sin_ready = 0;

static void sin_init(void) {
    if (sin_ready) return;
    for (int i = 0; i < 65536; i++) SINT[i] = (float) sin(i * 3.141592653589793 * 2.0 / 65536.0);
    sin_ready = 1;
}

static float mc_sin(float v) { return SINT[(int) (v * 10430.378f) & 65535]; }
static float mc_cos(float v) { return SINT[(int) (v * 10430.378f + 16384.0f) & 65535]; }

static float jr_float(JRand* r) { return jr_next(r, 24) / 16777216.0f; }

static void jr_skip(JRand* r, int n) { for (int i = 0; i < n; i++) jr_next(r, 1); }

static const int GRAD[16][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}, {1, 1, 0}, {0, -1, 1}, {-1, 1, 0}, {0, -1, -1}};

typedef struct { double ox, oy, oz; unsigned char p[256]; } Perlin;

static void perlin_init(Perlin* n, JRand* r) {
    n->ox = jr_double(r) * 256.0;
    n->oy = jr_double(r) * 256.0;
    n->oz = jr_double(r) * 256.0;
    for (int i = 0; i < 256; i++) n->p[i] = (unsigned char) i;
    for (int i = 0; i < 256; i++) {
        int j = jr_int(r, 256 - i);
        unsigned char t = n->p[i];
        n->p[i] = n->p[i + j];
        n->p[i + j] = t;
    }
}

static void simplex_consume(JRand* r) {
    jr_double(r); jr_double(r); jr_double(r);
    for (int i = 0; i < 256; i++) jr_int(r, 256 - i);
}

static int ffloor(double v) {
    int i = (int) v;
    return v < i ? i - 1 : i;
}

static double mlerp(double t, double a, double b) { return a + t * (b - a); }

static double mlerp2(double a, double b, double x00, double x10, double x01, double x11) { return mlerp(b, mlerp(a, x00, x10), mlerp(a, x01, x11)); }

static double mlerp3(double a, double b, double c, double v000, double v100, double v010, double v110, double v001, double v101, double v011, double v111) {
    return mlerp(c, mlerp2(a, b, v000, v100, v010, v110), mlerp2(a, b, v001, v101, v011, v111));
}

static double smooth(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

static double grad(int h, double x, double y, double z) {
    const int* g = GRAD[h & 15];
    return g[0] * x + g[1] * y + g[2] * z;
}

static double perlin_sample(const Perlin* n, double x, double y, double z, double yScale, double yMax) {
    double x1 = x + n->ox, y1 = y + n->oy, z1 = z + n->oz;
    int xi = ffloor(x1), yi = ffloor(y1), zi = ffloor(z1);
    double xf = x1 - xi, yf = y1 - yi, zf = z1 - zi;
    double sx = smooth(xf), sy = smooth(yf), sz = smooth(zf);
    double yo = 0.0;
    if (yScale != 0.0) {
        double t = yMax < yf ? yMax : yf;
        yo = ffloor(t / yScale) * yScale;
    }
    double yfa = yf - yo;
    const unsigned char* p = n->p;
    int a = p[xi & 255] + yi;
    int a2 = p[a & 255] + zi;
    int a3 = p[(a + 1) & 255] + zi;
    int b = p[(xi + 1) & 255] + yi;
    int b2 = p[b & 255] + zi;
    int b3 = p[(b + 1) & 255] + zi;
    double d1 = grad(p[a2 & 255], xf, yfa, zf);
    double d2 = grad(p[b2 & 255], xf - 1.0, yfa, zf);
    double d3 = grad(p[a3 & 255], xf, yfa - 1.0, zf);
    double d4 = grad(p[b3 & 255], xf - 1.0, yfa - 1.0, zf);
    double d5 = grad(p[(a2 + 1) & 255], xf, yfa, zf - 1.0);
    double d6 = grad(p[(b2 + 1) & 255], xf - 1.0, yfa, zf - 1.0);
    double d7 = grad(p[(a3 + 1) & 255], xf, yfa - 1.0, zf - 1.0);
    double d8 = grad(p[(b3 + 1) & 255], xf - 1.0, yfa - 1.0, zf - 1.0);
    return mlerp3(sx, sy, sz, d1, d2, d3, d4, d5, d6, d7, d8);
}

typedef struct { Perlin p[OCT_MAX]; int n; double lac, pers; } Octave;

static void octave_init(Octave* o, JRand* r, int n) {
    o->n = n;
    perlin_init(&o->p[n - 1], r);
    for (int i = n - 2; i >= 0; i--) perlin_init(&o->p[i], r);
    o->lac = pow(2.0, -(n - 1));
    o->pers = pow(2.0, n - 1) / (pow(2.0, n) - 1.0);
}

static double wrap(double v) {
    double q = v / 33554432.0 + 0.5;
    long long f = (long long) q;
    if (q < (double) f) f--;
    return v - (double) f * 33554432.0;
}

static double octave_sample(const Octave* o, double x, double y, double z, double yS, double yM, int fixY) {
    double sum = 0.0, d = o->lac, w = o->pers;
    for (int i = 0; i < o->n; i++) {
        const Perlin* p = &o->p[i];
        sum += perlin_sample(p, wrap(x * d), fixY ? -p->oy : wrap(y * d), wrap(z * d), yS * d, yM * d) * w;
        d *= 2.0;
        w /= 2.0;
    }
    return sum;
}

struct Terrain {
    uint64_t seed;
    Octave lower, upper, interp, depth;
    Generator g;
    int* cache;
    float W[25];
};

static float BDEPTH[256], BSCALE[256];
static unsigned char BKNOWN[256];

static void bset(int id, float d, float s) { BDEPTH[id] = d; BSCALE[id] = s; BKNOWN[id] = 1; }

static void biome_table(void) {
    static int done = 0;
    if (done) return;
    done = 1;
    bset(0, -1.0f, 0.1f); bset(1, 0.125f, 0.05f); bset(2, 0.125f, 0.05f); bset(3, 1.0f, 0.5f); bset(4, 0.1f, 0.2f);
    bset(5, 0.2f, 0.2f); bset(6, -0.2f, 0.1f); bset(7, -0.5f, 0.0f); bset(10, -1.0f, 0.1f); bset(11, -0.5f, 0.0f);
    bset(12, 0.125f, 0.05f); bset(13, 0.45f, 0.3f); bset(14, 0.2f, 0.3f); bset(15, 0.0f, 0.025f); bset(16, 0.0f, 0.025f);
    bset(17, 0.45f, 0.3f); bset(18, 0.45f, 0.3f); bset(19, 0.45f, 0.3f); bset(20, 0.8f, 0.3f); bset(21, 0.1f, 0.2f);
    bset(22, 0.45f, 0.3f); bset(23, 0.1f, 0.2f); bset(24, -1.8f, 0.1f); bset(25, 0.1f, 0.8f); bset(26, 0.0f, 0.025f);
    bset(27, 0.1f, 0.2f); bset(28, 0.45f, 0.3f); bset(29, 0.1f, 0.2f); bset(30, 0.2f, 0.2f); bset(31, 0.45f, 0.3f);
    bset(32, 0.2f, 0.2f); bset(33, 0.45f, 0.3f); bset(34, 1.0f, 0.5f); bset(35, 0.125f, 0.05f); bset(36, 1.5f, 0.025f);
    bset(37, 0.1f, 0.2f); bset(38, 1.5f, 0.025f); bset(39, 1.5f, 0.025f);
    bset(44, -1.0f, 0.1f); bset(45, -1.0f, 0.1f); bset(46, -1.0f, 0.1f); bset(47, -1.8f, 0.1f); bset(48, -1.8f, 0.1f);
    bset(49, -1.8f, 0.1f); bset(50, -1.8f, 0.1f);
    bset(129, 0.125f, 0.05f); bset(130, 0.225f, 0.25f); bset(131, 1.0f, 0.5f); bset(132, 0.1f, 0.4f); bset(133, 0.3f, 0.4f);
    bset(134, -0.1f, 0.3f); bset(140, 0.425f, 0.45000002f); bset(149, 0.2f, 0.4f); bset(151, 0.2f, 0.4f);
    bset(155, 0.2f, 0.4f); bset(156, 0.55f, 0.5f); bset(157, 0.2f, 0.4f); bset(158, 0.3f, 0.4f); bset(160, 0.2f, 0.2f);
    bset(161, 0.2f, 0.2f); bset(162, 1.0f, 0.5f); bset(163, 0.3625f, 1.225f); bset(164, 1.05f, 1.2125001f);
    bset(165, 0.1f, 0.2f); bset(166, 0.45f, 0.3f); bset(167, 0.45f, 0.3f); bset(168, 0.1f, 0.2f); bset(169, 0.45f, 0.3f);
}

static int is_ocean(int b) { return b == 0 || b == 10 || b == 24 || (b >= 44 && b <= 50); }

Terrain* terrain_new(uint64_t seed) {
    sin_init();
    biome_table();
    Terrain* t = (Terrain*) calloc(1, sizeof(Terrain));
    t->seed = seed;
    JRand r;
    jr_set(&r, seed);
    octave_init(&t->lower, &r, 16);
    octave_init(&t->upper, &r, 16);
    octave_init(&t->interp, &r, 8);
    for (int i = 0; i < 4; i++) simplex_consume(&r);
    jr_skip(&r, 2620);
    octave_init(&t->depth, &r, 16);
    for (int i = -2; i <= 2; i++)
        for (int j = -2; j <= 2; j++) t->W[i + 2 + (j + 2) * 5] = 10.0f / (float) sqrt((double) (i * i + j * j + 0.2f));
    setupGenerator(&t->g);
    applySeed(&t->g, seed);
    Range rg = {4, 0, 0, 9, 9, 15, 1};
    t->cache = allocCache(&t->g, rg);
    return t;
}

void terrain_free(Terrain* t) {
    if (!t) return;
    free(t->cache);
    free(t);
}


static double noise_main(const Terrain* t, int x, int yi, int z, double xz, double ys, double xz2, double ys2) {
    double lo = 0.0, up = 0.0, in = 0.0, d = 1.0;
    for (int i = 0; i < 16; i++) {
        const Perlin* pl = &t->lower.p[15 - i];
        double xx = wrap(x * xz * d), yy = wrap(yi * ys * d), zz = wrap(z * xz * d), ysd = ys * d;
        lo += perlin_sample(pl, xx, yy, zz, ysd, yi * ysd) / d;
        const Perlin* pu = &t->upper.p[15 - i];
        up += perlin_sample(pu, xx, yy, zz, ysd, yi * ysd) / d;
        if (i < 8) {
            const Perlin* pi = &t->interp.p[7 - i];
            in += perlin_sample(pi, wrap(x * xz2 * d), wrap(yi * ys2 * d), wrap(z * xz2 * d), ys2 * d, yi * ys2 * d) / d;
        }
        d /= 2.0;
    }
    double a = lo / 512.0, b = up / 512.0, c = (in / 10.0 + 1.0) / 2.0;
    if (c < 0.0) return a;
    if (c > 1.0) return b;
    return a + c * (b - a);
}

static double density_offset(const Terrain* t, int x, int z) {
    double r = octave_sample(&t->depth, x * 200.0, 10.0, z * 200.0, 1.0, 0.0, 1);
    double v = r < 0.0 ? -r * 0.3 : r;
    double w = v * 24.575625 - 2.0;
    return w < 0.0 ? w * 0.009486607142857142 : (w < 1.0 ? w : 1.0) * 0.006640625;
}


static double clamped_lerp(double a, double b, double t) {
    if (t < 0.0) return a;
    if (t > 1.0) return b;
    return a + t * (b - a);
}

static void column(const Terrain* t, const int* bio, int bx, int bz, int bw, int cellX, int cellZ, double* out) {
    float sSum = 0.0f, dSum = 0.0f, wSum = 0.0f;
    float cd = BDEPTH[bio[bz * bw + bx] & 255];
    for (int i = -2; i <= 2; i++)
        for (int j = -2; j <= 2; j++) {
            int bi = bio[(bz + j) * bw + (bx + i)] & 255;
            float d = BDEPTH[bi], s = BSCALE[bi];
            float w0 = d > cd ? 0.5f : 1.0f;
            float w = w0 * t->W[i + 2 + (j + 2) * 5] / (d + 2.0f);
            sSum += s * w;
            dSum += d * w;
            wSum += w;
        }
    float avgD = dSum / wSum, avgS = sSum / wSum;
    double d52 = avgD * 0.5f - 0.125f;
    double d54 = avgS * 0.9f + 0.1f;
    double off = d52 * 0.265625;
    double mul = 96.0 / d54;
    double xz = 684.412 * 0.9999999814507745, ys = 684.412 * 0.9999999814507745;
    double xz2 = xz / 80.0, ys2 = ys / 160.0;
    double rnd = density_offset(t, cellX, cellZ);
    for (int y = 0; y <= CELLS_Y; y++) {
        double n = noise_main(t, cellX, y, cellZ, xz, ys, xz2, ys2);
        double v38 = 1.0 - y * 2.0 / CELLS_Y + rnd;
        double v40 = v38 * 1.0 + (-0.46875);
        double v42 = (v40 + off) * mul;
        if (v42 > 0.0) n += v42 * 4.0; else n += v42;
        double s = (CELLS_Y - y - 0.0) / 3.0;
        n = clamped_lerp(-10.0, n, s);
        out[y] = n;
    }
}

static void fill(Terrain* t, int cx, int cz, unsigned char* mask) {
    Range rg = {4, cx * 4 - 2, cz * 4 - 2, 9, 9, 15, 1};
    genBiomes(&t->g, t->cache, rg);
    static double col[5][5][CELLS_Y + 1];
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 5; j++) column(t, t->cache, i + 2, j + 2, 9, cx * 4 + i, cz * 4 + j, col[i][j]);
    for (int ci = 0; ci < 4; ci++)
        for (int cj = 0; cj < 4; cj++)
            for (int yc = CELLS_Y - 1; yc >= 0; yc--) {
                double v000 = col[ci][cj][yc], v001 = col[ci][cj + 1][yc], v100 = col[ci + 1][cj][yc], v101 = col[ci + 1][cj + 1][yc];
                double w000 = col[ci][cj][yc + 1], w001 = col[ci][cj + 1][yc + 1], w100 = col[ci + 1][cj][yc + 1], w101 = col[ci + 1][cj + 1][yc + 1];
                for (int sy = 7; sy >= 0; sy--) {
                    int y = yc * 8 + sy;
                    double ty = sy / 8.0;
                    double a = mlerp(ty, v000, w000), b = mlerp(ty, v100, w100), c = mlerp(ty, v001, w001), d = mlerp(ty, v101, w101);
                    for (int sx = 0; sx < 4; sx++) {
                        double tx = sx / 4.0;
                        double e = mlerp(tx, a, b), f = mlerp(tx, c, d);
                        for (int sz = 0; sz < 4; sz++) {
                            double tz = sz / 4.0;
                            double v = mlerp(tz, e, f);
                            double q = v / 200.0;
                            q = q < -1.0 ? -1.0 : (q > 1.0 ? 1.0 : q);
                            q = q / 2.0 - q * q * q / 24.0;
                            int x = ci * 4 + sx, z = cj * 4 + sz;
                            mask[x + z * 16 + y * 256] = q > 0.0 ? 1 : (y < SEA ? 0 : 2);
                        }
                    }
                }
            }
}

static unsigned char* CARVE_EXCL;

typedef struct { unsigned char* m; int ccx, ccz; } Carve;

static int water_in(const Carve* c, int x0, int x1, int y0, int y1, int z0, int z1) {
    for (int i = x0; i < x1; i++)
        for (int j = z0; j < z1; j++)
            for (int y = y0 - 1; y <= y1 + 1; y++) {
                if (c->m[i + j * 16 + y * 256] == 0) return 1;
                if (y != y1 + 1 && !(i == x0 || i == x1 - 1 || j == z0 || j == z1 - 1)) y = y1;
            }
    return 0;
}

static void carve_region(Carve* c, double x, double y, double z, double hr, double vr, const float* canyon) {
    double mx = c->ccx * 16 + 8, mz = c->ccz * 16 + 8;
    if (x < mx - 16.0 - hr * 2.0 || z < mz - 16.0 - hr * 2.0 || x > mx + 16.0 + hr * 2.0 || z > mz + 16.0 + hr * 2.0) return;
    int x0 = ffloor(x - hr) - c->ccx * 16 - 1; if (x0 < 0) x0 = 0;
    int x1 = ffloor(x + hr) - c->ccx * 16 + 1; if (x1 > 16) x1 = 16;
    int y0 = ffloor(y - vr) - 1; if (y0 < 1) y0 = 1;
    int y1 = ffloor(y + vr) + 1; if (y1 > 248) y1 = 248;
    int z0 = ffloor(z - hr) - c->ccz * 16 - 1; if (z0 < 0) z0 = 0;
    int z1 = ffloor(z + hr) - c->ccz * 16 + 1; if (z1 > 16) z1 = 16;
    if (water_in(c, x0, x1, y0, y1, z0, z1)) return;
    for (int i = x0; i < x1; i++) {
        double dx = (i + c->ccx * 16 + 0.5 - x) / hr;
        for (int j = z0; j < z1; j++) {
            double dz = (j + c->ccz * 16 + 0.5 - z) / hr;
            if (dx * dx + dz * dz >= 1.0) continue;
            for (int yy = y1; yy > y0; yy--) {
                double dy = (yy - 0.5 - y) / vr;
                int skip;
                if (canyon) skip = (dx * dx + dz * dz) * canyon[yy - 1] + dy * dy / 6.0 >= 1.0;
                else skip = dy <= -0.7 || dx * dx + dy * dy + dz * dz >= 1.0;
                if (skip) continue;
                unsigned char* cell = &c->m[i + j * 16 + yy * 256];
                if (*cell == 1 && yy >= 5) *cell = yy < 11 ? 3 : 2;
                else if (*cell == 1 && CARVE_EXCL) CARVE_EXCL[i + j * 16 + yy * 256] = 1;
            }
        }
    }
}

static int in_range(const Carve* c, double x, double z, int step, int steps, float width) {
    double mx = c->ccx * 16 + 8, mz = c->ccz * 16 + 8;
    double dx = x - mx, dz = z - mz, rem = steps - step;
    double mxw = width + 2.0f + 16.0f;
    return dx * dx + dz * dz - rem * rem <= mxw * mxw;
}

static void cave_room(Carve* c, double x, double y, double z, float width) {
    double hr = 1.5 + mc_sin(1.5707964f) * width;
    carve_region(c, x + 1.0, y, z, hr, hr * 0.5, NULL);
}

static void cave_tunnel(Carve* c, int64_t seed, double x, double y, double z, float width, float yaw, float pitch, int start, int steps, double yScale) {
    JRand rnd;
    jr_set(&rnd, (uint64_t) seed);
    int branch = jr_int(&rnd, steps / 2) + steps / 4;
    int steep = jr_int(&rnd, 6) == 0;
    float yawD = 0.0f, pitchD = 0.0f;
    for (int i = start; i < steps; i++) {
        double hr = 1.5 + mc_sin(3.1415927f * i / steps) * width;
        double vr = hr * yScale;
        float cp = mc_cos(pitch);
        x += mc_cos(yaw) * cp;
        y += mc_sin(pitch);
        z += mc_sin(yaw) * cp;
        pitch *= steep ? 0.92f : 0.7f;
        pitch += pitchD * 0.1f;
        yaw += yawD * 0.1f;
        pitchD *= 0.9f;
        yawD *= 0.75f;
        float a = jr_float(&rnd), b = jr_float(&rnd), d = jr_float(&rnd);
        pitchD += (a - b) * d * 2.0f;
        a = jr_float(&rnd); b = jr_float(&rnd); d = jr_float(&rnd);
        yawD += (a - b) * d * 4.0f;
        if (i == branch && width > 1.0f) {
            int64_t s1 = jr_long(&rnd);
            float w1 = jr_float(&rnd) * 0.5f + 0.5f;
            cave_tunnel(c, s1, x, y, z, w1, yaw - 1.5707964f, pitch / 3.0f, i, steps, 1.0);
            int64_t s2 = jr_long(&rnd);
            float w2 = jr_float(&rnd) * 0.5f + 0.5f;
            cave_tunnel(c, s2, x, y, z, w2, yaw + 1.5707964f, pitch / 3.0f, i, steps, 1.0);
            return;
        }
        if (jr_int(&rnd, 4) != 0) {
            if (!in_range(c, x, z, i, steps, width)) return;
            carve_region(c, x, y, z, hr, vr, NULL);
        }
    }
}

static void canyon_tunnel(Carve* c, int64_t seed, double x, double y, double z, float width, float yaw, float pitch, int steps) {
    JRand rnd;
    jr_set(&rnd, (uint64_t) seed);
    float arr[256];
    float v = 1.0f;
    for (int i = 0; i < 256; i++) {
        if (i == 0 || jr_int(&rnd, 3) == 0) {
            float a = jr_float(&rnd), b = jr_float(&rnd);
            v = 1.0f + a * b;
        }
        arr[i] = v * v;
    }
    float pitchD = 0.0f, yawD = 0.0f;
    for (int i = 0; i < steps; i++) {
        double hr = 1.5 + mc_sin(i * 3.1415927f / steps) * width;
        double vr = hr * 3.0;
        hr *= jr_float(&rnd) * 0.25 + 0.75;
        vr *= jr_float(&rnd) * 0.25 + 0.75;
        float cp = mc_cos(pitch);
        float sp = mc_sin(pitch);
        x += mc_cos(yaw) * cp;
        y += sp;
        z += mc_sin(yaw) * cp;
        pitch *= 0.7f;
        pitch += pitchD * 0.05f;
        yaw += yawD * 0.05f;
        pitchD *= 0.8f;
        yawD *= 0.5f;
        float a = jr_float(&rnd), b = jr_float(&rnd), d = jr_float(&rnd);
        pitchD += (a - b) * d * 2.0f;
        a = jr_float(&rnd); b = jr_float(&rnd); d = jr_float(&rnd);
        yawD += (a - b) * d * 4.0f;
        if (jr_int(&rnd, 4) != 0) {
            if (!in_range(c, x, z, i, steps, width)) return;
            carve_region(c, x, y, z, hr, vr, arr);
        }
    }
}

static float cave_width(JRand* r) {
    float a = jr_float(r), b = jr_float(r);
    float v = a * 2.0f + b;
    if (jr_int(r, 10) == 0) {
        float p = jr_float(r), q = jr_float(r);
        v *= p * q * 3.0f + 1.0f;
    }
    return v;
}

static void cave_start(Carve* c, JRand* r, int nx, int nz) {
    int bound = 112;
    int count = jr_int(r, jr_int(r, jr_int(r, 15) + 1) + 1);
    for (int k = 0; k < count; k++) {
        double x = nx * 16 + jr_int(r, 16);
        double y = jr_int(r, jr_int(r, 120) + 8);
        double z = nz * 16 + jr_int(r, 16);
        int n = 1;
        if (jr_int(r, 4) == 0) {
            float w = 1.0f + jr_float(r) * 6.0f;
            jr_long(r);
            cave_room(c, x, y, z, w);
            n += jr_int(r, 4);
        }
        for (int m = 0; m < n; m++) {
            float yaw = jr_float(r) * 6.2831855f;
            float pitch = (jr_float(r) - 0.5f) / 4.0f;
            float w = cave_width(r);
            int steps = bound - jr_int(r, bound / 4);
            int64_t s = jr_long(r);
            cave_tunnel(c, s, x, y, z, w, yaw, pitch, 0, steps, 1.0);
        }
    }
}

static void canyon_start(Carve* c, JRand* r, int nx, int nz) {
    int bound = 112;
    double x = nx * 16 + jr_int(r, 16);
    double y = jr_int(r, jr_int(r, 40) + 8) + 20;
    double z = nz * 16 + jr_int(r, 16);
    float yaw = jr_float(r) * 6.2831855f;
    float pitch = (jr_float(r) - 0.5f) * 2.0f / 8.0f;
    float a = jr_float(r), b = jr_float(r);
    float w = (a * 2.0f + b) * 2.0f;
    int steps = bound - jr_int(r, bound / 4);
    int64_t s = jr_long(r);
    canyon_tunnel(c, s, x, y, z, w, yaw, pitch, steps);
}

static void large_seed(JRand* r, uint64_t seed, int nx, int nz) {
    jr_set(r, seed);
    int64_t a = jr_long(r);
    int64_t b = jr_long(r);
    jr_set(r, (uint64_t) (((int64_t) nx * a) ^ ((int64_t) nz * b) ^ (int64_t) seed));
}

static void carve_all(Terrain* t, int cx, int cz, unsigned char* mask) {
    Carve c = {mask, cx, cz};
    JRand r;
    for (int nx = cx - 8; nx <= cx + 8; nx++)
        for (int nz = cz - 8; nz <= cz + 8; nz++)
            for (int ci = 0; ci < 2; ci++) {
                large_seed(&r, t->seed + ci, nx, nz);
                float f = jr_float(&r);
                if (!(f <= (ci == 0 ? 0.14285715f : 0.02f))) continue;
                if (ci == 0) cave_start(&c, &r, nx, nz); else canyon_start(&c, &r, nx, nz);
            }
}

static int terrain_raw(Terrain* t, int cx, int cz, unsigned char* mask) {
    fill(t, cx, cz, mask);
    int ok = !is_ocean(t->cache[2 * 9 + 2]);
    if (ok) carve_all(t, cx, cz, mask);
    return ok;
}

static int terrain_chunk(Terrain* t, int cx, int cz, unsigned char* mask) {
    int ok = terrain_raw(t, cx, cz, mask);
    for (int i = 0; i < 65536; i++) if (mask[i] == 3) mask[i] = 0;
    return ok;
}

static int biome_cb(void* u, int qx, int qz) { return getBiomeAt(&((Terrain*) u)->g, 4, qx, 15, qz); }

#define RW 48
#define RIDX(x, y, z) ((x) + (z) * RW + (y) * RW * RW)

static unsigned char REG[RW * RW * 256];

typedef struct { int x0, y0, z0; } LakeBox;

static unsigned char REG_OCEAN[9];
static int AMB[16][4], NAMB;

static int touches_ocean(int x, int z, int ox, int oz) {
    for (int dz = 0; dz < 3; dz++)
        for (int dx = 0; dx < 3; dx++) {
            if (!REG_OCEAN[dz * 3 + dx]) continue;
            int cx0 = ox + dx * 16, cz0 = oz + dz * 16;
            if (x <= cx0 + 15 && x + 15 >= cx0 && z <= cz0 + 15 && z + 15 >= cz0) return 1;
        }
    return 0;
}

static int reg_get(int x, int y, int z) {
    if (y < 0 || y > 255) return 2;
    return REG[RIDX(x, y, z)];
}

static void amb_add(int x, int y, int z, int ox, int oz) {
    int ylo = 0, yhi = 255;
    int dx = (x - ox) >> 4, dz = (z - oz) >> 4;
    if (!REG_OCEAN[dz * 3 + dx]) {
        while (y > 5 && reg_get(x - ox, y, z - oz) == 2) y--;
        if (y <= 4) return;
        ylo = y - 5;
        yhi = y + 4;
    }
    AMB[NAMB][0] = x; AMB[NAMB][1] = z; AMB[NAMB][2] = ylo; AMB[NAMB][3] = yhi;
    NAMB++;
}

static int try_lake(JRand* r, int x, int y, int z, int liq, int ox, int oz, LakeBox* out) {
    while (y > 5 && reg_get(x - ox, y, z - oz) == 2) y--;
    if (y <= 4) return 0;
    y -= 4;
    unsigned char lake[2048];
    memset(lake, 0, sizeof lake);
    int n = jr_int(r, 4) + 4;
    for (int e = 0; e < n; e++) {
        double d1 = jr_double(r) * 6.0 + 3.0;
        double d2 = jr_double(r) * 4.0 + 2.0;
        double d3 = jr_double(r) * 6.0 + 3.0;
        double c1 = jr_double(r) * (16.0 - d1 - 2.0) + 1.0 + d1 / 2.0;
        double c2 = jr_double(r) * (8.0 - d2 - 4.0) + 2.0 + d2 / 2.0;
        double c3 = jr_double(r) * (16.0 - d3 - 2.0) + 1.0 + d3 / 2.0;
        for (int i = 1; i < 15; i++)
            for (int j = 1; j < 15; j++)
                for (int k = 1; k < 7; k++) {
                    double a = (i - c1) / (d1 / 2.0), b = (k - c2) / (d2 / 2.0), c = (j - c3) / (d3 / 2.0);
                    if (a * a + b * b + c * c < 1.0) lake[(i * 16 + j) * 8 + k] = 1;
                }
    }
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
            for (int k = 0; k < 8; k++) {
                int idx = (i * 16 + j) * 8 + k;
                int border = !lake[idx] && ((i < 15 && lake[((i + 1) * 16 + j) * 8 + k]) || (i > 0 && lake[((i - 1) * 16 + j) * 8 + k]) || (j < 15 && lake[(i * 16 + j + 1) * 8 + k]) || (j > 0 && lake[(i * 16 + j - 1) * 8 + k]) || (k < 7 && lake[(i * 16 + j) * 8 + k + 1]) || (k > 0 && lake[(i * 16 + j) * 8 + k - 1]));
                if (!border) continue;
                int v = reg_get(x + i - ox, y + k, z + j - oz);
                int liquid = v == 0 || v == 3;
                if (k >= 4 && liquid) return 0;
                if (k < 4 && v != 1 && v != liq) return 0;
            }
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
            for (int k = 0; k < 8; k++)
                if (lake[(i * 16 + j) * 8 + k] && y + k <= 255) REG[RIDX(x + i - ox, y + k, z + j - oz)] = (unsigned char) (k >= 4 ? 2 : liq);
    out->x0 = x; out->y0 = y; out->z0 = z;
    return 1;
}

static void lakes_of(Terrain* t, int sx, int sz, int ox, int oz, LakeBox* boxes, int* nb) {
    int biome = getBiomeAt(&t->g, 4, sx * 4 + 2, 2, sz * 4 + 2);
    int lavaOnly = biome == 2 || biome == 17 || biome == 130;
    JRand r;
    jr_set(&r, t->seed);
    int64_t a = jr_long(&r) | 1, b = jr_long(&r) | 1;
    uint64_t pop = (uint64_t) (((int64_t) (sx * 16) * a + (int64_t) (sz * 16) * b) ^ (int64_t) t->seed);
    int idx = 0;
    if (!lavaOnly) {
        jr_set(&r, pop + (uint64_t) idx + 10000ULL);
        if (jr_int(&r, 4) == 0) {
            int x = jr_int(&r, 16) + sx * 16, z = jr_int(&r, 16) + sz * 16, y = jr_int(&r, 256);
            if (touches_ocean(x, z, ox, oz)) amb_add(x, y, z, ox, oz);
            else if (try_lake(&r, x, y, z, 0, ox, oz, &boxes[*nb])) (*nb)++;
        }
        idx++;
    }
    jr_set(&r, pop + (uint64_t) idx + 10000ULL);
    if (jr_int(&r, 8) == 0) {
        int x = jr_int(&r, 16) + sx * 16, z = jr_int(&r, 16) + sz * 16;
        int y = jr_int(&r, jr_int(&r, 248) + 8);
        if (y < SEA || jr_int(&r, 10) == 0) {
            if (touches_ocean(x, z, ox, oz)) amb_add(x, y, z, ox, oz);
            else if (try_lake(&r, x, y, z, 3, ox, oz, &boxes[*nb])) (*nb)++;
        }
    }
}

int terrain_chunk_ex(Terrain* t, int cx, int cz, unsigned char* mask, unsigned char* excl) {
    memset(excl, 0, 65536);
    static unsigned char tmp[65536];
    CARVE_EXCL = excl;
    int center = terrain_raw(t, cx, cz, mask);
    CARVE_EXCL = NULL;
    if (!center) {
        for (int i = 0; i < 65536; i++) if (mask[i] == 3) mask[i] = 0;
        return 0;
    }
    int ox = (cx - 1) * 16, oz = (cz - 1) * 16;
    for (int dz = 0; dz < 3; dz++)
        for (int dx = 0; dx < 3; dx++) {
            unsigned char* src = (dx == 1 && dz == 1) ? mask : tmp;
            REG_OCEAN[dz * 3 + dx] = 0;
            if (src == tmp) REG_OCEAN[dz * 3 + dx] = !terrain_raw(t, cx - 1 + dx, cz - 1 + dz, tmp);
            for (int y = 0; y < 256; y++)
                for (int z = 0; z < 16; z++)
                    memcpy(&REG[RIDX(dx * 16, y, dz * 16 + z)], &src[z * 16 + y * 256], 16);
        }
    {
        LakeBox boxes[8];
        int nb = 0;
        NAMB = 0;
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 2; i++) lakes_of(t, cx - 1 + i, cz - 1 + j, ox, oz, boxes, &nb);
        for (int q = 0; q < NAMB; q++)
            for (int z = 0; z < 16; z++)
                for (int x = 0; x < 16; x++) {
                    int wx = cx * 16 + x, wz = cz * 16 + z;
                    if (wx < AMB[q][0] || wx > AMB[q][0] + 15 || wz < AMB[q][1] || wz > AMB[q][1] + 15) continue;
                    for (int y = AMB[q][2]; y <= AMB[q][3] && y < 256; y++) excl[x + z * 16 + y * 256] = 1;
                }
        for (int a = 0; a < nb; a++)
            for (int b = a + 1; b < nb; b++) {
                int x0 = boxes[a].x0 > boxes[b].x0 ? boxes[a].x0 : boxes[b].x0, x1 = (boxes[a].x0 < boxes[b].x0 ? boxes[a].x0 : boxes[b].x0) + 15;
                int z0 = boxes[a].z0 > boxes[b].z0 ? boxes[a].z0 : boxes[b].z0, z1 = (boxes[a].z0 < boxes[b].z0 ? boxes[a].z0 : boxes[b].z0) + 15;
                int y0 = boxes[a].y0 > boxes[b].y0 ? boxes[a].y0 : boxes[b].y0, y1 = (boxes[a].y0 < boxes[b].y0 ? boxes[a].y0 : boxes[b].y0) + 7;
                if (x0 > x1 || z0 > z1 || y0 > y1) continue;
                for (int y = y0; y <= y1 && y < 256; y++)
                    for (int z = z0; z <= z1; z++)
                        for (int x = x0; x <= x1; x++) {
                            int lx = x - cx * 16, lz = z - cz * 16;
                            if (lx >= 0 && lx < 16 && lz >= 0 && lz < 16) excl[lx + lz * 16 + y * 256] = 1;
                        }
            }
    }
    for (int y = 0; y < 256; y++)
        for (int z = 0; z < 16; z++)
            memcpy(&mask[z * 16 + y * 256], &REG[RIDX(16, y, 16 + z)], 16);
    for (int i = 0; i < 65536; i++) if (mask[i] == 3) mask[i] = 0;
    mineshaft_mark(t->seed, cx, cz, biome_cb, t, excl);
    return 1;
}

int terrain_surface(Terrain* t, int x, int z) {
    unsigned char* m = (unsigned char*) malloc(65536);
    terrain_chunk(t, x >> 4, z >> 4, m);
    int lx = x & 15, lz = z & 15, res = -1;
    for (int y = 255; y >= 0; y--)
        if (m[lx + lz * 16 + y * 256] != 2) { res = y; break; }
    free(m);
    return res;
}
