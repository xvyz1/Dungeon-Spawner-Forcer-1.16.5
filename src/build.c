#include "build.h"
#include "dungeon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MARGIN 6
#define CMD_LIMIT 30000

static const char* const MOBS[3] = {"SKELETON", "ZOMBIE", "SPIDER"};

static char* copy_str(const char* s) {
    size_t n = strlen(s) + 1;
    char* r = (char*) malloc(n);
    memcpy(r, s, n);
    return r;
}

int build_write(FILE* f, Planner* pl, Terrain* tr, int pref, int cx, int cz, const ChunkPlan* cp) {
    CmdSet cs = {0};
    if (!planner_commands(pl, cp->plan, cp->nplan, MARGIN, &cs)) {
        fprintf(f, "The commands for this layout could not be verified\n");
        return 0;
    }
    int ox = cx * 16, oz = cz * 16;
    int px = ox + cp->px, py = cp->py, pz = oz + cp->pz;
    int ax = ox + 30, az = oz + 8;
    int sy = terrain_surface(tr, ax, az);
    int known = sy >= 0;
    if (!known) sy = 100;
    int nl = cs.nclear + cs.nbuild + 3;
    char** lines = (char**) malloc(sizeof(char*) * nl);
    char buf[512];
    int k = 0;
    for (int i = 0; i < cs.nclear; i++) { cmd_format(&cs.clear[i], ox, oz, buf, sizeof buf); lines[k++] = copy_str(buf); }
    for (int i = 0; i < cs.nbuild; i++) { cmd_format(&cs.build[i], ox, oz, buf, sizeof buf); lines[k++] = copy_str(buf); }
    lines[k++] = copy_str("setblock ~ ~ ~ air");
    lines[k++] = copy_str("setblock ~ ~-1 ~ air");
    lines[k++] = copy_str("kill @e[type=command_block_minecart,distance=..3]");
    char** blobs;
    int nb = blobs_make(lines, k, CMD_LIMIT, 2, &blobs);
    fprintf(f, "Planned dungeons:\n");
    for (int i = 0; i < cp->nplan; i++) {
        const Room* r = &cp->plan[i];
        int inr = r->type == pref && room_dsq(r, cp->px, py, cp->pz) < 256;
        fprintf(f, "  %d %d %d  r%d,%d  %s%s\n", ox + r->x, r->y, oz + r->z, r->xr, r->zr, MOBS[r->type], inr ? "  (in range)" : "");
    }
    fprintf(f, "\nSteps:\n1. Teleport next to the chunk (/tp @p %d %d %d).\n", ax, sy + 3, az);
    fprintf(f, "2. Place a command block at %d %d %d (it needs a solid block under it), paste %s into it and activate it, not from the top.\n", ax, sy + 1, az, nb == 1 ? "the command below" : "each command below, one after the other");
    fprintf(f, "3. Go to the top of the chunk (/tp @p %d 255 %d), hold a book and quill, and type .s.\n", ox + 8, oz + 8);
    fprintf(f, "4. Teleport away to unload the chunk, or relog.\n");
    int step = 5;
    int others = 0;
    for (int i = 0; i < cp->nplan; i++) if (cp->plan[i].type != pref) others++;
    if (others) {
        fprintf(f, "%d. Remove the spawners of the other mobs (they are outside the sphere):\n", step++);
        for (int i = 0; i < cp->nplan; i++) if (cp->plan[i].type != pref) fprintf(f, "   %d %d %d %s\n", ox + cp->plan[i].x, cp->plan[i].y, oz + cp->plan[i].z, MOBS[cp->plan[i].type]);
    }
    fprintf(f, "%d. Remove any exposed water and lava in and around the chunk.\n", step++);
    fprintf(f, "%d. Teleport to the center (/tp @p %d.5 %d.5 %d.5): %d %s spawners are in range.\n", step, px, py, pz, cp->n, MOBS[pref]);
    fprintf(f, "\n%d clear + %d build commands%s\n", cs.nclear, cs.nbuild, known ? "" : "\n(ground height unknown, the placement above uses y 100: adjust it to the ground)");
    for (int i = 0; i < nb; i++) fprintf(f, "\n# command %d of %d (%d characters)\n%s\n", i + 1, nb, (int) strlen(blobs[i]), blobs[i]);
    for (int i = 0; i < nb; i++) free(blobs[i]);
    free(blobs);
    for (int i = 0; i < k; i++) free(lines[i]);
    free(lines);
    cmdset_free(&cs);
    return 1;
}
