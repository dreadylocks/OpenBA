// src/diag/showroom.cpp
// BANISHED_SHOWROOM — the building showroom (see showroom.h). Harness only, not retail.
#include "showroom.h"
#include "options.h"
#include "pausemenu_harness.h"   // PauseMenuHarness_QuitGame: the run ends after the last shot
#include "io/FileSystem.h"
#include "system/Filename.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

// Frames (GameUpdateLoop::Update calls) the world gets before the first shot: the fade-in
// and any texture created on first bind have landed by then.
constexpr int kSettleFrames = 300;
// Frames between pinning the camera on a building and requesting its shot.
constexpr int kFrameFrames = 60;
// Frames to wait for the retail grab to write its .bmp before giving up on a shot.
constexpr int kShotTimeoutFrames = 300;
// Frames between the .bmp appearing and moving it (lets the writer finish).
constexpr int kMoveDelayFrames = 5;
// Two headings per building, degrees, so the walls under the eaves on both sides are seen.
constexpr float kYaws[2] = {45.0f, 225.0f};
constexpr int kViews = 2;
// Camera distance per unit of the model's largest extent, and its clamp. The pin writes the
// distance directly, so it may go closer than retail's 20-unit zoom floor.
constexpr float kFit = 1.1f;
constexpr float kDistMin = 10.0f;
constexpr float kDistMax = 130.0f;
// Bare ground kept around the bounding box of the placed buildings, in tiles.
constexpr int kAreaMargin = 8;
constexpr const char* kOutDir = "showroom";

struct Entry {
    std::string path;   // "Template\X.rsc"
    std::string name;   // "X"
    bool placed = false;
    float worldX = 0.0f;             // footprint centre, the fallback focus
    float worldY = 0.0f;
    float footprint = 0.0f;          // larger footprint side, in tiles
    const float* bounds = nullptr;   // the model's world box: min xyz, max xyz
};

// One framed shot: where the camera looks and from how far.
struct Focus {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float dist = 40.0f;
};

enum class Phase { Settle, Frame, AwaitFile, Done };

struct State {
    bool loaded = false;
    std::vector<Entry> entries;
    Phase phase = Phase::Settle;
    int frame = 0;          // Tick calls so far
    int phaseStart = 0;     // frame the current phase began
    int current = -1;       // entry being framed / shot
    int view = 0;           // index into kYaws
    Focus focus;            // the current shot's framing
    int shots = 0;
    int fileSeenAt = -1;
    std::string pendingShot;   // the ScreenshotN.bmp the retail grab is about to write
    bool hasArea = false;      // the bare rectangle, in tiles (inclusive)
    int areaX0 = 0;
    int areaY0 = 0;
    int areaX1 = 0;
    int areaY1 = 0;
};

State& S() {
    static State s;
    return s;
}

std::string Trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

void Load() {
    State& s = S();
    if (s.loaded) return;
    s.loaded = true;
    const char* file = Opt().showroom;
    if (!file || !file[0]) return;
    FILE* f = std::fopen(file, "r");
    if (!f) {
        std::fprintf(stderr, "[Showroom] cannot open list %s\n", file);
        return;
    }
    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        std::string text = line;
        const size_t hash = text.find('#');
        if (hash != std::string::npos) text.resize(hash);
        // A tab-separated list (the ranking TSV) is accepted: its first column is the path.
        const size_t tab = text.find('\t');
        if (tab != std::string::npos) text.resize(tab);
        text = Trim(text);
        if (text.empty()) continue;
        for (char& c : text) {
            if (c == '/') c = '\\';
        }
        Entry e;
        e.path = text;
        const size_t slash = text.rfind('\\');
        std::string stem = slash == std::string::npos ? text : text.substr(slash + 1);
        const size_t dot = stem.rfind('.');
        if (dot != std::string::npos) stem.resize(dot);
        e.name = stem;
        s.entries.push_back(e);
    }
    std::fclose(f);
    std::fprintf(stderr, "[Showroom] list %s: %zu templates\n", file, s.entries.size());
}

// The file the retail grab will write next: <save dir>Screenshots\Screenshot<N>.bmp for
// the first N that does not exist (GameUpdateLoop::TakeScreenshot's own probe).
std::string NextScreenshotPath() {
    const IO::FileSystem* fs = IO::FileSystem::_singleton;
    if (!fs) return std::string();
    System::Filename saveDir(fs->GetSaveDirectory());
    const char* dir = saveDir.AsString()._data;
    if (!dir) return std::string();
    for (int n = 0; n < 100000; ++n) {
        char path[1024];
        std::snprintf(path, sizeof(path), "%sScreenshots\\Screenshot%d.bmp", dir, n);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return path;
    }
    return std::string();
}

void MoveShot(const std::string& from) {
    const State& s = S();
    const Entry& e = s.entries[s.current];
    std::error_code ec;
    std::filesystem::create_directories(kOutDir, ec);
    char to[512];
    std::snprintf(to, sizeof(to), "%s\\%02d_%s_y%03d.bmp", kOutDir, s.current, e.name.c_str(),
                  (int)kYaws[s.view]);
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
    if (!ec) std::filesystem::remove(from, ec);
    std::fprintf(stderr, "[Showroom] shot %d %s -> %s%s\n", s.current, e.name.c_str(), to,
                 ec ? " (MOVE FAILED)" : "");
}

// Frame the building on its model's world box (Scene::Clean has filled it in by the end
// of the settle); a box that is still empty or absurd falls back to the footprint.
Focus FrameFor(const Entry& e) {
    Focus f;
    f.x = e.worldX;
    f.y = e.worldY;
    float size = e.footprint;
    if (e.bounds) {
        const float* b = e.bounds;
        const float dx = b[3] - b[0];
        const float dy = b[4] - b[1];
        const float dz = b[5] - b[2];
        if (dx > 0.0f && dy > 0.0f && dz >= 0.0f && dx < 1000.0f && dy < 1000.0f && dz < 1000.0f) {
            f.x = (b[0] + b[3]) * 0.5f;
            f.y = (b[1] + b[4]) * 0.5f;
            f.z = (b[2] + b[5]) * 0.5f;
            const float across = std::sqrt(dx * dx + dy * dy);
            size = across > dz ? across : dz;
        }
    }
    f.dist = kFit * size;
    if (f.dist < kDistMin) f.dist = kDistMin;
    if (f.dist > kDistMax) f.dist = kDistMax;
    return f;
}

int NextPlaced(int after) {
    const State& s = S();
    for (int i = after + 1; i < (int)s.entries.size(); ++i) {
        if (s.entries[i].placed) return i;
    }
    return -1;
}

void Enter(Phase phase) {
    State& s = S();
    s.phase = phase;
    s.phaseStart = s.frame;
}

void Finish() {
    State& s = S();
    int placed = 0;
    for (const Entry& e : s.entries) {
        if (e.placed) ++placed;
    }
    std::fprintf(stderr, "[Showroom] DONE placed=%d of %zu, shots=%d\n", placed, s.entries.size(), s.shots);
    std::fflush(stderr);
    Enter(Phase::Done);
    // The camera is released with the last shot; without this the exe sits on a live world showing
    // whatever the free camera lands on until the script kills it. Quit Game through the pause menu
    // instead, so the settings round-trip and the normal shutdown run.
    PauseMenuHarness_QuitGame();
}

// Frame the current entry from the current heading, or finish when none is left.
void BeginShot() {
    State& s = S();
    if (s.current < 0) {
        Finish();
        return;
    }
    const Entry& e = s.entries[s.current];
    s.focus = FrameFor(e);
    std::fprintf(stderr, "[Showroom] framing %d %s yaw=%.0f focus=(%.1f,%.1f,%.1f) dist=%.1f", s.current,
                 e.name.c_str(), kYaws[s.view], s.focus.x, s.focus.y, s.focus.z, s.focus.dist);
    if (e.bounds) {
        const float* b = e.bounds;
        std::fprintf(stderr, " bounds=(%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)", b[0], b[1], b[2], b[3], b[4], b[5]);
    }
    std::fprintf(stderr, "\n");
    Enter(Phase::Frame);
}

}  // namespace

extern "C" bool Showroom_Enabled() {
    Load();
    return !S().entries.empty();
}

extern "C" int Showroom_Count() {
    Load();
    return (int)S().entries.size();
}

extern "C" const char* Showroom_Path(int index) {
    return S().entries[index].path.c_str();
}

extern "C" const char* Showroom_Name(int index) {
    return S().entries[index].name.c_str();
}

extern "C" void Showroom_NotePlaced(int index, bool placed, int tileX, int tileY, int w, int h, int mapDim,
                                    const float* worldBounds, const char* why) {
    State& s = S();
    Entry& e = s.entries[index];
    e.placed = placed;
    if (!placed) {
        std::fprintf(stderr, "[Showroom] NOT placed %d %s: %s\n", index, e.path.c_str(), why ? why : "?");
        return;
    }
    // Footprint centre in world units: world = cell - mapDim/2 (skill camera-pin).
    const float half = (float)(mapDim >> 1);
    e.worldX = (float)tileX + (float)w * 0.5f - half;
    e.worldY = (float)tileY + (float)h * 0.5f - half;
    e.footprint = (float)(w > h ? w : h);
    e.bounds = worldBounds;
    const int x0 = tileX - kAreaMargin;
    const int y0 = tileY - kAreaMargin;
    const int x1 = tileX + w - 1 + kAreaMargin;
    const int y1 = tileY + h - 1 + kAreaMargin;
    if (!s.hasArea) {
        s.hasArea = true;
        s.areaX0 = x0;
        s.areaY0 = y0;
        s.areaX1 = x1;
        s.areaY1 = y1;
    } else {
        s.areaX0 = x0 < s.areaX0 ? x0 : s.areaX0;
        s.areaY0 = y0 < s.areaY0 ? y0 : s.areaY0;
        s.areaX1 = x1 > s.areaX1 ? x1 : s.areaX1;
        s.areaY1 = y1 > s.areaY1 ? y1 : s.areaY1;
    }
    std::fprintf(stderr, "[Showroom] placed %d %s tile=(%d,%d) fp=%dx%d world=(%.1f,%.1f)\n", index,
                 e.path.c_str(), tileX, tileY, w, h, e.worldX, e.worldY);
}

extern "C" bool Showroom_InArea(int x, int y) {
    const State& s = S();
    return s.hasArea && x >= s.areaX0 && x <= s.areaX1 && y >= s.areaY0 && y <= s.areaY1;
}

extern "C" bool Showroom_WantsPause() {
    return Showroom_Enabled() && S().phase != Phase::Done;
}

extern "C" bool Showroom_Tick() {
    if (!Showroom_Enabled()) return false;
    State& s = S();
    ++s.frame;
    const int inPhase = s.frame - s.phaseStart;
    switch (s.phase) {
    case Phase::Settle:
        if (inPhase < kSettleFrames) return false;
        std::fprintf(stderr, "[Showroom] bare area tiles (%d,%d)-(%d,%d)\n", s.areaX0, s.areaY0, s.areaX1,
                     s.areaY1);
        s.current = NextPlaced(-1);
        s.view = 0;
        BeginShot();
        return false;
    case Phase::Frame:
        if (inPhase < kFrameFrames) return false;
        s.pendingShot = NextScreenshotPath();
        s.fileSeenAt = -1;
        Enter(Phase::AwaitFile);
        return true;
    case Phase::AwaitFile: {
        std::error_code ec;
        if (s.fileSeenAt < 0 && std::filesystem::exists(s.pendingShot, ec)) s.fileSeenAt = s.frame;
        if (s.fileSeenAt >= 0 && s.frame - s.fileSeenAt >= kMoveDelayFrames) {
            MoveShot(s.pendingShot);
            ++s.shots;
        } else if (inPhase < kShotTimeoutFrames) {
            return false;
        } else {
            std::fprintf(stderr, "[Showroom] shot %d %s: no %s after %d frames\n", s.current,
                         s.entries[s.current].name.c_str(), s.pendingShot.c_str(), kShotTimeoutFrames);
        }
        if (s.view + 1 < kViews) {
            ++s.view;
        } else {
            s.view = 0;
            s.current = NextPlaced(s.current);
        }
        BeginShot();
        return false;
    }
    case Phase::Done:
        return false;
    }
    return false;
}

extern "C" bool Showroom_CameraFocus(float* x, float* y, float* z, float* dist, float* yaw) {
    if (!Showroom_Enabled()) return false;
    const State& s = S();
    if (s.current < 0 || s.phase == Phase::Done) return false;
    *x = s.focus.x;
    *y = s.focus.y;
    *z = s.focus.z;
    *dist = s.focus.dist;
    *yaw = kYaws[s.view];
    return true;
}
