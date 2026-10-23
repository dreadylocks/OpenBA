// ============================================================================
// src/diag/darwin/input_trace_darwin.cpp -- the hands-on test logger (see input_trace.h)
// ============================================================================
// LINE FORMAT: "<tag> t=<seconds since start> key=value ...". Tags:
//   H  header (once)      M  mouse move (rate limited)   B  mouse button
//   W  wheel              K  key down/up                 E  window event
//   Q  quit / close request                              S  once-a-second summary
//   X  last line, written at process exit
// A key line carries sc= (SDL scancode), dik= (the DIK the raw scancode maps to), eff_dn= /
// eff_up= (the DIK codes that went down / up AFTER the Mac key layer, i.e. what the game's
// ReadKeyboard reports, same code path: ApplyMacKeyLayer + kSdlScancodeToDik).
// Mouse x/y are SDL points; px/py are pixels (points * window pixel density), the unit the
// engine works in. The S line carries the extreme pixel positions seen in the last second
// (mn_x mx_x mn_y mx_y), so a rate-limited M line cannot hide an edge touch.
// ============================================================================
// Darwin only: a src/diag/darwin file with no posix twin is compiled on Apple alone
// (src/platform/darwin/README.md), so no other host sees it.
#include "input_trace.h"

#include "app/options.h"
#include "mac_keymap.h"
#include "sdl_dik_tables.h"

#include <SDL3/SDL.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace Diag {
namespace InputTrace {

namespace {

std::mutex g_mu;
FILE* g_f = nullptr;
bool g_installed = false;
bool g_atexit = false;
Uint64 g_t0 = 0;
Uint64 g_lastMove = 0;
Uint64 g_lastSummary = 0;
bool g_prevEff[256];       // DIK state last logged
float g_mnx = 1e9f, g_mxx = -1e9f, g_mny = 1e9f, g_mxy = -1e9f;
uint64_t g_events = 0;
SDL_TimerID g_timer = 0;

double Now() { return double(SDL_GetTicksNS() - g_t0) / 1e9; }

void Line(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
// Callers hold g_mu.
void Line(const char* tag, const char* fmt, ...) {
    if (!g_f) return;
    char body[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    char out[640];
    const int n = snprintf(out, sizeof out, "%s t=%.3f %s\n", tag, Now(), body);
    fwrite(out, 1, size_t(n), g_f);
    fflush(g_f);
}

SDL_Window* GameWindow() {
    int n = 0;
    SDL_Window** w = SDL_GetWindows(&n);
    SDL_Window* r = (w && n > 0) ? w[0] : nullptr;
    SDL_free(w);
    return r;
}

struct WinInfo {
    int ptw = 0, pth = 0, pxw = 0, pxh = 0;
    float density = 1;
    bool fs = false, focus = false;
};
WinInfo QueryWindow(SDL_Window* w) {
    WinInfo i;
    if (!w) return i;
    SDL_GetWindowSize(w, &i.ptw, &i.pth);
    SDL_GetWindowSizeInPixels(w, &i.pxw, &i.pxh);
    i.density = SDL_GetWindowPixelDensity(w);
    if (i.density <= 0) i.density = 1;
    const SDL_WindowFlags f = SDL_GetWindowFlags(w);
    i.fs = (f & SDL_WINDOW_FULLSCREEN) != 0;
    i.focus = (f & SDL_WINDOW_INPUT_FOCUS) != 0;
    return i;
}

const char* WindowEventName(Uint32 t) {
    switch (t) {
        case SDL_EVENT_WINDOW_SHOWN:
            return "shown";
        case SDL_EVENT_WINDOW_HIDDEN:
            return "hidden";
        case SDL_EVENT_WINDOW_EXPOSED:
            return "exposed";
        case SDL_EVENT_WINDOW_MOVED:
            return "moved";
        case SDL_EVENT_WINDOW_RESIZED:
            return "resized";
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            return "pixel_size_changed";
        case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
            return "metal_view_resized";
        case SDL_EVENT_WINDOW_MINIMIZED:
            return "minimized";
        case SDL_EVENT_WINDOW_MAXIMIZED:
            return "maximized";
        case SDL_EVENT_WINDOW_RESTORED:
            return "restored";
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
            return "mouse_enter";
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            return "mouse_leave";
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            return "focus_gained";
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            return "focus_lost";
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            return "close_requested";
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
            return "enter_fullscreen";
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            return "leave_fullscreen";
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            return "display_changed";
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            return "display_scale_changed";
        case SDL_EVENT_WINDOW_DESTROYED:
            return "destroyed";
        default:
            return nullptr;
    }
}

// The DIK state the game would read right now (raw SDL state + Mac layer + table).
void EffectiveDik(bool (&out)[256]) {
    int count = 0;
    const bool* raw = SDL_GetKeyboardState(&count);
    bool eff[Platform::Input::MacKeys::kStateCount];
    if (count > Platform::Input::MacKeys::kStateCount) count = Platform::Input::MacKeys::kStateCount;
    Platform::Input::MacKeys::ApplyMacKeyLayer(raw, count, eff);
    for (int i = 0; i < 256; ++i)
        out[i] = false;
    if (count > Platform::Input::kSdlScancodeCount) count = Platform::Input::kSdlScancodeCount;
    for (int s = 0; s < count; ++s)
        if (eff[s] && Platform::Input::kSdlScancodeToDik[s]) out[Platform::Input::kSdlScancodeToDik[s]] = true;
}

void Track(float px, float py) {
    if (px < g_mnx) g_mnx = px;
    if (px > g_mxx) g_mxx = px;
    if (py < g_mny) g_mny = py;
    if (py > g_mxy) g_mxy = py;
}

float DensityOf(Uint32 windowID) {
    SDL_Window* w = windowID ? SDL_GetWindowFromID(windowID) : GameWindow();
    if (!w) w = GameWindow();
    const float d = w ? SDL_GetWindowPixelDensity(w) : 1.0f;
    return d > 0 ? d : 1.0f;
}

void Summary() {   // holds g_mu
    const WinInfo i = QueryWindow(GameWindow());
    float x = 0, y = 0;
    const SDL_MouseButtonFlags b = SDL_GetMouseState(&x, &y);
    Line("S", "win=%dx%d px=%dx%d density=%.2f fs=%d focus=%d mouse_px=%.1f,%.1f btn=%u ev=%llu mn_x=%.1f mx_x=%.1f mn_y=%.1f mx_y=%.1f",
         i.ptw, i.pth, i.pxw, i.pxh, i.density, i.fs, i.focus, x * i.density, y * i.density, unsigned(b),
         (unsigned long long)g_events, g_mnx > 1e8f ? -1.f : g_mnx, g_mxx < -1e8f ? -1.f : g_mxx,
         g_mny > 1e8f ? -1.f : g_mny, g_mxy < -1e8f ? -1.f : g_mxy);
    g_mnx = g_mny = 1e9f;
    g_mxx = g_mxy = -1e9f;
}

bool OnEvent(void*, SDL_Event* e) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_f) return true;
    ++g_events;
    const Uint64 now = SDL_GetTicksNS();
    switch (e->type) {
        case SDL_EVENT_MOUSE_MOTION: {
            const float d = DensityOf(e->motion.windowID);
            Track(e->motion.x * d, e->motion.y * d);
            if (now - g_lastMove >= 50000000ull) {   // 20 lines/s at most
                g_lastMove = now;
                Line("M", "x=%.1f y=%.1f px=%.1f py=%.1f rel=%.1f,%.1f btn=%u", e->motion.x, e->motion.y,
                     e->motion.x * d, e->motion.y * d, e->motion.xrel, e->motion.yrel, unsigned(e->motion.state));
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const float d = DensityOf(e->button.windowID);
            Track(e->button.x * d, e->button.y * d);
            Line("B", "btn=%d down=%d clicks=%d x=%.1f y=%.1f px=%.1f py=%.1f mod=0x%x", e->button.button,
                 e->button.down ? 1 : 0, e->button.clicks, e->button.x, e->button.y, e->button.x * d,
                 e->button.y * d, unsigned(SDL_GetModState()));
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            Line("W", "y=%.2f x=%.2f flipped=%d mod=0x%x", e->wheel.y, e->wheel.x,
                 e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? 1 : 0, unsigned(SDL_GetModState()));
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            bool now_eff[256];
            EffectiveDik(now_eff);
            std::string dn, up;
            char b[16];
            for (int i = 1; i < 256; ++i) {
                if (now_eff[i] && !g_prevEff[i]) {
                    snprintf(b, sizeof b, "%s0x%02X", dn.empty() ? "" : "+", i);
                    dn += b;
                }
                if (!now_eff[i] && g_prevEff[i]) {
                    snprintf(b, sizeof b, "%s0x%02X", up.empty() ? "" : "+", i);
                    up += b;
                }
                g_prevEff[i] = now_eff[i];
            }
            const int sc = int(e->key.scancode);
            const int dik = (sc >= 0 && sc < Platform::Input::kSdlScancodeCount) ? Platform::Input::kSdlScancodeToDik[sc] : 0;
            std::string kname = SDL_GetScancodeName(e->key.scancode);
            for (char& c : kname)
                if (c == ' ' || c == '=') c = '_';
            if (kname.empty()) kname = "?";
            Line("K", "down=%d rep=%d sc=%d name=%s dik=0x%02X mod=0x%x eff_dn=%s eff_up=%s", e->key.down ? 1 : 0,
                 e->key.repeat ? 1 : 0, sc, kname.c_str(), dik, unsigned(e->key.mod),
                 dn.empty() ? "-" : dn.c_str(), up.empty() ? "-" : up.c_str());
            break;
        }
        case SDL_EVENT_QUIT:
            Line("Q", "kind=quit");
            break;
        default:
            if (e->type >= SDL_EVENT_WINDOW_FIRST && e->type <= SDL_EVENT_WINDOW_LAST) {
                if (e->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) Line("Q", "kind=close_requested");
                if (const char* n = WindowEventName(e->type)) {
                    SDL_Window* w = SDL_GetWindowFromID(e->window.windowID);
                    const WinInfo i = QueryWindow(w);
                    Line("E", "name=%s d1=%d d2=%d win=%dx%d px=%dx%d density=%.2f fs=%d focus=%d", n,
                         int(e->window.data1), int(e->window.data2), i.ptw, i.pth, i.pxw, i.pxh, i.density,
                         i.fs, i.focus);
                }
            }
            break;
    }
    if (now - g_lastSummary >= 1000000000ull) {
        g_lastSummary = now;
        Summary();
    }
    return true;
}

// Once a second a timer thread asks the MAIN thread (SDL_RunOnMainThread, run by the event pump)
// to log an S line, so S lines also appear while no event arrives. The SDL window queries in
// Summary() therefore never run off the main thread.
void TickOnMain(void*) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_f || SDL_WasInit(SDL_INIT_VIDEO) == 0) return;
    const Uint64 now = SDL_GetTicksNS();
    if (now - g_lastSummary >= 1000000000ull) {
        g_lastSummary = now;
        Summary();
    }
}

Uint32 SDLCALL OnTimer(void*, SDL_TimerID, Uint32 interval) {
    SDL_RunOnMainThread(TickOnMain, nullptr, false);
    return interval;
}

void AtExit() { Shutdown(); }

}  // namespace

namespace {
// Everything of Install() that touches the trace state. Caller holds g_mu.
bool InstallLocked(const char* path) {
    if (g_installed) return false;
    FILE* f = fopen(path, "a");
    if (!f) return false;
    g_f = f;
    g_installed = true;
    g_t0 = SDL_GetTicksNS();
    g_lastSummary = g_t0;
    g_events = 0;
    for (int i = 0; i < 256; ++i)
        g_prevEff[i] = false;
    if (!g_atexit) {
        g_atexit = true;
        atexit(AtExit);
    }
    const WinInfo i = QueryWindow(GameWindow());
    Line("H", "version=1 platform=%s video=%s sdl=%d win=%dx%d px=%dx%d density=%.2f",
         SDL_GetPlatform(), SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "none",
         int(SDL_GetVersion()), i.ptw, i.pth, i.pxw, i.pxh, i.density);
    return true;
}

}  // namespace

bool Install(const char* path) {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (!InstallLocked(path))
            return false;
    }
    // Registered outside g_mu: OnEvent takes g_mu under SDL's watcher lock, so holding g_mu
    // across SDL_AddEventWatch would be a lock-order inversion. Shutdown removes it outside too.
    SDL_AddEventWatch(OnEvent, nullptr);
    g_timer = SDL_AddTimer(1000, OnTimer, nullptr);
    return true;
}

void Shutdown() {
    if (g_timer) {
        SDL_RemoveTimer(g_timer);
        g_timer = 0;
    }
    SDL_RemoveEventWatch(OnEvent, nullptr);
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_f) return;
    if (SDL_WasInit(SDL_INIT_VIDEO) != 0) Summary();
    Line("X", "events=%llu", (unsigned long long)g_events);
    fclose(g_f);
    g_f = nullptr;
    g_installed = false;
}

void Start() {
    if (Opt().inputTrace) Install("input_trace.log");
}

}  // namespace InputTrace
}  // namespace Diag
