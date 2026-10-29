#pragma once
#include <stdint.h>
#include "dungeon.h"

typedef struct Planner Planner;

typedef struct { int ok, failPi, failNext; } DesignInfo;

typedef struct {
    int x0, y0, z0, x1, y1, z1, air;
} Box;

typedef struct {
    Box* clear; int nclear;
    Box* build; int nbuild;
} CmdSet;

Planner* planner_new(void);
void planner_free(Planner* p);
void planner_chunk(Planner* p, uint64_t seed, int cx, int cz, const unsigned char* mask);
int planner_design(Planner* p, const Room* plan, int n, DesignInfo* info);
int planner_verify(Planner* p, const Room* plan, int n);
int planner_commands(Planner* p, const Room* plan, int n, int margin, CmdSet* out);
void cmdset_free(CmdSet* c);

typedef struct Memo Memo;
Memo* memo_new(void);
void memo_free(Memo* m);
int memo_blocked(const Memo* m, const Room* plan, int n);
void memo_record(Memo* m, const Room* plan, int n, const DesignInfo* info);
uint64_t plan_hash(const Room* plan, int n);

int cmd_format(const Box* b, int ox, int oz, char* out, int cap);
int blobs_make(char** lines, int n, int limit, int drop, char*** blobs);
