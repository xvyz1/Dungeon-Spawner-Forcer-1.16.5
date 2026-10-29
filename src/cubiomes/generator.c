#include "generator.h"
#include "layers.h"
#include <stdlib.h>
#include <string.h>

void setupGenerator(Generator *g)
{
    g->mc = MC_1_16_5;
    g->seed = 0;
    g->sha = 0;
    setupLayerStack(&g->ls);
}

void applySeed(Generator *g, uint64_t seed)
{
    g->seed = seed;
    setLayerSeed(g->ls.entry_1, seed);
    g->sha = g->ls.entry_1->startSalt;
}

static const Layer *getLayerForScale(const Generator *g, int scale)
{
    switch (scale)
    {
    case 1:   return g->ls.entry_1;
    case 4:   return g->ls.entry_4;
    case 16:  return g->ls.entry_16;
    case 64:  return g->ls.entry_64;
    case 256: return g->ls.entry_256;
    default:
        return NULL;
    }
}

int *allocCache(const Generator *g, Range r)
{
    const Layer *entry = getLayerForScale(g, r.scale);
    if (!entry) return NULL;
    size_t len = (size_t)r.sx * r.sz;
    size_t len2d = getMinLayerCacheSize(entry, r.sx, r.sz);
    len += len2d - (size_t)r.sx * r.sz;
    return (int*) calloc(len, sizeof(int));
}

int genBiomes(const Generator *g, int *cache, Range r)
{
    const Layer *entry = getLayerForScale(g, r.scale);
    if (!entry) return -1;
    return genArea(entry, cache, r.x, r.z, r.sx, r.sz);
}

int getBiomeAt(const Generator *g, int scale, int x, int y, int z)
{
    (void) y;
    Range r = {scale, x, z, 1, 1, 0, 1};
    int *ids = allocCache(g, r);
    int id = genBiomes(g, ids, r);
    if (id == 0)
        id = ids[0];
    else
        id = none;
    free(ids);
    return id;
}

Layer *setupLayer(Layer *l, mapfunc_t *map, int mc,
    int8_t zoom, int8_t edge, uint64_t saltbase, Layer *p, Layer *p2)
{
    //Layer *l = g->layers + layerId;
    l->getMap = map;
    l->mc = mc;
    l->zoom = zoom;
    l->edge = edge;
    l->scale = 0;
    if (saltbase == 0 || saltbase == LAYER_INIT_SHA)
        l->layerSalt = saltbase;
    else
        l->layerSalt = getLayerSalt(saltbase);
    l->startSalt = 0;
    l->startSeed = 0;
    l->noise = NULL;
    l->data = NULL;
    l->p = p;
    l->p2 = p2;
    return l;
}

static void setupScale(Layer *l, int scale)
{
    l->scale = scale;
    if (l->p)
        setupScale(l->p, scale * l->zoom);
    if (l->p2)
        setupScale(l->p2, scale * l->zoom);
}

void setupLayerStack(LayerStack *g)
{
    int mc = MC_1_16_5;
    memset(g, 0, sizeof(LayerStack));
    Layer *p, *l = g->layers;
    p = setupLayer(l+L_CONTINENT_4096, mapContinent,   mc, 1, 0, 1,    0, 0);
    p = setupLayer(l+L_ZOOM_2048,      mapZoomFuzzy,   mc, 2, 3, 2000, p, 0);
    p = setupLayer(l+L_LAND_2048,      mapLand,        mc, 1, 2, 1,    p, 0);
    p = setupLayer(l+L_ZOOM_1024,      mapZoom,        mc, 2, 3, 2001, p, 0);
    p = setupLayer(l+L_LAND_1024_A,    mapLand,        mc, 1, 2, 2,    p, 0);
    p = setupLayer(l+L_LAND_1024_B,    mapLand,        mc, 1, 2, 50,   p, 0);
    p = setupLayer(l+L_LAND_1024_C,    mapLand,        mc, 1, 2, 70,   p, 0);
    p = setupLayer(l+L_ISLAND_1024,    mapIsland,      mc, 1, 2, 2,    p, 0);
    p = setupLayer(l+L_SNOW_1024,      mapSnow,        mc, 1, 2, 2,    p, 0);
    p = setupLayer(l+L_LAND_1024_D,    mapLand,        mc, 1, 2, 3,    p, 0);
    p = setupLayer(l+L_COOL_1024,      mapCool,        mc, 1, 2, 2,    p, 0);
    p = setupLayer(l+L_HEAT_1024,      mapHeat,        mc, 1, 2, 2,    p, 0);
    p = setupLayer(l+L_SPECIAL_1024,   mapSpecial,     mc, 1, 2, 3,    p, 0);
    p = setupLayer(l+L_ZOOM_512,       mapZoom,        mc, 2, 3, 2002, p, 0);
    p = setupLayer(l+L_ZOOM_256,       mapZoom,        mc, 2, 3, 2003, p, 0);
    p = setupLayer(l+L_LAND_256,       mapLand,        mc, 1, 2, 4,    p, 0);
    p = setupLayer(l+L_MUSHROOM_256,   mapMushroom,    mc, 1, 2, 5,    p, 0);
    p = setupLayer(l+L_DEEP_OCEAN_256, mapDeepOcean,   mc, 1, 2, 4,    p, 0);
    p = setupLayer(l+L_BIOME_256,      mapBiome,       mc, 1, 0, 200,  p, 0);
    p = setupLayer(l+L_BAMBOO_256,     mapBamboo,      mc, 1, 0, 1001, p, 0);
    p = setupLayer(l+L_ZOOM_128,       mapZoom,        mc, 2, 3, 1000, p, 0);
    p = setupLayer(l+L_ZOOM_64,        mapZoom,        mc, 2, 3, 1001, p, 0);
    p = setupLayer(l+L_BIOME_EDGE_64,  mapBiomeEdge,   mc, 1, 2, 1000, p, 0);
    p = setupLayer(l+L_RIVER_INIT_256, mapNoise,       mc, 1, 0, 100,
                   l+L_DEEP_OCEAN_256, 0);
    p = setupLayer(l+L_ZOOM_128_HILLS, mapZoom,        mc, 2, 3, 1000, p, 0);
    p = setupLayer(l+L_ZOOM_64_HILLS,  mapZoom,        mc, 2, 3, 1001, p, 0);
    p = setupLayer(l+L_HILLS_64,       mapHills,       mc, 1, 2, 1000,
                   l+L_BIOME_EDGE_64, l+L_ZOOM_64_HILLS);
    p = setupLayer(l+L_SUNFLOWER_64,   mapSunflower,   mc, 1, 0, 1001, p, 0);
    p = setupLayer(l+L_ZOOM_32,        mapZoom,        mc, 2, 3, 1000, p, 0);
    p = setupLayer(l+L_LAND_32,        mapLand,        mc, 1, 2, 3,    p, 0);
    p = setupLayer(l+L_ZOOM_16,        mapZoom,        mc, 2, 3, 1001, p, 0);
    p = setupLayer(l+L_SHORE_16,       mapShore,       mc, 1, 2, 1000, p, 0);
    p = setupLayer(l+L_ZOOM_8,         mapZoom,        mc, 2, 3, 1002, p, 0);
    p = setupLayer(l+L_ZOOM_4,         mapZoom,        mc, 2, 3, 1003, p, 0);
    p = setupLayer(l+L_SMOOTH_4,       mapSmooth,      mc, 1, 2, 1000, p, 0);
    p = setupLayer(l+L_ZOOM_128_RIVER, mapZoom,        mc, 2, 3, 1000,
                   l+L_RIVER_INIT_256, 0);
    p = setupLayer(l+L_ZOOM_64_RIVER,  mapZoom,        mc, 2, 3, 1001, p, 0);
    p = setupLayer(l+L_ZOOM_32_RIVER,  mapZoom,        mc, 2, 3, 1000, p, 0);
    p = setupLayer(l+L_ZOOM_16_RIVER,  mapZoom,        mc, 2, 3, 1001, p, 0);
    p = setupLayer(l+L_ZOOM_8_RIVER,   mapZoom,        mc, 2, 3, 1002, p, 0);
    p = setupLayer(l+L_ZOOM_4_RIVER,   mapZoom,        mc, 2, 3, 1003, p, 0);
    p = setupLayer(l+L_RIVER_4,        mapRiver,       mc, 1, 2, 1,    p, 0);
    p = setupLayer(l+L_SMOOTH_4_RIVER, mapSmooth,      mc, 1, 2, 1000, p, 0);
    p = setupLayer(l+L_RIVER_MIX_4,    mapRiverMix,    mc, 1, 0, 100,
                   l+L_SMOOTH_4, l+L_SMOOTH_4_RIVER);
    p = setupLayer(l+L_OCEAN_TEMP_256, mapOceanTemp,   mc, 1, 0, 2,    0, 0);
    p->noise = &g->oceanRnd;
    p = setupLayer(l+L_ZOOM_128_OCEAN, mapZoom,        mc, 2, 3, 2001, p, 0);
    p = setupLayer(l+L_ZOOM_64_OCEAN,  mapZoom,        mc, 2, 3, 2002, p, 0);
    p = setupLayer(l+L_ZOOM_32_OCEAN,  mapZoom,        mc, 2, 3, 2003, p, 0);
    p = setupLayer(l+L_ZOOM_16_OCEAN,  mapZoom,        mc, 2, 3, 2004, p, 0);
    p = setupLayer(l+L_ZOOM_8_OCEAN,   mapZoom,        mc, 2, 3, 2005, p, 0);
    p = setupLayer(l+L_ZOOM_4_OCEAN,   mapZoom,        mc, 2, 3, 2006, p, 0);
    p = setupLayer(l+L_OCEAN_MIX_4,    mapOceanMix,    mc, 1, 17, 100,
                   l+L_RIVER_MIX_4, l+L_ZOOM_4_OCEAN);
    p = setupLayer(l+L_VORONOI_1,      mapVoronoi,     mc, 4, 3, LAYER_INIT_SHA, p, 0);
    g->entry_1 = p;
    g->entry_4 = l + L_OCEAN_MIX_4;
    g->entry_16 = l + L_SHORE_16;
    g->entry_64 = l + L_SUNFLOWER_64;
    g->entry_256 = l + L_BAMBOO_256;
    setupScale(g->entry_1, 1);
}

/* Recursively calculates the minimum buffer size required to generate an area
 * of the specified size from the current layer onwards.
 */
static void getMaxArea(
    const Layer *layer, int areaX, int areaZ, int *maxX, int *maxZ, size_t *siz)
{
    if (layer == NULL)
        return;

    areaX += layer->edge;
    areaZ += layer->edge;

    // multi-layers and zoom-layers use a temporary copy of their parent area
    if (layer->p2 || layer->zoom != 1)
        *siz += areaX * areaZ;

    if (areaX > *maxX) *maxX = areaX;
    if (areaZ > *maxZ) *maxZ = areaZ;

    if (layer->zoom == 2)
    {
        areaX >>= 1;
        areaZ >>= 1;
    }
    else if (layer->zoom == 4)
    {
        areaX >>= 2;
        areaZ >>= 2;
    }

    getMaxArea(layer->p, areaX, areaZ, maxX, maxZ, siz);
    if (layer->p2)
        getMaxArea(layer->p2, areaX, areaZ, maxX, maxZ, siz);
}

size_t getMinLayerCacheSize(const Layer *layer, int sizeX, int sizeZ)
{
    int maxX = sizeX, maxZ = sizeZ;
    size_t bufsiz = 0;
    getMaxArea(layer, sizeX, sizeZ, &maxX, &maxZ, &bufsiz);
    return bufsiz + maxX * (size_t)maxZ;
}

int genArea(const Layer *layer, int *out, int areaX, int areaZ, int areaWidth, int areaHeight)
{
    memset(out, 0, sizeof(*out)*areaWidth*areaHeight);
    return layer->getMap(layer, out, areaX, areaZ, areaWidth, areaHeight);
}


