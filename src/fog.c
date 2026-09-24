#include "fog.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "util.h"

int fog_any(const Map *m)
{
    if (!m || !m->fog_on) return 0;
    for (int i = 0; i < FOG_PATCH_MAX; i++) {
        const FogPatch *p = &m->fog_patches[i];
        if (fog_patch_live(m, i + 1) && p->x1 >= p->x0) return 1;
    }
    return 0;
}

int fog_token_hidden(const Map *m, const Token *t)
{
    for (int y = t->y; y < t->y + t->size; y++)
        for (int x = t->x; x < t->x + t->size; x++)
            if (!fog_creature_hidden(m, x, y)) return 0;
    return 1;
}

int fog_find(const Map *m, const char *prefix)
{
    if (!prefix || !*prefix) return 0;
    size_t len = strlen(prefix);
    int found = 0, hits = 0;
    for (int i = 0; i < FOG_PATCH_MAX; i++) {
        const FogPatch *p = &m->fog_patches[i];
        if (!p->name[0] || p->dead || strncasecmp(p->name, prefix, len) != 0) continue;
        if (strlen(p->name) == len) return i + 1;
        found = i + 1;
        hits++;
    }
    return hits > 1 ? -1 : found;
}

int fog_create(Map *m, const char *name)
{
    size_t n = strlen(name);
    if (n == 0 || n >= FOG_NAME_MAX || !isalpha((unsigned char)name[0])) return -1;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)name[i])) return -1;

    for (int i = 0; i < FOG_PATCH_MAX; i++) {
        const FogPatch *p = &m->fog_patches[i];
        if (p->name[0] && !p->dead && !strcasecmp(p->name, name)) return i + 1;
    }
    for (int i = 0; i < FOG_PATCH_MAX; i++) {
        FogPatch *p = &m->fog_patches[i];
        if (p->name[0] || p->dead) continue;
        memset(p, 0, sizeof *p);
        str_lcpy(p->name, name, sizeof p->name);
        p->reveal    = 2;
        p->memory    = 1;
        p->soft_edge = -1;
        p->x1        = -1;
        map_touch(m);
        return i + 1;
    }
    return 0;
}

void fog_delete(Map *m, int id)
{
    if (id < 1 || id > FOG_PATCH_MAX) return;
    FogPatch *p = &m->fog_patches[id - 1];
    if (p->x1 >= p->x0)
        for (int y = p->y0; y <= p->y1; y++)
            for (int x = p->x0; x <= p->x1; x++)
                if ((fog_at(m, x, y) & FOG_ID) == (uint8_t)id)
                    m->fog[(size_t)y * (size_t)m->w + (size_t)x] = 0;
    memset(p, 0, sizeof *p);
    p->x1   = -1;
    p->dead = 1;
    map_touch(m);
}

void fog_paint(Map *m, Undo *u, int x, int y, int id)
{
    if (!map_in_bounds(m, x, y)) return;
    uint8_t want = (uint8_t)((unsigned)id & FOG_ID);
    if (fog_at(m, x, y) == want) return;
    undo_set_fog(u, m, x, y, want);
}

void fog_light(Map *m, Undo *u, int x, int y, int on)
{
    uint8_t f = fog_at(m, x, y);
    if (!(f & FOG_ID)) return;
    uint8_t want = on ? (uint8_t)(f | FOG_HELD | FOG_SEEN)
                      : (uint8_t)(f & ~(FOG_HELD | FOG_SEEN | FOG_LIT | FOG_RIM));
    if (want != f) undo_set_fog(u, m, x, y, want);
}

int fog_light_patch(Map *m, Undo *u, int id, int on)
{
    if (id < 1 || id > FOG_PATCH_MAX) return 0;
    const FogPatch *p = &m->fog_patches[id - 1];
    int changed = 0;
    if (p->x1 < p->x0) return 0;
    undo_begin(u);
    for (int y = p->y0; y <= p->y1; y++)
        for (int x = p->x0; x <= p->x1; x++) {
            uint8_t f = fog_at(m, x, y);
            if ((f & FOG_ID) != (uint8_t)id) continue;
            fog_light(m, u, x, y, on);
            changed += fog_at(m, x, y) != f;
        }
    undo_end(u);
    return changed;
}

int fog_count(const Map *m, int id, int *seen)
{
    int n = 0, s = 0;
    const FogPatch *p = &m->fog_patches[id - 1];
    if (p->x1 >= p->x0)
        for (int y = p->y0; y <= p->y1; y++)
            for (int x = p->x0; x <= p->x1; x++) {
                uint8_t f = fog_at(m, x, y);
                if ((f & FOG_ID) != (uint8_t)id) continue;
                n++;
                s += (f & (FOG_SEEN | FOG_HELD | FOG_LIT)) != 0;
            }
    if (seen) *seen = s;
    return n;
}
