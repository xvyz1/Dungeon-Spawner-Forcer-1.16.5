#pragma once
#include <stdio.h>
#include "plan.h"
#include "search.h"
#include "terrain.h"

int build_write(FILE* f, Planner* pl, Terrain* tr, int pref, int cx, int cz, const ChunkPlan* cp);
