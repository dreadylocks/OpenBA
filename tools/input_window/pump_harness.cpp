// ============================================================================
// tools/input_window/pump_harness.cpp -- the REAL pump / input code against synthetic SDL events
// ============================================================================
// Links the shipping src/platform/sdl/{pump,input,display}_sdl.cpp and window_messages.cpp
// (not copies) and drives them with SDL_PushEvent and SDL's own keyboard-state entry points,
// on SDL's DUMMY video driver: no window is ever shown, no sound, no focus change, nothing
// reaches the screen. Build + run: tools/input_window/run_harness.sh (build/mac-input).
//
// It stands in for window_sdl.cpp with the two functions the
// code under test calls: Window::Sdl::GameWindow() and Window::NativeHandle().
// Exit code 0 = every check passed.
// ============================================================================
#include "platform_input.h"
#include "platform_window.h"
#include "platform_window_hooks.h"
#include "window_messages.h"
#include "window_sdl_internal.h"
#include "mac_keymap.h"
#include "sdl_dik_tables.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstring>
#include <vector>

// SDL internals (static libSDL3.a keeps them linkable): inject keyboard state exactly as a
// driver would, so Platform::Input::ReadKeyboard reads real SDL state.
extern "C" {
bool SDL_SetKeyboardFocus(SDL_Window* window);
bool SDL_SendKeyboardKey(Uint64 timestamp, SDL_KeyboardID keyboardID, int rawcode,
                         SDL_Scancode scancode, bool down);
}

// mac_keymap.h spells SDL's numbers so `unit` needs no SDL header: pin them to the real enums.
namespace MK = Platform::Input::MacKeys;
static_assert(MK::kBackspace == SDL_SCANCODE_BACKSPACE && MK::kReturn == SDL_SCANCODE_RETURN, "");
static_assert(MK::kF9 == SDL_SCANCODE_F9 && MK::kF10 == SDL_SCANCODE_F10 && MK::kF11 == SDL_SCANCODE_F11 && MK::kF12 == SDL_SCANCODE_F12, "");
static_assert(MK::kPrintScreen == SDL_SCANCODE_PRINTSCREEN && MK::kScrollLock == SDL_SCANCODE_SCROLLLOCK && MK::kPause == SDL_SCANCODE_PAUSE, "");
static_assert(MK::kInsert == SDL_SCANCODE_INSERT && MK::kHome == SDL_SCANCODE_HOME && MK::kPageUp == SDL_SCANCODE_PAGEUP, "");
static_assert(MK::kDelete == SDL_SCANCODE_DELETE && MK::kEnd == SDL_SCANCODE_END && MK::kPageDown == SDL_SCANCODE_PAGEDOWN, "");
static_assert(MK::kRight == SDL_SCANCODE_RIGHT && MK::kLeft == SDL_SCANCODE_LEFT && MK::kDown == SDL_SCANCODE_DOWN && MK::kUp == SDL_SCANCODE_UP, "");
static_assert(MK::kF13 == SDL_SCANCODE_F13 && MK::kF14 == SDL_SCANCODE_F14 && MK::kF15 == SDL_SCANCODE_F15, "");
static_assert(MK::kLCtrl == SDL_SCANCODE_LCTRL && MK::kLAlt == SDL_SCANCODE_LALT && MK::kLGui == SDL_SCANCODE_LGUI, "");
static_assert(MK::kRCtrl == SDL_SCANCODE_RCTRL && MK::kRAlt == SDL_SCANCODE_RALT && MK::kRGui == SDL_SCANCODE_RGUI, "");
static_assert(MK::kStateCount == SDL_SCANCODE_COUNT, "");
static_assert(MK::kModCtrl == SDL_KMOD_CTRL && MK::kModGui == SDL_KMOD_GUI && MK::kModOption == SDL_KMOD_ALT, "");
static_assert(MK::kButtonLeft == SDL_BUTTON_LMASK && MK::kButtonMiddle == SDL_BUTTON_MMASK, "");
static_assert(SDL_SCANCODE_F == 9, "");

static SDL_Window* g_win = nullptr;
namespace Platform {
namespace Window {
namespace Sdl {
SDL_Window* GameWindow() { return g_win; }
}  // namespace Sdl
void* NativeHandle() { return g_win; }
}  // namespace Window
}  // namespace Platform

// The Darwin window hook display_sdl.cpp calls (window_darwin.cpp is not linked here): identity.
namespace Platform {
namespace Window {
void FitToUsableScreen(SDL_Window*, const FrameBorders&, int32_t*, int32_t*, int32_t*, int32_t*) {
}
}  // namespace Window
}  // namespace Platform

static int g_fail = 0;
static int g_pass = 0;
#define CHECK(c)                                             \
    do {                                                     \
        if (c) {                                             \
            ++g_pass;                                        \
        } else {                                             \
            ++g_fail;                                        \
            std::printf("FAIL line %d: %s\n", __LINE__, #c); \
        }                                                    \
    } while (0)

struct Msg {
    uint32_t m;
    uint64_t w;
    int64_t l;
};
static std::vector<Msg> g_msgs;
static int64_t Handler(void*, uint32_t m, uint64_t w, int64_t l) {
    g_msgs.push_back({ m, w, l });
    return 0;
}
static bool g_trace = false;
static bool PumpOnce() {
    g_msgs.clear();
    bool r = Platform::Pump(Handler);
    if (g_trace) {
        std::printf("pump->%d:", (int)r);
        for (auto& x : g_msgs)
            std::printf(" %04x", x.m);
        std::printf("\n");
    }
    return r;
}
static bool Saw(uint32_t m) {
    for (auto& x : g_msgs)
        if (x.m == m) return true;
    return false;
}

static void PushWindow(SDL_EventType t, int d1 = 0, int d2 = 0) {
    SDL_Event e;
    SDL_zero(e);
    e.type = t;
    e.window.windowID = SDL_GetWindowID(g_win);
    e.window.data1 = d1;
    e.window.data2 = d2;
    SDL_PushEvent(&e);
}
static void PushKey(SDL_Scancode sc, SDL_Keymod mod, bool repeat = false) {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.windowID = SDL_GetWindowID(g_win);
    e.key.scancode = sc;
    e.key.mod = mod;
    e.key.down = true;
    e.key.repeat = repeat;
    SDL_PushEvent(&e);
}
static void Key(SDL_Scancode sc, bool down) { SDL_SendKeyboardKey(0, 0, 0, sc, down); }
static void Release(std::initializer_list<SDL_Scancode> l) {
    for (auto s : l)
        Key(s, false);
}
static bool DikDown(const uint8_t* dik, int code) { return (dik[code] & 0x80) != 0; }

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--trace") == 0)
            g_trace = true;
    }
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SDL_Init: %s\n", SDL_GetError());
        return 2;
    }
    g_win = SDL_CreateWindow("harness", 640, 360, SDL_WINDOW_RESIZABLE);
    if (!g_win) {
        std::printf("window: %s\n", SDL_GetError());
        return 2;
    }
    std::printf("video driver: %s\n", SDL_GetCurrentVideoDriver());

    // Drain whatever the window creation queued.
    SDL_PumpEvents();
    PumpOnce();

    // -- 1. quit / close / minimise ------------------------------------------------------
    SDL_Event q;
    SDL_zero(q);
    q.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&q);   // Cmd+Q / Dock Quit
    CHECK(PumpOnce() == true);
    PumpOnce();                                                             // (a quit returns at once: drain the rest)
    PushWindow(SDL_EVENT_WINDOW_CLOSE_REQUESTED);                           // the red close button / Cmd+W
    CHECK(PumpOnce() == false);
    CHECK(Saw(Platform::Window::kMsgClose) && Saw(Platform::Window::kMsgDestroy));
    Platform::Window::PostQuitMessage(0);                                   // WM_DESTROY's arm
    Platform::Window::PostQuitMessage(0);
    CHECK(PumpOnce() == true);                                              // once...
    CHECK(PumpOnce() == false);                                             // ...not twice
    PushWindow(SDL_EVENT_WINDOW_MINIMIZED);                                 // yellow button / Cmd+M
    PumpOnce();
    CHECK(Saw(Platform::Window::kMsgSize) && g_msgs[0].w == Platform::Window::kSizeMinimized);
    PushWindow(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, 1280, 720);             // live resize
    PumpOnce();
    CHECK(Saw(Platform::Window::kMsgSize) && g_msgs[0].l == ((720 << 16) | 1280));
    PushWindow(SDL_EVENT_WINDOW_FOCUS_GAINED);
    PumpOnce();
    CHECK(Saw(Platform::Window::kMsgActivate) && Saw(Platform::Window::kMsgSetFocus));
    PushWindow(SDL_EVENT_WINDOW_FOCUS_LOST);
    PumpOnce();
    CHECK(Saw(Platform::Window::kMsgKillFocus));
    PushWindow(SDL_EVENT_WINDOW_MOVED, 10, 10);                             // window position memory
    PumpOnce();
    CHECK(Saw(Platform::Window::kMsgExitSizeMove));

    // -- 2. mouse messages ---------------------------------------------------------------
    {
        SDL_Event e;
        SDL_zero(e);
        e.type = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.y = 1.0f;
        e.wheel.windowID = SDL_GetWindowID(g_win);
        SDL_PushEvent(&e);
        CHECK(Platform::Input::MacKeys::kStateCount == SDL_SCANCODE_COUNT);
        SDL_Event b;
        SDL_zero(b);
        b.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        b.button.button = SDL_BUTTON_MIDDLE;
        b.button.down = true;
        SDL_PushEvent(&b);
        PumpOnce();
        CHECK(Saw(Platform::Window::kMsgMouseWheel));
        {   // WM_MBUTTONDOWN rides in the high word of WM_SETCURSOR's lParam (a Win32 hit-test pair)
            bool mb = false;
            for (auto& x : g_msgs)
                if (x.m == Platform::Window::kMsgSetCursor && ((x.l >> 16) & 0xFFFF) == Platform::Window::kMsgMButtonDown) mb = true;
            CHECK(mb);
        }
    }

    // -- 3. the full-screen chord (Control+Command+F) -------------------------------------
#if defined(__APPLE__)
    CHECK((SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN) == 0);
    PushKey(SDL_SCANCODE_F, (SDL_Keymod)(SDL_KMOD_LCTRL | SDL_KMOD_LGUI));
    PumpOnce();
    SDL_SyncWindow(g_win);
    CHECK(g_msgs.empty());                                                   // the game sees no key
    CHECK((SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN) != 0);
    PushKey(SDL_SCANCODE_F, (SDL_Keymod)(SDL_KMOD_LCTRL | SDL_KMOD_LGUI), true);   // key repeat: ignored
    PumpOnce();
    SDL_SyncWindow(g_win);
    CHECK((SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN) != 0);
    PushKey(SDL_SCANCODE_F, (SDL_Keymod)(SDL_KMOD_RCTRL | SDL_KMOD_RGUI));          // toggles back
    PumpOnce();
    SDL_SyncWindow(g_win);
    CHECK((SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN) == 0);
    PushKey(SDL_SCANCODE_F, SDL_KMOD_LGUI);                                  // Command+F alone: nothing
    PumpOnce();
    SDL_SyncWindow(g_win);
    CHECK((SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN) == 0);
#endif

    // -- 4. keyboard through the real ReadKeyboard (SDL state -> Mac layer -> DIK) -----------
    SDL_ShowWindow(g_win);
    SDL_SetKeyboardFocus(g_win);
    uint8_t dik[256];
    if (SDL_GetKeyboardFocus() != g_win) {
        std::printf("note: keyboard focus not settable on the dummy driver; keyboard checks skipped\n");
    } else {
        CHECK(Platform::Input::ReadKeyboard(nullptr, dik) == Platform::Input::kOk);
        Key(SDL_SCANCODE_A, true);
        Platform::Input::ReadKeyboard(nullptr, dik);
        CHECK(DikDown(dik, 0x1E));                                           // A
        Release({ SDL_SCANCODE_A });
#if defined(__APPLE__)
        Key(SDL_SCANCODE_LGUI, true);                                        // Command = Control
        Platform::Input::ReadKeyboard(nullptr, dik);
        CHECK(DikDown(dik, 0x1D) && !DikDown(dik, 0xDB));
        CHECK(Platform::Input::AsyncKeyState(0x11) < 0);                    // legacy VK_CONTROL
        Release({ SDL_SCANCODE_LGUI });
        Key(SDL_SCANCODE_RALT, true);
        Key(SDL_SCANCODE_LEFT, true);          // Right Option + Left = Home
        Platform::Input::ReadKeyboard(nullptr, dik);
        CHECK(DikDown(dik, 0xC7) && !DikDown(dik, 0xCB) && !DikDown(dik, 0xB8));
        CHECK(Platform::Input::AsyncKeyState(0x24) < 0);                    // legacy VK_HOME
        Release({ SDL_SCANCODE_LEFT });
        Key(SDL_SCANCODE_RETURN, true);                                      // + Return = Insert
        Platform::Input::ReadKeyboard(nullptr, dik);
        CHECK(DikDown(dik, 0xD2) && !DikDown(dik, 0x1C));
        Release({ SDL_SCANCODE_RETURN, SDL_SCANCODE_RALT });
        Key(SDL_SCANCODE_F13, true);
        Key(SDL_SCANCODE_F14, true);
        Key(SDL_SCANCODE_F15, true);
        Platform::Input::ReadKeyboard(nullptr, dik);
        CHECK(DikDown(dik, 0xB7) && DikDown(dik, 0x46) && DikDown(dik, 0xC5));
        Release({ SDL_SCANCODE_F13, SDL_SCANCODE_F14, SDL_SCANCODE_F15 });
        Key(SDL_SCANCODE_PAGEUP, true);                                      // fn+Up arrives natively
        Platform::Input::ReadKeyboard(nullptr, dik);
        CHECK(DikDown(dik, 0xC9));
        Release({ SDL_SCANCODE_PAGEUP });
#endif
        Platform::Input::ReadKeyboard(nullptr, dik);
        int down = 0;
        for (int i = 0; i < 256; ++i)
            down += DikDown(dik, i);
        CHECK(down == 0);
    }

    // -- 5. coordinates (density 1 on the dummy driver: the identity, and the round trips) ---
    SDL_SetWindowPosition(g_win, 100, 80);
    SDL_SyncWindow(g_win);
    {
        int px, py;
        SDL_GetWindowPosition(g_win, &px, &py);
        Platform::Point p{ 0, 0 };
        Platform::Window::ClientToScreen(nullptr, &p);
        CHECK(p.x == px && p.y == py);
        Platform::Input::ScreenToClient(nullptr, &p);
        CHECK(p.x == 0 && p.y == 0);
        Platform::Rect r{ px + 10, py + 20, px + 110, py + 220 };
        Platform::Window::ClipCursor(&r);
        const SDL_Rect* got = SDL_GetWindowMouseRect(g_win);
        if (got) std::printf("mouse rect %d %d %d %d (window %d %d)\n", got->x, got->y, got->w, got->h, px, py);
        if (!got)
            std::printf("note: the dummy driver has no mouse-rect support (SDL_Unsupported); clip checks skipped\n");
        else
            CHECK(got->x == 10 && got->y == 20 && got->w == 100 && got->h == 200);
        Platform::Window::ClipCursor(nullptr);
        CHECK(SDL_GetWindowMouseRect(g_win) == nullptr);   // (also true when unsupported)
        Platform::Rect cr;
        Platform::Window::GetClientRect(nullptr, &cr);
        int w, h;
        SDL_GetWindowSizeInPixels(g_win, &w, &h);
        CHECK(cr.right == w && cr.bottom == h);
    }
    std::printf("harness: %d checks passed, %d failed\n", g_pass, g_fail);
    SDL_DestroyWindow(g_win);
    SDL_Quit();
    return g_fail == 0 ? 0 : 1;
}
