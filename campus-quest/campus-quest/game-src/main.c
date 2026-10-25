/*
 * COLLEGE STORY - SSEC CAMPUS  (Stage 7: rooms, doors, bigger world)
 * Pure C + SDL3.  No game engine.
 *
 * What is inside this file:
 *   1. Settings and small drawing helpers
 *   2. The campus data (buildings, roads, hedges, trees ...) taken from your PPT
 *   3. Interiors (desks, tables, beds ...) shown when you walk inside a building
 *   4. Collision (you cannot walk through walls, trees, hedges)
 *   5. Drawing (ground -> shadows -> depth-sorted objects -> HUD / minimap)
 *   6. main() - the game loop
 *
 * Map units: 1 unit = 10 pixels of your PPT picture.  Map = 92 x 130 units.
 * Interiors: every building now has real rooms, corridors, doors and furniture.
 */
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define SCREEN_W 1280
#define SCREEN_H 720
#define MAP_W 92.0f
#define MAP_H 130.0f

/* Isometric projection: how a map unit becomes screen pixels */
/* WORLD_SCALE makes the whole campus bigger on screen; PLAYER_SCALE makes the boy smaller.
   Bigger WORLD_SCALE + smaller PLAYER_SCALE = a bigger-feeling map. */
#define WORLD_SCALE  1.6f
#define PLAYER_SCALE 1.0f
#define SX (11.0f * WORLD_SCALE)   /* pixels sideways per map unit */
#define SY (6.4f  * WORLD_SCALE)   /* pixels downwards per map unit */
#define ZS (4.6f  * WORLD_SCALE)   /* pixels per metre of height   */

#define PLAYER_SPEED 8.0f  /* map units per second         */
#define WALL_T 0.7f        /* building wall thickness      */
#define DOOR_H 2.7f        /* door height (metres)         */
#define PI 3.14159265f

#define MINF(a,b) ((a)<(b)?(a):(b))
#define MAXF(a,b) ((a)>(b)?(a):(b))

/* ------------------------------------------------------------------ */
/* 1. Basic helpers                                                    */
/* ------------------------------------------------------------------ */
static SDL_Renderer *R;
/* Floors of the MAIN COLLEGE (3 floors, each FH metres high). curfloor = floor you are on (0 = 1st floor).
   zoff lifts everything that is drawn while it is set (used for upper floors). */
#define FH 4.0f
static int curfloor = 0;
static float zoff = 0;
static bool in_cluster(int bi) { return bi >= 4 && bi <= 6; }      /* main college north wing + west + east wing */
/* multi-floor groups: 1 = main college (3 floors), 2 = girls hostel, 3 = A block, 4 = B block (4 floors each) */
static int grp(int bi) { return in_cluster(bi) ? 1 : bi == 3 ? 2 : bi == 12 ? 3 : bi == 13 ? 4 : bi == 1 ? 5 : 0; }
static int curgrp = 0;                                   /* group the player is inside (0 = none) */
static int nfl(int g) { return g == 5 ? 3 : g ? 4 : 1; }     /* main college 4 floors, mech/civil 3, hostels 4 */
static int ofloor(int bi) { return (grp(bi) && grp(bi) == curgrp) ? curfloor : 0; }
static const char *fname(int g, int f)
{
    static const char *h[4] = {"GROUND FLOOR", "1ST FLOOR", "2ND FLOOR", "3RD FLOOR"};
    (void)g;
    return h[f];
}
static float camX = 0, camY = 0;   /* camera (follows the player) */
static float zoom = 2.0f;          /* camera zoom-in (the boy is NOT enlarged, only the camera moves closer) */          /* Z / X keys change this       */
static float gtime = 0;            /* seconds since start          */

static SDL_FColor C(float r, float g, float b) { SDL_FColor c = {r, g, b, 1.0f}; return c; }
static SDL_FColor CA(SDL_FColor c, float a)    { c.a = a; return c; }
static SDL_FColor SH(SDL_FColor c, float k)    { SDL_FColor o = {c.r*k, c.g*k, c.b*k, c.a}; return o; }
static SDL_FColor LT(SDL_FColor c, float k)    {
    SDL_FColor o = {MINF(1,c.r*k), MINF(1,c.g*k), MINF(1,c.b*k), c.a}; return o;
}

static unsigned seed = 12345u;
static float rnd(void) { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / 16777216.0f; }

/* map position (x,y) + height z (metres) -> screen position */
static SDL_FPoint iso(float x, float y, float z)
{
    SDL_FPoint p = {(x - y) * SX - camX, (x + y) * SY - (z + zoff) * ZS - camY};
    return p;
}

/* Is a map rectangle (with height h) inside the visible screen? */
static bool vis(float x0, float y0, float x1, float y1, float h)
{
    float VW = SCREEN_W / zoom, VH = SCREEN_H / zoom, m = 40;
    float left  = iso(x0, y1, 0).x, right  = iso(x1, y0, 0).x;
    float top   = iso(x0, y0, h).y, bottom = iso(x1, y1, 0).y;
    return right > -m && left < VW + m && bottom > -m && top < VH + m;
}

/* Filled convex polygon (screen coordinates) */
static void poly(const SDL_FPoint *p, int n, SDL_FColor c)
{
    SDL_Vertex v[64];
    int idx[192], k = 0;
    if (n < 3 || n > 64) return;
    for (int i = 0; i < n; i++) {
        v[i].position = p[i];
        v[i].color = c;
        v[i].tex_coord = (SDL_FPoint){0, 0};
    }
    for (int i = 1; i < n - 1; i++) { idx[k++] = 0; idx[k++] = i; idx[k++] = i + 1; }
    SDL_RenderGeometry(R, NULL, v, n, idx, k);
}

static void scircle(float cx, float cy, float r, SDL_FColor c, int seg)
{
    SDL_FPoint p[32];
    if (seg > 32) seg = 32;
    for (int i = 0; i < seg; i++) {
        float a = 2 * PI * i / seg;
        p[i] = (SDL_FPoint){cx + cosf(a) * r, cy + sinf(a) * r};
    }
    poly(p, seg, c);
}

static void sell(float cx, float cy, float rx, float ry, SDL_FColor c)
{
    SDL_FPoint p[16];
    for (int i = 0; i < 16; i++) {
        float a = 2 * PI * i / 16;
        p[i] = (SDL_FPoint){cx + cosf(a) * rx, cy + sinf(a) * ry};
    }
    poly(p, 16, c);
}

/* thick line in screen space */
static void sline(float x1, float y1, float x2, float y2, float w, SDL_FColor c)
{
    float dx = x2 - x1, dy = y2 - y1, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;
    float nx = -dy / len * w / 2, ny = dx / len * w / 2;
    SDL_FPoint p[4] = {{x1 + nx, y1 + ny}, {x2 + nx, y2 + ny}, {x2 - nx, y2 - ny}, {x1 - nx, y1 - ny}};
    poly(p, 4, c);
}

/* flat rectangle on the ground (or at height z) */
static void grect(float x0, float y0, float x1, float y1, float z, SDL_FColor c)
{
    SDL_FPoint p[4] = {iso(x0, y0, z), iso(x1, y0, z), iso(x1, y1, z), iso(x0, y1, z)};
    poly(p, 4, c);
}

/* flat oval on the ground */
static void goval(float cx, float cy, float rx, float ry, float z, SDL_FColor c, int seg)
{
    SDL_FPoint p[48];
    if (seg > 48) seg = 48;
    for (int i = 0; i < seg; i++) {
        float a = 2 * PI * i / seg;
        p[i] = iso(cx + cosf(a) * rx, cy + sinf(a) * ry, z);
    }
    poly(p, seg, c);
}

/* oval ring (like a running track line) */
static void gring(float cx, float cy, float rx, float ry, float w, SDL_FColor c)
{
    int seg = 40;
    for (int i = 0; i < seg; i++) {
        float a0 = 2 * PI * i / seg, a1 = 2 * PI * (i + 1) / seg;
        SDL_FPoint p[4] = {
            iso(cx + cosf(a0) * rx,       cy + sinf(a0) * ry,       0),
            iso(cx + cosf(a1) * rx,       cy + sinf(a1) * ry,       0),
            iso(cx + cosf(a1) * (rx - w), cy + sinf(a1) * (ry - w), 0),
            iso(cx + cosf(a0) * (rx - w), cy + sinf(a0) * (ry - w), 0)};
        poly(p, 4, c);
    }
}

/* white frame lines on the ground */
static void gframe(float x0, float y0, float x1, float y1, float w, SDL_FColor c)
{
    grect(x0, y0, x1, y0 + w, 0, c);
    grect(x0, y1 - w, x1, y1, 0, c);
    grect(x0, y0, x0 + w, y1, 0, c);
    grect(x1 - w, y0, x1, y1, 0, c);
}

/* vertical wall panel between two ground points */
static void vquad(float ax, float ay, float bx, float by, float z0, float z1, SDL_FColor c)
{
    SDL_FPoint p[4] = {iso(ax, ay, z0), iso(bx, by, z0), iso(bx, by, z1), iso(ax, ay, z1)};
    poly(p, 4, c);
}

/* a box: front (+y) face, side (+x) face, and top */
static void drawbox(float x0, float y0, float x1, float y1, float z0, float z1,
                    SDL_FColor top, SDL_FColor side)
{
    vquad(x0, y1, x1, y1, z0, z1, SH(side, 0.72f));
    vquad(x1, y0, x1, y1, z0, z1, SH(side, 0.92f));
    grect(x0, y0, x1, y1, z1, top);
}

/* a round tower / tank / fountain base */
static void cylinder(float cx, float cy, float rx, float ry, float z0, float z1,
                     SDL_FColor top, SDL_FColor side)
{
    int seg = 24;
    for (int i = 0; i < seg; i++) {
        float a0 = 2 * PI * i / seg, a1 = 2 * PI * (i + 1) / seg, am = (a0 + a1) / 2;
        if (cosf(am) + sinf(am) < -0.2f) continue;      /* hidden back half */
        float shade = 0.7f + 0.3f * (cosf(am) - sinf(am) * 0.4f);
        vquad(cx + cosf(a0) * rx, cy + sinf(a0) * ry, cx + cosf(a1) * rx, cy + sinf(a1) * ry,
              z0, z1, SH(side, MAXF(0.55f, MINF(1.0f, shade))));
    }
    goval(cx, cy, rx, ry, z1, top, seg);
}

/* ------------------------------------------------------------------ */
/* 2. Campus data                                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    float x0, y0, x1, y1;
    float z0, h;
    SDL_FColor top, side;
    int style;
    bool solid;
} Box;

enum { S_PLAIN, S_HEDGE, S_WALL, S_TANK, S_LAMP, S_HOOP, S_NET, S_STAGE, S_CAR, S_SIGN, S_BED, S_BENCH, S_POST };

static Box boxes[700];
static int nbox = 0;

static void add_box(float x0, float y0, float x1, float y1, float z0, float h,
                    SDL_FColor top, SDL_FColor side, int style, bool solid)
{
    float lx = x1 - x0, ly = y1 - y0;
    if ((style == S_HEDGE || style == S_WALL || style == S_BED) && (lx > 4.5f || ly > 4.5f)) {
        bool alongx = lx >= ly;
        float len = alongx ? lx : ly;
        int n = (int)ceilf(len / 4.0f);
        for (int i = 0; i < n; i++) {
            float a = len * i / n, e = len * (i + 1) / n;
            if (alongx) add_box(x0 + a, y0, x0 + e, y1, z0, h, top, side, style, solid);
            else add_box(x0, y0 + a, x1, y0 + e, z0, h, top, side, style, solid);
        }
        return;
    }
    if (nbox >= 700) return;
    boxes[nbox++] = (Box){x0, y0, x1, y1, z0, h, top, side, style, solid};
}

typedef struct {
    const char *name;
    float x0, y0, x1, y1, h;
    SDL_FColor roof, wall;
    int kind;                 /* what is inside (see enum below) */
    float doorx, dw;          /* door position and width on the south wall */
    float f;                  /* 0 = outside view, 1 = see-inside view      */
} Bld;

enum { K_CLASS, K_MESS, K_HOSTEL, K_GYM, K_KITCHEN, K_HALL, K_STORE };

#define NB 16
static Bld blds[NB] = {
    {"COLISEUM",           26.7,  5.3, 36.5, 18.3, 10, {0.80,0.80,0.88,1}, {0.70,0.72,0.82,1}, K_HALL},
    {"MECH / CIVIL BLOCK", 57.5,  5.8, 84.8, 17.0, 12, {0.66,0.72,0.82,1}, {0.58,0.65,0.78,1}, K_CLASS},
    {"KITCHEN",             8.2, 23.0, 19.0, 32.2,  6, {0.78,0.55,0.42,1}, {0.86,0.66,0.50,1}, K_KITCHEN},
    {"GIRLS HOSTEL",       19.0, 23.0, 36.5, 32.2, 16, {0.88,0.72,0.74,1}, {0.90,0.78,0.78,1}, K_HOSTEL},
    {"MAIN COLLEGE",       42.8, 23.6, 85.8, 35.2, 16, {0.72,0.76,0.88,1}, {0.90,0.86,0.76,1}, K_CLASS},
    {"MAIN COLLEGE W",     42.8, 35.2, 54.8, 50.5, 16, {0.72,0.76,0.88,1}, {0.90,0.86,0.76,1}, K_CLASS},
    {"MAIN COLLEGE E",     72.6, 35.2, 85.8, 50.5, 16, {0.72,0.76,0.88,1}, {0.90,0.86,0.76,1}, K_CLASS},
    {"GYM",                 8.3, 36.0, 12.3, 48.7,  7, {0.45,0.50,0.58,1}, {0.62,0.66,0.72,1}, K_GYM},
    {"CONVENTION HALL",    12.3, 40.3, 19.8, 48.7,  9, {0.78,0.74,0.88,1}, {0.84,0.80,0.92,1}, K_HALL},
    {"MESS 1",             19.8, 40.3, 30.5, 48.7,  6, {0.86,0.72,0.46,1}, {0.93,0.82,0.62,1}, K_MESS},
    {"MESS 2",             30.5, 40.3, 36.7, 48.7,  6, {0.86,0.72,0.46,1}, {0.93,0.82,0.62,1}, K_MESS},
    {"FOOD COURT",          8.5, 55.8, 17.1, 65.7,  5, {0.80,0.62,0.40,1}, {0.84,0.74,0.52,1}, K_MESS},
    {"A BLOCK HOSTEL",     44.5, 95.0, 61.8,104.3, 16, {0.78,0.70,0.86,1}, {0.90,0.80,0.82,1}, K_HOSTEL},
    {"B BLOCK HOSTEL",     64.6, 95.0, 82.8,104.3, 16, {0.70,0.80,0.86,1}, {0.82,0.88,0.90,1}, K_HOSTEL},
    {"SEPARATE BLOCK",     73.8,116.3, 83.3,123.2,  6, {0.70,0.76,0.86,1}, {0.86,0.82,0.76,1}, K_STORE},
    {"STORE ROOM",         78.0,109.0, 82.5,115.5,  4, {0.62,0.60,0.58,1}, {0.78,0.74,0.70,1}, K_STORE},
};

/* Doors. side: S = south wall, N = north wall, E = east wall, W = west wall.
   c = centre of the doorway along the wall, w = width, leaf = draw a wooden door when seen from outside */
typedef struct { int bld; char side; float c, w; bool leaf; bool ground_only; } Door;
static Door doors[40];
static int ndoor = 0;
static void add_door(int bi, char side, float c, float w, bool leaf)
{
    /* outside doors of the main college exist only on the 1st floor */
    bool g = grp(bi) && (leaf || (bi == 4 && side == 'N'));
    if (ndoor < 40) doors[ndoor++] = (Door){bi, side, c, w, leaf, g};
}
static bool door_hits(int bi, char side, float a0, float a1)
{
    for (int i = 0; i < ndoor; i++)
        if (doors[i].bld == bi && doors[i].side == side && a1 > doors[i].c - doors[i].w / 2 - 0.3f && a0 < doors[i].c + doors[i].w / 2 + 0.3f)
            return true;
    return false;
}

/* Roads, paths, sand (ground rectangles) */
enum { Z_SAND, Z_PAVE, Z_ROAD, Z_ASPH, Z_COURT };
typedef struct { float x0, y0, x1, y1; int type; } Zone;
static Zone zones[] = {
    /* sand */
    { 8.3, 32.2, 36.7, 37.2, Z_SAND},
    {42.7,107.7, 71.6,122.5, Z_SAND},
    { 7.8, 94.3, 36.0,122.3, Z_SAND},
    {26.1, 69.8, 35.2, 84.0, Z_COURT},
    /* paving / paths */
    {54.8, 35.2, 72.6, 50.5, Z_PAVE},
    { 8.3, 37.2, 36.7, 40.3, Z_PAVE},
    { 8.3, 49.0, 37.0, 51.6, Z_PAVE},
    {21.7, 70.7, 25.6, 85.8, Z_PAVE},
    {31.0, 67.0, 37.0, 69.2, Z_PAVE},
    {81.8, 54.0, 85.8, 64.5, Z_PAVE},
    {42.4,105.5, 79.7,107.7, Z_PAVE},
    {62.4, 95.0, 64.4,105.5, Z_PAVE},
    {42.8, 20.6, 66.0, 22.8, Z_PAVE},      /* path: main road -> back door of main college */
    {61.5, 20.6, 66.0, 23.6, Z_PAVE},
    /* roads */
    {37.0, 23.0, 42.8,108.0, Z_ROAD},
    { 8.6, 86.0, 37.0, 90.5, Z_ROAD},
    {42.8, 51.0, 85.8, 54.0, Z_ROAD},
    /* car parking */
    { 8.7, 72.5, 12.9, 82.2, Z_ASPH},
};
#define NZ ((int)(sizeof(zones) / sizeof(zones[0])))

static SDL_FColor zone_col(int t)
{
    switch (t) {
    case Z_SAND:  return C(0.90f, 0.78f, 0.55f);   /* warm golden sand (your white areas) */
    case Z_PAVE:  return C(0.80f, 0.78f, 0.74f);
    case Z_ROAD:  return C(0.45f, 0.47f, 0.52f);
    case Z_ASPH:  return C(0.36f, 0.37f, 0.40f);
    default:      return C(0.78f, 0.42f, 0.30f);   /* basketball court */
    }
}

/* Names shown on the HUD */
typedef struct { const char *name; float x0, y0, x1, y1; } Area;
static Area areas[] = {
    {"VOLLEYBALL & KABADDI GROUNDS",  7.8, 94.3, 36.0,122.3},
    {"BASKETBALL COURT",             26.1, 69.8, 35.2, 84.0},
    {"MAIN PLAYGROUND",              45.5, 57.0, 85.5, 92.5},
    {"COLLEGE COURTYARD & FOUNTAIN", 54.8, 35.2, 72.6, 50.5},
    {"CAR PARKING",                   8.7, 72.5, 12.9, 82.2},
    {"OPEN SAND GROUND",             42.7,107.7, 71.6,122.5},
    {"START / END POINT",             4.5, 86.0, 14.3, 90.5},
    {"MAIN ROAD",                    37.0, 23.0, 42.8,108.0},
};

/* trees, tufts of grass, grass patches */
typedef struct { float x, y, s, ph; int type; } Tree;
static Tree trees[420];
static int ntree = 0;
static void add_tree(float x, float y, int type, float s)
{
    if (ntree >= 420) return;
    trees[ntree++] = (Tree){x, y, s, rnd() * 6.28f, type};
}

#define NTUFT 1500
#define NPATCH 260
static float tuft[NTUFT][3];
static float patch[NPATCH][4];
static int ntuft = 0, npatch = 0;

/* fountain */
#define FX 63.7f
#define FY 43.0f
#define FR 3.3f

/* furniture inside buildings */
static int build_fl = 0;       /* floor being built (main college only) */
typedef struct { float x0, y0, x1, y1, h; SDL_FColor top, side; int owner; bool solid; float z0; int floor; } Furn;
#define MAXFURN 6500
static Furn furn[MAXFURN];
static int nfurn = 0;

/* floor rugs / carpets (flat, drawn as part of the floor so they never cover the player) */
typedef struct { int owner; float x0, y0, x1, y1; SDL_FColor c; int floor; } Rug;
static Rug rugs[300];
static int nrug = 0;
static void add_rug(int bi, float x0, float y0, float x1, float y1, SDL_FColor c)
{
    if (nrug < 300) rugs[nrug++] = (Rug){bi, x0, y0, x1, y1, c, build_fl};
}

/* named rooms (shown on the HUD and as small signs when you are inside) */
typedef struct { int bld; float x0, y0, x1, y1; char name[28]; int floor; } Room;
static Room rooms[300];
static int nroom = 0;
static void add_room(int bi, float x0, float y0, float x1, float y1, const char *name)
{
    if (nroom >= 300) return;
    Room *r = &rooms[nroom++];
    r->floor = build_fl;
    r->bld = bi; r->x0 = x0; r->y0 = y0; r->x1 = x1; r->y1 = y1;
    snprintf(r->name, sizeof r->name, "%s", name);
}

/* "clear zone": furniture is not placed here (keeps doorways free) */
static bool cz_on = false;
static float cz[4];

/* z0 = height where the object starts, h = height where it ends (metres) */
static void add_furn_z(int bi, float x0, float y0, float x1, float y1, float z0, float h,
                       SDL_FColor top, SDL_FColor side, bool solid)
{
    if (nfurn >= MAXFURN) return;
    if (cz_on && (solid || z0 > 0.001f) && x1 > cz[0] && x0 < cz[2] && y1 > cz[1] && y0 < cz[3]) return;
    furn[nfurn++] = (Furn){x0, y0, x1, y1, h, top, side, bi, solid, z0, build_fl};
}
static void add_furn(int bi, float x0, float y0, float x1, float y1, float h,
                     SDL_FColor top, SDL_FColor side, bool solid)
{
    add_furn_z(bi, x0, y0, x1, y1, 0, h, top, side, solid);
}

/* ------------------------------------------------------------------ */
/* 3b. Rooms, corridors and furniture                                  */
/* ------------------------------------------------------------------ */
#define IWT 0.22f      /* thickness of room partition walls            */
#define IWH 1.7f       /* height of partition walls (cut-away look)    */

enum { RM_CLASS, RM_OFFICE, RM_STAFF, RM_MECH, RM_CIVIL, RM_DRAW, RM_LIB, RM_COMP, RM_HOSTEL, RM_SCI };

static SDL_FColor wood_top(void)  { return C(0.82f, 0.66f, 0.46f); }
static SDL_FColor wood_side(void) { return C(0.55f, 0.40f, 0.27f); }
static SDL_FColor steel_top(void) { return C(0.78f, 0.80f, 0.83f); }
static SDL_FColor steel_side(void){ return C(0.50f, 0.53f, 0.57f); }

/* partition walls with door gaps (gc = gap centres, ascending) */
static void wall_h(int bi, float y, float xa, float xb, const float *gc, int ng, float gw)
{
    float x = xa;
    for (int i = 0; i < ng; i++) {
        float g0 = gc[i] - gw / 2, g1 = gc[i] + gw / 2;
        if (g0 > x + 0.05f) add_furn(bi, x, y, g0, y + IWT, IWH, C(0.97f,0.95f,0.90f), C(0.86f,0.82f,0.76f), true);
        x = g1;
    }
    if (xb > x + 0.05f) add_furn(bi, x, y, xb, y + IWT, IWH, C(0.97f,0.95f,0.90f), C(0.86f,0.82f,0.76f), true);
}
static void wall_v(int bi, float x, float ya, float yb, const float *gc, int ng, float gw)
{
    float y = ya;
    for (int i = 0; i < ng; i++) {
        float g0 = gc[i] - gw / 2, g1 = gc[i] + gw / 2;
        if (g0 > y + 0.05f) add_furn(bi, x, y, x + IWT, g0, IWH, C(0.97f,0.95f,0.90f), C(0.86f,0.82f,0.76f), true);
        y = g1;
    }
    if (yb > y + 0.05f) add_furn(bi, x, y, x + IWT, yb, IWH, C(0.97f,0.95f,0.90f), C(0.86f,0.82f,0.76f), true);
}

static void put_table(int bi, float x, float y, float w, float d, float h)
{
    add_furn(bi, x, y, x + w, y + d, h, wood_top(), wood_side(), true);
}
static void put_chair(int bi, float x, float y, SDL_FColor c)
{
    add_furn(bi, x, y, x + 0.4f, y + 0.4f, 0.45f, c, SH(c, 0.6f), false);
}
static void put_board(int bi, float x0, float x1, float y)
{
    add_furn_z(bi, x0, y, x1, y + 0.1f, 0.9f, 2.3f, C(0.92f,0.92f,0.92f), C(0.30f,0.55f,0.40f), false);
}
static void put_monitor(int bi, float x, float y)
{
    add_furn_z(bi, x, y, x + 0.5f, y + 0.12f, 0.78f, 1.2f, C(0.15f,0.17f,0.20f), C(0.10f,0.30f,0.55f), false);
}
static void put_plant(int bi, float x, float y)
{
    add_furn(bi, x, y, x + 0.4f, y + 0.4f, 0.4f, C(0.65f,0.38f,0.25f), C(0.55f,0.30f,0.20f), true);
    add_furn_z(bi, x + 0.05f, y + 0.05f, x + 0.35f, y + 0.35f, 0.4f, 1.3f, C(0.30f,0.65f,0.28f), C(0.18f,0.48f,0.20f), false);
}
static void bookshelf(int bi, float x0, float x1, float y)
{
    SDL_FColor bc[6] = {C(0.80f,0.25f,0.25f), C(0.25f,0.45f,0.80f), C(0.90f,0.75f,0.25f),
                        C(0.30f,0.65f,0.40f), C(0.65f,0.35f,0.70f), C(0.92f,0.55f,0.25f)};
    add_furn(bi, x0, y, x1, y + 0.5f, 0.25f, C(0.60f,0.42f,0.28f), C(0.45f,0.32f,0.22f), true);
    int n = 0;
    for (float x = x0 + 0.05f; x + 0.28f < x1; x += 0.31f, n++)
        add_furn_z(bi, x, y + 0.05f, x + 0.28f, y + 0.45f, 0.25f, 1.3f + (n % 4) * 0.25f,
                   LT(bc[n % 6], 1.1f), bc[(n + 2) % 6], false);
}

static void furnish_class(int bi, float x0, float y0, float x1, float y1)
{
    float w = x1 - x0, d = y1 - y0;
    put_board(bi, x0 + 0.4f, x1 - 0.4f, y0);
    put_table(bi, (x0 + x1) / 2 - 0.8f, y0 + 0.45f, 1.6f, 0.7f, 0.92f);          /* teacher's desk */
    int cols = (int)((w - 0.6f) / 1.55f); if (cols < 1) cols = 1;
    float pitch = (w - 0.6f) / cols;
    int rows = (int)((d - 2.4f) / 1.3f); if (rows < 1) rows = 1;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            float x = x0 + 0.3f + c * pitch + (pitch - 1.1f) / 2, y = y0 + 1.9f + r * 1.3f;
            put_table(bi, x, y, 1.1f, 0.55f, 0.78f);
            put_chair(bi, x + 0.35f, y + 0.62f, C(0.25f, 0.45f, 0.80f));
        }
}

static void furnish_office(int bi, float x0, float y0, float x1, float y1)
{
    float cx = (x0 + x1) / 2, w = x1 - x0;
    add_furn(bi, x0 + 0.3f, y0 + 0.1f, x1 - 0.3f, y0 + 0.6f, 1.8f, C(0.58f,0.62f,0.66f), C(0.42f,0.46f,0.52f), true);
    put_table(bi, cx - 0.9f, y0 + 1.6f, 1.8f, 0.8f, 0.8f);
    add_furn_z(bi, cx - 0.25f, y0 + 1.75f, cx + 0.25f, y0 + 1.9f, 0.8f, 1.2f, C(0.15f,0.17f,0.20f), C(0.12f,0.35f,0.60f), false);
    put_chair(bi, cx - 0.2f, y0 + 2.5f, C(0.20f,0.20f,0.24f));
    put_chair(bi, cx - 0.9f, y0 + 3.2f, C(0.70f,0.35f,0.25f));
    put_chair(bi, cx + 0.5f, y0 + 3.2f, C(0.70f,0.35f,0.25f));
    if (w > 5.0f) add_furn(bi, x0 + 0.2f, y0 + 3.0f, x0 + 0.9f, y0 + 4.6f, 0.5f, C(0.30f,0.40f,0.55f), C(0.22f,0.30f,0.42f), true); /* sofa */
    put_plant(bi, x1 - 0.7f, y0 + 0.9f);
    add_rug(bi, cx - 1.4f, y0 + 1.2f, cx + 1.4f, y0 + 3.9f, C(0.55f,0.30f,0.30f));
}

static void furnish_staff(int bi, float x0, float y0, float x1, float y1)
{
    float w = x1 - x0, d = y1 - y0;
    int cols = (int)((w - 0.4f) / 2.0f); if (cols < 1) cols = 1;
    float pitch = (w - 0.4f) / cols;
    bookshelf(bi, x0 + 0.3f, x1 - 0.3f, y0 + 0.1f);
    for (int r = 0; r < (d > 6 ? 2 : 1); r++)
        for (int c = 0; c < cols; c++) {
            float x = x0 + 0.2f + c * pitch + (pitch - 1.3f) / 2, y = y0 + 1.5f + r * 2.2f;
            put_table(bi, x, y, 1.3f, 0.7f, 0.78f);
            add_furn_z(bi, x + 0.4f, y + 0.1f, x + 0.85f, y + 0.22f, 0.78f, 1.15f, C(0.15f,0.17f,0.20f), C(0.12f,0.35f,0.60f), false);
            put_chair(bi, x + 0.45f, y + 0.8f, C(0.35f,0.35f,0.40f));
        }
}

static void furnish_lib(int bi, float x0, float y0, float x1, float y1)
{
    bookshelf(bi, x0 + 0.3f, x1 - 0.3f, y0 + 0.1f);
    add_furn(bi, x0 + 0.2f, y0 + 0.8f, x0 + 0.7f, y1 - 1.9f, 0.25f, C(0.60f,0.42f,0.28f), C(0.45f,0.32f,0.22f), true);
    for (float y = y0 + 1.0f; y + 0.45f < y1 - 1.9f; y += 0.36f)
        add_furn_z(bi, x0 + 0.25f, y, x0 + 0.65f, y + 0.3f, 0.25f, 1.3f + ((int)(y * 7) % 4) * 0.2f,
                   ((int)(y * 7) % 2) ? C(0.30f,0.50f,0.80f) : C(0.85f,0.40f,0.30f), C(0.5f,0.3f,0.3f), false);
    put_table(bi, (x0 + x1) / 2 - 0.2f, y0 + 1.6f, 1.6f, 0.9f, 0.8f);
    put_chair(bi, (x0 + x1) / 2 - 0.1f, y0 + 2.6f, C(0.55f,0.35f,0.20f));
    put_chair(bi, (x0 + x1) / 2 + 0.7f, y0 + 2.6f, C(0.55f,0.35f,0.20f));
}

static void furnish_comp(int bi, float x0, float y0, float x1, float y1)
{
    for (float y = y0 + 0.5f; y + 1.4f <= y1 - 1.6f; y += 1.9f) {
        add_furn(bi, x0 + 0.3f, y, x1 - 0.3f, y + 0.7f, 0.75f, C(0.90f,0.90f,0.92f), C(0.60f,0.62f,0.68f), true);
        for (float x = x0 + 0.5f; x + 0.55f < x1 - 0.3f; x += 1.05f) {
            put_monitor(bi, x, y + 0.2f);
            put_chair(bi, x + 0.05f, y + 0.85f, C(0.20f,0.22f,0.30f));
        }
    }
    add_rug(bi, x0 + 0.2f, y0 + 0.3f, x1 - 0.2f, y1 - 0.2f, C(0.72f,0.78f,0.88f));
}

static void furnish_mech(int bi, float x0, float y0, float x1, float y1)
{
    float w = x1 - x0, d = y1 - y0;
    add_furn(bi, x0 + 0.2f, y0 + 0.1f, x0 + 1.0f, y0 + 0.9f, 1.7f, C(0.80f,0.22f,0.20f), C(0.60f,0.15f,0.14f), true); /* tool cabinet */
    add_furn(bi, x1 - 1.0f, y0 + 0.1f, x1 - 0.2f, y0 + 0.9f, 1.7f, C(0.80f,0.22f,0.20f), C(0.60f,0.15f,0.14f), true);
    int rows = (int)((d - 2.6f) / 2.2f); if (rows < 1) rows = 1;
    for (int r = 0; r < rows; r++) {
        float y = y0 + 1.4f + r * 2.2f;
        add_furn(bi, x0 + 0.3f, y, x1 - 0.3f, y + 0.9f, 0.9f, steel_top(), steel_side(), true);
        for (float x = x0 + 0.5f; x + 1.3f < x1 - 0.3f; x += 1.55f) {
            add_furn_z(bi, x, y + 0.1f, x + 1.3f, y + 0.8f, 0.9f, 1.5f, C(0.30f,0.45f,0.72f), C(0.20f,0.32f,0.52f), false);
            add_furn_z(bi, x + 0.1f, y + 0.3f, x + 0.4f, y + 0.6f, 1.5f, 1.8f, C(0.82f,0.82f,0.85f), C(0.60f,0.60f,0.64f), false);
        }
    }
    (void)w;
}

static void furnish_civil(int bi, float x0, float y0, float x1, float y1)
{
    SDL_FColor pc[4] = {C(0.95f,0.85f,0.70f), C(0.85f,0.90f,0.95f), C(0.95f,0.80f,0.80f), C(0.80f,0.92f,0.80f)};
    float d = y1 - y0;
    int rows = (int)((d - 2.6f) / 2.2f); if (rows < 1) rows = 1;
    for (int r = 0; r < rows; r++) {
        float y = y0 + 1.2f + r * 2.2f;
        add_furn(bi, x0 + 0.3f, y, x1 - 0.3f, y + 0.9f, 0.9f, wood_top(), wood_side(), true);
        int n = 0;
        for (float x = x0 + 0.5f; x + 0.6f < x1 - 0.3f; x += 0.85f, n++)           /* scale models of buildings */
            add_furn_z(bi, x, y + 0.15f, x + 0.6f, y + 0.75f, 0.9f, 1.15f + (n % 3) * 0.3f, pc[n % 4], SH(pc[n % 4], 0.8f), false);
    }
    add_furn_z(bi, x1 - 0.7f, y0 + 0.2f, x1 - 0.55f, y0 + 0.35f, 0, 1.5f, C(0.25f,0.25f,0.28f), C(0.25f,0.25f,0.28f), false);  /* survey tripod */
    add_furn_z(bi, x1 - 0.8f, y0 + 0.15f, x1 - 0.45f, y0 + 0.4f, 1.5f, 1.7f, C(0.90f,0.70f,0.15f), C(0.80f,0.60f,0.10f), false);
    add_furn_z(bi, x0 + 0.5f, y0 + 0.2f, x0 + 0.65f, y0 + 0.35f, 0, 1.5f, C(0.25f,0.25f,0.28f), C(0.25f,0.25f,0.28f), false);
    add_furn_z(bi, x0 + 0.4f, y0 + 0.15f, x0 + 0.75f, y0 + 0.4f, 1.5f, 1.7f, C(0.90f,0.70f,0.15f), C(0.80f,0.60f,0.10f), false);
}

static void furnish_sci(int bi, float x0, float y0, float x1, float y1)
{
    SDL_FColor fc[4] = {C(0.30f,0.70f,0.90f), C(0.90f,0.40f,0.35f), C(0.50f,0.85f,0.45f), C(0.95f,0.80f,0.30f)};
    float d = y1 - y0;
    int rows = (int)((d - 2.2f) / 1.9f); if (rows < 1) rows = 1;
    for (int r = 0; r < rows; r++) {
        float y = y0 + 1.0f + r * 1.9f;
        add_furn(bi, x0 + 0.3f, y, x1 - 0.3f, y + 0.9f, 0.9f, C(0.30f,0.34f,0.38f), C(0.20f,0.22f,0.26f), true);
        int n = 0;
        for (float x = x0 + 0.5f; x + 0.3f < x1 - 0.3f; x += 0.7f, n++)
            add_furn_z(bi, x, y + 0.3f, x + 0.3f, y + 0.6f, 0.9f, 1.25f, fc[n % 4], SH(fc[n % 4], 0.8f), false);   /* flasks */
    }
    add_furn(bi, x0 + 0.2f, y0 + 0.1f, x1 - 0.2f, y0 + 0.6f, 1.8f, C(0.60f,0.45f,0.32f), C(0.45f,0.33f,0.24f), true);
}

static void furnish_draw(int bi, float x0, float y0, float x1, float y1)
{
    put_board(bi, x0 + 0.4f, x1 - 0.4f, y0);
    int cols = (int)((x1 - x0 - 0.6f) / 1.5f); if (cols < 1) cols = 1;
    float pitch = (x1 - x0 - 0.6f) / cols;
    int rows = (int)((y1 - y0 - 2.4f) / 1.4f); if (rows < 1) rows = 1;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            float x = x0 + 0.3f + c * pitch + (pitch - 1.1f) / 2, y = y0 + 1.5f + r * 1.4f;
            add_furn(bi, x, y, x + 1.1f, y + 0.8f, 0.95f, C(0.95f,0.95f,0.90f), C(0.55f,0.50f,0.42f), true);   /* drawing board */
            put_chair(bi, x + 0.35f, y + 0.9f, C(0.60f,0.35f,0.20f));
        }
}

static void furnish_hostel_room(int bi, float x0, float y0, float x1, float y1)
{
    (void)y1;
    SDL_FColor frame = C(0.50f,0.36f,0.24f);
    add_furn(bi, x0 + 0.1f, y0 + 0.15f, x0 + 0.95f, y0 + 2.15f, 0.5f, C(0.45f,0.62f,0.92f), frame, true);
    add_furn_z(bi, x0 + 0.13f, y0 + 0.2f, x0 + 0.92f, y0 + 0.6f, 0.5f, 0.62f, C(1,1,1), C(0.92f,0.92f,0.96f), false);
    add_furn(bi, x1 - 0.95f, y0 + 0.15f, x1 - 0.1f, y0 + 2.15f, 0.5f, C(0.92f,0.55f,0.65f), frame, true);
    add_furn_z(bi, x1 - 0.92f, y0 + 0.2f, x1 - 0.13f, y0 + 0.6f, 0.5f, 0.62f, C(1,1,1), C(0.92f,0.92f,0.96f), false);
    put_table(bi, x0 + 0.1f, y0 + 2.6f, 0.6f, 1.2f, 0.75f);                          /* study table */
    add_furn(bi, x1 - 0.7f, y0 + 2.6f, x1 - 0.1f, y0 + 3.6f, 1.8f, C(0.60f,0.66f,0.78f), C(0.42f,0.48f,0.62f), true); /* cupboard */
    add_rug(bi, x0 + 0.5f, y0 + 2.3f, x1 - 0.5f, y0 + 4.3f, C(0.82f,0.60f,0.70f));
}

static void furnish_room(int bi, int type, float x0, float y0, float x1, float y1)
{
    switch (type) {
    case RM_OFFICE: furnish_office(bi, x0, y0, x1, y1); break;
    case RM_STAFF:  furnish_staff(bi, x0, y0, x1, y1);  break;
    case RM_MECH:   furnish_mech(bi, x0, y0, x1, y1);   break;
    case RM_CIVIL:  furnish_civil(bi, x0, y0, x1, y1);  break;
    case RM_DRAW:   furnish_draw(bi, x0, y0, x1, y1);   break;
    case RM_LIB:    furnish_lib(bi, x0, y0, x1, y1);    break;
    case RM_COMP:   furnish_comp(bi, x0, y0, x1, y1);   break;
    case RM_SCI:    furnish_sci(bi, x0, y0, x1, y1);    break;
    case RM_HOSTEL: furnish_hostel_room(bi, x0, y0, x1, y1); break;
    default:        furnish_class(bi, x0, y0, x1, y1);  break;
    }
}

/* a row of rooms along the north side, a corridor along the south side, one door per room */
static void south_rooms(int bi, const float *xb, int n, float cor_d, const char * const *names, const int *types)
{
    Bld *b = &blds[bi];
    float iy0 = b->y0 + WALL_T, iy1 = b->y1 - WALL_T, wy = iy1 - cor_d;
    float gc[16];
    for (int i = 0; i < n && i < 16; i++) {
        float rx0 = xb[i] + (i > 0 ? IWT : 0), rx1 = xb[i + 1], cx = (rx0 + rx1) / 2;
        gc[i] = cx;
        if (i > 0) wall_v(bi, xb[i], iy0, wy + IWT, NULL, 0, 0);
        add_room(bi, rx0, iy0, rx1, wy, names[i]);
        cz_on = true; cz[0] = cx - 0.9f; cz[1] = wy - 1.7f; cz[2] = cx + 0.9f; cz[3] = wy;
        furnish_room(bi, types[i], rx0, iy0, rx1, wy);
        cz_on = false;
    }
    wall_h(bi, wy, xb[0], xb[n], gc, n, 1.5f);
}

/* a wing: corridor down the middle, 3 rooms on each side, doors facing the corridor */
static void wing_rooms(int bi, float cx, const char * const *wn, const int *wt,
                       const char * const *en, const int *et)
{
    Bld *b = &blds[bi];
    float ix0 = b->x0 + WALL_T, ix1 = b->x1 - WALL_T, iy0 = b->y0 + WALL_T, iy1 = b->y1 - WALL_T;
    float lw = cx - 1.0f - IWT, re = cx + 1.0f;
    float dep = (iy1 - iy0) / 3, ry[4], gy[3];
    for (int k = 0; k < 4; k++) ry[k] = iy0 + dep * k;
    for (int k = 0; k < 3; k++) gy[k] = (ry[k] + ry[k + 1]) / 2;
    wall_v(bi, lw, iy0, iy1, gy, 3, 1.4f);
    wall_v(bi, re, iy0, iy1, gy, 3, 1.4f);
    for (int k = 1; k < 3; k++) { wall_h(bi, ry[k], ix0, lw, NULL, 0, 0); wall_h(bi, ry[k], re + IWT, ix1, NULL, 0, 0); }
    add_rug(bi, cx - 0.5f, iy0, cx + 0.5f, iy1, C(0.55f,0.25f,0.28f));
    for (int k = 0; k < 3; k++) {
        float y0 = ry[k] + (k > 0 ? IWT : 0), y1 = ry[k + 1];
        add_room(bi, ix0, y0, lw, y1, wn[k]);
        cz_on = true; cz[0] = lw - 1.7f; cz[1] = gy[k] - 0.9f; cz[2] = lw; cz[3] = gy[k] + 0.9f;
        furnish_room(bi, wt[k], ix0, y0, lw, y1);
        add_room(bi, re + IWT, y0, ix1, y1, en[k]);
        cz[0] = re + IWT; cz[2] = re + IWT + 1.7f;
        furnish_room(bi, et[k], re + IWT, y0, ix1, y1);
        cz_on = false;
    }
}

/* ---- dining / serving ---- */
static void serve_counter(int bi, float x0, float x1, float y, int variant)
{
    SDL_FColor foods[5] = {C(0.96f,0.95f,0.88f), C(0.62f,0.38f,0.16f), C(0.92f,0.50f,0.12f), C(0.35f,0.62f,0.25f), C(0.85f,0.22f,0.18f)};
    add_furn(bi, x0, y, x1, y + 0.9f, 1.0f, steel_top(), steel_side(), true);
    int n = 0;
    for (float x = x0 + 0.2f; x + 0.7f < x1; x += 0.95f, n++) {
        SDL_FColor fc = foods[(n + variant) % 5];
        add_furn_z(bi, x, y + 0.12f, x + 0.7f, y + 0.78f, 1.0f, 1.25f, C(0.72f,0.74f,0.77f), C(0.55f,0.57f,0.60f), false);
        add_furn_z(bi, x + 0.08f, y + 0.2f, x + 0.62f, y + 0.7f, 1.25f, 1.29f, fc, SH(fc, 0.8f), false);
    }
}

static void dining_table(int bi, float x, float y, float w)
{
    SDL_FColor bench = C(0.62f,0.45f,0.30f), bs = C(0.45f,0.32f,0.22f);
    add_furn(bi, x, y, x + w, y + 0.4f, 0.45f, bench, bs, true);
    add_furn(bi, x, y + 0.5f, x + w, y + 1.3f, 0.8f, C(0.90f,0.84f,0.72f), bs, true);
    add_furn(bi, x, y + 1.4f, x + w, y + 1.8f, 0.45f, bench, bs, true);
    for (float px = x + 0.15f; px + 0.32f < x + w; px += 0.62f) {
        add_furn_z(bi, px, y + 0.58f, px + 0.32f, y + 0.88f, 0.8f, 0.84f, C(1,1,1), C(0.85f,0.85f,0.9f), false);
        add_furn_z(bi, px + 0.06f, y + 0.64f, px + 0.26f, y + 0.82f, 0.84f, 0.88f, C(0.92f,0.55f,0.15f), C(0.75f,0.40f,0.10f), false);
        add_furn_z(bi, px, y + 0.92f, px + 0.32f, y + 1.22f, 0.8f, 0.84f, C(1,1,1), C(0.85f,0.85f,0.9f), false);
        add_furn_z(bi, px + 0.06f, y + 0.98f, px + 0.26f, y + 1.16f, 0.84f, 0.88f, C(0.96f,0.95f,0.88f), C(0.8f,0.8f,0.7f), false);
    }
}

/* long table running north-south (for the narrow Mess 2 hall) */
static void dining_table_v(int bi, float x, float y, float len)
{
    SDL_FColor bench = C(0.62f,0.45f,0.30f), bs = C(0.45f,0.32f,0.22f);
    add_furn(bi, x, y, x + 0.4f, y + len, 0.45f, bench, bs, true);
    add_furn(bi, x + 0.5f, y, x + 1.3f, y + len, 0.8f, C(0.90f,0.84f,0.72f), bs, true);
    add_furn(bi, x + 1.4f, y, x + 1.8f, y + len, 0.45f, bench, bs, true);
    for (float py = y + 0.15f; py + 0.32f < y + len; py += 0.62f) {
        add_furn_z(bi, x + 0.58f, py, x + 0.88f, py + 0.32f, 0.8f, 0.84f, C(1,1,1), C(0.85f,0.85f,0.9f), false);
        add_furn_z(bi, x + 0.64f, py + 0.06f, x + 0.82f, py + 0.26f, 0.84f, 0.88f, C(0.92f,0.55f,0.15f), C(0.75f,0.40f,0.10f), false);
        add_furn_z(bi, x + 0.92f, py, x + 1.22f, py + 0.32f, 0.8f, 0.84f, C(1,1,1), C(0.85f,0.85f,0.9f), false);
        add_furn_z(bi, x + 0.98f, py + 0.06f, x + 1.16f, py + 0.26f, 0.84f, 0.88f, C(0.96f,0.95f,0.88f), C(0.8f,0.8f,0.7f), false);
    }
}

static void dining_area(int bi, float x0, float y0, float x1, float y1, float ax, float tw)
{
    for (float y = y0; y + 1.8f <= y1; y += 2.5f) {
        for (float x = x0; x + tw <= ax - 0.9f; x += tw + 0.35f) dining_table(bi, x, y, tw);
        for (float x = x1 - tw; x >= ax + 0.9f; x -= tw + 0.35f) dining_table(bi, x, y, tw);
    }
}

static void furnish_hall(int bi, float ix0, float iy0, float ix1, float iy1)
{
    float cx = (ix0 + ix1) / 2;
    add_furn(bi, ix0 + 0.3f, iy0 + 0.2f, ix1 - 0.3f, iy0 + 2.2f, 0.6f, C(0.72f,0.47f,0.32f), C(0.50f,0.32f,0.22f), true);   /* stage */
    add_furn_z(bi, cx - 0.35f, iy0 + 0.8f, cx + 0.35f, iy0 + 1.3f, 0.6f, 1.6f, C(0.60f,0.40f,0.28f), C(0.45f,0.30f,0.20f), false); /* podium */
    add_furn_z(bi, ix0 + 0.3f, iy0 + 0.2f, ix0 + 1.1f, iy0 + 0.5f, 0.6f, 3.2f, C(0.65f,0.12f,0.18f), C(0.55f,0.10f,0.15f), false);
    add_furn_z(bi, ix1 - 1.1f, iy0 + 0.2f, ix1 - 0.3f, iy0 + 0.5f, 0.6f, 3.2f, C(0.65f,0.12f,0.18f), C(0.55f,0.10f,0.15f), false);
    add_rug(bi, cx - 0.8f, iy0 + 2.2f, cx + 0.8f, iy1, C(0.62f,0.20f,0.24f));
    for (float y = iy0 + 3.2f; y + 0.6f < iy1 - 1.2f; y += 1.0f) {
        for (float x = ix0 + 0.4f; x + 0.5f <= cx - 0.9f; x += 0.62f) {
            add_furn(bi, x, y, x + 0.5f, y + 0.5f, 0.5f, C(0.78f,0.22f,0.30f), C(0.50f,0.12f,0.18f), true);
            add_furn_z(bi, x, y, x + 0.5f, y + 0.1f, 0.5f, 1.0f, C(0.78f,0.22f,0.30f), C(0.55f,0.14f,0.20f), false);
        }
        for (float x = ix1 - 0.9f; x >= cx + 0.9f; x -= 0.62f) {
            add_furn(bi, x, y, x + 0.5f, y + 0.5f, 0.5f, C(0.78f,0.22f,0.30f), C(0.50f,0.12f,0.18f), true);
            add_furn_z(bi, x, y, x + 0.5f, y + 0.1f, 0.5f, 1.0f, C(0.78f,0.22f,0.30f), C(0.55f,0.14f,0.20f), false);
        }
    }
}

static void furnish_gym(int bi, float ix0, float iy0, float ix1, float iy1)
{
    SDL_FColor dk = C(0.20f,0.22f,0.25f), dks = C(0.12f,0.13f,0.15f);
    float y;
    /* dumbbell rack at the back wall */
    add_furn(bi, ix0 + 0.1f, iy0 + 0.1f, ix1 - 0.1f, iy0 + 0.7f, 0.9f, dk, dks, true);
    for (int i = 0; i < 6; i++)
        add_furn_z(bi, ix0 + 0.2f + i * 0.4f, iy0 + 0.2f, ix0 + 0.5f + i * 0.4f, iy0 + 0.6f, 0.9f, 1.1f,
                   (i % 2) ? C(0.85f,0.20f,0.20f) : C(0.25f,0.45f,0.85f), dks, false);
    /* west wall: treadmills */
    for (y = iy0 + 1.2f; y + 1.7f < iy1 - 1.5f && y < iy0 + 5.5f; y += 1.9f) {
        add_furn(bi, ix0 + 0.05f, y, ix0 + 0.85f, y + 1.6f, 0.3f, C(0.30f,0.32f,0.36f), dks, true);
        add_furn_z(bi, ix0 + 0.1f, y + 0.1f, ix0 + 0.8f, y + 1.5f, 0.3f, 0.34f, C(0.12f,0.12f,0.14f), dks, false);
        add_furn_z(bi, ix0 + 0.05f, y, ix0 + 0.85f, y + 0.2f, 0.3f, 1.35f, C(0.18f,0.20f,0.24f), C(0.10f,0.30f,0.50f), false);
    }
    /* west wall, further south: exercise bikes */
    for (; y + 1.1f < iy1 - 1.5f; y += 1.5f)
        add_furn(bi, ix0 + 0.1f, y, ix0 + 0.7f, y + 1.0f, 1.1f, C(0.80f,0.25f,0.20f), C(0.50f,0.15f,0.12f), true);
    /* east wall: bench press, squat rack, cable machine */
    float by = iy0 + 1.2f;
    add_furn(bi, ix1 - 0.85f, by, ix1 - 0.1f, by + 1.5f, 0.45f, C(0.12f,0.12f,0.14f), dks, true);
    add_furn_z(bi, ix1 - 0.95f, by + 0.25f, ix1, by + 0.4f, 1.05f, 1.15f, C(0.85f,0.86f,0.88f), C(0.6f,0.6f,0.65f), false);
    add_furn_z(bi, ix1 - 0.95f, by + 0.15f, ix1, by + 0.25f, 0.6f, 1.45f, C(0.15f,0.15f,0.18f), dks, false);
    add_furn_z(bi, ix1 - 0.95f, by + 0.4f, ix1, by + 0.5f, 0.6f, 1.45f, C(0.15f,0.15f,0.18f), dks, false);
    by += 2.4f;
    add_furn(bi, ix1 - 0.85f, by, ix1 - 0.1f, by + 0.1f, 2.0f, C(0.70f,0.70f,0.74f), C(0.45f,0.45f,0.50f), true);
    add_furn(bi, ix1 - 0.85f, by + 1.0f, ix1 - 0.1f, by + 1.1f, 2.0f, C(0.70f,0.70f,0.74f), C(0.45f,0.45f,0.50f), true);
    add_furn_z(bi, ix1 - 0.95f, by, ix1, by + 1.1f, 1.5f, 1.58f, C(0.85f,0.86f,0.88f), C(0.6f,0.6f,0.65f), false);
    by += 2.0f;
    add_furn(bi, ix1 - 0.85f, by, ix1 - 0.1f, by + 0.8f, 2.2f, C(0.75f,0.76f,0.80f), C(0.45f,0.46f,0.52f), true);
    add_furn_z(bi, ix1 - 0.8f, by + 0.1f, ix1 - 0.15f, by + 0.7f, 1.2f, 1.9f, C(0.85f,0.20f,0.20f), C(0.60f,0.12f,0.12f), false);
    by += 1.4f;
    add_furn(bi, ix1 - 0.85f, by, ix1 - 0.1f, by + 1.2f, 0.4f, C(0.25f,0.25f,0.30f), dks, true);                     /* leg press */
    add_rug(bi, ix0 + 0.9f, iy0 + 1.0f, ix1 - 0.9f, iy1 - 1.8f, C(0.25f,0.45f,0.80f));
}

static void furnish_kitchen(int bi, float ix0, float iy0, float ix1, float iy1)
{
    (void)iy1;
    add_furn(bi, ix0 + 0.3f, iy0 + 0.1f, ix1 - 0.3f, iy0 + 1.0f, 0.9f, steel_top(), steel_side(), true);
    for (int i = 0; i < 4; i++) {
        float x = ix0 + 0.6f + i * 1.7f;
        add_furn_z(bi, x, iy0 + 0.2f, x + 1.2f, iy0 + 0.9f, 0.9f, 0.98f, C(0.15f,0.16f,0.18f), C(0.10f,0.10f,0.12f), false);
        add_furn_z(bi, x + 0.15f, iy0 + 0.28f, x + 1.05f, iy0 + 0.82f, 0.98f, 1.5f, C(0.80f,0.82f,0.85f), C(0.58f,0.60f,0.64f), false);   /* big pot */
    }
    add_furn_z(bi, ix1 - 2.3f, iy0 + 0.2f, ix1 - 0.6f, iy0 + 0.9f, 0.9f, 0.95f, C(0.35f,0.58f,0.80f), C(0.25f,0.42f,0.60f), false);     /* sink */
    add_furn(bi, ix0 + 2.0f, iy0 + 3.0f, ix1 - 2.0f, iy0 + 4.2f, 0.9f, steel_top(), steel_side(), true);                                  /* prep island */
    SDL_FColor vc[4] = {C(0.35f,0.65f,0.25f), C(0.90f,0.35f,0.20f), C(0.95f,0.80f,0.25f), C(0.70f,0.30f,0.60f)};
    int n = 0;
    for (float x = ix0 + 2.3f; x + 0.5f < ix1 - 2.2f; x += 0.8f, n++)
        add_furn_z(bi, x, iy0 + 3.3f, x + 0.5f, iy0 + 3.9f, 0.9f, 1.15f, vc[n % 4], SH(vc[n % 4], 0.7f), false);
    add_furn(bi, ix0 + 0.15f, iy0 + 1.8f, ix0 + 1.15f, iy0 + 2.8f, 1.9f, C(0.92f,0.93f,0.95f), C(0.70f,0.72f,0.76f), true);              /* fridge */
    add_furn(bi, ix1 - 0.65f, iy0 + 1.8f, ix1 - 0.15f, iy0 + 5.0f, 1.9f, C(0.60f,0.42f,0.28f), C(0.45f,0.32f,0.22f), true);              /* shelves */
    for (float y = iy0 + 1.9f; y + 0.3f < iy0 + 4.9f; y += 0.4f)
        add_furn_z(bi, ix1 - 0.6f, y, ix1 - 0.2f, y + 0.3f, 1.9f, 2.2f, C(0.90f,0.80f,0.55f), C(0.70f,0.55f,0.30f), false);
    add_rug(bi, ix0 + 1.5f, iy0 + 4.6f, ix1 - 1.5f, iy0 + 5.6f, C(0.55f,0.62f,0.70f));
}

static void furnish_store(int bi, float ix0, float iy0, float ix1, float iy1)
{
    SDL_FColor cb = C(0.76f,0.60f,0.38f), cs = C(0.55f,0.40f,0.24f);
    add_furn(bi, ix0 + 0.3f, iy0 + 0.1f, ix1 - 0.3f, iy0 + 0.6f, 1.8f, C(0.62f,0.64f,0.68f), C(0.45f,0.47f,0.52f), true);
    for (float x = ix0 + 0.5f; x + 0.6f < ix1 - 0.4f; x += 0.75f)
        add_furn_z(bi, x, iy0 + 0.15f, x + 0.6f, iy0 + 0.55f, 1.8f, 2.2f, cb, cs, false);
    for (int i = 0; i < 3; i++) {
        float x = ix0 + 0.5f + i * 1.3f, y = iy0 + 1.4f;
        if (x + 1.0f > ix1 - 0.4f) break;
        add_furn(bi, x, y, x + 1.0f, y + 0.9f, 0.8f, cb, cs, true);
        add_furn_z(bi, x + 0.15f, y + 0.1f, x + 0.85f, y + 0.8f, 0.8f, 1.4f, LT(cb, 1.1f), cs, false);
    }
    if (iy1 - iy0 > 5)
        add_furn(bi, ix0 + 0.5f, iy0 + 3.2f, ix0 + 1.6f, iy0 + 4.3f, 1.0f, C(0.60f,0.62f,0.66f), C(0.40f,0.42f,0.46f), true);
}

/* ---- MAIN COLLEGE: 3 floors, each with its own rooms; stairs at both ends of the front corridor ---- */
typedef struct { float x0, y0, x1, y1; int g; } Stair;
static Stair stairs[8];
static int nstairs = 0;
static void add_stair(int g, float x0, float y0, float x1, float y1) { if (nstairs < 8) stairs[nstairs++] = (Stair){x0, y0, x1, y1, g}; }

static const char *NWN[4][6] = {
    {"PRINCIPAL OFFICE", "ADMIN OFFICE", "STAFF ROOM", "CLASS 101", "CLASS 102", "SEMINAR HALL"},
    {"CLASS 201", "CLASS 202", "CLASS 203", "CLASS 204", "CLASS 205", "STAFF ROOM 2"},
    {"CLASS 301", "CLASS 302", "CLASS 303", "DRAWING HALL", "CLASS 304", "HOD OFFICE"},
    {"CLASS 401", "CLASS 402", "CLASS 403", "SEMINAR HALL 2", "CLASS 404", "DEPT OFFICE 4"}};
static const int NWT[4][6] = {
    {RM_OFFICE, RM_OFFICE, RM_STAFF, RM_CLASS, RM_CLASS, RM_CLASS},
    {RM_CLASS, RM_CLASS, RM_CLASS, RM_CLASS, RM_CLASS, RM_STAFF},
    {RM_CLASS, RM_CLASS, RM_CLASS, RM_DRAW, RM_CLASS, RM_OFFICE},
    {RM_CLASS, RM_CLASS, RM_CLASS, RM_CLASS, RM_CLASS, RM_OFFICE}};
static const char *WWN[4][6] = {
    {"COMPUTER LAB", "LIBRARY", "CLASS 111", "CLASS 112", "CLASS 113", "DEPT OFFICE"},
    {"CLASS 211", "CLASS 212", "CLASS 213", "COMPUTER LAB 1", "COMPUTER LAB 2", "CLASS 214"},
    {"CLASS 311", "SEMINAR 301", "CLASS 312", "PHYSICS LAB", "CHEMISTRY LAB", "DEPT OFFICE 3"},
    {"RESEARCH LAB", "CLASS 411", "CLASS 412", "CLASS 413", "PROJECT LAB", "STAFF ROOM 4"}};
static const int WWT[4][6] = {
    {RM_COMP, RM_LIB, RM_CLASS, RM_CLASS, RM_CLASS, RM_STAFF},
    {RM_CLASS, RM_CLASS, RM_CLASS, RM_COMP, RM_COMP, RM_CLASS},
    {RM_CLASS, RM_CLASS, RM_CLASS, RM_SCI, RM_SCI, RM_STAFF},
    {RM_COMP, RM_CLASS, RM_CLASS, RM_CLASS, RM_COMP, RM_STAFF}};
static const char *EWN[4][6] = {
    {"CLASS 121", "PHYSICS LAB", "CHEMISTRY LAB", "DRAWING HALL", "CLASS 122", "HOD OFFICE"},
    {"CLASS 221", "CLASS 222", "CLASS 223", "DRAWING HALL 2", "CLASS 224", "CLASS 225"},
    {"LAB 321", "LAB 322", "COMPUTER LAB 3", "CLASS 321", "CLASS 322", "OFFICE 3"},
    {"CLASS 421", "CLASS 422", "LAB 423", "LAB 424", "CLASS 425", "OFFICE 4"}};
static const int EWT[4][6] = {
    {RM_CLASS, RM_SCI, RM_SCI, RM_DRAW, RM_CLASS, RM_OFFICE},
    {RM_CLASS, RM_CLASS, RM_CLASS, RM_DRAW, RM_CLASS, RM_CLASS},
    {RM_SCI, RM_SCI, RM_COMP, RM_CLASS, RM_CLASS, RM_OFFICE},
    {RM_CLASS, RM_CLASS, RM_SCI, RM_SCI, RM_CLASS, RM_OFFICE}};

static void build_cluster(int bi)
{
    Bld *b = &blds[bi];
    float ix0 = b->x0 + WALL_T, ix1 = b->x1 - WALL_T, iy0 = b->y0 + WALL_T, iy1 = b->y1 - WALL_T;
    for (int fl = 0; fl < 4; fl++) {
        build_fl = fl;
        if (bi == 4) {
            float a0 = FX - 1.5f, a1 = FX + 1.5f, wy = iy1 - 2.2f;
            float xw[4], xe[4], wl = a0 - IWT, er = a1 + IWT;
            for (int k = 0; k < 4; k++) { xw[k] = ix0 + (wl - ix0) * k / 3.0f; xe[k] = er + (ix1 - er) * k / 3.0f; }
            wall_v(bi, a0 - IWT, iy0, wy + IWT, NULL, 0, 0);
            wall_v(bi, a1, iy0, wy + IWT, NULL, 0, 0);
            south_rooms(bi, xw, 3, 2.2f, &NWN[fl][0], &NWT[fl][0]);
            south_rooms(bi, xe, 3, 2.2f, &NWN[fl][3], &NWT[fl][3]);
            add_rug(bi, a0 + 0.3f, iy0, a1 - 0.3f, iy1, C(0.62f,0.20f,0.24f));     /* hallway: front door -> back door */
            add_rug(bi, ix0, wy + 0.3f, ix1, wy + 1.9f, C(0.78f,0.76f,0.72f));
            put_plant(bi, a0 + 0.05f, iy0 + 1.0f);
            put_plant(bi, a1 - 0.45f, iy0 + 1.0f);
            /* staircases (steps) at both ends of the corridor */
            if (fl == 0) { add_stair(1, 43.6f, 32.6f, 45.4f, 34.4f); add_stair(1, 83.2f, 32.6f, 85.0f, 34.4f); }
            for (int i = 0; i < 5; i++) {
                float h = 0.5f * (5 - i);
                add_furn(bi, 43.6f + i * 0.36f, 32.6f, 43.6f + (i + 1) * 0.36f, 34.4f, h, C(0.88f,0.82f,0.70f), C(0.62f,0.55f,0.45f), true);
                add_furn(bi, 85.0f - (i + 1) * 0.36f, 32.6f, 85.0f - i * 0.36f, 34.4f, h, C(0.88f,0.82f,0.70f), C(0.62f,0.55f,0.45f), true);
            }
            add_room(bi, 43.6f, 32.6f, 47.0f, 34.4f, "STAIRS");
            add_room(bi, 81.6f, 32.6f, 85.0f, 34.4f, "STAIRS");
            add_room(bi, a0, iy0, a1, wy, "MAIN HALLWAY");
            add_room(bi, ix0, wy + IWT, ix1, iy1, "CORRIDOR");
        } else if (bi == 5) {
            wing_rooms(bi, 48.8f, &WWN[fl][0], &WWT[fl][0], &WWN[fl][3], &WWT[fl][3]);
        } else {
            wing_rooms(bi, 79.2f, &EWN[fl][0], &EWT[fl][0], &EWN[fl][3], &EWT[fl][3]);
        }
    }
    build_fl = 0;
}

/* GIRLS HOSTEL / A BLOCK / B BLOCK: 4 floors (ground,1st,2nd,3rd); 5 separate rooms per floor,
   a corridor, and a stair hall at the east end */
static void build_hostel(int bi)
{
    Bld *b = &blds[bi];
    float ix0 = b->x0 + WALL_T, ix1 = b->x1 - WALL_T, iy0 = b->y0 + WALL_T, iy1 = b->y1 - WALL_T, wy = iy1 - 2.0f;
    const char *pre = bi == 12 ? "A" : bi == 13 ? "B" : "GH";
    const char *cname = bi == 12 ? "A BLOCK CORRIDOR" : bi == 13 ? "B BLOCK CORRIDOR" : "GIRLS HOSTEL CORRIDOR";
    SDL_FColor rugc = bi == 12 ? C(0.72f,0.55f,0.78f) : bi == 13 ? C(0.55f,0.70f,0.78f) : C(0.78f,0.55f,0.65f);
    float xb[6];
    for (int k = 0; k < 6; k++) xb[k] = ix0 + (ix1 - 3.0f - ix0) * k / 5.0f;
    for (int fl = 0; fl < 4; fl++) {
        build_fl = fl;
        char nb[5][28]; const char *np[5]; int ty[5];
        for (int k = 0; k < 5; k++) {
            if (fl == 0) snprintf(nb[k], 28, "ROOM %s-G0%d", pre, k + 1);
            else snprintf(nb[k], 28, "ROOM %s-%d0%d", pre, fl, k + 1);
            np[k] = nb[k]; ty[k] = RM_HOSTEL;
        }
        south_rooms(bi, xb, 5, 2.0f, np, ty);
        wall_v(bi, xb[5], iy0, wy + IWT, NULL, 0, 0);                    /* last room | stair hall */
        add_rug(bi, ix0 + 0.3f, iy1 - 1.3f, ix1 - 0.3f, iy1 - 0.7f, rugc);
        /* stairs against the east wall, climbing towards the north wall */
        float sx0 = ix1 - 1.9f, sx1 = ix1 - 0.1f, sy0 = iy0 + 0.2f;
        for (int i = 0; i < 5; i++)
            add_furn(bi, sx0, sy0 + i * 0.64f, sx1, sy0 + (i + 1) * 0.64f, 0.5f * (5 - i), C(0.88f,0.82f,0.70f), C(0.62f,0.55f,0.45f), true);
        if (fl == 0) add_stair(grp(bi), sx0, sy0, sx1, sy0 + 3.2f);
        add_room(bi, xb[5] + IWT, iy0, ix1, iy0 + 3.6f, "STAIRS");
        add_room(bi, ix0, wy + IWT, ix1, iy1, cname);
    }
    build_fl = 0;
}

/* MECH / CIVIL BLOCK: 3 floors (ground, 1st, 2nd), 4 rooms per floor + stair hall at the east end */
static void build_mech(int bi)
{
    Bld *b = &blds[bi];
    float ix0 = b->x0 + WALL_T, ix1 = b->x1 - WALL_T, iy0 = b->y0 + WALL_T, iy1 = b->y1 - WALL_T, wy = iy1 - 2.0f;
    static const char *nm[3][4] = {
        {"MECH WORKSHOP", "MECH LAB", "CIVIL LAB", "DRAWING HALL"},
        {"MECH CLASS 201", "MECH CLASS 202", "CIVIL CLASS 203", "CIVIL CLASS 204"},
        {"CAD LAB", "CIVIL DESIGN LAB", "MECH STAFF ROOM", "HOD OFFICE"}};
    static const int ty[3][4] = {
        {RM_MECH, RM_MECH, RM_CIVIL, RM_DRAW}, {RM_CLASS, RM_CLASS, RM_CLASS, RM_CLASS}, {RM_COMP, RM_CIVIL, RM_STAFF, RM_OFFICE}};
    float xb[5];
    for (int k = 0; k < 5; k++) xb[k] = ix0 + (ix1 - 3.0f - ix0) * k / 4.0f;
    for (int fl = 0; fl < 3; fl++) {
        build_fl = fl;
        south_rooms(bi, xb, 4, 2.0f, nm[fl], ty[fl]);
        wall_v(bi, xb[4], iy0, wy + IWT, NULL, 0, 0);
        add_rug(bi, ix0 + 0.3f, iy1 - 1.3f, ix1 - 0.3f, iy1 - 0.7f, C(0.55f, 0.25f, 0.28f));
        float sx0 = ix1 - 1.9f, sx1 = ix1 - 0.1f, sy0 = iy0 + 0.2f;
        for (int i = 0; i < 5; i++)
            add_furn(bi, sx0, sy0 + i * 0.64f, sx1, sy0 + (i + 1) * 0.64f, 0.5f * (5 - i), C(0.88f,0.82f,0.70f), C(0.62f,0.55f,0.45f), true);
        if (fl == 0) add_stair(5, sx0, sy0, sx1, sy0 + 3.2f);
        add_room(bi, xb[4] + IWT, iy0, ix1, iy0 + 3.6f, "STAIRS");
        add_room(bi, ix0, wy + IWT, ix1, iy1, "MECH BLOCK CORRIDOR");
    }
    build_fl = 0;
}

static void build_interiors(void)
{
    nfurn = 0; nroom = 0; nrug = 0;
    for (int bi = 0; bi < NB; bi++) {
        Bld *b = &blds[bi];
        float ix0 = b->x0 + WALL_T, ix1 = b->x1 - WALL_T, iy0 = b->y0 + WALL_T, iy1 = b->y1 - WALL_T;

        if (bi == 1) {                                   /* MECH / CIVIL BLOCK: 3 floors */
            build_mech(bi);
        } else if (in_cluster(bi)) {                     /* MAIN COLLEGE: 3 floors */
            build_cluster(bi);
        } else if (bi == 3 || bi == 12 || bi == 13) {    /* hostels: 4 floors */
            build_hostel(bi);
        } else switch (b->kind) {
        case K_HOSTEL: {
            static const char *nm[5] = {"ROOM 1", "ROOM 2", "ROOM 3", "ROOM 4", "ROOM 5"};
            static const int ty[5] = {RM_HOSTEL, RM_HOSTEL, RM_HOSTEL, RM_HOSTEL, RM_HOSTEL};
            float xb[6];
            for (int k = 0; k < 6; k++) xb[k] = ix0 + (ix1 - ix0) * k / 5.0f;
            south_rooms(bi, xb, 5, 2.0f, nm, ty);
            add_rug(bi, ix0 + 0.3f, iy1 - 1.3f, ix1 - 0.3f, iy1 - 0.7f, C(0.78f,0.55f,0.65f));
            break;
        }
        case K_MESS:
            if (bi != 11) {
                serve_counter(bi, ix0 + 0.3f, ix1 - 0.3f, iy0 + 0.3f, bi);
                add_furn_z(bi, ix0 + 0.3f, iy0, ix1 - 0.3f, iy0 + 0.1f, 1.9f, 2.8f, C(0.2f,0.2f,0.2f), C(0.16f,0.30f,0.22f), false);  /* menu board */
            }
            if (bi == 9)  { cz_on = true; cz[0] = ix1 - 1.6f; cz[1] = 43.6f; cz[2] = ix1;        cz[3] = 45.4f; }
            if (bi == 10) { cz_on = true; cz[0] = ix0;        cz[1] = 43.6f; cz[2] = ix0 + 1.6f; cz[3] = 45.4f; }
            if (bi == 10) {                              /* MESS 2: narrow hall -> three long tables */
                dining_table_v(bi, 34.1f, 42.5f, 1.6f);
                dining_table_v(bi, 34.1f, 44.3f, 1.6f);
                dining_table_v(bi, 31.3f, 45.8f, 1.6f);
            } else
            dining_area(bi, ix0 + 0.3f, iy0 + 2.2f, ix1 - 0.3f, iy1 - 0.15f, b->doorx, (ix1 - ix0 > 8) ? 1.5f : 1.4f);
            cz_on = false;
            add_room(bi, ix0, iy0, ix1, iy1, bi == 11 ? "FOOD STALLS & SEATING" : "DINING HALL");
            break;
        case K_GYM:
            furnish_gym(bi, ix0, iy0, ix1, iy1);
            add_room(bi, ix0, iy0, ix1, iy1, "WORKOUT FLOOR");
            break;
        case K_KITCHEN:
            furnish_kitchen(bi, ix0, iy0, ix1, iy1);
            add_room(bi, ix0, iy0, ix1, iy1, "COOKING AREA");
            break;
        case K_HALL:
            furnish_hall(bi, ix0, iy0, ix1, iy1);
            add_room(bi, ix0, iy0, ix1, iy1, bi == 0 ? "ARENA" : "CONVENTION AREA");
            break;
        case K_STORE:
            furnish_store(bi, ix0, iy0, ix1, iy1);
            add_room(bi, ix0, iy0, ix1, iy1, "STOCK ROOM");
            break;
        default: break;
        }
        if (bi == 11) {                                  /* FOOD COURT: three stalls */
            SDL_FColor sc[3] = {C(0.85f,0.30f,0.25f), C(0.30f,0.65f,0.35f), C(0.95f,0.75f,0.25f)};
            float sw = (ix1 - ix0 - 0.6f) / 3;
            for (int k = 0; k < 3; k++) {
                serve_counter(bi, ix0 + 0.3f + k * sw + 0.05f, ix0 + 0.3f + (k + 1) * sw - 0.05f, iy0 + 0.3f, k + 1);
                add_furn_z(bi, ix0 + 0.3f + k * sw, iy0, ix0 + 0.3f + (k + 1) * sw, iy0 + 0.1f, 1.9f, 2.9f, C(0.9f,0.9f,0.9f), sc[k], false);
            }
        }
    }
}

static bool in_compound(float x, float y)
{
    return (x > 24.5f && x < 86.5f && y > 2 && y < 20) ||
           (x > 7.8f && x < 86.5f && y > 20 && y < 93) ||
           (x > 1.9f && x < 84.3f && y > 82.4f && y < 124);
}

/* is this spot covered by something that is not grass? */
static bool hard(float x, float y)
{
    for (int i = 0; i < NZ; i++)
        if (x > zones[i].x0 - 0.3f && x < zones[i].x1 + 0.3f && y > zones[i].y0 - 0.3f && y < zones[i].y1 + 0.3f) return true;
    for (int i = 0; i < NB; i++)
        if (x > blds[i].x0 - 1 && x < blds[i].x1 + 1.6f && y > blds[i].y0 - 1 && y < blds[i].y1 + 1.6f) return true;
    for (int i = 0; i < nbox; i++)
        if (x > boxes[i].x0 - 0.4f && x < boxes[i].x1 + 0.4f && y > boxes[i].y0 - 0.4f && y < boxes[i].y1 + 0.4f) return true;
    if (x > 45 && x < 86 && y > 56 && y < 93) return true;
    if (x > 42 && x < 49 && y > 56 && y < 63) return true;
    if (x > 33.5f && x < 38 && y > 100.5f && y < 105) return true;
    return false;
}

static void init_world(void)
{
    SDL_FColor brickT = C(0.72f, 0.60f, 0.50f), brickS = C(0.82f, 0.68f, 0.56f);
    SDL_FColor hedgeT = C(0.22f, 0.52f, 0.20f), hedgeS = C(0.15f, 0.40f, 0.15f);

    /* compound wall (from the blueprint outline). The start gate side is closed
       except the tank/arch area. */
    add_box(24.5f,  1.8f, 87.1f,  2.4f, 0, 2.0f, brickT, brickS, S_WALL, true);
    add_box(86.6f,  2.0f, 87.1f, 93.3f, 0, 2.0f, brickT, brickS, S_WALL, true);
    add_box(84.0f, 92.8f, 87.1f, 93.3f, 0, 2.0f, brickT, brickS, S_WALL, true);
    add_box(84.0f, 93.0f, 84.5f,124.4f, 0, 2.0f, brickT, brickS, S_WALL, true);
    add_box( 1.4f,124.0f, 84.5f,124.5f, 0, 2.0f, brickT, brickS, S_WALL, true);
    add_box( 1.4f, 82.0f,  1.9f,124.5f, 0, 2.0f, brickT, brickS, S_WALL, true);
    add_box( 1.4f, 82.0f,  8.0f, 82.4f, 0, 2.0f, brickT, brickS, S_WALL, true);
    add_box( 7.5f, 20.0f,  8.0f, 82.4f, 0, 2.0f, brickT, brickS, S_WALL, true);
    add_box( 7.5f, 19.7f, 24.7f, 20.2f, 0, 2.0f, brickT, brickS, S_WALL, true);
    add_box(24.2f,  2.0f, 24.7f, 20.0f, 0, 2.0f, brickT, brickS, S_WALL, true);

    /* hedges / garden strips (the green bars in your PPT) */
    float hd[][4] = {
        {42.6f, 54.3f, 81.8f, 56.5f}, {42.8f, 62.4f, 45.2f, 86.0f}, {42.6f, 92.5f, 84.0f, 94.5f},
        { 4.8f, 92.5f,  7.7f,124.0f}, { 7.7f,122.5f, 71.6f,124.0f}, { 7.7f, 92.2f, 35.8f, 94.0f},
        {36.2f,108.0f, 42.5f,122.5f}, {35.3f, 51.7f, 36.9f, 66.7f}, {35.3f, 69.8f, 36.9f, 85.5f},
        {38.9f, 37.6f, 40.5f, 49.8f}, {38.9f, 54.3f, 40.5f, 85.7f}, { 8.6f, 66.3f, 13.0f, 71.8f},
        { 8.8f, 82.4f, 12.9f, 84.0f}};
    for (int i = 0; i < 13; i++)
        add_box(hd[i][0], hd[i][1], hd[i][2], hd[i][3], 0, 1.0f, hedgeT, hedgeS, S_HEDGE, true);
    /* flower bed on the start road */
    add_box(14.4f, 87.6f, 32.2f, 88.9f, 0, 0.5f, C(0.40f, 0.28f, 0.18f), C(0.45f, 0.45f, 0.45f), S_BED, true);

    /* stage, sign, tank, start arch */
    add_box(80.8f, 64.5f, 85.8f, 79.7f, 0, 1.6f, C(0.62f, 0.62f, 0.64f), C(0.55f, 0.55f, 0.58f), S_STAGE, true);
    add_box(42.6f, 86.2f, 45.9f, 91.2f, 0, 1.8f, C(0.97f, 0.97f, 0.97f), C(0.92f, 0.92f, 0.95f), S_SIGN, true);
    add_box(4.9f, 86.9f, 8.5f, 90.3f, 0, 3.2f, C(0.60f, 0.78f, 0.95f), C(0.35f, 0.55f, 0.85f), S_TANK, true);
    add_box(10.6f, 86.1f, 11.4f, 86.9f, 0, 4.2f, C(0.65f, 0.45f, 0.30f), C(0.55f, 0.38f, 0.25f), S_PLAIN, true);
    add_box(10.6f, 89.7f, 11.4f, 90.5f, 0, 4.2f, C(0.65f, 0.45f, 0.30f), C(0.55f, 0.38f, 0.25f), S_PLAIN, true);
    add_box(10.4f, 86.1f, 11.6f, 90.5f, 4.2f, 0.7f, C(0.75f, 0.52f, 0.34f), C(0.60f, 0.40f, 0.26f), S_PLAIN, false);

    /* component submission hub: lamps around the plaza */
    for (int k = 0; k < 6; k++) {
        float an = 2 * PI * k / 6 + 0.3f;
        float lx = 6.7f + cosf(an) * 2.9f, ly = 88.6f + sinf(an) * 2.9f;
        if (lx > 10.0f && ly > 86.0f && ly < 91.0f) continue;        /* keep the start arch clear */
        add_box(lx, ly, lx + 0.3f, ly + 0.3f, 0, 1.1f, C(0.5f, 0.95f, 1.0f), C(0.2f, 0.3f, 0.5f), S_LAMP, false);
    }

    /* parked cars */
    add_box( 9.1f, 73.2f, 10.7f, 76.8f, 0, 0.9f, C(0.85f, 0.15f, 0.15f), C(0.75f, 0.12f, 0.12f), S_CAR, true);
    add_box(11.0f, 77.8f, 12.6f, 81.4f, 0, 0.9f, C(0.20f, 0.35f, 0.80f), C(0.15f, 0.28f, 0.70f), S_CAR, true);
    add_box( 9.1f, 78.4f, 10.7f, 82.0f, 0, 0.9f, C(0.92f, 0.92f, 0.92f), C(0.82f, 0.82f, 0.84f), S_CAR, true);

    /* volleyball nets (+ posts) and basketball hoops */
    float vb[][4] = {{9.0f, 95.8f, 20.7f, 106.6f}, {23.2f, 111.0f, 34.9f, 121.4f}};
    for (int i = 0; i < 2; i++) {
        float ym = (vb[i][1] + vb[i][3]) / 2;
        add_box(vb[i][0], ym - 0.06f, vb[i][2], ym + 0.06f, 1.0f, 1.4f, C(1,1,1), C(0.9f,0.9f,0.9f), S_NET, false);
        add_box(vb[i][0] - 0.2f, ym - 0.15f, vb[i][0] + 0.1f, ym + 0.15f, 0, 2.5f, C(0.8f,0.8f,0.8f), C(0.6f,0.6f,0.6f), S_POST, true);
        add_box(vb[i][2] - 0.1f, ym - 0.15f, vb[i][2] + 0.2f, ym + 0.15f, 0, 2.5f, C(0.8f,0.8f,0.8f), C(0.6f,0.6f,0.6f), S_POST, true);
    }
    add_box(30.4f, 70.3f, 30.9f, 70.8f, 0, 3.4f, C(0.8f,0.8f,0.8f), C(0.5f,0.5f,0.5f), S_HOOP, true);
    add_box(30.4f, 83.1f, 30.9f, 83.6f, 0, 3.4f, C(0.8f,0.8f,0.8f), C(0.5f,0.5f,0.5f), S_HOOP, true);
    /* football goal posts on the main playground */
    add_box(47.0f, 72.6f, 47.4f, 73.0f, 0, 2.4f, C(1,1,1), C(0.9f,0.9f,0.9f), S_POST, true);
    add_box(47.0f, 77.0f, 47.4f, 77.4f, 0, 2.4f, C(1,1,1), C(0.9f,0.9f,0.9f), S_POST, true);
    add_box(78.8f, 72.6f, 79.2f, 73.0f, 0, 2.4f, C(1,1,1), C(0.9f,0.9f,0.9f), S_POST, true);
    add_box(78.8f, 77.0f, 79.2f, 77.4f, 0, 2.4f, C(1,1,1), C(0.9f,0.9f,0.9f), S_POST, true);

    /* benches around the fountain courtyard and lamp posts on the main road */
    float bn[][2] = {{57.0f, 40.0f}, {68.5f, 40.0f}, {57.0f, 47.5f}, {68.5f, 47.5f}};
    for (int i = 0; i < 4; i++)
        add_box(bn[i][0], bn[i][1], bn[i][0] + 2.2f, bn[i][1] + 0.8f, 0, 0.6f,
                C(0.60f, 0.42f, 0.28f), C(0.45f, 0.32f, 0.20f), S_BENCH, true);
    for (float y = 28; y < 105; y += 11) {
        add_box(42.3f, y, 42.6f, y + 0.3f, 0, 4.2f, C(0.9f,0.9f,0.6f), C(0.25f,0.25f,0.28f), S_LAMP, true);
        add_box(37.1f, y + 5, 37.4f, y + 5.3f, 0, 4.2f, C(0.9f,0.9f,0.6f), C(0.25f,0.25f,0.28f), S_LAMP, true);
    }

    /* buildings: doors on the south wall */
    for (int i = 0; i < NB; i++) {
        blds[i].doorx = (blds[i].x0 + blds[i].x1) / 2;
        blds[i].dw = MINF(3.0f, (blds[i].x1 - blds[i].x0) * 0.5f);
        blds[i].f = 0;
    }
    /* doors */
    ndoor = 0;
    for (int i = 0; i < NB; i++) {
        if (i == 4) {
            add_door(4, 'S', FX, 3.6f, true);        /* MAIN COLLEGE FRONT entrance (courtyard / fountain side) */
            add_door(4, 'N', FX, 3.0f, false);       /* MAIN COLLEGE BACK entrance (north side)                 */
            add_door(4, 'S', 48.8f, 2.0f, false);    /* passage into the west wing */
            add_door(4, 'S', 79.2f, 2.0f, false);    /* passage into the east wing */
        } else if (i == 5) {
            add_door(5, 'N', 48.8f, 2.0f, false);
            add_door(5, 'S', 48.8f, 2.0f, true);     /* west wing exit to the road */
        } else if (i == 6) {
            add_door(6, 'N', 79.2f, 2.0f, false);
            add_door(6, 'S', 79.2f, 2.0f, true);     /* east wing exit to the road */
        } else add_door(i, 'S', blds[i].doorx, blds[i].dw, true);
    }
    add_door(9, 'E', 44.5f, 1.8f, false);            /* MESS 1 <-> MESS 2 */
    add_door(10, 'W', 44.5f, 1.8f, false);
    blds[4].doorx = FX;
    build_interiors();

    /* ---- trees ---- */
    for (float y = 38; y < 50; y += 3.0f) add_tree(39.7f, y, 0, 1.0f);
    for (float y = 55; y < 86; y += 3.0f) add_tree(39.7f, y, (int)(y) % 2 ? 0 : 2, 1.0f);
    for (float y = 53; y < 66; y += 3.5f) add_tree(36.1f, y, 0, 0.95f);
    for (float y = 71; y < 85; y += 3.5f) add_tree(36.1f, y, 3, 1.0f);
    for (float y = 96; y < 122; y += 4.0f) add_tree(6.3f, y, (y > 108) ? 0 : 1, 1.0f);
    for (float x = 10; x < 70; x += 5.0f) add_tree(x, 123.2f, 1, 0.95f);
    for (float y = 109; y < 122; y += 3.5f) { add_tree(38.0f, y, 0, 1.0f); add_tree(41.0f, y + 1.0f, 2, 0.9f); }
    add_tree(10.8f, 68.8f, 0, 1.0f);  add_tree(12.3f, 66.8f, 2, 0.9f);
    float ex[][3] = {                      /* hand placed shade trees */
        {44, 10, 0}, {48, 7, 0}, {52, 14, 2}, {40, 15, 0}, {35, 20, 1}, {56, 19.5f, 0}, {50, 19.2f, 0},
        {20, 53, 0}, {27, 54, 2}, {33, 55, 0}, {14, 53, 0}, {22, 62, 0}, {29, 64, 0}, {18, 69, 2},
        {14, 34.5f, 0}, {16, 34, 2}, {26, 33.5f, 0}, {33, 36, 0},
        {47, 60, 3}, {46.5f, 90, 3}, {83.5f, 91, 3}, {83.5f, 58, 3}, {55, 52.7f, 3}, {75, 52.7f, 3},
        {46, 109, 0}, {50, 118, 2}, {56, 110, 0}, {60, 119, 0}, {66, 111, 2}, {69, 120, 0},
        {85, 108, 0}, {85, 100, 0}, {85, 120, 0},
        {13.5f, 92.0f, 3}, {16.5f, 91.8f, 3}, {19.5f, 91.8f, 3}, {23, 85.0f, 3}, {29, 85.0f, 3},
        {9.5f, 84.3f, 0}, {20, 98.8f, 0}, {38, 99.5f, 0}, {43.5f, 84.5f, 0},
        {44.0f, 19.0f, 0}, {70, 19.5f, 2}, {80, 19.5f, 0}, {38, 25, 0}};
    for (size_t i = 0; i < sizeof(ex) / sizeof(ex[0]); i++)
        add_tree(ex[i][0], ex[i][1], (int)ex[i][2], 1.0f + (rnd() - 0.5f) * 0.2f);
    /* forest outside the compound wall (decoration only) */
    for (int n = 0; n < 150 && ntree < 415; n++) {
        float x = rnd() * MAP_W, y = rnd() * MAP_H;
        if (in_compound(x, y) || (x > 3.5f && x < 87.6f && y > 1 && y < 125) || (y > 81 && x > 0.9f && x < 87.6f && y < 125)) continue;
        add_tree(x, y, (int)(rnd() * 4) == 3 ? 3 : (rnd() > 0.4f ? 0 : 1), 0.9f + rnd() * 0.4f);
    }
    for (int n = 0; n < 60 && ntree < 418; n++) {
        float x = rnd() * 24, y = rnd() * 20;
        if (x > 4 && y > 2) add_tree(x, y, rnd() > 0.5f ? 0 : 1, 1.0f + rnd() * 0.3f);
    }

    /* grass look: big colour patches and little tufts */
    for (int n = 0; n < NPATCH; n++) {
        float x = 4 + rnd() * 82, y = 2 + rnd() * 122;
        if (!in_compound(x, y)) continue;
        patch[npatch][0] = x; patch[npatch][1] = y; patch[npatch][2] = 1.5f + rnd() * 3;
        patch[npatch][3] = (rnd() - 0.5f) * 0.10f; npatch++;
    }
    for (int n = 0; n < 6000 && ntuft < NTUFT; n++) {
        float x = 4 + rnd() * 82, y = 2 + rnd() * 122;
        if (!in_compound(x, y) || hard(x, y)) continue;
        tuft[ntuft][0] = x; tuft[ntuft][1] = y; tuft[ntuft][2] = rnd(); ntuft++;
    }
}

/* ------------------------------------------------------------------ */
/* 4. Collision                                                        */
/* ------------------------------------------------------------------ */
static bool in_doorway(const Bld *b, float x, float y)
{
    int bi = (int)(b - blds);
    for (int i = 0; i < ndoor; i++) {
        const Door *d = &doors[i];
        if (d->bld != bi) continue;
        if (d->ground_only && ofloor(bi) > 0) continue;
        float hw = d->w / 2;
        switch (d->side) {
        case 'S': if (y >= b->y1 - WALL_T - 0.01f && fabsf(x - d->c) < hw) return true; break;
        case 'N': if (y <= b->y0 + WALL_T + 0.01f && fabsf(x - d->c) < hw) return true; break;
        case 'E': if (x >= b->x1 - WALL_T - 0.01f && fabsf(y - d->c) < hw) return true; break;
        case 'W': if (x <= b->x0 + WALL_T + 0.01f && fabsf(y - d->c) < hw) return true; break;
        }
    }
    return false;
}

static bool wall_hit(const Bld *b, float x, float y)
{
    if (x < b->x0 || x > b->x1 || y < b->y0 || y > b->y1) return false;
    if (x > b->x0 + WALL_T && x < b->x1 - WALL_T && y > b->y0 + WALL_T && y < b->y1 - WALL_T) return false;
    if (in_doorway(b, x, y)) return false;
    return true;
}

/* standing just outside a doorway? (used to open the cut-away view so you can see yourself) */
static bool near_door(int bi, float x, float y)
{
    const Bld *b = &blds[bi];
    for (int i = 0; i < ndoor; i++) {
        const Door *d = &doors[i];
        if (d->bld != bi) continue;
        float hw = d->w / 2 + 0.8f;
        switch (d->side) {
        case 'S': if (fabsf(x - d->c) < hw && y > b->y1 && y < b->y1 + 2.2f) return true; break;
        case 'N': if (fabsf(x - d->c) < hw && y < b->y0 && y > b->y0 - 2.6f) return true; break;
        case 'E': if (fabsf(y - d->c) < hw && x > b->x1 && x < b->x1 + 2.2f) return true; break;
        case 'W': if (fabsf(y - d->c) < hw && x < b->x0 && x > b->x0 - 2.2f) return true; break;
        }
    }
    return false;
}

static bool blocked_pt(float x, float y)
{
    if (x < 1 || x > MAP_W - 1 || y < 1 || y > MAP_H - 1) return true;
    for (int i = 0; i < NB; i++) if (wall_hit(&blds[i], x, y)) return true;
    for (int i = 0; i < nbox; i++)
        if (boxes[i].solid && x > boxes[i].x0 && x < boxes[i].x1 && y > boxes[i].y0 && y < boxes[i].y1) return true;
    for (int i = 0; i < ntree; i++) {
        float dx = x - trees[i].x, dy = y - trees[i].y;
        if (dx * dx + dy * dy < 0.30f) return true;
    }
    if ((x - FX) * (x - FX) + (y - FY) * (y - FY) < FR * FR) return true;
    for (int i = 0; i < nfurn; i++)
        if (furn[i].solid && furn[i].floor == ofloor(furn[i].owner) && x > furn[i].x0 && x < furn[i].x1 && y > furn[i].y0 && y < furn[i].y1) return true;
    return false;
}

/* the player is a small circle: test the centre and 4 side points */
static bool blocked(float x, float y)
{
    const float r = 0.22f;
    return blocked_pt(x, y) || blocked_pt(x + r, y) || blocked_pt(x - r, y) ||
           blocked_pt(x, y + r) || blocked_pt(x, y - r);
}

static bool inside_bld(const Bld *b, float x, float y)
{
    return x > b->x0 && x < b->x1 && y > b->y0 && y < b->y1 + 0.3f;
}

/* ------------------------------------------------------------------ */
/* 5. Drawing                                                          */
/* ------------------------------------------------------------------ */
/* paved plaza around the CODE SUBMISSION HUB (round tank) */
static void draw_plaza(void)
{
    float cx = 6.7f, cy = 88.6f;
    if (!vis(cx - 5, cy - 5, cx + 5, cy + 5, 0)) return;
    goval(cx, cy, 3.5f, 3.5f, 0.02f, C(0.30f, 0.34f, 0.44f), 40);
    goval(cx, cy, 3.3f, 3.3f, 0.03f, C(0.74f, 0.76f, 0.82f), 40);
    goval(cx, cy, 2.8f, 2.8f, 0.04f, C(0.28f, 0.52f, 0.78f), 40);
    goval(cx, cy, 2.6f, 2.6f, 0.05f, C(0.82f, 0.84f, 0.90f), 40);
    for (int k = 0; k < 8; k++) {                                         /* paving spokes */
        float an = PI * k / 4 + 0.2f;
        SDL_FPoint a = iso(cx + cosf(an) * 2.0f, cy + sinf(an) * 2.0f, 0.06f), b = iso(cx + cosf(an) * 2.6f, cy + sinf(an) * 2.6f, 0.06f);
        sline(a.x, a.y, b.x, b.y, 1.6f * WORLD_SCALE, C(0.40f, 0.85f, 0.95f));
    }
    /* path from the plaza to the start point */
    grect(cx + 3.0f, 87.6f, 10.4f, 89.6f, 0.03f, C(0.74f, 0.76f, 0.82f));
}

static void draw_ground(void)
{
    SDL_FColor grass = C(0.36f, 0.62f, 0.30f);

    grect(0, 0, MAP_W, MAP_H, 0, C(0.24f, 0.44f, 0.26f));            /* outside forest floor */
    grect(24.5f, 2, 85.8f, 20, 0, grass);                              /* inside the wall */
    grect(7.8f, 20, 85.8f, 93, 0, grass);
    grect(1.9f, 82.4f, 84.3f, 124, 0, grass);
    grect(85.5f, 2.0f, 86.6f, 93.0f, 0, grass);

    for (int i = 0; i < npatch; i++) {                                 /* soft colour patches */
        if (!vis(patch[i][0] - 3, patch[i][1] - 3, patch[i][0] + 3, patch[i][1] + 3, 0)) continue;
        float k = patch[i][3];
        goval(patch[i][0], patch[i][1], patch[i][2], patch[i][2] * 0.8f, 0,
              C(0.36f + k, 0.62f + k * 1.2f, 0.30f + k * 0.5f), 12);
    }

    for (int i = 0; i < ntuft; i++) {                                  /* grass blades */
        if (!vis(tuft[i][0], tuft[i][1], tuft[i][0], tuft[i][1], 0)) continue;
        SDL_FPoint b = iso(tuft[i][0], tuft[i][1], 0);
        float sway = sinf(gtime * 2 + tuft[i][0] * 0.7f) * 0.8f;
        float k = tuft[i][2];
        SDL_SetRenderDrawColorFloat(R, 0.20f + k * 0.12f, 0.46f + k * 0.18f, 0.16f + k * 0.08f, 1);
        SDL_RenderLine(R, b.x, b.y, b.x - 1.8f + sway, b.y - 4);
        SDL_RenderLine(R, b.x, b.y, b.x + sway, b.y - 5);
        SDL_RenderLine(R, b.x, b.y, b.x + 1.8f + sway, b.y - 3.5f);
    }

    /* sand / paving / roads / parking / court */
    for (int i = 0; i < NZ; i++) {
        Zone *z = &zones[i];
        if (!vis(z->x0, z->y0, z->x1, z->y1, 0)) continue;
        grect(z->x0, z->y0, z->x1, z->y1, 0, zone_col(z->type));
        if (z->type == Z_PAVE || z->type == Z_ROAD) {                  /* kerb line */
            SDL_FColor k = C(0.70f, 0.68f, 0.60f);
            grect(z->x0, z->y0, z->x1, z->y0 + 0.2f, 0, k);
            grect(z->x0, z->y1 - 0.2f, z->x1, z->y1, 0, k);
        }
    }
    /* paving tile lines in the courtyard */
    for (float x = 54.8f; x < 72.6f; x += 2.2f) grect(x, 35.2f, x + 0.07f, 50.5f, 0, C(0.70f, 0.69f, 0.66f));
    for (float y = 35.2f; y < 50.5f; y += 2.2f) grect(54.8f, y, 72.6f, y + 0.07f, 0, C(0.70f, 0.69f, 0.66f));

    /* road markings */
    for (float y = 24; y < 107; y += 3) {
        grect(37.4f, y, 37.6f, y + 1.5f, 0, C(0.95f, 0.95f, 0.9f));
        grect(42.2f, y, 42.4f, y + 1.5f, 0, C(0.95f, 0.95f, 0.9f));
    }
    for (float x = 43; x < 85; x += 3) grect(x, 52.4f, x + 1.5f, 52.6f, 0, C(0.95f, 0.95f, 0.9f));
    for (float x = 9; x < 36; x += 3) {
        if (x > 13.5f && x < 33) continue;
        grect(x, 88.2f, x + 1.5f, 88.4f, 0, C(0.95f, 0.95f, 0.9f));
    }

    /* car park bay lines */
    for (float y = 73; y < 82.2f; y += 3.2f) grect(8.7f, y, 12.9f, y + 0.1f, 0, C(0.9f, 0.9f, 0.9f));

    /* basketball court markings */
    SDL_FColor wl = C(0.97f, 0.97f, 0.97f);
    gframe(26.1f, 69.8f, 35.2f, 84.0f, 0.15f, wl);
    grect(26.1f, 76.85f, 35.2f, 77.0f, 0, wl);
    gring(30.65f, 76.9f, 1.8f, 1.8f, 0.15f, wl);
    gframe(28.6f, 69.8f, 32.7f, 73.4f, 0.12f, wl);
    gframe(28.6f, 80.4f, 32.7f, 84.0f, 0.12f, wl);

    /* volleyball (sand) + kabaddi (clay) courts inside the big sand ground */
    gframe(7.8f, 94.3f, 36.0f, 122.3f, 0.35f, C(0.78f, 0.64f, 0.42f));
    float vb[2][4] = {{9.0f, 95.8f, 20.7f, 106.6f}, {23.2f, 111.0f, 34.9f, 121.4f}};
    float kb[2][4] = {{23.2f, 95.8f, 34.9f, 106.6f}, {9.0f, 111.0f, 20.7f, 121.4f}};
    for (int i = 0; i < 2; i++) {
        grect(vb[i][0], vb[i][1], vb[i][2], vb[i][3], 0, C(0.95f, 0.85f, 0.62f));
        gframe(vb[i][0], vb[i][1], vb[i][2], vb[i][3], 0.18f, C(0.20f, 0.40f, 0.85f));
        float ym = (vb[i][1] + vb[i][3]) / 2;
        grect(vb[i][0], ym - 0.08f, vb[i][2], ym + 0.08f, 0, C(0.20f, 0.40f, 0.85f));
        grect(vb[i][0], ym - 3.2f, vb[i][2], ym - 3.1f, 0, C(0.20f, 0.40f, 0.85f));
        grect(vb[i][0], ym + 3.1f, vb[i][2], ym + 3.2f, 0, C(0.20f, 0.40f, 0.85f));
        grect(kb[i][0], kb[i][1], kb[i][2], kb[i][3], 0, C(0.80f, 0.56f, 0.38f));
        gframe(kb[i][0], kb[i][1], kb[i][2], kb[i][3], 0.18f, wl);
        float km = (kb[i][1] + kb[i][3]) / 2;
        grect(kb[i][0], km - 0.08f, kb[i][2], km + 0.08f, 0, wl);
        grect(kb[i][0], km - 1.6f, kb[i][2], km - 1.5f, 0, wl);
        grect(kb[i][0], km + 1.5f, kb[i][2], km + 1.6f, 0, wl);
        grect(kb[i][0] + 2, kb[i][1], kb[i][0] + 2.1f, kb[i][3], 0, wl);
        grect(kb[i][2] - 2.1f, kb[i][1], kb[i][2] - 2, kb[i][3], 0, wl);
    }

    /* main playground: mowed grass stripes, running track, markings */
    grect(45.5f, 57, 85.5f, 92.5f, 0, C(0.30f, 0.58f, 0.27f));
    for (float x = 45.5f; x < 85; x += 4) grect(x, 57, x + 2, 92.5f, 0, C(0.34f, 0.63f, 0.30f));
    goval(63.5f, 75, 16.0f, 15.5f, 0, C(0.76f, 0.40f, 0.30f), 40);
    goval(63.5f, 75, 12.6f, 12.1f, 0, C(0.33f, 0.62f, 0.29f), 40);
    gring(63.5f, 75, 16.0f, 15.5f, 0.15f, wl);
    gring(63.5f, 75, 12.6f, 12.1f, 0.15f, wl);
    gframe(47, 66, 80, 84, 0.15f, wl);
    grect(63.4f, 66, 63.6f, 84, 0, wl);
    gring(63.5f, 75, 3, 3, 0.15f, wl);

    /* stairs (blue-white steps in your PPT) */
    for (int s = 0; s < 6; s++) {
        SDL_FColor sc = (s % 2) ? C(0.55f, 0.65f, 0.85f) : C(0.92f, 0.92f, 0.95f);
        grect(42.7f, 56.7f + s * 0.9f, 48.5f, 57.6f + s * 0.9f, 0, sc);
        grect(34.2f, 101.5f + s * 0.42f, 37.5f, 101.92f + s * 0.42f, 0, sc);
    }
    /* sand for the courtyard bed under the fountain */
    goval(FX, FY, FR + 1.2f, FR + 1.2f, 0, C(0.80f, 0.76f, 0.68f), 28);
}

static void draw_shadows(void)
{
    SDL_FColor sh = C(0.05f, 0.10f, 0.05f);
    sh.a = 0.22f;
    for (int i = 0; i < nbox; i++) {
        Box *b = &boxes[i];
        if (b->z0 > 0 || b->h < 0.8f || !vis(b->x0, b->y0, b->x1 + 3, b->y1 + 3, 0)) continue;
        float len = 0.2f * b->h, sx = len * 0.8f, sy = len * 0.5f;
        SDL_FPoint p[6] = {iso(b->x0, b->y0, 0), iso(b->x1, b->y0, 0), iso(b->x1 + sx, b->y0 + sy, 0),
                           iso(b->x1 + sx, b->y1 + sy, 0), iso(b->x0 + sx, b->y1 + sy, 0), iso(b->x0, b->y1, 0)};
        poly(p, 6, sh);
    }
    for (int i = 0; i < NB; i++) {
        Bld *b = &blds[i];
        if (!vis(b->x0, b->y0, b->x1 + 6, b->y1 + 6, 0)) continue;
        float len = 0.2f * b->h * (1 - b->f * 0.8f), sx = len * 0.8f, sy = len * 0.5f;
        SDL_FPoint p[6] = {iso(b->x0, b->y0, 0), iso(b->x1, b->y0, 0), iso(b->x1 + sx, b->y0 + sy, 0),
                           iso(b->x1 + sx, b->y1 + sy, 0), iso(b->x0 + sx, b->y1 + sy, 0), iso(b->x0, b->y1, 0)};
        poly(p, 6, sh);
    }
    for (int i = 0; i < ntree; i++) {
        Tree *t = &trees[i];
        if (!vis(t->x - 2, t->y - 2, t->x + 2, t->y + 2, 0)) continue;
        goval(t->x + 0.9f, t->y + 0.6f, 1.1f * t->s, 0.8f * t->s, 0, sh, 12);
    }
}

/* ---- trees ---- */
static void draw_palm(float bx, float by, float s, float ph)
{
    float sw = sinf(gtime * 1.2f + ph) * 1.0f, lean = 3.0f * s;
    float topx = bx + lean + sw * 0.4f, topy = by - 34 * s;
    for (int i = 0; i < 6; i++) {
        float t0 = i / 6.0f, t1 = (i + 1) / 6.0f;
        sline(bx + lean * t0 * t0, by - 34 * s * t0, bx + lean * t1 * t1, by - 34 * s * t1,
              (3.2f - 1.4f * t0) * s, C(0.52f, 0.40f, 0.28f));
    }
    for (int k = 0; k < 8; k++) {
        float a = k * PI / 4 + ph * 0.2f, len = 13 * s;
        float dx = cosf(a) * len, dy = sinf(a) * len * 0.40f, droop = 5 * s * fabsf(cosf(a)) + 2 * s;
        SDL_FColor col = (k % 2) ? C(0.18f, 0.50f, 0.18f) : C(0.28f, 0.62f, 0.22f);
        float mx = topx + dx * 0.55f, my = topy + dy * 0.55f - 3 * s;
        sline(topx, topy, mx, my, 2.4f * s, col);
        sline(mx, my, topx + dx + sw * 0.5f, topy + dy + droop, 1.6f * s, col);
    }
    scircle(topx - 1, topy + 1.5f, 1.6f * s, C(0.45f, 0.32f, 0.18f), 8);
    scircle(topx + 1.5f, topy + 2, 1.6f * s, C(0.45f, 0.32f, 0.18f), 8);
}

static void draw_tree(const Tree *t)
{
    SDL_FPoint b = iso(t->x, t->y, 0);
    float s = t->s * WORLD_SCALE, sw = sinf(gtime * 1.5f + t->ph) * 0.9f;
    if (t->type == 3) { draw_palm(b.x, b.y, s, t->ph); return; }
    if (t->type == 1) {                                                /* dark pine tree */
        sline(b.x, b.y, b.x, b.y - 8 * s, 2.4f * s, C(0.40f, 0.28f, 0.18f));
        for (int i = 0; i < 3; i++) {
            float w = (10 - i * 2.4f) * s, y0 = b.y - (6 + i * 7) * s, y1 = y0 - 11 * s;
            SDL_FPoint p[3] = {{b.x - w, y0}, {b.x + w, y0}, {b.x + sw * (i + 1) * 0.3f, y1}};
            poly(p, 3, C(0.10f + i * 0.04f, 0.34f + i * 0.05f, 0.16f + i * 0.02f));
        }
        return;
    }
    sline(b.x, b.y, b.x, b.y - 10 * s, 2.6f * s, C(0.42f, 0.30f, 0.20f));
    scircle(b.x, b.y - 15 * s, 11 * s, C(0.12f, 0.38f, 0.14f), 14);
    scircle(b.x - 2 * s + sw * 0.4f, b.y - 18 * s, 9.5f * s, C(0.20f, 0.50f, 0.18f), 14);
    scircle(b.x - 3 * s + sw, b.y - 21 * s, 6 * s, C(0.34f, 0.65f, 0.25f), 12);
    if (t->type == 2) {                                                /* flowering tree */
        for (int i = 0; i < 7; i++) {
            float a = i * 0.9f + t->ph;
            scircle(b.x + cosf(a) * 7 * s, b.y - 17 * s + sinf(a * 1.7f) * 6 * s, 1.7f * s,
                    (i % 2) ? C(0.98f, 0.55f, 0.70f) : C(1.0f, 0.85f, 0.30f), 6);
        }
    }
}

/* ---- boxes (hedges, walls, cars, stage ...) ---- */
static void draw_box_obj(const Box *b)
{
    float z0 = b->z0, z1 = b->z0 + b->h;
    switch (b->style) {
    case S_TANK: {                      /* CODE SUBMISSION HUB */
        float cx = (b->x0 + b->x1) / 2, cy = (b->y0 + b->y1) / 2, rx = (b->x1 - b->x0) / 2, ry = (b->y1 - b->y0) / 2;
        float t = SDL_GetTicks() * 0.001f;
        cylinder(cx, cy, rx + 0.55f, ry + 0.55f, 0, 0.25f, C(0.70f, 0.72f, 0.80f), C(0.52f, 0.55f, 0.64f));      /* stepped base */
        cylinder(cx, cy, rx + 0.25f, ry + 0.25f, 0.25f, 0.5f, C(0.80f, 0.82f, 0.90f), C(0.58f, 0.62f, 0.72f));
        cylinder(cx, cy, rx, ry, 0.5f, 3.0f, C(0.16f, 0.22f, 0.40f), C(0.20f, 0.34f, 0.68f));                    /* body */
        cylinder(cx, cy, rx + 0.05f, ry + 0.05f, 1.2f, 1.32f, C(0.45f, 0.95f, 1.0f), C(0.35f, 0.88f, 1.0f));      /* glowing bands */
        cylinder(cx, cy, rx + 0.05f, ry + 0.05f, 2.1f, 2.22f, C(0.45f, 0.95f, 1.0f), C(0.35f, 0.88f, 1.0f));
        cylinder(cx, cy, rx + 0.2f, ry + 0.2f, 3.0f, 3.25f, C(0.10f, 0.14f, 0.28f), C(0.12f, 0.18f, 0.36f));     /* rim */
        goval(cx, cy, rx - 0.1f, ry - 0.1f, 3.27f, C(0.04f, 0.09f, 0.20f), 24);                                   /* dock pad */
        float pl = 0.5f + 0.5f * sinf(t * 2.2f);
        goval(cx, cy, rx - 0.2f, ry - 0.2f, 3.28f, CA(C(0.2f, 0.9f, 1.0f), 0.20f + 0.12f * pl), 24);
        goval(cx, cy, (rx - 0.2f) * 0.62f, (ry - 0.2f) * 0.62f, 3.29f, CA(C(0.3f, 0.95f, 1.0f), 0.28f + 0.15f * pl), 20);
        goval(cx, cy, (rx - 0.2f) * 0.28f, (ry - 0.2f) * 0.28f, 3.30f, CA(C(0.8f, 1.0f, 1.0f), 0.55f), 14);
        SDL_FPoint c0 = iso(cx, cy, 3.3f), c1 = iso(cx, cy, 6.6f);                                               /* light beam */
        sline(c0.x, c0.y, c1.x, c1.y, 15 * WORLD_SCALE, CA(C(0.4f, 0.95f, 1.0f), 0.10f));
        sline(c0.x, c0.y, c1.x, c1.y, 7 * WORLD_SCALE, CA(C(0.6f, 1.0f, 1.0f), 0.16f));
        float bob = 0.18f * sinf(t * 2.0f);                                                                       /* floating HTML chip */
        drawbox(cx - 0.5f, cy - 0.5f, cx + 0.5f, cy + 0.5f, 4.1f + bob, 4.85f + bob, C(0.97f, 0.55f, 0.20f), C(0.85f, 0.42f, 0.12f));
        SDL_FPoint tp = iso(cx, cy, 4.85f + bob);
        SDL_SetRenderDrawColorFloat(R, 1, 1, 1, 1);
        SDL_RenderDebugText(R, tp.x - 12, tp.y - 4, "</>");
        SDL_FPoint sp = iso(cx, cy, 9.0f);                                                                        /* sign */
        const char *msg = "CODE SUBMISSION HUB";
        float w = (float)strlen(msg) * 8 + 12;
        SDL_SetRenderDrawColorFloat(R, 0.05f, 0.10f, 0.24f, 0.88f);
        SDL_FRect rr = {sp.x - w / 2, sp.y - 8, w, 18};
        SDL_RenderFillRect(R, &rr);
        SDL_SetRenderDrawColorFloat(R, 0.40f, 0.95f, 1.0f, 1);
        SDL_RenderRect(R, &rr);
        SDL_RenderDebugText(R, sp.x - w / 2 + 6, sp.y - 4, msg);
        break;
    }
    case S_LAMP: {
        SDL_FPoint p = iso(b->x0 + 0.15f, b->y0 + 0.15f, 0);
        sline(p.x, p.y, p.x, p.y - b->h * ZS, 1.5f * WORLD_SCALE, C(0.22f, 0.22f, 0.26f));
        scircle(p.x, p.y - b->h * ZS - 1, 2.4f * WORLD_SCALE, C(1.0f, 0.93f, 0.55f), 8);
        scircle(p.x, p.y - b->h * ZS - 1, 5.5f * WORLD_SCALE, CA(C(1.0f, 0.93f, 0.55f), 0.18f), 10);
        break;
    }
    case S_POST: case S_HOOP: {
        SDL_FPoint p = iso((b->x0 + b->x1) / 2, (b->y0 + b->y1) / 2, 0);
        sline(p.x, p.y, p.x, p.y - b->h * ZS, 1.6f, b->top);
        if (b->style == S_HOOP) {
            sline(p.x - 4, p.y - b->h * ZS, p.x + 4, p.y - b->h * ZS, 1.4f, C(0.95f, 0.95f, 0.95f));
            sell(p.x, p.y - b->h * ZS + 2, 3.2f, 1.6f, C(1.0f, 0.45f, 0.10f));
        }
        break;
    }
    case S_NET: {
        vquad(b->x0, b->y0, b->x1, b->y0, 0.9f, 2.3f, CA(C(0.95f, 0.95f, 0.95f), 0.35f));
        vquad(b->x0, b->y0, b->x1, b->y0, 2.1f, 2.3f, C(1, 1, 1));
        break;
    }
    case S_CAR:
        drawbox(b->x0, b->y0, b->x1, b->y1, z0, z1, b->top, b->side);
        drawbox(b->x0 + 0.15f, b->y0 + 0.6f, b->x1 - 0.15f, b->y1 - 0.5f, z1, z1 + 0.6f,
                C(0.62f, 0.78f, 0.88f), C(0.50f, 0.64f, 0.74f));
        break;
    case S_HEDGE:
        drawbox(b->x0, b->y0, b->x1, b->y1, z0, z1, b->top, b->side);
        {
            float lx = b->x1 - b->x0, ly = b->y1 - b->y0;
            int n = (int)(MAXF(lx, ly) / 1.1f);
            for (int i = 0; i <= n; i++) {
                float t = (n > 0) ? (float)i / n : 0;
                float x = b->x0 + 0.5f * lx + (lx > ly ? (t - 0.5f) * (lx - 0.6f) : 0);
                float y = b->y0 + 0.5f * ly + (ly >= lx ? (t - 0.5f) * (ly - 0.6f) : 0);
                SDL_FPoint p = iso(x, y, z1);
                scircle(p.x, p.y, 3.2f * WORLD_SCALE, (i % 2) ? C(0.30f, 0.62f, 0.26f) : C(0.17f, 0.45f, 0.17f), 8);
            }
        }
        break;
    case S_BED:
        drawbox(b->x0, b->y0, b->x1, b->y1, z0, z1, b->top, b->side);
        for (float x = b->x0 + 0.4f; x < b->x1; x += 0.9f) {
            SDL_FPoint p = iso(x, (b->y0 + b->y1) / 2, z1);
            sline(p.x, p.y, p.x, p.y - 2.5f, 1.0f, C(0.2f, 0.5f, 0.2f));
            scircle(p.x, p.y - 3.2f, 1.9f, ((int)(x * 3) % 3 == 0) ? C(0.95f, 0.3f, 0.4f) :
                    ((int)(x * 3) % 3 == 1) ? C(1.0f, 0.85f, 0.2f) : C(0.9f, 0.5f, 0.95f), 6);
        }
        break;
    case S_STAGE:
        drawbox(b->x0, b->y0, b->x1, b->y1, z0, z1, b->top, b->side);
        grect(b->x0 + 0.4f, b->y0 + 0.4f, b->x1 - 0.4f, b->y1 - 0.4f, z1, C(0.70f, 0.18f, 0.20f));
        grect(b->x1 - 0.9f, b->y0 + 0.4f, b->x1 - 0.4f, b->y1 - 0.4f, z1, C(0.95f, 0.78f, 0.25f));
        break;
    default:
        drawbox(b->x0, b->y0, b->x1, b->y1, z0, z1, b->top, b->side);
    }
}

static void draw_fountain(void)
{
    cylinder(FX, FY, FR, FR, 0, 0.8f, C(0.78f, 0.76f, 0.72f), C(0.72f, 0.70f, 0.66f));
    goval(FX, FY, FR - 0.5f, FR - 0.5f, 0.7f, C(0.28f, 0.58f, 0.85f), 24);
    for (int r = 0; r < 3; r++) {
        float rr = fmodf(gtime * 0.6f + r * 0.33f, 1.0f) * (FR - 0.6f);
        gring(FX, FY, rr + 0.25f, rr + 0.25f, 0.12f, CA(C(0.85f, 0.95f, 1.0f), 0.5f));
    }
    SDL_FPoint c = iso(FX, FY, 0.7f);
    const float W = WORLD_SCALE;
    sline(c.x, c.y, c.x, c.y - 9 * W, 3.2f * W, C(0.80f, 0.78f, 0.74f));
    sline(c.x, c.y - 6 * W, c.x, c.y - 10 * W, 5.5f * W, C(0.85f, 0.84f, 0.80f));
    for (int k = 0; k < 8; k++) {
        float a = k * PI / 4 + gtime * 0.3f;
        for (int j = 0; j < 4; j++) {
            float t = fmodf(gtime * 1.2f + j * 0.25f + k * 0.07f, 1.0f);
            float x = c.x + cosf(a) * t * 12 * W, y = c.y - 11 * W + (-4 * t * (1 - t) * 12 + t * t * 14) * W + sinf(a) * t * 5 * W;
            scircle(x, y, 1.1f * W, CA(C(0.85f, 0.95f, 1.0f), 0.85f), 5);
        }
    }
}

static int bi_of(const Bld *b) { return (int)(b - blds); }

static void draw_windows(const Bld *b, float hf, float alpha)
{
    int floors = (int)(b->h / 4.0f);
    if (floors < 1) floors = 1;
    float fh = b->h / floors;
    SDL_FColor wc = CA(C(0.50f, 0.78f, 0.92f), alpha), wl = CA(C(0.80f, 0.93f, 1.0f), alpha);
    float len = b->x1 - b->x0;
    int n = (int)(len / 3.0f); if (n < 1) n = 1;
    float st = len / n;
    for (int f = 0; f < floors; f++) {
        float z0 = f * fh + fh * 0.30f, z1 = f * fh + fh * 0.75f;
        if (z1 > hf) continue;
        for (int i = 0; i < n; i++) {
            float xa = b->x0 + i * st + st * 0.22f, xb = b->x0 + (i + 1) * st - st * 0.22f;
            if (f == 0 && door_hits(bi_of(b), 'S', xa, xb)) continue;
            vquad(xa, b->y1, xb, b->y1, z0, z1, wc);
            vquad(xa, b->y1, xb, b->y1, z1 - (z1 - z0) * 0.3f, z1, wl);
        }
    }
    len = b->y1 - b->y0;
    n = (int)(len / 3.0f); if (n < 1) n = 1;
    st = len / n;
    for (int f = 0; f < floors; f++) {
        float z0 = f * fh + fh * 0.30f, z1 = f * fh + fh * 0.75f;
        if (z1 > hf) continue;
        for (int i = 0; i < n; i++) {
            float ya = b->y0 + i * st + st * 0.22f, yb = b->y0 + (i + 1) * st - st * 0.22f;
            if (door_hits(bi_of(b), 'E', ya, yb)) continue;
            vquad(b->x1, ya, b->x1, yb, z0, z1, wc);
            vquad(b->x1, ya, b->x1, yb, z1 - (z1 - z0) * 0.3f, z1, wl);
        }
    }
}

/* One wall face with the doorways cut out (and a lintel over each doorway).
   S/E = outer faces, N/W = inner faces seen from inside. */
static void face_geom(int bi, char side, float *a0, float *a1, float *fixed)
{
    Bld *b = &blds[bi];
    switch (side) {
    case 'S': *a0 = b->x0; *a1 = b->x1; *fixed = b->y1; break;
    case 'E': *a0 = b->y0; *a1 = b->y1; *fixed = b->x1; break;
    case 'N': *a0 = b->x0 + WALL_T; *a1 = b->x1 - WALL_T; *fixed = b->y0 + WALL_T; break;
    default:  *a0 = b->y0 + WALL_T; *a1 = b->y1 - WALL_T; *fixed = b->x0 + WALL_T; break;
    }
}
static bool skipg = false;      /* true while drawing an upper floor: ground-floor doors are not there */
static int face_gaps(int bi, char side, float *g0, float *g1)
{
    int n = 0;
    for (int i = 0; i < ndoor && n < 8; i++) {
        if (doors[i].bld != bi || doors[i].side != side) continue;
        if (skipg && doors[i].ground_only) continue;
        g0[n] = doors[i].c - doors[i].w / 2; g1[n] = doors[i].c + doors[i].w / 2; n++;
    }
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && g0[j] < g0[j - 1]; j--) {
            float t = g0[j]; g0[j] = g0[j - 1]; g0[j - 1] = t;
            t = g1[j]; g1[j] = g1[j - 1]; g1[j - 1] = t;
        }
    return n;
}
static void face(int bi, char side, float zt, SDL_FColor col)
{
    float a0, a1, fx, g0[8], g1[8];
    face_geom(bi, side, &a0, &a1, &fx);
    int n = face_gaps(bi, side, g0, g1);
    bool alongx = (side == 'S' || side == 'N');
    float p = a0;
    for (int k = 0; k <= n; k++) {
        float u1 = (k < n) ? g0[k] : a1;
        if (u1 > p + 0.01f) {
            if (alongx) vquad(p, fx, u1, fx, 0, zt, col); else vquad(fx, p, fx, u1, 0, zt, col);
        }
        if (k < n) {
            if (zt > DOOR_H + 0.05f) {
                if (alongx) vquad(g0[k], fx, g1[k], fx, DOOR_H, zt, col); else vquad(fx, g0[k], fx, g1[k], DOOR_H, zt, col);
            }
            p = g1[k];
        }
    }
}
/* the flat top of a wall (thickness band), also with the doorways left open */
static void face_top(int bi, char side, float z, SDL_FColor col)
{
    Bld *b = &blds[bi];
    float a0, a1, fx, g0[8], g1[8];
    face_geom(bi, side, &a0, &a1, &fx);
    int n = face_gaps(bi, side, g0, g1);
    if (side == 'N') a0 = b->x0, a1 = b->x1;
    if (side == 'W') a0 = b->y0, a1 = b->y1;
    float p = a0;
    for (int k = 0; k <= n; k++) {
        float u1 = (k < n) ? g0[k] : a1;
        if (u1 > p + 0.01f) {
            switch (side) {
            case 'S': grect(p, b->y1 - WALL_T, u1, b->y1, z, col); break;
            case 'E': grect(b->x1 - WALL_T, p, b->x1, u1, z, col); break;
            case 'N': grect(p, b->y0, u1, b->y0 + WALL_T, z, col); break;
            default:  grect(b->x0, p, b->x0 + WALL_T, u1, z, col); break;
            }
        }
        if (k < n) p = g1[k];
    }
}

/* Part B: the walls you see from outside + roof. f = 1 means "cut-away" view. */
static void draw_bld_outside(int bi)
{
    Bld *b = &blds[bi];
    float f = b->f < 0.02f ? 0 : b->f;
    float tgt = ofloor(bi) * FH + 0.9f;   /* cut-away height follows the floor you are on */
    float hf = b->h - (b->h - tgt) * f;
    float a = 1 - f;
    SDL_FColor wall = b->wall;

    face(bi, 'S', hf, SH(wall, 0.78f));              /* front wall (with its doorways) */
    face(bi, 'E', hf, SH(wall, 0.95f));              /* side wall */
    /* floor-level base stripe */
    vquad(b->x0, b->y1, b->x1, b->y1, 0, MINF(0.5f, hf), SH(wall, 0.55f));
    vquad(b->x1, b->y0, b->x1, b->y1, 0, MINF(0.5f, hf), SH(wall, 0.60f));
    /* floor separation lines */
    int floors = (int)(b->h / 4.0f);
    for (int i = 1; i < floors; i++) {
        float z = i * (b->h / floors);
        if (z < hf) {
            vquad(b->x0, b->y1, b->x1, b->y1, z - 0.15f, z + 0.15f, SH(wall, 0.60f));
            vquad(b->x1, b->y0, b->x1, b->y1, z - 0.15f, z + 0.15f, SH(wall, 0.66f));
        }
    }
    if (f < 0.6f) draw_windows(b, hf, 1 - f / 0.6f);

    /* wooden doors you see from outside (they fade away in the cut-away view) */
    for (int i = 0; i < ndoor; i++) {
        if (doors[i].bld != bi || doors[i].side != 'S' || !doors[i].leaf) continue;
        float dh = MINF(DOOR_H, b->h - 0.6f), d0 = doors[i].c - doors[i].w / 2, d1 = doors[i].c + doors[i].w / 2;
        vquad(d0 - 0.15f, b->y1, d1 + 0.15f, b->y1, 0, dh + 0.2f, CA(C(0.85f, 0.85f, 0.80f), a));
        vquad(d0, b->y1, d1, b->y1, 0, dh, CA(C(0.30f, 0.20f, 0.14f), a));
        vquad(doors[i].c - 0.04f, b->y1, doors[i].c + 0.04f, b->y1, 0, dh, CA(C(0.15f, 0.10f, 0.07f), a));
    }

    /* wall tops when cut-away */
    if (f > 0.02f) {
        SDL_FColor wt = CA(LT(wall, 1.15f), 1);
        face_top(bi, 'S', hf, wt);
        face_top(bi, 'E', hf, wt);
    }
    /* roof */
    if (a > 0.02f) {
        grect(b->x0, b->y0, b->x1, b->y1, b->h, CA(SH(b->roof, 0.75f), a));
        grect(b->x0 + 0.5f, b->y0 + 0.5f, b->x1 - 0.5f, b->y1 - 0.5f, b->h, CA(b->roof, a));
        if (f < 0.3f) {
            float cx = (b->x0 + b->x1) / 2, cy = (b->y0 + b->y1) / 2;
            drawbox(cx - 1.4f, cy - 0.9f, cx + 0.2f, cy + 0.9f, b->h, b->h + 0.9f, C(0.80f, 0.82f, 0.84f), C(0.60f, 0.62f, 0.66f));
            if (b->x1 - b->x0 > 12)
                drawbox(cx + 3.5f, cy - 0.9f, cx + 5.1f, cy + 0.9f, b->h, b->h + 0.9f, C(0.80f, 0.82f, 0.84f), C(0.60f, 0.62f, 0.66f));
        }
    }
}

/* Part A: floor and the two back walls, seen from inside */
static void draw_bld_inside(int bi)
{
    Bld *b = &blds[bi];
    skipg = ofloor(bi) > 0;
    float ix0 = b->x0 + WALL_T, ix1 = b->x1 - WALL_T, iy0 = b->y0 + WALL_T, iy1 = b->y1 - WALL_T;
    SDL_FColor fl, inner = LT(b->wall, 1.12f);
    switch (b->kind) {
    case K_MESS:    fl = C(0.84f, 0.72f, 0.54f); break;
    case K_HOSTEL:  fl = C(0.72f, 0.58f, 0.44f); break;
    case K_GYM:     fl = C(0.58f, 0.62f, 0.68f); break;
    case K_KITCHEN: fl = C(0.88f, 0.88f, 0.90f); break;
    case K_HALL:    fl = C(0.58f, 0.32f, 0.34f); break;
    case K_STORE:   fl = C(0.62f, 0.60f, 0.56f); break;
    default:        fl = C(0.80f, 0.82f, 0.86f); break;
    }
    grect(b->x0, b->y0, b->x1, b->y1, 0, SH(b->wall, 0.6f));          /* under the walls */
    grect(ix0, iy0, ix1, iy1, 0.02f, fl);
    SDL_FColor tl = SH(fl, 0.9f);
    for (float x = ix0 + 2; x < ix1; x += 2) grect(x, iy0, x + 0.05f, iy1, 0.03f, tl);
    for (float y = iy0 + 2; y < iy1; y += 2) grect(ix0, y, ix1, y + 0.05f, 0.03f, tl);
    /* the 2 back walls (inner faces) and their tops */
    /* rugs / carpets lie on the floor */
    for (int i = 0; i < nrug; i++) {
        if (rugs[i].owner != bi || rugs[i].floor != ofloor(bi)) continue;
        grect(rugs[i].x0 - 0.08f, rugs[i].y0 - 0.08f, rugs[i].x1 + 0.08f, rugs[i].y1 + 0.08f, 0.035f, SH(rugs[i].c, 0.75f));
        grect(rugs[i].x0, rugs[i].y0, rugs[i].x1, rugs[i].y1, 0.04f, rugs[i].c);
    }
    float hb = b->h - (b->h - 3.4f) * b->f;       /* back walls get lower so you can see inside */
    if (hb > b->h) hb = b->h;
    face(bi, 'N', hb, SH(inner, 0.95f));           /* back wall (with the BACK doorway) */
    face(bi, 'W', hb, SH(inner, 0.80f));
    vquad(ix0, iy0, ix1, iy0, 0, 0.35f, SH(inner, 0.6f));
    vquad(ix0, iy0, ix0, iy1, 0, 0.35f, SH(inner, 0.5f));
    SDL_FColor wt = LT(b->wall, 1.15f);
    face_top(bi, 'N', hb, wt);
    face_top(bi, 'W', hb, wt);
    /* board on the wall (only in single-room buildings) */
    if (b->kind == K_CLASS || b->kind == K_HOSTEL || b->kind == K_MESS || b->kind == K_KITCHEN) {
        /* rooms have their own boards / signs */
    } else if (b->kind != K_GYM) {
        float cx = (ix0 + ix1) / 2, hw = MINF(2.6f, (ix1 - ix0) * 0.3f);
        SDL_FColor bc = (b->kind == K_CLASS) ? C(0.96f, 0.97f, 0.96f) : C(0.25f, 0.40f, 0.30f);
        vquad(cx - hw, iy0, cx + hw, iy0, 1.3f, 2.7f, bc);
        vquad(cx - hw, iy0, cx + hw, iy0, 1.2f, 1.3f, C(0.5f, 0.38f, 0.25f));
    } else {
        vquad(ix0 + 0.3f, iy0, ix1 - 0.3f, iy0, 0.9f, 2.6f, C(0.70f, 0.82f, 0.90f));   /* mirror */
    }
    /* window light on the back wall */
    float cx = (ix0 + ix1) / 2;
    if (ix1 - ix0 > 7 && b->kind != K_CLASS && b->kind != K_HOSTEL) {
        vquad(ix0 + 1.0f, iy0, ix0 + 3.0f, iy0, 1.4f, 3.0f, C(0.60f, 0.82f, 0.95f));
        vquad(ix1 - 3.0f, iy0, ix1 - 1.0f, iy0, 1.4f, 3.0f, C(0.60f, 0.82f, 0.95f));
    }
    (void)cx;
    skipg = false;
}

static void draw_player(float px, float py, float walk, float faceX, bool moving)
{
    const float k = PLAYER_SCALE;                 /* smaller boy = bigger-feeling campus */
    SDL_FPoint b = iso(px, py, 0);
    float bob = (moving ? fabsf(sinf(walk)) * 0.8f : 0) * 0.8f * k;
    float sw  = (moving ? sinf(walk) * 1.8f : 0) * 0.8f * k;
    float s = faceX >= 0 ? 1.0f : -1.0f;
    sell(b.x, b.y + 0.5f * k, 2.9f * k, 1.3f * k, CA(C(0, 0, 0), 0.30f));
    sline(b.x - 0.8f * k, b.y - 2.5f * k - bob, b.x - 0.8f * k + sw, b.y, 1.2f * k, C(0.18f,0.20f,0.35f));
    sline(b.x + 0.8f * k, b.y - 2.5f * k - bob, b.x + 0.8f * k - sw, b.y, 1.2f * k, C(0.18f,0.20f,0.35f));
    sline(b.x - s * 2.0f * k, b.y - 5.0f * k - bob, b.x - s * 2.0f * k, b.y - 3.2f * k - bob, 1.7f * k, C(0.55f,0.32f,0.18f));
    sline(b.x, b.y - 6.1f * k - bob, b.x, b.y - 2.5f * k - bob, 3.2f * k, C(0.20f,0.45f,0.90f));
    sline(b.x - 1.6f * k, b.y - 5.2f * k - bob, b.x - 1.9f * k + sw * 0.5f, b.y - 3.4f * k - bob, 1.0f * k, C(0.88f,0.68f,0.52f));
    sline(b.x + 1.6f * k, b.y - 5.2f * k - bob, b.x + 1.9f * k - sw * 0.5f, b.y - 3.4f * k - bob, 1.0f * k, C(0.88f,0.68f,0.52f));
    scircle(b.x, b.y - 7.8f * k - bob, 1.7f * k, C(0.90f,0.70f,0.54f), 10);
    sell(b.x, b.y - 8.6f * k - bob, 1.8f * k, 1.1f * k, C(0.12f,0.08f,0.06f));
}

/* ---- depth sorting ---- */
enum { O_BOX, O_TREE, O_BLD, O_BLDIN, O_FURN, O_PLAYER, O_FOUNT, O_ITEM };
typedef struct { int kind, idx; float x0, y0, x1, y1; int group, role; bool done; } Obj;
static Obj objs[4500];
static int nobj = 0;
/* ================================================================== */
/* DEBUG CODE HUNT: 20 Python, C and Java snippets (10 valid, 10 buggy) */
/* ================================================================== */
#define NCOMP 20
typedef struct { const char *title, *l1, *l2; bool correct; } Comp;
static const Comp comps[NCOMP] = {
    /* Ten valid snippets, mixed across Python, C and Java. */
    {"PYTHON", "nums = [2, 4, 6]", "total = sum(nums)", true},
    {"C", "int total = 5 + 3;", "", true},
    {"JAVA", "int max = Math.max(a, b);", "", true},
    {"PYTHON", "for n in nums:", "    print(n)", true},
    {"C", "int values[3] = {2, 4, 6};", "", true},
    {"JAVA", "String name = \"Maya\";", "", true},
    {"PYTHON", "average = sum(nums) / len(nums)", "", true},
    {"C", "int clamp(int n) { return n < 0 ? 0 : n; }", "", true},
    {"JAVA", "int[] nums = {2, 4, 6};", "", true},
    {"PYTHON", "word = \"campus\"", "print(word.upper())", true},
    /* Ten snippets with a basic syntax or type error. */
    {"C", "int total = 5 + ;", "", false},
    {"JAVA", "int age = \"18\";", "", false},
    {"PYTHON", "if score >= 50", "", false},
    {"C", "int 2count = 4;", "", false},
    {"JAVA", "System.out.println(\"Hi\")", "", false},
    {"PYTHON", "items = [1, 2, 3", "", false},
    {"C", "char grade = \"A\";", "", false},
    {"JAVA", "int[] values = {1, 2, \"3\"};", "", false},
    {"PYTHON", "total = 10 / 0", "", false},
    {"C", "if (x > 0) {", "    y = 1;", false},
};

/* state 0 = lying where it was hidden, 1 = in the bag, 2 = submitted */
typedef struct { int comp, bld, floor; float x, y; int state; bool armed; } Item;
static Item items[NCOMP];
static int nitems = 0;

static bool spot_ok(int bld, int fl, float x, float y)
{
    curgrp = bld >= 0 ? grp(bld) : 0; curfloor = fl;
    if (blocked(x, y)) return false;
    for (int a = 0; a < 8; a++)
        if (blocked(x + cosf(a * PI / 4) * 0.5f, y + sinf(a * PI / 4) * 0.5f)) return false;
    return true;
}

static bool find_spot(int bld, int fl, float x0, float y0, float x1, float y1, float cx, float cy, float *ox, float *oy)
{
    float best = 1e9f; bool found = false;
    for (float y = y0; y <= y1; y += 0.15f)
        for (float x = x0; x <= x1; x += 0.15f) {
            float d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
            if (d < best && spot_ok(bld, fl, x, y)) { best = d; *ox = x; *oy = y; found = true; }
        }
    return found;
}

static void place_items(void)
{
    static const struct { const char *b; int fl; const char *r; } sp[16] = {
        {"MAIN COLLEGE W", 0, "LIBRARY"},        {"MAIN COLLEGE", 0, "STAFF ROOM"},
        {"MAIN COLLEGE W", 1, "COMPUTER LAB 1"}, {"MAIN COLLEGE E", 2, "LAB 322"},
        {"MAIN COLLEGE W", 3, "RESEARCH LAB"},   {"MECH / CIVIL BLOCK", 0, "MECH WORKSHOP"},
        {"MECH / CIVIL BLOCK", 1, "CIVIL CLASS 203"}, {"MECH / CIVIL BLOCK", 2, "CAD LAB"},
        {"GIRLS HOSTEL", 0, "ROOM GH-G02"},      {"A BLOCK HOSTEL", 2, "ROOM A-203"},
        {"B BLOCK HOSTEL", 1, "ROOM B-104"},     {"GYM", 0, "WORKOUT FLOOR"},
        {"KITCHEN", 0, "COOKING AREA"},          {"MESS 2", 0, "DINING HALL"},
        {"CONVENTION HALL", 0, "CONVENTION AREA"}, {"STORE ROOM", 0, "STOCK ROOM"}};
    /* 4 outdoor spots: two on the football field, two on the volleyball / sand court */
    static const float od[4][2] = {{55.0f, 68.0f}, {72.0f, 82.0f}, {18.0f, 102.0f}, {30.0f, 112.0f}};
    /* which letter goes where: 8 right + 8 wrong inside, 2 right + 2 wrong outside */
    static const int perm[NCOMP] = {0, 10, 3, 12, 5, 14, 7, 16, 1, 18, 9, 11, 2, 13, 4, 15, 6, 17, 8, 19};
    nitems = 0;
    for (int j = 0; j < 16; j++) {
        int bi = -1;
        for (int i = 0; i < NB; i++) if (!strcmp(blds[i].name, sp[j].b)) bi = i;
        if (bi < 0) continue;
        for (int r = 0; r < nroom; r++) {
            if (rooms[r].bld != bi || rooms[r].floor != sp[j].fl || strcmp(rooms[r].name, sp[j].r)) continue;
            float x, y, cx = (rooms[r].x0 + rooms[r].x1) / 2, cy = (rooms[r].y0 + rooms[r].y1) / 2;
            if (find_spot(bi, sp[j].fl, rooms[r].x0 + 0.6f, rooms[r].y0 + 0.6f, rooms[r].x1 - 0.6f, rooms[r].y1 - 0.6f, cx, cy, &x, &y))
                items[nitems++] = (Item){perm[j], bi, sp[j].fl, x, y, 0, true};
            break;
        }
    }
    for (int j = 0; j < 4; j++) {
        float x, y;
        if (find_spot(-1, 0, od[j][0] - 3, od[j][1] - 3, od[j][0] + 3, od[j][1] + 3, od[j][0], od[j][1], &x, &y))
            items[nitems++] = (Item){perm[16 + j], -1, 0, x, y, 0, true};
    }
    curgrp = 0; curfloor = 0;
}

/* a rolled-up golden letter that floats and glows */
static void draw_item(int k)
{
    const Item *it = &items[k];
    float x = it->x, y = it->y, bob = 0.07f * sinf(gtime * 2.4f + k), pul = 0.5f + 0.5f * sinf(gtime * 3 + k);
    goval(x, y, 0.65f, 0.65f, 0.03f, CA(C(1.0f, 0.85f, 0.3f), 0.16f + 0.12f * pul), 18);
    goval(x, y, 0.38f, 0.38f, 0.04f, CA(C(1.0f, 0.92f, 0.55f), 0.30f), 14);
    drawbox(x - 0.30f, y - 0.16f, x + 0.30f, y + 0.16f, 0.24f + bob, 0.36f + bob, C(0.95f, 0.85f, 0.60f), C(0.80f, 0.66f, 0.42f));
    drawbox(x - 0.37f, y - 0.20f, x - 0.29f, y + 0.20f, 0.22f + bob, 0.38f + bob, C(0.62f, 0.40f, 0.18f), C(0.46f, 0.28f, 0.12f));
    drawbox(x + 0.29f, y - 0.20f, x + 0.37f, y + 0.20f, 0.22f + bob, 0.38f + bob, C(0.62f, 0.40f, 0.18f), C(0.46f, 0.28f, 0.12f));
    drawbox(x - 0.04f, y - 0.17f, x + 0.04f, y + 0.17f, 0.36f + bob, 0.39f + bob, C(0.78f, 0.12f, 0.12f), C(0.55f, 0.08f, 0.08f));
    SDL_FPoint a = iso(x, y, 0.42f + bob), b = iso(x, y, 1.8f + bob);
    sline(a.x, a.y, b.x, b.y, 6.0f, CA(C(1.0f, 0.92f, 0.45f), 0.10f + 0.10f * pul));
}

static int order[4500], nord = 0;

static void push(int kind, int idx, float x0, float y0, float x1, float y1, int group, int role, float h)
{
    if (nobj >= 4500) return;
    if (!vis(x0, y0, x1, y1, h)) return;
    objs[nobj++] = (Obj){kind, idx, x0, y0, x1, y1, group, role, false};
}

static void obox(const Obj *o, const Obj *other, float *x0, float *y0, float *x1, float *y1)
{
    if (o->group >= 0 && o->group != other->group) {
        *x0 = blds[o->group].x0; *y0 = blds[o->group].y0; *x1 = blds[o->group].x1; *y1 = blds[o->group].y1;
    } else {
        *x0 = o->x0; *y0 = o->y0; *x1 = o->x1; *y1 = o->y1;
    }
}

/* must a be drawn before b ? */
static bool behind(const Obj *a, const Obj *b)
{
    if (a->group >= 0 && a->group == b->group && a->role != b->role) return a->role < b->role;
    float ax0, ay0, ax1, ay1, bx0, by0, bx1, by1;
    obox(a, b, &ax0, &ay0, &ax1, &ay1);
    obox(b, a, &bx0, &by0, &bx1, &by1);
    if (ax1 <= bx0) return true;
    if (bx1 <= ax0) return false;
    if (ay1 <= by0) return true;
    if (by1 <= ay0) return false;
    return (ax0 + ax1 + ay0 + ay1) < (bx0 + bx1 + by0 + by1);
}

static void visit(int i)
{
    objs[i].done = true;
    for (int j = 0; j < nobj; j++)
        if (!objs[j].done && behind(&objs[j], &objs[i])) visit(j);
    order[nord++] = i;
}

/* Inside one building the order must be: floor+back walls, then furniture and player, then front walls/roof */
static int lmid[4500], lnm, lout[4500], lno;
static void lvisit(int oi)
{
    objs[oi].done = true;
    for (int k = 0; k < lnm; k++)
        if (!objs[lmid[k]].done && behind(&objs[lmid[k]], &objs[oi])) lvisit(lmid[k]);
    lout[lno++] = oi;
}

static void fix_groups(void)
{
    for (int g = 0; g < NB; g++) {
        static int pos[4500]; int np = 0, a = -1, b = -1;
        lnm = 0; lno = 0;
        for (int n = 0; n < nord; n++) {
            int oi = order[n];
            if (objs[oi].group != g) continue;
            pos[np++] = n;
            if (objs[oi].role == 0) a = oi;
            else if (objs[oi].role == 2) b = oi;
            else { lmid[lnm++] = oi; objs[oi].done = false; }
        }
        if (np < 2) continue;
        for (int k = 0; k < lnm; k++) if (!objs[lmid[k]].done) lvisit(lmid[k]);
        int p = 0;
        if (a >= 0) order[pos[p++]] = a;
        for (int k = 0; k < lno; k++) order[pos[p++]] = lout[k];
        if (b >= 0) order[pos[p++]] = b;
    }
}

/* a long object can make the depth sort loop; push anything that must be behind a building back in front of it in the draw list */
static void repair_order(void)
{
    for (int n = 0; n < nord; n++) {
        const Obj *o = &objs[order[n]];
        if (o->kind != O_BLD && o->kind != O_BLDIN) continue;
        for (int m = n + 1; m < nord; m++) {
            const Obj *x = &objs[order[m]];
            if (x->group >= 0 && x->group == o->group) continue;
            if (behind(x, o) && !behind(o, x)) {
                int t = order[m];
                memmove(&order[n + 1], &order[n], (size_t)(m - n) * sizeof(int));
                order[n] = t;
                n++;
                o = &objs[order[n]];
            }
        }
    }
}

static void draw_scene(float px, float py, int pgroup, float walk, float faceX, bool moving)
{
    nobj = 0; nord = 0;
    for (int i = 0; i < NB; i++) {
        Bld *b = &blds[i];
        bool open = b->f > 0.02f || pgroup == i;
        if (open) {
            push(O_BLDIN, i, b->x0, b->y0, b->x1, b->y1, i, 0, b->h);
            push(O_BLD, i, b->x0, b->y0, b->x1, b->y1, i, 2, b->h);
            for (int k = 0; k < nfurn; k++)
                if (furn[k].owner == i && furn[k].floor == ofloor(i)) push(O_FURN, k, furn[k].x0, furn[k].y0, furn[k].x1, furn[k].y1, i, 1, 2);
            for (int k = 0; k < nitems; k++)
                if (items[k].state == 0 && items[k].bld == i && items[k].floor == ofloor(i))
                    push(O_ITEM, k, items[k].x - 0.3f, items[k].y - 0.3f, items[k].x + 0.3f, items[k].y + 0.3f, i, 1, 2);
        } else {
            push(O_BLD, i, b->x0, b->y0, b->x1, b->y1, -1, 1, b->h);
        }
    }
    for (int i = 0; i < nbox; i++) push(O_BOX, i, boxes[i].x0, boxes[i].y0, boxes[i].x1, boxes[i].y1, -1, 1, boxes[i].z0 + boxes[i].h + 1);
    for (int k = 0; k < nitems; k++)
        if (items[k].state == 0 && items[k].bld < 0) push(O_ITEM, k, items[k].x - 0.3f, items[k].y - 0.3f, items[k].x + 0.3f, items[k].y + 0.3f, -1, 1, 2);
    for (int i = 0; i < ntree; i++) push(O_TREE, i, trees[i].x - 0.5f, trees[i].y - 0.5f, trees[i].x + 0.5f, trees[i].y + 0.5f, -1, 1, 9);
    push(O_FOUNT, 0, FX - FR, FY - FR, FX + FR, FY + FR, -1, 1, 8);
    push(O_PLAYER, 0, px - 0.3f, py - 0.3f, px + 0.3f, py + 0.3f, pgroup, 1, 3);

    for (int i = 0; i < nobj; i++) if (!objs[i].done) visit(i);
    fix_groups();
    repair_order();

    for (int n = 0; n < nord; n++) {
        Obj *o = &objs[order[n]];
        switch (o->kind) {
        case O_BLDIN:  zoff = ofloor(o->idx) * FH; draw_bld_inside(o->idx); zoff = 0; break;
        case O_BLD:    draw_bld_outside(o->idx); break;
        case O_ITEM:   zoff = items[o->idx].bld >= 0 ? ofloor(items[o->idx].bld) * FH : 0; draw_item(o->idx); zoff = 0; break;
        case O_FURN: {
            Furn *f = &furn[o->idx];
            zoff = ofloor(f->owner) * FH;
            drawbox(f->x0, f->y0, f->x1, f->y1, f->z0, f->h, f->top, f->side);
            zoff = 0;
            break;
        }
        case O_BOX:    draw_box_obj(&boxes[o->idx]); break;
        case O_TREE:   draw_tree(&trees[o->idx]); break;
        case O_FOUNT:  draw_fountain(); break;
        case O_PLAYER: zoff = curfloor * FH; draw_player(px, py, walk, faceX, moving); zoff = 0; break;
        }
    }
}

/* ---- text ---- */
static void text(float x, float y, const char *s, float scale, SDL_FColor c)
{
    SDL_SetRenderScale(R, scale, scale);
    SDL_SetRenderDrawColorFloat(R, c.r, c.g, c.b, c.a);
    SDL_RenderDebugText(R, x / scale, y / scale, s);
    SDL_SetRenderScale(R, 1, 1);
}

static void labels(float px, float py)
{
    for (int i = 0; i < NB; i++) {
        Bld *b = &blds[i];
        if (b->f > 0.5f) continue;
        float cx = (b->x0 + b->x1) / 2, cy = (b->y0 + b->y1) / 2;
        if (fabsf(cx - px) > 45 || fabsf(cy - py) > 45) continue;
        SDL_FPoint p = iso(cx, cy, b->h + 1.2f);
        float w = (float)strlen(b->name) * 8 + 8, h = 12;
        float x = p.x - w / 2, y = p.y - 6;
        SDL_SetRenderDrawColorFloat(R, 0.05f, 0.08f, 0.10f, 0.65f);
        SDL_FRect r = {x, y, w, h};
        SDL_RenderFillRect(R, &r);
        SDL_SetRenderDrawColorFloat(R, 1, 1, 0.9f, 1);
        SDL_RenderDebugText(R, x + 4, y + 2, b->name);
    }
    /* room signs (only while you are inside that building) */
    for (int i = 0; i < nroom; i++) {
        Room *r = &rooms[i];
        if (blds[r->bld].f < 0.7f || r->floor != ofloor(r->bld)) continue;
        float cx = (r->x0 + r->x1) / 2;
        if (fabsf(cx - px) > 28 || fabsf((r->y0 + r->y1) / 2 - py) > 20) continue;
        SDL_FPoint p = iso(cx, r->y0 + 0.2f, 2.9f + ofloor(r->bld) * FH);
        float w = (float)strlen(r->name) * 8 + 6;
        SDL_SetRenderDrawColorFloat(R, 0.10f, 0.12f, 0.16f, 0.78f);
        SDL_FRect rr = {p.x - w / 2, p.y - 5, w, 11};
        SDL_RenderFillRect(R, &rr);
        SDL_SetRenderDrawColorFloat(R, 1, 0.95f, 0.7f, 1);
        SDL_RenderDebugText(R, p.x - w / 2 + 3, p.y - 3, r->name);
    }
    /* a few labels on the ground */
    SDL_FPoint p = iso(44.2f, 88.7f, 2.4f);
    SDL_SetRenderDrawColorFloat(R, 0.8f, 0.1f, 0.15f, 1);
    SDL_RenderDebugText(R, p.x - 44, p.y - 6, "I LOVE SSEC");
    p = iso(FX, FY + 4.5f, 0);
    SDL_SetRenderDrawColorFloat(R, 1, 1, 1, 0.9f);
    SDL_RenderDebugText(R, p.x - 20, p.y, "FOUNTAIN");
    p = iso(83, 72, 2.2f);
    SDL_RenderDebugText(R, p.x - 16, p.y - 4, "STAGE");
    p = iso(63.5f, 75, 0);
    SDL_RenderDebugText(R, p.x - 60, p.y - 4, "MAIN PLAYGROUND");
    p = iso(31, 77, 0);
    SDL_RenderDebugText(R, p.x - 28, p.y - 4, "BASKETBALL");
    p = iso(14.6f, 106.7f, 0);
    SDL_RenderDebugText(R, p.x - 24, p.y, "VOLLEYBALL");
    p = iso(29, 106.7f, 0);
    SDL_RenderDebugText(R, p.x - 20, p.y, "KABADDI");
    p = iso(10.8f, 77.0f, 0);
    SDL_RenderDebugText(R, p.x - 20, p.y + 22, "CAR PARKING");
    p = iso(24, 87.6f, 0.6f);
    SDL_RenderDebugText(R, p.x - 40, p.y - 12, "FLOWERS & PLANTS");
    p = iso(12, 90.8f, 0);
    SDL_RenderDebugText(R, p.x - 20, p.y + 6, "START / END");
}

/* ---- minimap (small, around the player) ---- */
static void fillr(float x, float y, float w, float h, SDL_FColor c)
{
    SDL_FRect r = {x, y, w, h};
    SDL_SetRenderDrawColorFloat(R, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(R, &r);
}

static void draw_map_content(float ox, float oy, float sc, float cx, float cy, bool names)
{
    /* (ox,oy) = screen position of map point (cx,cy) */
#define MX(wx) (ox + ((wx) - cx) * sc)
#define MY(wy) (oy + ((wy) - cy) * sc)
    fillr(MX(0), MY(0), MAP_W * sc, MAP_H * sc, C(0.20f, 0.38f, 0.22f));
    fillr(MX(24.5f), MY(2), 61.3f * sc, 18 * sc, C(0.33f, 0.58f, 0.30f));
    fillr(MX(7.8f), MY(20), 78 * sc, 73 * sc, C(0.33f, 0.58f, 0.30f));
    fillr(MX(4.5f), MY(86), 79.8f * sc, 38 * sc, C(0.33f, 0.58f, 0.30f));
    for (int i = 0; i < NZ; i++)
        fillr(MX(zones[i].x0), MY(zones[i].y0), (zones[i].x1 - zones[i].x0) * sc, (zones[i].y1 - zones[i].y0) * sc, zone_col(zones[i].type));
    fillr(MX(45.5f), MY(57), 40 * sc, 35.5f * sc, C(0.30f, 0.58f, 0.27f));
    for (int i = 0; i < nbox; i++) {
        Box *b = &boxes[i];
        if (b->style == S_HEDGE) fillr(MX(b->x0), MY(b->y0), (b->x1 - b->x0) * sc, (b->y1 - b->y0) * sc, C(0.18f, 0.46f, 0.18f));
        else if (b->style == S_WALL) fillr(MX(b->x0), MY(b->y0), MAXF(1, (b->x1 - b->x0) * sc), MAXF(1, (b->y1 - b->y0) * sc), C(0.85f, 0.75f, 0.65f));
        else if (b->style == S_STAGE) fillr(MX(b->x0), MY(b->y0), (b->x1 - b->x0) * sc, (b->y1 - b->y0) * sc, C(0.6f, 0.6f, 0.65f));
    }
    for (int i = 0; i < ntree; i++) {
        if (!in_compound(trees[i].x, trees[i].y)) continue;
        float x = MX(trees[i].x), y = MY(trees[i].y);
        fillr(x - sc * 0.5f, y - sc * 0.5f, sc, sc, C(0.12f, 0.36f, 0.14f));
    }
    scircle(MX(FX), MY(FY), FR * sc, C(0.30f, 0.60f, 0.88f), 14);
    for (int i = 0; i < NB; i++) {
        Bld *b = &blds[i];
        fillr(MX(b->x0), MY(b->y0), (b->x1 - b->x0) * sc, (b->y1 - b->y0) * sc, SH(b->roof, 0.85f));
        fillr(MX(b->x0) + 1, MY(b->y0) + 1, (b->x1 - b->x0) * sc - 2, (b->y1 - b->y0) * sc - 2, b->roof);
        if (names) {
            SDL_SetRenderDrawColorFloat(R, 0.05f, 0.05f, 0.1f, 1);
            SDL_RenderDebugText(R, MX(b->x0) + 2, MY(b->y0) + 2, b->name);
        }
    }
#undef MX
#undef MY
}

static void draw_minimap(float px, float py, float fdx, float fdy)
{
    float mw = 190, mh = 190, mx = SCREEN_W - mw - 22, my = 22, rad = 17.0f;
    float sc = mw / (2 * rad);
    fillr(mx - 5, my - 5, mw + 10, mh + 10, C(0.06f, 0.09f, 0.08f));
    SDL_Rect clip = {(int)mx, (int)my, (int)mw, (int)mh};
    SDL_SetRenderClipRect(R, &clip);
    draw_map_content(mx + mw / 2, my + mh / 2, sc, px, py, false);
    /* player arrow */
    float cx = mx + mw / 2, cy = my + mh / 2, l = sqrtf(fdx * fdx + fdy * fdy);
    if (l < 0.01f) { fdx = 0; fdy = 1; l = 1; }
    fdx /= l; fdy /= l;
    SDL_FPoint tri[3] = {{cx + fdx * 8, cy + fdy * 8}, {cx - fdy * 5 - fdx * 5, cy + fdx * 5 - fdy * 5},
                         {cx + fdy * 5 - fdx * 5, cy - fdx * 5 - fdy * 5}};
    poly(tri, 3, C(1.0f, 0.90f, 0.1f));
    SDL_SetRenderClipRect(R, NULL);
    SDL_SetRenderDrawColorFloat(R, 1, 1, 1, 1);
    SDL_FRect fr = {mx, my, mw, mh};
    SDL_RenderRect(R, &fr);
    text(mx + mw / 2 - 4, my + 3, "N", 1.0f, C(1, 1, 1));
}

static void draw_fullmap(float px, float py)
{
    fillr(0, 0, SCREEN_W, SCREEN_H, CA(C(0, 0, 0), 0.75f));
    float sc = 4.9f, ox = SCREEN_W / 2.0f, oy = SCREEN_H / 2.0f;
    fillr(ox - MAP_W * sc / 2 - 6, oy - MAP_H * sc / 2 - 6, MAP_W * sc + 12, MAP_H * sc + 12, C(0.06f, 0.09f, 0.08f));
    draw_map_content(ox, oy, sc, MAP_W / 2, MAP_H / 2, true);
    float x = ox + (px - MAP_W / 2) * sc, y = oy + (py - MAP_H / 2) * sc;
    float pulse = 4 + 2 * sinf(gtime * 6);
    scircle(x, y, pulse + 2, C(0, 0, 0), 12);
    scircle(x, y, pulse, C(1.0f, 0.90f, 0.1f), 12);
    text(20, 20, "FULL CAMPUS MAP  (press M or ESC to close)", 1.5f, C(1, 1, 1));
    text(20, 46, "YELLOW DOT = YOU", 1.5f, C(1, 0.9f, 0.1f));
}

static bool near_stair(float x, float y)
{
    for (int i = 0; i < nstairs; i++)
        if (curgrp && stairs[i].g == curgrp && x > stairs[i].x0 - 0.9f && x < stairs[i].x1 + 0.9f && y > stairs[i].y0 - 0.9f && y < stairs[i].y1 + 0.9f) return true;
    return false;
}

static const char *place_name(float x, float y)
{
    static char pn[96];
    for (int i = 0; i < nroom; i++)
        if (rooms[i].floor == ofloor(rooms[i].bld) && x > rooms[i].x0 && x < rooms[i].x1 && y > rooms[i].y0 && y < rooms[i].y1) {
            if (grp(rooms[i].bld)) snprintf(pn, sizeof pn, "%s - %s - %s", in_cluster(rooms[i].bld) ? "MAIN COLLEGE" : blds[rooms[i].bld].name, fname(curgrp, curfloor), rooms[i].name);
            else snprintf(pn, sizeof pn, "%s - %s", blds[rooms[i].bld].name, rooms[i].name);
            return pn;
        }
    for (int i = 0; i < NB; i++) if (inside_bld(&blds[i], x, y)) return blds[i].name;
    for (size_t i = 0; i < sizeof(areas) / sizeof(areas[0]); i++)
        if (x > areas[i].x0 && x < areas[i].x1 && y > areas[i].y0 && y < areas[i].y1) return areas[i].name;
    return "CAMPUS GROUNDS";
}

/* ------------------------------------------------------------------ */
/* 6. Main loop                                                        */
/* ------------------------------------------------------------------ */
/* ================================================================== */
/* CODE DEBUG HUNT: parchment look, sketches and the pop-up screens    */
/* ================================================================== */
enum { UI_NONE, UI_INTRO, UI_PICK, UI_BAG, UI_SUBMIT, UI_CONFIRM, UI_RESULT };
static int ui = UI_INTRO;
static int pick_item = -1;
static int bl[NCOMP], nbl = 0;           /* items in the bag (indices into items[]) */
static int bsel = 0, btop = 0;           /* selection + scroll in bag / submit list */
static bool chk[NCOMP];                  /* ticked in the submit list */
static int res_comp[NCOMP], nres = 0, res_right = 0, res_wrong = 0, res_left = 0;
static SDL_FRect r_yes, r_no, r_bag, r_sub, r_all, r_row[NCOMP], r_rm[NCOMP];

static const SDL_FColor INKC = {0.28f, 0.16f, 0.07f, 1.0f};
static SDL_FColor INK(float a) { SDL_FColor c = INKC; c.a = a; return c; }

#define PX0 128.0f
#define PY0 72.0f
#define PW 1024.0f
#define PH 576.0f

static bool inr(const SDL_FRect *r, float x, float y) { return r->w > 0 && x >= r->x && x <= r->x + r->w && y >= r->y && y <= r->y + r->h; }
static void ctext(float cx, float y, const char *s, float sc, SDL_FColor c) { text(cx - (float)strlen(s) * 4 * sc, y, s, sc, c); }

static int wrapt(const char *s, int maxc, char out[][64], int maxl)
{
    int n = 0; const char *p = s;
    while (*p && n < maxl) {
        int len = (int)strlen(p);
        if (len <= maxc) { strcpy(out[n++], p); break; }
        int cut = maxc;
        while (cut > 0 && p[cut] != ' ') cut--;
        if (cut == 0) cut = maxc;
        memcpy(out[n], p, (size_t)cut); out[n][cut] = 0; n++;
        p += cut; while (*p == ' ') p++;
    }
    return n;
}

/* old paper scroll with wooden rollers (drawn in code, no image file needed) */
static void draw_parchment(float x, float y, float w, float h)
{
    fillr(x + 12, y + 14, w, h, CA(C(0, 0, 0), 0.40f));
    fillr(x + 16, y + 18, w - 32, h - 36, C(0.87f, 0.73f, 0.48f));
    for (int i = 0; i < 12; i++) {                                   /* aged, darker edges */
        float in = i * 4.0f, a = 0.21f - i * 0.017f;
        SDL_FColor c = CA(C(0.45f, 0.27f, 0.09f), a);
        fillr(x + 16 + in, y + 18 + in, w - 32 - 2 * in, 4, c);
        fillr(x + 16 + in, y + h - 22 - in, w - 32 - 2 * in, 4, c);
        fillr(x + 16 + in, y + 18 + in, 4, h - 36 - 2 * in, c);
        fillr(x + w - 20 - in, y + 18 + in, 4, h - 36 - 2 * in, c);
    }
    scircle(x + w * 0.20f, y + h * 0.30f, 70, CA(C(0.55f, 0.35f, 0.12f), 0.06f), 24);
    scircle(x + w * 0.78f, y + h * 0.72f, 90, CA(C(0.55f, 0.35f, 0.12f), 0.06f), 24);
    scircle(x + w * 0.58f, y + h * 0.18f, 45, CA(C(0.50f, 0.30f, 0.10f), 0.07f), 20);
    for (int s = 0; s < 2; s++) {                                    /* top and bottom rollers */
        float ry = s ? y + h - 30 : y;
        fillr(x, ry, w, 30, C(0.56f, 0.34f, 0.14f));
        fillr(x, ry + 4, w, 5, CA(C(1, 1, 1), 0.28f));
        fillr(x, ry + 22, w, 8, CA(C(0, 0, 0), 0.22f));
        scircle(x - 3, ry + 15, 18, C(0.46f, 0.27f, 0.10f), 18);
        scircle(x + w + 3, ry + 15, 18, C(0.46f, 0.27f, 0.10f), 18);
        scircle(x - 6, ry + 11, 7, CA(C(1, 1, 1), 0.25f), 10);
        scircle(x + w + 0, ry + 11, 7, CA(C(1, 1, 1), 0.25f), 10);
    }
}

static void dim_screen(void) { fillr(0, 0, SCREEN_W, SCREEN_H, CA(C(0, 0, 0), 0.58f)); }

static void ui_button(SDL_FRect *rr, float x, float y, float w, float h, const char *label, SDL_FColor bg)
{
    *rr = (SDL_FRect){x, y, w, h};
    fillr(x + 3, y + 3, w, h, CA(C(0, 0, 0), 0.30f));
    fillr(x, y, w, h, bg);
    fillr(x, y, w, 3, CA(C(1, 1, 1), 0.25f));
    ctext(x + w / 2, y + h / 2 - 6, label, 1.4f, C(0.98f, 0.93f, 0.78f));
}

/* ---- hand-drawn wireframe sketches (ink on paper) ---- */
static float ska_x, ska_y, ska_w, ska_h;
#define SKX(f) (ska_x + (f) * ska_w)
#define SKY(f) (ska_y + (f) * ska_h)
static void sk_fill(float x, float y, float w, float h, float a) { fillr(SKX(x), SKY(y), w * ska_w, h * ska_h, INK(a)); }
static void sk_box(float x, float y, float w, float h)
{
    float X = SKX(x), Y = SKY(y), W = w * ska_w, H = h * ska_h, t = 3;
    fillr(X, Y, W, t, INK(0.9f)); fillr(X, Y + H - t, W, t, INK(0.9f));
    fillr(X, Y, t, H, INK(0.9f)); fillr(X + W - t, Y, t, H, INK(0.9f));
}
static void sk_line(float x1, float y1, float x2, float y2) { sline(SKX(x1), SKY(y1), SKX(x2), SKY(y2), 3, INK(0.9f)); }
static void sk_dot(float x, float y, float r, float a) { scircle(SKX(x), SKY(y), r * ska_w, INK(a), 20); }
static void sk_tri(float x1, float y1, float x2, float y2, float x3, float y3, float a)
{
    SDL_FPoint p[3] = {{SKX(x1), SKY(y1)}, {SKX(x2), SKY(y2)}, {SKX(x3), SKY(y3)}};
    poly(p, 3, INK(a));
}
static void sk_lines(float x, float y, float w, int n, float step)
{
    for (int i = 0; i < n; i++) sk_fill(x, y + i * step, w * (i == n - 1 ? 0.6f : 1.0f), 0.025f, 0.55f);
}

static void draw_sketch(int id, float x, float y, float w, float h)
{
    (void)id; /* All code clues use the same neutral programming illustration. */
    ska_x = x; ska_y = y; ska_w = w; ska_h = h;
    fillr(x, y, w, h, CA(C(0.08f, 0.14f, 0.24f), 0.98f));
    /* Editor window and title bar. */
    sk_fill(0.03f, 0.03f, 0.94f, 0.13f, 0.28f);
    sk_box(0.03f, 0.03f, 0.94f, 0.94f);
    sk_dot(0.09f, 0.095f, 0.018f, 0.95f);
    sk_dot(0.15f, 0.095f, 0.018f, 0.78f);
    sk_dot(0.21f, 0.095f, 0.018f, 0.60f);
    /* Code glyphs and syntax-colored lines, with no old stock/plane artwork. */
    sk_line(0.18f, 0.29f, 0.13f, 0.37f);
    sk_line(0.13f, 0.37f, 0.18f, 0.45f);
    sk_line(0.31f, 0.29f, 0.36f, 0.37f);
    sk_line(0.36f, 0.37f, 0.31f, 0.45f);
    sk_fill(0.46f, 0.30f, 0.28f, 0.035f, 0.90f);
    sk_fill(0.46f, 0.40f, 0.43f, 0.035f, 0.68f);
    sk_fill(0.22f, 0.57f, 0.25f, 0.035f, 0.72f);
    sk_fill(0.22f, 0.67f, 0.55f, 0.035f, 0.86f);
    sk_fill(0.22f, 0.77f, 0.37f, 0.035f, 0.62f);
    sk_fill(0.22f, 0.87f, 0.47f, 0.035f, 0.78f);
}

static void bag_rebuild(void)
{
    nbl = 0;
    for (int k = 0; k < nitems; k++) if (items[k].state == 1) bl[nbl++] = k;
    if (bsel >= nbl) bsel = nbl - 1;
    if (bsel < 0) bsel = 0;
}
static int bag_count(void) { int n = 0; for (int k = 0; k < nitems; k++) if (items[k].state == 1) n++; return n; }

static void ui_take(void) { items[pick_item].state = 1; ui = UI_NONE; }
static void ui_leave(void) { items[pick_item].armed = false; ui = UI_NONE; }
static void ui_remove(int li)          /* put the letter back where it was found */
{
    if (li < 0 || li >= nbl) return;
    items[bl[li]].state = 0; items[bl[li]].armed = false;
    bag_rebuild();
}
static void ui_open_bag(void) { bag_rebuild(); bsel = 0; btop = 0; ui = UI_BAG; }
static void ui_open_submit(void)
{
    bag_rebuild(); bsel = 0; btop = 0;
    for (int i = 0; i < NCOMP; i++) chk[i] = false;
    ui = UI_SUBMIT;
}
static int nchk(void) { int n = 0; for (int i = 0; i < nbl; i++) if (chk[i]) n++; return n; }

static void do_submit(void)
{
    nres = res_right = res_wrong = 0;
    for (int i = 0; i < nbl; i++) {
        if (!chk[i]) continue;
        int k = bl[i];
        items[k].state = 2;
        res_comp[nres++] = items[k].comp;
        if (comps[items[k].comp].correct) res_right++; else res_wrong++;
    }
    res_left = 10 - res_right;
    ui = UI_RESULT;
}
static int final_score(void) { int s = (res_right - res_wrong) * 10; return s < 0 ? 0 : s; }     /* out of 100 */

static void list_move(int d)
{
    if (nbl == 0) return;
    bsel += d;
    if (bsel < 0) bsel = 0;
    if (bsel >= nbl) bsel = nbl - 1;
    if (bsel < btop) btop = bsel;
    if (bsel >= btop + 16) btop = bsel - 15;
}

static void ui_key(SDL_Keycode k)
{
    switch (ui) {
    case UI_INTRO:
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE || k == SDLK_ESCAPE) ui = UI_NONE;
        break;
    case UI_PICK:
        if (k == SDLK_Y || k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_E) ui_take();
        else if (k == SDLK_N || k == SDLK_ESCAPE || k == SDLK_BACKSPACE) ui_leave();
        break;
    case UI_BAG:
        if (k == SDLK_ESCAPE || k == SDLK_B) ui = UI_NONE;
        else if (k == SDLK_UP || k == SDLK_W) list_move(-1);
        else if (k == SDLK_DOWN || k == SDLK_S) list_move(1);
        else if (k == SDLK_R || k == SDLK_DELETE || k == SDLK_BACKSPACE) ui_remove(bsel);
        break;
    case UI_SUBMIT:
        if (k == SDLK_ESCAPE) ui = UI_NONE;
        else if (k == SDLK_UP || k == SDLK_W) list_move(-1);
        else if (k == SDLK_DOWN || k == SDLK_S) list_move(1);
        else if (k == SDLK_SPACE && nbl) chk[bsel] = !chk[bsel];
        else if (k == SDLK_A) { bool all = nchk() == nbl; for (int i = 0; i < nbl; i++) chk[i] = !all; }
        else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && nchk() > 0) ui = UI_CONFIRM;
        break;
    case UI_CONFIRM:
        if (k == SDLK_Y || k == SDLK_RETURN || k == SDLK_KP_ENTER) do_submit();
        else if (k == SDLK_N || k == SDLK_ESCAPE) ui = UI_SUBMIT;
        break;
    default: break;      /* UI_NONE / UI_RESULT: nothing (after submitting no key works) */
    }
}

static void ui_click(float x, float y)
{
    switch (ui) {
    case UI_INTRO: ui = UI_NONE; break;
    case UI_NONE: if (inr(&r_bag, x, y)) ui_open_bag(); break;
    case UI_PICK:
        if (inr(&r_yes, x, y)) ui_take(); else if (inr(&r_no, x, y)) ui_leave();
        break;
    case UI_BAG:
        for (int i = btop; i < nbl && i < btop + 16; i++) {
            if (inr(&r_rm[i], x, y)) { ui_remove(i); return; }
            if (inr(&r_row[i], x, y)) { bsel = i; return; }
        }
        break;
    case UI_SUBMIT:
        for (int i = btop; i < nbl && i < btop + 16; i++)
            if (inr(&r_row[i], x, y)) { bsel = i; chk[i] = !chk[i]; return; }
        if (inr(&r_all, x, y)) { bool all = nchk() == nbl; for (int i = 0; i < nbl; i++) chk[i] = !all; }
        else if (inr(&r_sub, x, y) && nchk() > 0) ui = UI_CONFIRM;
        break;
    case UI_CONFIRM:
        if (inr(&r_yes, x, y)) do_submit(); else if (inr(&r_no, x, y)) ui = UI_SUBMIT;
        break;
    default: break;
    }
}

/* ---- drawing of the screens ---- */
static void draw_bag_button(void)
{
    float x = SCREEN_W - 100, y = SCREEN_H / 2 - 60, w = 84, h = 108;
    r_bag = (SDL_FRect){x, y, w, h};
    fillr(x, y, w, h, CA(C(0.04f, 0.07f, 0.06f), 0.62f));
    fillr(x, y, w, 3, CA(C(1, 0.9f, 0.5f), 0.7f));
    SDL_FColor lea = C(0.62f, 0.38f, 0.16f), dk = C(0.42f, 0.24f, 0.09f);
    scircle(x + 42, y + 26, 14, dk, 16); scircle(x + 42, y + 29, 8, C(0.10f, 0.14f, 0.12f), 14);
    fillr(x + 15, y + 30, 54, 52, lea);
    fillr(x + 15, y + 30, 54, 20, dk);
    fillr(x + 25, y + 56, 34, 20, CA(dk, 0.9f));
    fillr(x + 38, y + 44, 8, 10, C(0.95f, 0.8f, 0.3f));
    ctext(x + 42, y + 88, "BAG [B]", 1.2f, C(1.0f, 0.95f, 0.7f));
    int n = bag_count();
    if (n > 0) {
        char b[16]; snprintf(b, sizeof b, "%d", n);
        scircle(x + 68, y + 22, 13, C(0.85f, 0.15f, 0.12f), 16);
        ctext(x + 68, y + 17, b, 1.3f, C(1, 1, 1));
    }
}

static void draw_comp_info(int ci, float x, float y, float w)          /* title + description below a sketch */
{
    const Comp *c = &comps[ci];
    char ln[3][64];
    int n = wrapt(c->title, (int)(w / 12.8f), ln, 3);
    for (int i = 0; i < n; i++) text(x, y + i * 22, ln[i], 1.6f, INK(1));
    text(x, y + n * 22 + 8, c->l1, 1.3f, INK(0.9f));
    text(x, y + n * 22 + 26, c->l2, 1.3f, INK(0.9f));
}

static void draw_ui_pick(void)
{
    dim_screen();
    draw_parchment(PX0, PY0, PW, PH);
    ctext(PX0 + PW / 2, PY0 + 52, "~ A LETTER FOUND ON THE CAMPUS ~", 1.5f, INK(0.85f));
    const Comp *c = &comps[items[pick_item].comp];
    draw_sketch(items[pick_item].comp, PX0 + 90, PY0 + 100, 380, 340);
    ctext(PX0 + 280, PY0 + 455, "- Campus Archive -", 1.2f, INK(0.7f));
    float x0 = PX0 + 515, y = PY0 + 112;
    char ln[4][64];
    int n = wrapt(c->title, 19, ln, 4);
    for (int i = 0; i < n; i++) text(x0, y + i * 40, ln[i], 2.6f, INK(1));
    y += n * 40 + 14;
    fillr(x0, y, 420, 3, INK(0.5f));
    text(x0, y + 18, c->l1, 1.6f, INK(0.95f));
    text(x0, y + 44, c->l2, 1.6f, INK(0.95f));
    text(x0, PY0 + PH - 190, "Take this code snippet?", 1.5f, INK(1));
    ui_button(&r_yes, x0, PY0 + PH - 150, 205, 46, "[Y] YES, TAKE IT", C(0.30f, 0.45f, 0.20f));
    ui_button(&r_no, x0 + 220, PY0 + PH - 150, 205, 46, "[N] NO, LEAVE IT", C(0.55f, 0.22f, 0.15f));
    text(x0, PY0 + PH - 90, "If you leave it, it stays right here.", 1.1f, INK(0.7f));
}

static void draw_list_panel(const char *title, bool submit)
{
    dim_screen();
    draw_parchment(PX0, PY0, PW, PH);
    ctext(PX0 + PW / 2, PY0 + 50, title, 2.0f, INK(1));
    float x0 = PX0 + 70, y0 = PY0 + 100, rw = 480, rh = 24;
    for (int i = 0; i < NCOMP; i++) { r_row[i].w = 0; r_rm[i].w = 0; }
    if (nbl == 0) {
        text(x0 + 20, y0 + 60, submit ? "Your bag is empty - nothing to submit." : "Your bag is empty.", 1.6f, INK(0.8f));
        text(x0 + 20, y0 + 96, "Walk onto a glowing letter to read it.", 1.3f, INK(0.7f));
    }
    for (int i = btop; i < nbl && i < btop + 16; i++) {
        float y = y0 + (i - btop) * rh;
        const Comp *c = &comps[items[bl[i]].comp];
        r_row[i] = (SDL_FRect){x0, y, rw - (submit ? 0 : 100), rh};
        if (i == bsel) fillr(x0 - 4, y, rw + 4, rh - 2, INK(0.18f));
        char b[64];
        if (submit) snprintf(b, sizeof b, "[%c] %.27s", chk[i] ? 'X' : ' ', c->title);
        else snprintf(b, sizeof b, "%.29s", c->title);
        text(x0, y + 5, b, 1.3f, INK(1));
        if (!submit) {
            r_rm[i] = (SDL_FRect){x0 + rw - 92, y + 1, 90, rh - 4};
            fillr(r_rm[i].x, r_rm[i].y, r_rm[i].w, r_rm[i].h, C(0.55f, 0.22f, 0.15f));
            ctext(r_rm[i].x + 45, r_rm[i].y + 4, "REMOVE", 1.1f, C(1, 0.95f, 0.85f));
        }
    }
    if (nbl > 0) {
        draw_sketch(items[bl[bsel]].comp, PX0 + 600, PY0 + 100, 330, 250);
        draw_comp_info(items[bl[bsel]].comp, PX0 + 600, PY0 + 362, 330);
    }
    char b[96];
    if (!submit) {
        snprintf(b, sizeof b, "%d in bag   |   UP/DOWN choose   R = remove   ESC / B = close", nbl);
        text(PX0 + 70, PY0 + PH - 78, b, 1.15f, INK(0.85f));
        text(PX0 + 70, PY0 + PH - 58, "Removed letters go back to where you found them.", 1.0f, INK(0.65f));
    } else {
        snprintf(b, sizeof b, "SPACE = tick   A = tick all   ENTER = submit   ESC = cancel   (%d ticked)", nchk());
        text(PX0 + 70, PY0 + PH - 130, b, 1.1f, INK(0.85f));
        ui_button(&r_all, PX0 + 70, PY0 + PH - 100, 170, 38, "TICK ALL [A]", C(0.45f, 0.32f, 0.16f));
        char sb[48]; snprintf(sb, sizeof sb, "SUBMIT %d (FINAL)", nchk());
        ui_button(&r_sub, PX0 + PW - 330, PY0 + PH - 108, 270, 46, sb, nchk() ? C(0.30f, 0.45f, 0.20f) : C(0.45f, 0.40f, 0.34f));
    }
}

static void draw_ui_confirm(void)
{
    draw_list_panel("CODE SUBMISSION HUB", true);
    dim_screen();
    float w = 640, h = 270, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
    draw_parchment(x, y, w, h);
    char b[80];
    snprintf(b, sizeof b, "Submit these %d snippets?", nchk());
    ctext(x + w / 2, y + 78, b, 2.0f, INK(1));
    ctext(x + w / 2, y + 116, "This is FINAL. After this you cannot", 1.4f, INK(0.9f));
    ctext(x + w / 2, y + 138, "move or press any key.", 1.4f, INK(0.9f));
    ui_button(&r_yes, x + 90, y + h - 90, 200, 44, "[Y] SUBMIT", C(0.30f, 0.45f, 0.20f));
    ui_button(&r_no, x + w - 290, y + h - 90, 200, 44, "[N] GO BACK", C(0.55f, 0.22f, 0.15f));
}

static void draw_ui_result(void)
{
    dim_screen();
    draw_parchment(PX0, PY0, PW, PH);
    ctext(PX0 + PW / 2, PY0 + 48, "DEBUG RESULT", 2.4f, INK(1));
    float x0 = PX0 + 60, y0 = PY0 + 98;
    text(x0, y0 - 6, "SUBMITTED CODE SNIPPETS", 1.2f, INK(0.8f));
    for (int i = 0; i < nres; i++) {
        const Comp *c = &comps[res_comp[i]];
        float y = y0 + 16 + i * 19;
        char b[64]; snprintf(b, sizeof b, "%.28s", c->title);
        text(x0, y, b, 1.1f, INK(1));
        text(x0 + 330, y, c->correct ? "+10" : "-10", 1.2f, c->correct ? C(0.10f, 0.42f, 0.12f) : C(0.70f, 0.10f, 0.08f));
    }
    if (nres == 0) text(x0, y0 + 20, "(nothing was submitted)", 1.2f, INK(0.7f));
    float rx = PX0 + 480;
    text(rx, y0 - 6, "DEBUG SCORE", 1.6f, INK(0.9f));
    char b[96];
    snprintf(b, sizeof b, "%d%%", res_right * 10);
    text(rx, y0 + 22, b, 6.0f, INK(1));
    fillr(rx, y0 + 80, 440, 28, INK(0.18f));
    fillr(rx, y0 + 80, 440.0f * res_right / 10.0f, 28, C(0.30f, 0.50f, 0.18f));
    fillr(rx, y0 + 80, 440, 3, INK(0.6f)); fillr(rx, y0 + 105, 440, 3, INK(0.6f));
    snprintf(b, sizeof b, "Correct snippets : %d   (+%d)", res_right, res_right * 10);
    text(rx, y0 + 132, b, 1.35f, C(0.10f, 0.40f, 0.12f));
    snprintf(b, sizeof b, "Buggy snippets   : %d   (-%d)", res_wrong, res_wrong * 10);
    text(rx, y0 + 158, b, 1.35f, C(0.68f, 0.10f, 0.08f));
    snprintf(b, sizeof b, "Valid snippets left : %d", res_left);
    text(rx, y0 + 184, b, 1.35f, INK(0.85f));
    fillr(rx, y0 + 214, 440, 3, INK(0.6f));
    snprintf(b, sizeof b, "FINAL SCORE: %d / 100", final_score());
    text(rx, y0 + 232, b, 2.8f, INK(1));
    snprintf(b, sizeof b, "(%d x 10  -  %d x 10  =  %d)", res_right, res_wrong, (res_right - res_wrong) * 10);
    text(rx, y0 + 272, b, 1.2f, INK(0.75f));
    text(rx, y0 + 330, "Submission is final. Close the window to exit.", 1.2f, INK(0.7f));
}

static void draw_ui_intro(void)
{
    dim_screen();
    draw_parchment(PX0, PY0, PW, PH);
    ctext(PX0 + PW / 2, PY0 + 56, "YOUR MISSION", 3.0f, INK(1));
    static const char *ln[] = {
        "DEBUG QUEST - Find valid beginner code!",
        "",
        "Find 20 short Python, C and Java code snippets around campus.",
        "10 snippets are correct. The other 10 contain a basic bug.",
        "Read each snippet and collect only code that is valid.",
        "",
        "Walk onto a letter to read it, then take it or leave it.",
        "Taken letters go into your BAG [B]. Open the bag to remove any.",
        "",
        "When ready, go to the CODE SUBMISSION HUB (the blue",
        "round tower near the start), press E, tick what you want and submit.",
        "",
        "Correct code = +10 marks     Buggy code = -10 marks",
        "Submitting is FINAL - after that you cannot move or press any key.",
    };
    for (size_t i = 0; i < sizeof ln / sizeof ln[0]; i++) ctext(PX0 + PW / 2, PY0 + 112 + (float)i * 26, ln[i], 1.5f, INK(i == 0 ? 1.0f : 0.92f));
    ui_button(&r_yes, PX0 + PW / 2 - 130, PY0 + PH - 100, 260, 46, "[ENTER] START", C(0.30f, 0.45f, 0.20f));
}

static void draw_ui(void)
{
    switch (ui) {
    case UI_INTRO:   draw_ui_intro(); break;
    case UI_PICK:    draw_ui_pick(); break;
    case UI_BAG:     { char t[40]; snprintf(t, sizeof t, "YOUR BAG"); draw_list_panel(t, false); break; }
    case UI_SUBMIT:  draw_list_panel("CODE SUBMISSION HUB - CHOOSE", true); break;
    case UI_CONFIRM: draw_ui_confirm(); break;
    case UI_RESULT:  draw_ui_result(); break;
    default: break;
    }
}

static bool near_hub(float x, float y) { float dx = x - 6.7f, dy = y - 88.6f; return dx * dx + dy * dy < 3.6f * 3.6f; }

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) { printf("SDL error: %s\n", SDL_GetError()); return 1; }
    SDL_Window *window = SDL_CreateWindow("College Story - SSEC Campus", SCREEN_W, SCREEN_H, 0);
    if (!window) { printf("Window error: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    R = SDL_CreateRenderer(window, NULL);
    if (!R) { printf("Renderer error: %s\n", SDL_GetError()); SDL_DestroyWindow(window); SDL_Quit(); return 1; }
    SDL_SetRenderVSync(R, 1);
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);

    init_world();
    place_items();

    float px = 12.8f, py = 88.3f;          /* start point */
    float fdx = 1, fdy = 0.2f, walk = 0;
    bool moving = false, showmap = false, running = true, first = true;
    Uint64 previous = SDL_GetTicks();

    while (running) {
        Uint64 now = SDL_GetTicks();
        float dt = (now - previous) / 1000.0f;
        previous = now;
        if (dt > 0.05f) dt = 0.05f;
        gtime += dt;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                SDL_ConvertEventToRenderCoordinates(R, &e);
                ui_click(e.button.x, e.button.y);
            }
            if (e.type == SDL_EVENT_KEY_DOWN && ui != UI_NONE) { ui_key(e.key.key); continue; }
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                if (e.key.key == SDLK_B) ui_open_bag();
                if (e.key.key == SDLK_H) ui = UI_INTRO;
                if (e.key.key == SDLK_E && near_hub(px, py)) ui_open_submit();
                if (e.key.key == SDLK_ESCAPE) { if (showmap) showmap = false; else running = false; }
                if (e.key.key == SDLK_M) showmap = !showmap;
                if (e.key.key == SDLK_E && curfloor < nfl(curgrp) - 1 && near_stair(px, py)) curfloor++;     /* stairs up   */
                if (e.key.key == SDLK_Q && curfloor > 0 && near_stair(px, py)) curfloor--;     /* stairs down */
            }
        }

        const bool *k = SDL_GetKeyboardState(NULL);
        float dx = 0, dy = 0;
        if (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP])    dy -= 1;
        if (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN])  dy += 1;
        if (k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT])  dx -= 1;
        if (k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) dx += 1;
        if (ui != UI_NONE) dx = dy = 0;                       /* pop-up screens (and the final result) freeze the player */
        if (ui == UI_NONE && (k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_EQUALS])) zoom += dt * 0.8f;
        if (ui == UI_NONE && (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_MINUS]))  zoom -= dt * 0.8f;
        if (zoom < 0.55f) zoom = 0.55f;
        if (zoom > 3.4f) zoom = 3.4f;

        /* W = up the screen. Screen-up in the isometric map is (-x,-y), so convert. */
        float mx = dx + dy, my = dy - dx;
        float len = sqrtf(mx * mx + my * my);
        moving = len > 0;
        if (moving) {
            mx /= len; my /= len;
            fdx = mx; fdy = my;
            float speed = PLAYER_SPEED * ((k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) ? 1.7f : 1.0f);
            float nx = px + mx * speed * dt, ny = py + my * speed * dt;
            if (!blocked(nx, py)) px = nx;
            if (!blocked(px, ny)) py = ny;
            walk += dt * 11;
        }

        /* leaving the main college always puts you back on the 1st floor */
        curgrp = 0;
        for (int i = 0; i < NB; i++) if (grp(i) && inside_bld(&blds[i], px, py)) curgrp = grp(i);
        if (!curgrp) curfloor = 0;
        /* walking onto a letter opens it */
        if (ui == UI_NONE) {
            for (int q = 0; q < nitems; q++) {
                Item *it = &items[q];
                if (it->state != 0) continue;
                bool here = it->bld < 0 || (ofloor(it->bld) == it->floor && inside_bld(&blds[it->bld], px, py));
                float ddx = px - it->x, ddy = py - it->y, dd = sqrtf(ddx * ddx + ddy * ddy);
                if (!here || dd > 1.9f) it->armed = true;
                else if (it->armed && dd < 0.75f) { ui = UI_PICK; pick_item = q; break; }
            }
        }
        /* which building are we in? fade its roof away */
        int pgroup = -1;
        bool opened[NB];
        for (int i = 0; i < NB; i++) {
            bool in = inside_bld(&blds[i], px, py);
            if (in) pgroup = i;
            opened[i] = in || near_door(i, px, py);
        }
        /* the three parts of the MAIN COLLEGE open together (front door, back door, wings) */
        if (opened[4] || opened[5] || opened[6]) opened[4] = opened[5] = opened[6] = true;
        /* buildings touching the one you are in, on its east / south side, would hide you: cut them away too */
        for (int pass = 0; pass < 2; pass++) {
            bool base[NB];
            memcpy(base, opened, sizeof base);
            for (int i = 0; i < NB; i++) {
                if (!base[i]) continue;
                for (int j = 0; j < NB; j++) {
                    if (j == i) continue;
                    bool east  = fabsf(blds[j].x0 - blds[i].x1) < 1.7f && blds[j].y1 > blds[i].y0 && blds[j].y0 < blds[i].y1;
                    bool south = fabsf(blds[j].y0 - blds[i].y1) < 1.7f && blds[j].x1 > blds[i].x0 && blds[j].x0 < blds[i].x1;
                    if (east || south) opened[j] = true;
                }
            }
        }
        for (int i = 0; i < NB; i++) {
            bool in = opened[i];
            float target = in ? 1.0f : 0.0f;
            blds[i].f += (target - blds[i].f) * MINF(1.0f, dt * 7.0f);
            if (!in && blds[i].f < 0.01f) blds[i].f = 0;
        }

        /* camera follows the player smoothly */
        float VW = SCREEN_W / zoom, VH = SCREEN_H / zoom;
        float tx = (px - py) * SX - VW / 2, ty = (px + py) * SY - curfloor * FH * ZS - VH / 2 - 8;
        if (first) { camX = tx; camY = ty; first = false; }
        camX += (tx - camX) * MINF(1.0f, dt * 9);
        camY += (ty - camY) * MINF(1.0f, dt * 9);

        SDL_SetRenderScale(R, 1, 1);
        SDL_SetRenderDrawColorFloat(R, 0.24f, 0.44f, 0.26f, 1);
        SDL_RenderClear(R);
        SDL_SetRenderScale(R, zoom, zoom);
        draw_ground();
        draw_plaza();
        draw_shadows();
        draw_scene(px, py, pgroup, walk, (mx - my), moving);
        labels(px, py);
        SDL_SetRenderScale(R, 1, 1);

        /* HUD */
        if (!showmap) fillr(10, 10, 600, 94, CA(C(0.04f, 0.07f, 0.06f), 0.70f));
        if (!showmap) text(20, 18, "COLLEGE STORY - SSEC CAMPUS", 1.5f, C(1.0f, 0.95f, 0.6f));
        char buf[128];
        snprintf(buf, sizeof buf, "LOCATION: %s", place_name(px, py));
        if (!showmap) text(20, 40, buf, 1.2f, C(0.7f, 1.0f, 0.8f));
        if (!showmap) text(20, 58, "WASD MOVE  SHIFT RUN  Z/X ZOOM  E/Q STAIRS+HUB  B BAG  M MAP  ESC QUIT", 1.0f, C(0.85f, 0.85f, 0.85f));
        if (!showmap && (curfloor > 0 || curgrp > 1 || near_stair(px, py))) {
            char sb[96];
            snprintf(sb, sizeof sb, "YOU ARE ON THE %s", fname(curgrp, curfloor));
            if (curfloor > 0 || (curgrp > 1)) text(20, 76, sb, 1.2f, C(1.0f, 0.9f, 0.4f));
            if (near_stair(px, py)) {
                snprintf(sb, sizeof sb, "STAIRS:  E = GO UP%s   Q = GO DOWN%s", curfloor < nfl(curgrp) - 1 ? "" : " (TOP)", curfloor > 0 ? "" : " (GROUND)");
                fillr(SCREEN_W / 2 - 250, SCREEN_H - 70, 500, 34, CA(C(0.04f, 0.07f, 0.06f), 0.80f));
                text(SCREEN_W / 2 - 235, SCREEN_H - 59, sb, 1.4f, C(1.0f, 0.95f, 0.5f));
            }
        }
        if (ui == UI_NONE && !showmap && near_hub(px, py)) {
            char hb[80];
            snprintf(hb, sizeof hb, "PRESS E - SUBMIT CODE   (%d in your bag)", bag_count());
            fillr(SCREEN_W / 2 - 300, SCREEN_H - 70, 600, 34, CA(C(0.04f, 0.07f, 0.06f), 0.80f));
            text(SCREEN_W / 2 - 285, SCREEN_H - 59, hb, 1.4f, C(1.0f, 0.95f, 0.5f));
        }
        if (!showmap) draw_minimap(px, py, fdx, fdy);
        if (showmap) draw_fullmap(px, py);

        if (ui != UI_RESULT) draw_bag_button();
        draw_ui();

        SDL_RenderPresent(R);
    }

    SDL_DestroyRenderer(R);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
