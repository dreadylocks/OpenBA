// ============================================================================
// src/platform/common/user_dir_override.cpp -- the harness override of "<Documents>\Banished"
// ============================================================================
// NOT RETAIL. The BANISHED_USER_DIR flag (src/app/options.h, harness section; set by
// tools/game_dirs.sh) replaces the game's per-user data folder, "<Documents>\Banished",
// as a WHOLE: the override directory itself holds settings.cfg, Save\ and the derived
// Cache\. Two game directories run at once by two agents would otherwise share one
// settings.cfg and one save folder (the tools/game_dirs.sh header).
//
// It lives here, not in options.cpp, because the platform layer never reads Options
// (AGENTS.md §4): src/app/main.cpp calls SetUserDirOverride once after OptionsInit, and the
// two consumers of the folder (IO::SettingsStore::DefaultPath and FileSystem::GetSaveDirectory)
// ask UserDirOverride() first. Shared by every platform: the state is a string, nothing in it
// is OS-specific, so it is one file under common/ rather than three twins.
//
// Allocator-free (platform_settings.h's banner): main.cpp's harness guard reads the
// settings before the engine allocator exists.
// ============================================================================

#include "platform_settings.h"

#include <string>

namespace {

std::string g_userDir;   // empty = no override

}  // namespace

namespace Platform {

void SetUserDirOverride(const char* dirUtf8) {
    g_userDir.clear();
    if (dirUtf8 == nullptr || dirUtf8[0] == 0)
        return;
    g_userDir = dirUtf8;
    // No trailing separator, as DocumentsDir: the consumers append their own.
    while (g_userDir.size() > 1 && (g_userDir.back() == '\\' || g_userDir.back() == '/'))
        g_userDir.pop_back();
}

const char* UserDirOverride() {
    return g_userDir.empty() ? nullptr : g_userDir.c_str();
}

}  // namespace Platform
