#pragma once
#include <stdint.h>

int mineshaft_mark(uint64_t seed, int cx, int cz, int (*biome_at)(void* user, int qx, int qz), void* user, unsigned char* excl);
