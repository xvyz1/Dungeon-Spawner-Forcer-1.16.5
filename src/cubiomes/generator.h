#ifndef GENERATOR_H_
#define GENERATOR_H_

#include "layers.h"

typedef struct
{
    int scale, x, z, sx, sz, y, sy;
} Range;

typedef struct
{
    int mc;
    uint64_t seed;
    uint64_t sha;
    LayerStack ls;
} Generator;

void setupGenerator(Generator *g);
void applySeed(Generator *g, uint64_t seed);
int *allocCache(const Generator *g, Range r);
int genBiomes(const Generator *g, int *cache, Range r);
int getBiomeAt(const Generator *g, int scale, int x, int y, int z);
void setupLayerStack(LayerStack *g);
size_t getMinLayerCacheSize(const Layer *layer, int sizeX, int sizeZ);
Layer *setupLayer(Layer *l, mapfunc_t *map, int mc, int8_t zoom, int8_t edge, uint64_t saltbase, Layer *p, Layer *p2);
int genArea(const Layer *layer, int *out, int areaX, int areaZ, int areaWidth, int areaHeight);

#endif
