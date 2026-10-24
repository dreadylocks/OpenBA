// ============================================================================
// src/engine/io/settings_store.cpp — settings.cfg on disk (Phase D.4 SETTINGS E.3)
// ============================================================================
// NOT RETAIL; see settings_store.h. Standard library + the platform seam only.
// ============================================================================

#include "io/settings_store.h"

#include <mutex>

#include "platform_settings.h"   // src/platform/common

namespace IO {
namespace SettingsStore {

namespace {

// plans/SETTINGS.md §F: the retail map has no lock and neither had the
// registry writes, but the tmp-file replace adds one failure mode of its own
// (two writers on one `settings.cfg.tmp`), so the store serialises its writes.
std::mutex g_writeLock;

void AppendEntry(void* ctx, const char* key, const char* value) {
    static_cast<SettingsFile::Entries*>(ctx)->push_back(  // CAST-OK: user_data
        SettingsFile::Entry{key ? key : "", value ? value : ""});
}

}  // namespace

bool DefaultPath(std::string& out) {
    // BANISHED_USER_DIR (harness, tools/game_dirs.sh): the override IS the
    // "<Documents>\Banished" folder, so the file sits directly inside it.
    if (const char* user = Platform::UserDirOverride()) {
        out = std::string(user) + "\\settings.cfg";
        return true;
    }
    std::string docs;
    if (!Platform::DocumentsDir(docs))
        return false;
    // GetSaveDirectory: Format("%s\\%s\\", docs, "Banished").
    out = docs + "\\Banished\\settings.cfg";
    return true;
}

Source Load(const std::string& path, SettingsFile::Entries& out, LegacyReader legacy) {
    std::string text;
    if (Platform::ReadWholeFile(path.c_str(), text)) {
        out = SettingsFile::Parse(text);
        return Source::File;
    }
    SettingsFile::Entries imported;
    if (legacy != nullptr && legacy(&AppendEntry, &imported)) {
        out.swap(imported);
        return Source::Legacy;
    }
    out.clear();
    return Source::None;
}

Source Load(const std::string& path, SettingsFile::Entries& out) {
    return Load(path, out, &Platform::ReadLegacySettings);
}

bool Store(const std::string& path, const SettingsFile::Entries& entries) {
    std::lock_guard<std::mutex> hold(g_writeLock);

    SettingsFile::Entries disk;
    std::string text;
    if (Platform::ReadWholeFile(path.c_str(), text))
        disk = SettingsFile::Parse(text);

    const std::string out = SettingsFile::Serialize(SettingsFile::Merge(disk, entries));
    return Platform::WriteFileAtomic(path.c_str(), out.data(), out.size());
}

}  // namespace SettingsStore
}  // namespace IO
