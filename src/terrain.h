#pragma once
#include <stdint.h>

typedef struct Terrain Terrain;

Terrain* terrain_new(uint64_t seed);
void terrain_free(Terrain* t);
int terrain_chunk_ex(Terrain* t, int cx, int cz, unsigned char* mask, unsigned char* excl);
int terrain_surface(Terrain* t, int x, int z);
