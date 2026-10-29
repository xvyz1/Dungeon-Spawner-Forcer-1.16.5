#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint64_t* t; uint32_t cap, cnt; } HSet;

static inline void hs_init(HSet* s) { s->t = NULL; s->cap = 0; s->cnt = 0; }
static inline void hs_clear(HSet* s) { if (s->t) memset(s->t, 0, sizeof(uint64_t) * s->cap); s->cnt = 0; }
static inline void hs_free(HSet* s) { free(s->t); s->t = NULL; s->cap = s->cnt = 0; }

static inline int hs_has(const HSet* s, uint64_t h) {
    if (!s->t) return 0;
    h |= 1;
    uint32_t i = (uint32_t) h & (s->cap - 1);
    while (s->t[i]) { if (s->t[i] == h) return 1; i = (i + 1) & (s->cap - 1); }
    return 0;
}

static inline void hs_add(HSet* s, uint64_t h) {
    h |= 1;
    if (!s->t) { s->cap = 1u << 12; s->t = (uint64_t*) calloc(s->cap, sizeof(uint64_t)); }
    if (s->cnt * 2 >= s->cap) {
        uint32_t oc = s->cap;
        uint64_t* ot = s->t;
        s->cap = oc * 2;
        s->t = (uint64_t*) calloc(s->cap, sizeof(uint64_t));
        s->cnt = 0;
        for (uint32_t k = 0; k < oc; k++) if (ot[k]) {
            uint32_t i = (uint32_t) ot[k] & (s->cap - 1);
            while (s->t[i]) i = (i + 1) & (s->cap - 1);
            s->t[i] = ot[k];
            s->cnt++;
        }
        free(ot);
    }
    uint32_t i = (uint32_t) h & (s->cap - 1);
    while (s->t[i]) { if (s->t[i] == h) return; i = (i + 1) & (s->cap - 1); }
    s->t[i] = h;
    s->cnt++;
}
