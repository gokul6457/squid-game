/*
 * sdl_shim.h - a tiny stand-in for the parts of SDL3 that "College Story" uses.
 * It lets the very same game code run in a web browser (WebAssembly): every drawing call is
 * forwarded to JavaScript (a WebGL renderer in public/js/game.js) and keyboard / mouse events
 * are pushed in from JavaScript.  Not used for the normal desktop build.
 */
#ifndef SDL_SHIM_H
#define SDL_SHIM_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef uint8_t Uint8; typedef uint32_t Uint32; typedef uint64_t Uint64;
typedef Uint32 SDL_Keycode;
typedef struct { float r, g, b, a; } SDL_FColor;
typedef struct { float x, y; } SDL_FPoint;
typedef struct { float x, y, w, h; } SDL_FRect;
typedef struct { int x, y, w, h; } SDL_Rect;
typedef struct { SDL_FPoint position; SDL_FColor color; SDL_FPoint tex_coord; } SDL_Vertex;
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;

#define SDL_BLENDMODE_BLEND 1
#define SDL_BUTTON_LEFT 1
enum { SDL_EVENT_QUIT = 0x100, SDL_EVENT_KEY_DOWN = 0x300, SDL_EVENT_MOUSE_BUTTON_DOWN = 0x401 };

/* key codes (the JS side sends the same numbers) */
enum {
    SDLK_BACKSPACE = 8, SDLK_RETURN = 13, SDLK_ESCAPE = 27, SDLK_SPACE = 32, SDLK_DELETE = 127,
    SDLK_UP = 1001, SDLK_DOWN = 1002, SDLK_LEFT = 1003, SDLK_RIGHT = 1004, SDLK_KP_ENTER = 1005,
    SDLK_A = 'a', SDLK_B = 'b', SDLK_C = 'c', SDLK_D = 'd', SDLK_E = 'e', SDLK_F = 'f', SDLK_G = 'g', SDLK_H = 'h',
    SDLK_I = 'i', SDLK_J = 'j', SDLK_K = 'k', SDLK_L = 'l', SDLK_M = 'm', SDLK_N = 'n', SDLK_O = 'o', SDLK_P = 'p',
    SDLK_Q = 'q', SDLK_R = 'r', SDLK_S = 's', SDLK_T = 't', SDLK_U = 'u', SDLK_V = 'v', SDLK_W = 'w', SDLK_X = 'x',
    SDLK_Y = 'y', SDLK_Z = 'z'
};
/* scancodes = real SDL values */
enum {
    SDL_SCANCODE_A = 4, SDL_SCANCODE_D = 7, SDL_SCANCODE_S = 22, SDL_SCANCODE_W = 26, SDL_SCANCODE_X = 27, SDL_SCANCODE_Z = 29,
    SDL_SCANCODE_MINUS = 45, SDL_SCANCODE_EQUALS = 46, SDL_SCANCODE_RIGHT = 79, SDL_SCANCODE_LEFT = 80,
    SDL_SCANCODE_DOWN = 81, SDL_SCANCODE_UP = 82, SDL_SCANCODE_LSHIFT = 225, SDL_SCANCODE_RSHIFT = 229
};

typedef struct {
    Uint32 type;
    struct { SDL_Keycode key; bool repeat; } key;
    struct { Uint8 button; float x, y; } button;
} SDL_Event;

/* ---- JavaScript side (see public/js/game.js) ---- */
#define JSIMPORT(n) __attribute__((import_module("env"), import_name(#n)))
JSIMPORT(js_color)    void js_color(float r, float g, float b, float a);
JSIMPORT(js_fillrect) void js_fillrect(float x, float y, float w, float h);
JSIMPORT(js_rect)     void js_rect(float x, float y, float w, float h);
JSIMPORT(js_line)     void js_line(float x1, float y1, float x2, float y2);
JSIMPORT(js_geometry) void js_geometry(const SDL_Vertex *v, int n, const int *idx, int k);
JSIMPORT(js_text)     void js_text(float x, float y, const char *s);
JSIMPORT(js_scale)    void js_scale(float sx, float sy);
JSIMPORT(js_clip)     void js_clip(int on, float x, float y, float w, float h);
JSIMPORT(js_clear)    void js_clear(void);
JSIMPORT(js_present)  void js_present(void);
JSIMPORT(js_ticks)    double js_ticks(void);
/* game -> page messages */
JSIMPORT(js_started)  void js_started(void);
JSIMPORT(js_report)   void js_report(int score, int right, int wrong, int seconds, const int *ids, int count);

static SDL_Event shim_q[128];
static int shim_qh = 0, shim_qt = 0;
static bool shim_keys[512];
static float shim_cr = 0, shim_cg = 0, shim_cb = 0, shim_ca = 1;

static SDL_Renderer *SDL_CreateRenderer(SDL_Window *w, const char *n) { (void)w; (void)n; return (SDL_Renderer *)1; }
static bool SDL_SetRenderDrawBlendMode(SDL_Renderer *r, int m) { (void)r; (void)m; return true; }
static bool SDL_SetRenderDrawColorFloat(SDL_Renderer *r, float cr, float cg, float cb, float ca)
{ (void)r; shim_cr = cr; shim_cg = cg; shim_cb = cb; shim_ca = ca; js_color(cr, cg, cb, ca); return true; }
static bool SDL_RenderClear(SDL_Renderer *r) { (void)r; js_clear(); return true; }
static bool SDL_RenderFillRect(SDL_Renderer *r, const SDL_FRect *a) { (void)r; js_fillrect(a->x, a->y, a->w, a->h); return true; }
static bool SDL_RenderRect(SDL_Renderer *r, const SDL_FRect *a) { (void)r; js_rect(a->x, a->y, a->w, a->h); return true; }
static bool SDL_RenderLine(SDL_Renderer *r, float x1, float y1, float x2, float y2) { (void)r; js_line(x1, y1, x2, y2); return true; }
static bool SDL_RenderGeometry(SDL_Renderer *r, SDL_Texture *t, const SDL_Vertex *v, int n, const int *idx, int k)
{ (void)r; (void)t; js_geometry(v, n, idx, k); return true; }
static bool SDL_RenderDebugText(SDL_Renderer *r, float x, float y, const char *s) { (void)r; js_text(x, y, s); return true; }
static bool SDL_SetRenderScale(SDL_Renderer *r, float sx, float sy) { (void)r; js_scale(sx, sy); return true; }
static bool SDL_SetRenderClipRect(SDL_Renderer *r, const SDL_Rect *c)
{ (void)r; if (c) js_clip(1, (float)c->x, (float)c->y, (float)c->w, (float)c->h); else js_clip(0, 0, 0, 0, 0); return true; }
static bool SDL_RenderPresent(SDL_Renderer *r) { (void)r; js_present(); return true; }
static Uint64 SDL_GetTicks(void) { return (Uint64)js_ticks(); }
static const bool *SDL_GetKeyboardState(int *n) { if (n) *n = 512; return shim_keys; }
static bool SDL_PollEvent(SDL_Event *e)
{
    if (shim_qh == shim_qt) return false;
    *e = shim_q[shim_qh]; shim_qh = (shim_qh + 1) % 128; return true;
}
static bool SDL_ConvertEventToRenderCoordinates(SDL_Renderer *r, SDL_Event *e) { (void)r; (void)e; return true; }

/* called from JavaScript */
__attribute__((export_name("shim_key"))) void shim_key(int down, int keycode, int scancode, int repeat)
{
    if (scancode > 0 && scancode < 512) shim_keys[scancode] = down != 0;
    if (down && keycode > 0 && (shim_qt + 1) % 128 != shim_qh) {
        SDL_Event e; e.type = SDL_EVENT_KEY_DOWN; e.key.key = (SDL_Keycode)keycode; e.key.repeat = repeat != 0; e.button.button = 0; e.button.x = e.button.y = 0;
        shim_q[shim_qt] = e; shim_qt = (shim_qt + 1) % 128;
    }
}
__attribute__((export_name("shim_mouse"))) void shim_mouse(float x, float y)
{
    if ((shim_qt + 1) % 128 == shim_qh) return;
    SDL_Event e; e.type = SDL_EVENT_MOUSE_BUTTON_DOWN; e.key.key = 0; e.key.repeat = false; e.button.button = SDL_BUTTON_LEFT; e.button.x = x; e.button.y = y;
    shim_q[shim_qt] = e; shim_qt = (shim_qt + 1) % 128;
}
__attribute__((export_name("shim_blur"))) void shim_blur(void) { for (int i = 0; i < 512; i++) shim_keys[i] = false; }
#endif
