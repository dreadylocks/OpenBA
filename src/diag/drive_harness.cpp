// src/diag/drive_harness.cpp -- BANISHED_DRIVE=<dir>: play the game from a script in a file
//
// For agents testing the game on a machine where the window cannot be given real input (it sits
// in the background while someone else uses the screen): they look at the window with a
// screenshot and drive it through this mailbox. Set by tools/drive.sh and the two-player test
// runs (docs/fork/TWO_PLAYER_TESTING.md).
//
//   <dir>/cmd      the script to play: drive_script.h's language (move / click / drag / wheel /
//                  key / type / wait ...). The harness takes it (deletes it) at most half a
//                  second after it appears and plays it, one input frame per game frame, through
//                  Platform::Input::Virtual: the game reads the script's cursor, buttons, wheel
//                  and keys as if they were the devices, in menus and in game alike. A script
//                  with an error is not played. Write it atomically (cmd.tmp, then rename).
//   <dir>/status   one line: "idle", "playing line N" (N = the script's line) or "error: ..."
//                  (atomic writes).
//   <dir>/out      a log, appended: each script's start, its errors, its end.
//   <dir>/info     the window's client area: "client WxH points, density D, WPxHP pixels".
//   <dir>/camera   in game, once a second: where the camera looks (map tile, distance, yaw) and where
//                  this town started ("camera tile X,Y dist D yaw A; town tile X,Y; map N").
//                  The script's `camera X Y [DIST [YAW]]` puts the focus on a tile, and `shot` takes
//                  retail's screenshot with the UI (<save folder>/Screenshots/ScreenshotN.bmp).
//
// While a script plays, the window counts as in front and the real mouse and keyboard are
// ignored; between scripts the devices are the player's again.
#include "drive_harness.h"
#include "drive_script.h"
#include "options.h"
#include "platform/common/platform_input_virtual.h"
#include "platform/common/platform_settings.h"   // Platform::ReadWholeFile / WriteFileAtomic
#include "Entity/ComponentEntity.h"   // InternalPtr<ComponentEntity>::Get() needs the class complete
#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "Game/PlayerController.h"
#include "Game/PlayerToolManager.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

extern "C" bool StartTown_GetSpawnTile(int* x, int* y);   // src/game/Game/starttown.cpp

namespace {

Drive::Runner s_runner;
Drive::Client s_lastClient;
std::chrono::steady_clock::time_point s_start = std::chrono::steady_clock::now();
std::chrono::steady_clock::time_point s_nextCheck;
std::chrono::steady_clock::time_point s_nextCameraWrite;
int  s_lastLine = -1;
bool s_wasRunning = false;

std::string PathOf(const char* name) {
    std::string p = Opt().drive;
    p += '/';
    p += name;
    return p;
}

void Status(const std::string& line) {
    const std::string body = line + "\n";
    Platform::WriteFileAtomic(PathOf("status").c_str(), body.data(), body.size());
}

void Out(const std::string& line) {
    std::fprintf(stderr, "[Drive] %s\n", line.c_str());
    std::fflush(stderr);
    if (FILE* f = std::fopen(PathOf("out").c_str(), "a")) {
        std::fprintf(f, "%s\n", line.c_str());
        std::fclose(f);
    }
}

void WriteInfo(const Drive::Client& c) {
    const float d = c.density > 0.0f ? c.density : 1.0f;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "client %dx%d points, density %.2f, %dx%d pixels\n",
                  static_cast<int>(std::lround(c.widthPx / d)), static_cast<int>(std::lround(c.heightPx / d)), d,
                  c.widthPx, c.heightPx);
    Platform::WriteFileAtomic(PathOf("info").c_str(), buf, std::char_traits<char>::length(buf));
}

// A map tile's world position (camera_input.cpp's convention: cell - (dim / 2 - 0.5)) and back.
float TileToWorld(float tile, int dim) {
    return tile - (static_cast<float>(dim >> 1) - 0.5f);
}

// The script's `camera` step: the focus on a tile, and the distance (with the zoom target, so the
// smoothing does not pull it back) and the heading when given. Nothing outside a game.
void ApplyCamera(const Drive::Frame& f) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_game == nullptr || g->m_playerController == nullptr || g->m_toolManager == nullptr) {
        Out("camera: not in a game");
        return;
    }
    const int dim = g->m_game->size;
    PlayerController* pc = g->m_playerController;
    pc->target.x = TileToWorld(f.cameraX, dim);
    pc->target.y = TileToWorld(f.cameraY, dim);
    if (f.cameraDist >= 0.0f) {
        pc->distance = f.cameraDist;
        g->m_toolManager->field_0x3C = f.cameraDist;
    }
    if (f.cameraYaw >= 0.0f) pc->yaw = f.cameraYaw;
}

// <dir>/camera, once a second in game.
void WriteCamera() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_game == nullptr || g->m_playerController == nullptr) return;
    const int dim = g->m_game->size;
    const PlayerController* pc = g->m_playerController;
    const float half = static_cast<float>(dim >> 1) - 0.5f;
    int tx = -1;
    int ty = -1;
    StartTown_GetSpawnTile(&tx, &ty);
    char buf[200];
    std::snprintf(buf, sizeof(buf), "camera tile %.1f,%.1f dist %.1f yaw %.1f; town tile %d,%d; map %d\n",
                  pc->target.x + half, pc->target.y + half, pc->distance, pc->yaw, tx, ty, dim);
    Platform::WriteFileAtomic(PathOf("camera").c_str(), buf, std::char_traits<char>::length(buf));
}

// Takes <dir>/cmd when it is there: parses it and starts it, or reports why not.
void TakeScript() {
    std::string text;
    if (!Platform::ReadWholeFile(PathOf("cmd").c_str(), text)) return;
    std::remove(PathOf("cmd").c_str());
    const Drive::Parsed parsed = Drive::Parse(text);
    if (!parsed.errors.empty()) {
        for (const std::string& e : parsed.errors) Out("error " + e);
        Status("error: " + parsed.errors.front());
        return;
    }
    s_lastLine = -1;
    s_runner.Start(parsed);
    Out("script: " + std::to_string(parsed.commands) + " command(s), " + std::to_string(parsed.steps.size()) +
        " step(s)");
}

bool Source(const Platform::Input::Virtual::Client& client, Platform::Input::Virtual::Frame* out) {
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - s_start).count();

    Drive::Client c;
    c.widthPx = client.widthPx;
    c.heightPx = client.heightPx;
    c.density = client.density;
    if (c.widthPx != s_lastClient.widthPx || c.heightPx != s_lastClient.heightPx ||
        c.density != s_lastClient.density) {
        s_lastClient = c;
        WriteInfo(c);
    }

    if (now >= s_nextCameraWrite) {
        s_nextCameraWrite = now + std::chrono::seconds(1);
        WriteCamera();
    }
    if (!s_runner.Running() && now >= s_nextCheck) {
        s_nextCheck = now + std::chrono::milliseconds(500);
        TakeScript();
    }
    if (!s_runner.Running()) {
        if (s_wasRunning) {
            s_wasRunning = false;
            Out("script done");
            Status("idle");
        }
        return false;
    }
    s_wasRunning = true;
    const Drive::Frame f = s_runner.Next(c, seconds);
    if (f.camera) ApplyCamera(f);
    if (f.shot) {
        GameUpdateLoop* g = GameUpdateLoop::_singleton;
        if (g != nullptr && g->m_game != nullptr) {
            g->m_quickLoadPressed = 1;   // Draw's step 13: TakeScreenshot after the UI, which clears it
            Out("shot: requested (a new ScreenshotN.bmp in the save folder's Screenshots)");
        } else {
            Out("shot: not in a game");
        }
    }
    const int line = s_runner.CurrentLine();
    if (line != s_lastLine && line != 0) {
        s_lastLine = line;
        Status("playing line " + std::to_string(line));
    }
    out->x = f.x;
    out->y = f.y;
    out->buttons = f.buttons;
    out->wheel = f.wheel;
    for (int i = 0; i < 256; ++i) out->keys[i] = f.keys[i];
    return true;
}

}  // namespace

// src/app/main.cpp, after OptionsInit: installs the source when BANISHED_DRIVE names a folder.
void DriveHarness_Install() {
    if (Opt().drive == nullptr) return;
    Status("idle");
    Out("ready: write a script to " + PathOf("cmd"));
    Platform::Input::Virtual::SetSource(&Source);
}
