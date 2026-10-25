#!/usr/bin/env python3
"""Turns the desktop game (main.c) into web_game.c for the WebAssembly build.
   usage:  python3 make_web.py ../../main.c web_game.c
   The only changes: SDL3 is replaced by sdl_shim.h, main() becomes game_init()/game_frame()
   (a browser cannot run a blocking loop), and the game reports "started" / "finished" to the page."""
import sys, re
src = open(sys.argv[1], encoding='utf-8', errors='replace').read().replace('\r\n', '\n')

def rep(old, new):
    global src
    if old not in src:
        sys.exit("make_web.py: pattern not found: " + old[:70])
    src = src.replace(old, new, 1)

rep('#include <SDL3/SDL.h>', '#include "sdl_shim.h"')

# --- web wording ---
rep('"COLLEGE STORY - SSEC CAMPUS", 1.5f', '"CAMPUS QUEST - SSEC", 1.5f')
rep('  M MAP  ESC QUIT"', '  M MAP"')
rep('"DEBUG QUEST - Find valid beginner code!",', '"DEBUG QUEST - Find valid beginner code!",')

# --- play timer + messages to the page ---
rep('static int ui = UI_INTRO;',
    'static int ui = UI_INTRO;\nstatic Uint64 play_t0 = 0, play_ms = 0;\nstatic bool play_started = false;\nstatic int final_score(void);\n'
    'static void start_play(void) { if (!play_started) { play_started = true; play_t0 = SDL_GetTicks(); js_started(); } }')
rep('if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE || k == SDLK_ESCAPE) ui = UI_NONE;',
    'if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE || k == SDLK_ESCAPE) { ui = UI_NONE; start_play(); }')
rep('case UI_INTRO: ui = UI_NONE; break;', 'case UI_INTRO: ui = UI_NONE; start_play(); break;')
rep('    res_left = 10 - res_right;\n    ui = UI_RESULT;',
    '    res_left = 10 - res_right;\n    ui = UI_RESULT;\n    play_ms = play_started ? SDL_GetTicks() - play_t0 : 0;\n'
    '    js_report(final_score(), res_right, res_wrong, (int)(play_ms / 1000), res_comp, nres);')
rep('    text(rx, y0 + 330, "Submission is final. Close the window to exit.", 1.2f, INK(0.7f));',
    '    { int sec = (int)(play_ms / 1000); snprintf(b, sizeof b, "TIME TAKEN: %02d:%02d", sec / 60, sec % 60);\n'
    '      text(rx, y0 + 300, b, 1.8f, INK(1)); }\n'
    '    text(rx, y0 + 346, "Submission is final. Your result has been sent.", 1.2f, INK(0.7f));')

# --- main() -> game_init() / game_frame() ---
a = src.index('int main(void)\n{')
body_start = src.index('    while (running) {\n', a) + len('    while (running) {\n')
body_end = src.index('        SDL_RenderPresent(R);\n    }\n', body_start) + len('        SDL_RenderPresent(R);\n')
body = src[body_start:body_end]
new_main = '''static float px = 12.8f, py = 88.3f;          /* start point */
static float fdx = 1, fdy = 0.2f, walk = 0;
static bool moving = false, showmap = false, running = true, first = true;
static Uint64 previous = 0;

__attribute__((export_name("game_init"))) void game_init(void)
{
    R = SDL_CreateRenderer(NULL, NULL);
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    init_world();
    place_items();
    previous = SDL_GetTicks();
}

__attribute__((export_name("game_elapsed"))) int game_elapsed(void)   /* seconds played so far (for the page's live timer) */
{
    if (!play_started) return 0;
    return (int)((ui == UI_RESULT ? play_ms : SDL_GetTicks() - play_t0) / 1000);
}

__attribute__((export_name("game_frame"))) void game_frame(void)
{
    {
''' + body + '''    }
}
'''
src = src[:a] + new_main
open(sys.argv[2], 'w', encoding='utf-8').write(src)
print("web_game.c written")
