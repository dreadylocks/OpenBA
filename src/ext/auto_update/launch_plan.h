#pragma once
// ============================================================================
// src/ext/auto_update/launch_plan.h -- NOT RETAIL. ext.auto_update: what a launch does with the
// staging folder (docs/fork/AUTO_UPDATE.md §6, §8)
// ============================================================================
// Pure: the decisions only, as a function of what the staging folder holds (staging.h reads it and
// carries the plan out). tests/unit/auto_update_test.cpp walks every row.
//
// TWO MODELS (Platform::Update::Model, platform_update.h):
//   InPlace   (Windows, Linux): the new program replaces this one in its own folder; the old one
//             starts it and watches it (the supervisor, auto_update.cpp).
//   Versioned (macOS): each installed build is a folder Updates/<version>/OpenBA.app, `current`
//             names the one to run, and the player's own bundle hands over to it with execv.
// ============================================================================

#include <set>
#include <string>

namespace AutoUpdate {

// The staging folder's small state files (staging.h), as read at launch.
struct StagingState {
    std::string           ready;            // `ready`: a verified, unpacked build ("" none)
    bool                  readyComplete = false;   // staged/<ready>/ holds the package's program
    std::string           pending;          // `pending`: handed over or installed, not yet started
    std::string           started;          // `started`: the last build that reached its menu
    std::set<std::string> bad;              // `bad`: builds that failed to start; never installed again
    // Versioned model only.
    std::string current;                    // `current`: Updates/<current>/OpenBA.app is the one to run
    bool        currentComplete = false;    // that bundle holds its engine
    std::string previous;                   // `previous`: what `current` was before ("" = the player's bundle)
};

// ---- InPlace -----------------------------------------------------------------------------------
enum class InPlaceStep {
    Nothing,   // nothing staged
    Discard,   // staged/<ready> is not to be installed (why says so); delete it
    Install,   // install staged/<ready> over this program and start it
};
struct InPlacePlan {
    InPlaceStep step = InPlaceStep::Nothing;
    std::string version;   // the staged build
    std::string why;       // Discard: the reason, for the log
};
// `mine`: this program's OPENBA_VERSION. `enabled`: ext.auto_update.
InPlacePlan PlanInPlace(const StagingState& s, const std::string& mine, bool enabled);

// ---- Versioned ---------------------------------------------------------------------------------
struct VersionedPlan {
    std::string markBad;            // add to `bad` ("" none): its last start never reached the menu
    bool        discardReady = false;
    bool        promoteReady = false;   // staged/<ready> -> <ready>; current and previous below
    std::string current;            // `current` after the plan
    std::string previous;           // `previous` after the plan
    bool        stateChanged = false;   // current / previous differ from what was read
    std::string run;                // the installed build to hand over to; "" = carry on as this program
    bool        writePending = false;   // write pending = run before handing over (its first start)
    bool        clearPending = false;
    std::string notice;             // one line for the menu ("" none)
    std::string why;                // discardReady: the reason, for the log
};
// `mine`: this program's OPENBA_VERSION. `selfInstalled`: this program is Updates/<mine>/OpenBA.app
// (a build the updater installed), not the player's own bundle. `enabled`: ext.auto_update. Off
// stops new builds being promoted; the installed `current` still runs (off keeps the player on the
// build they are on).
VersionedPlan PlanVersioned(const StagingState& s, const std::string& mine, bool selfInstalled, bool enabled);

}  // namespace AutoUpdate
