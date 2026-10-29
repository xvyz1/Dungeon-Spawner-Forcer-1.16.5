#pragma once
#include <stdint.h>

#define MOB_SKELETON 0
#define MOB_ZOMBIE 1
#define MOB_SPIDER 2

typedef struct { int x, y, z, xr, zr, type; } Room;
typedef struct Scan Scan;
typedef int (*LeafFn)(void* user, const Room* rooms, int count);

Scan* scan_new(void);
void scan_free(Scan* s);
void scan_stone(Scan* s, int top);
void scan_mask(Scan* s, const unsigned char* mask);
void scan_exclude(Scan* s, const unsigned char* excl);
void scan_chunk(Scan* s, uint64_t seed, int cx, int cz, int pref, int minTyped, LeafFn fn, void* user);
int stand_best(const Room* rooms, int count, int pref, int need, int* px, int* py, int* pz, int* typedInRange);
int room_dsq(const Room* r, int x, int y, int z);
