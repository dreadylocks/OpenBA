// src/ext/auto_update/launch_plan.cpp -- NOT RETAIL. ext.auto_update: the launch decisions
// (launch_plan.h; docs/fork/AUTO_UPDATE.md §6, §8).
#include "ext/auto_update/launch_plan.h"

#include "ext/auto_update/manifest.h"   // IsNewer

namespace AutoUpdate {

InPlacePlan PlanInPlace(const StagingState& s, const std::string& mine, bool enabled) {
    InPlacePlan plan;
    if (s.ready.empty()) return plan;
    plan.version = s.ready;
    plan.step = InPlaceStep::Discard;
    if (!enabled) {
        plan.why = "updates are off (ext.auto_update=0)";
    } else if (!s.readyComplete) {
        plan.why = "the staged build is incomplete";
    } else if (s.bad.count(s.ready) != 0) {
        plan.why = "it failed to start before";
    } else if (!IsNewer(s.ready, mine)) {
        plan.why = "it is not newer than this build (" + mine + ")";
    } else {
        plan.step = InPlaceStep::Install;
    }
    return plan;
}

VersionedPlan PlanVersioned(const StagingState& s, const std::string& mine, bool selfInstalled, bool enabled) {
    VersionedPlan plan;
    plan.current = s.current;
    plan.previous = s.previous;
    std::set<std::string> bad = s.bad;
    bool currentComplete = s.currentComplete;
    std::string failed;

    // 1. A hand-over that never reached the menu. When this program IS that attempt (it was just
    //    handed over to), it is still starting: carry on and let its menu write `started`.
    if (!s.pending.empty() && s.started != s.pending) {
        if (selfInstalled && s.pending == mine) return plan;
        failed = s.pending;
        plan.markBad = failed;
        bad.insert(failed);
        plan.clearPending = true;
        if (plan.current == failed) {
            plan.current = plan.previous;
            plan.previous.clear();
            currentComplete = !plan.current.empty();   // the previous build ran before
            plan.stateChanged = true;
        }
    }

    // 2. A staged build: promote it when it is newer than both this program and `current`.
    if (!s.ready.empty()) {
        const bool currentUsable = !plan.current.empty() && currentComplete && bad.count(plan.current) == 0;
        const std::string newest = currentUsable && IsNewer(plan.current, mine) ? plan.current : mine;
        if (!enabled) {
            plan.why = "updates are off (ext.auto_update=0)";
        } else if (!s.readyComplete) {
            plan.why = "the staged build is incomplete";
        } else if (bad.count(s.ready) != 0) {
            plan.why = "it failed to start before";
        } else if (!IsNewer(s.ready, newest)) {
            plan.why = "it is not newer than " + newest;
        } else {
            plan.promoteReady = true;
            if (plan.current != s.ready) {
                plan.previous = currentUsable ? plan.current : std::string();
                plan.current = s.ready;
            }
            currentComplete = true;
            plan.stateChanged = true;
        }
        plan.discardReady = !plan.promoteReady;
    }

    // 3. What runs: `current` when it is usable and newer than this program, else this program.
    const bool currentUsable = !plan.current.empty() && currentComplete && bad.count(plan.current) == 0;
    if (currentUsable && IsNewer(plan.current, mine)) {
        plan.run = plan.current;
    } else if (currentUsable && selfInstalled && plan.current != mine) {
        // An installed build that is not `current` (a rollback moved `current` below it) hands over
        // to `current`.
        plan.run = plan.current;
    }
    if (!plan.run.empty()) plan.writePending = s.started != plan.run;   // its first start is watched
    if (!failed.empty()) {
        const std::string now = plan.run.empty() ? mine : plan.run;
        plan.notice = "OpenBA " + failed + " did not start, so this is " + now + " again.";
    }
    return plan;
}

}  // namespace AutoUpdate
