#include "search.h"
#include "hset.h"
#include "threads.h"
#include <string.h>

double wall_time(void) { return ds_time(); }

typedef struct {
    Planner* pl;
    Memo* memo;
    HSet seen;
    const SearchSpec* spec;
    ChunkPlan* out;
    int minN;
    int maxSeen;
    double deadline;
    int timedOut;
} Ctx;

static int leaf(void* user, const Room* rooms, int count) {
    Ctx* c = (Ctx*) user;
    const SearchSpec* s = c->spec;
    int n, px, py, pz;
    if (s->fixedP) {
        px = s->px; py = s->py; pz = s->pz;
        n = 0;
        for (int i = 0; i < count; i++) if ((s->pref < 0 || rooms[i].type == s->pref) && room_dsq(&rooms[i], px, py, pz) < 256) n++;
    } else if (!stand_best(rooms, count, s->pref, c->minN, &px, &py, &pz, &n)) return 0;
    if (n < c->minN) return 0;
    if (n > c->maxSeen) c->maxSeen = n;
    uint64_t h = plan_hash(rooms, count);
    if (hs_has(&c->seen, h)) return 0;
    hs_add(&c->seen, h);
    if (memo_blocked(c->memo, rooms, count)) { c->out->skips++; return 0; }
    if (wall_time() > c->deadline) { c->timedOut = 1; return 1; }
    DesignInfo info;
    int ok = planner_design(c->pl, rooms, count, &info);
    c->out->designs++;
    if (ok && planner_verify(c->pl, rooms, count)) {
        c->out->found = 1;
        c->out->n = n;
        c->out->nplan = count;
        memcpy(c->out->plan, rooms, sizeof(Room) * count);
        c->out->px = px; c->out->py = py; c->out->pz = pz;
        return 1;
    }
    if (!ok) memo_record(c->memo, rooms, count, &info);
    return 0;
}

void chunk_search(Planner* pl, Scan* sc, const SearchSpec* spec, ChunkPlan* out) {
    memset(out, 0, sizeof *out);
    out->exhaustive = 1;
    Ctx c;
    memset(&c, 0, sizeof c);
    c.pl = pl; c.memo = memo_new(); hs_init(&c.seen);
    c.spec = spec; c.out = out;
    double t0 = wall_time();
    c.deadline = t0 + spec->tlimit;
    for (int minN = spec->maxN; minN >= spec->minN && !out->found && !c.timedOut; minN--) {
        c.minN = minN;
        scan_chunk(sc, spec->seed, spec->cx, spec->cz, spec->pref, minN, leaf, &c);
    }
    out->upper = c.maxSeen;
    if (c.timedOut) out->exhaustive = 0;
    out->seconds = wall_time() - t0;
    memo_free(c.memo);
    hs_free(&c.seen);
}
