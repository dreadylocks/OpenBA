// ============================================================================
// src/ext/neighbour/walkers.cpp -- NOT RETAIL. ext.neighbour: the neighbour's villagers, seen (walkers.h)
// ============================================================================
// Ours: CitizenBehavior's list (as town.cpp walks it). Each citizen's ModelComponent holds what the
// neighbour needs: the render position and yaw the move tick writes, the clip on its animation's newest
// layer, the mesh group (gender), and _color (rgb: the size scale, a: the clothes).
// Theirs: one Graphics::MeshInstance each, made as the building mirrors are (shared_map.cpp AddMirror),
// see-through, its clip played as a layer of its own (not the shared instanced pose pool, which only
// moves with this town's simulation clock), its time advanced here with real time.
// ============================================================================

// The runtime type headers first (moddialog.cpp's note: Volume.h's Rectangle).
#include "graphics/Scene.h"
#include "graphics/Mesh.h"
#include "graphics/AnimationGroup.h"
#include "math/Math.h"

#include "ext/neighbour/walkers.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

#include "Component/CharacterComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/ComponentDescription.h"
#include "Component/ModelComponent.h"
#include "Component/ModelDescription.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Game/GameUpdateLoop.h"
#include "object/External.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "system/Timer.h"

#include "ext/neighbour/link.h"
#include "ext/neighbour/protocol.h"

namespace Neighbour {
namespace Walkers {

namespace {

using Clock = std::chrono::steady_clock;

constexpr double kSendEvery    = 1.0;      // seconds between our samples
constexpr double kGlide        = 1.0;      // a sample is reached over this long (one sample interval)
constexpr double kForgetAfter  = 4.0;      // a villager no sample mentions for this long goes
constexpr size_t kMaxSent      = 400;      // villagers we send (a bigger town: the first ones)
constexpr size_t kMaxShown     = 400;      // the neighbour's we draw
constexpr size_t kMaxListChars = 12000;    // one message's list (the relay takes 16 KB frames)
constexpr float  kOpacity      = 0.6f;     // see-through, a little more solid than the buildings

bool s_on = false;
Clock::time_point s_lastSend;
Clock::time_point s_lastTick;
bool s_ticked = false;

// Ours: a small id per citizen while it lives (a pointer seen again keeps its id).
std::unordered_map<const CitizenComponent*, uint32_t> s_ids;
uint32_t s_nextId = 1;

// Theirs.
struct Ghost {
    Graphics::MeshInstance*   mesh = nullptr;
    uint32_t                  sceneId = 0;
    bool                      inScene = false;
    Graphics::AnimationGroup* group = nullptr;
    int                       clip = -1;
    Math::Vector3             from, to;
    float                     yawFrom = 0.0f, yawTo = 0.0f;
    Clock::time_point         start;     // the glide from `from` to `to` began
    Clock::time_point         seen;      // the last sample that named it
};
std::map<uint32_t, Ghost> s_ghosts;
size_t s_shownLogged = 0;   // the count last written to the log

std::string PathOf(const ::Object::External* x) {
    if (x == nullptr) return std::string();
    const System::Filename p = x->GetPath();
    return p.AsString()._data != nullptr ? std::string(p.AsString()._data) : std::string();
}

double Seconds(Clock::duration d) { return std::chrono::duration<double>(d).count(); }

// ---- ours -------------------------------------------------------------------------------------

void SendOurs(Link* link) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    CitizenBehavior* tm = g != nullptr ? g->m_citizenList : nullptr;
    if (tm == nullptr) return;
    std::unordered_map<const CitizenComponent*, uint32_t> ids;
    std::map<std::string, std::string> lists;   // template path -> "id,x,y,z,yaw,clip,grp,scale,cloth;..."
    size_t sent = 0;
    for (int i = 0; i < tm->citizens._size && sent < kMaxSent; ++i) {
        CitizenComponent* c = tm->citizens._data[i].Get();
        if (c == nullptr || c->_entity.Get() == nullptr || ((c->_entity->_entityFlags >> 10) & 1)) continue;
        CharacterComponent* ch = c->character.Get();
        ModelComponent* mc = ch != nullptr ? ch->modelComponent.Get() : nullptr;
        if (mc == nullptr || mc->modelId < 0) continue;   // not in the scene
        const std::string tmpl = PathOf(c->_entity->_description);
        if (tmpl.empty()) continue;
        auto known = s_ids.find(c);
        const uint32_t id = known != s_ids.end() ? known->second : s_nextId++;
        ids[c] = id;
        const Graphics::MeshInstance& mi = mc->meshInstance;
        const int clip = mi._animation._layers._size > 0 ? mi._animation._layers._data[0]._clipIndex : 0;
        const Math::Vector3& p = mc->transform.translation;
        std::string& out = lists[tmpl];
        if (out.size() > kMaxListChars) continue;
        if (!out.empty()) out += ';';
        out += std::to_string(id) + ',' + std::to_string(std::lround(p.x * 100.0f)) + ',' +
               std::to_string(std::lround(p.y * 100.0f)) + ',' + std::to_string(std::lround(p.z * 100.0f)) + ',' +
               std::to_string(std::lround(mc->yaw * 10.0f)) + ',' + std::to_string(clip) + ',' +
               std::to_string(mc->meshGroup) + ',' + std::to_string(std::lround(mi._color[0] * 100.0f)) + ',' +
               std::to_string(std::lround(mi._color[3]));
        ++sent;
    }
    s_ids.swap(ids);
    for (const auto& kv : lists) {
        Message m("walkers");
        m.Set("tmpl", kv.first).Set("list", kv.second);
        link->Send(m);
    }
}

// ---- theirs -----------------------------------------------------------------------------------

void FreeGhost(Ghost& gh, bool sceneAlive) {
    if (gh.inScene && sceneAlive) {
        GameUpdateLoop* g = GameUpdateLoop::_singleton;
        if (g != nullptr && g->m_graphicsScene != nullptr) g->m_graphicsScene->RemoveObject(gh.sceneId);
    }
    gh.inScene = false;
    if (gh.mesh != nullptr) {
        if (sceneAlive) gh.mesh->_animation.ResetAnimationState();
        gh.mesh->~MeshInstance();
        System::MemoryAllocator::MemFree(gh.mesh);
        gh.mesh = nullptr;
    }
}

void FreeAll(bool sceneAlive) {
    for (auto& kv : s_ghosts) FreeGhost(kv.second, sceneAlive);
    s_ghosts.clear();
}

// The clip on a layer of its own (not instanced: the pool moves with this town's clock); an index the
// group does not have is the standing clip, 0.
void PlayClip(Ghost& gh, int clip) {
    if (gh.group == nullptr || gh.mesh == nullptr || gh.mesh->GetMesh().Get() == nullptr) return;
    if (clip < 0 || clip >= gh.group->_clips._size) clip = 0;
    if (clip == gh.clip) return;
    const System::Time t0{0};
    gh.mesh->_animation.PlayAnimation(Object::ExternalPtr<Graphics::AnimationGroup>(gh.group), clip,
                                      gh.mesh->GetMesh()->GetNodes(), t0, t0, 1.0f, false);
    gh.clip = clip;
}

// A new villager of the neighbour's, standing where its first sample says.
bool MakeGhost(Ghost& gh, const ModelDescription* md, int grp, const Math::Vector3& pos, float yaw, float scale,
               float cloth, int clip) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (g == nullptr || g->m_graphicsScene == nullptr) return false;
    const int group = (grp >= 0 && grp < md->_meshGroupCount) ? grp : 0;
    const ModelMeshGroup& mg = md->_meshGroups[group];
    if (mg.meshes == nullptr || mg.meshCount <= 0 || mg.meshes[0] == nullptr) return false;
    void* mem = System::MemoryAllocator::MemAlloc(0x120, 8);
    if (mem == nullptr) return false;
    gh.mesh = ::new (mem) Graphics::MeshInstance();
    gh.mesh->SetMesh(Object::ExternalPtr<Graphics::GraphicsMesh>(mg.meshes[0]));
    gh.mesh->SetTransform(Math::Transform::FromYawPitchOffset(yaw, 0.0f, pos));
    gh.mesh->_color[0] = scale;
    gh.mesh->_color[1] = scale;
    gh.mesh->_color[2] = scale;
    gh.mesh->_color[3] = cloth;
    gh.mesh->_opacity = kOpacity;
    gh.mesh->_opacityPrev = kOpacity;
    gh.group = mg.field_0x00;
    gh.clip = -1;
    PlayClip(gh, clip);
    gh.mesh->Update(System::Time{0});
    gh.sceneId = g->m_graphicsScene->AddObject(gh.mesh);
    gh.inScene = true;
    gh.from = pos;
    gh.to = pos;
    gh.yawFrom = yaw;
    gh.yawTo = yaw;
    return true;
}

float LerpYaw(float a, float b, float f) {
    float d = std::fmod(b - a, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return a + d * f;
}

Math::Vector3 Lerp(const Math::Vector3& a, const Math::Vector3& b, float f) {
    return Math::Vector3(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f);
}

void MoveTheirs(double dt) {
    const Clock::time_point now = Clock::now();
    const bool rootOnly = Graphics::MeshInstance::_rootOnlyUpdates;   // a sim iteration's setting
    Graphics::MeshInstance::_rootOnlyUpdates = false;
    const System::Time step{static_cast<int64_t>(dt * static_cast<double>(System::Timer::_ticksPerSecond))};
    for (auto it = s_ghosts.begin(); it != s_ghosts.end();) {
        Ghost& gh = it->second;
        if (Seconds(now - gh.seen) > kForgetAfter) {
            FreeGhost(gh, true);
            it = s_ghosts.erase(it);
            continue;
        }
        float f = static_cast<float>(Seconds(now - gh.start) / kGlide);
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        // Body first, then the pose: the bones are computed from the current transform.
        gh.mesh->SetTransform(Math::Transform::FromYawPitchOffset(LerpYaw(gh.yawFrom, gh.yawTo, f), 0.0f,
                                                                  Lerp(gh.from, gh.to, f)));
        gh.mesh->Update(step);
        ++it;
    }
    Graphics::MeshInstance::_rootOnlyUpdates = rootOnly;
}

// "id,x,y,z,yaw,clip,grp,scale,cloth": false when it is not nine integers.
bool ParseEntry(const std::string& s, int64_t v[9]) {
    size_t at = 0;
    for (int k = 0; k < 9; ++k) {
        const size_t comma = s.find(',', at);
        const std::string field = s.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        if (field.empty() || field.size() > 12) return false;
        char* end = nullptr;
        v[k] = std::strtoll(field.c_str(), &end, 10);
        if (end == nullptr || *end != '\0') return false;
        if (comma == std::string::npos) return k == 8;
        at = comma + 1;
    }
    return false;
}

}  // namespace

void Begin(bool on) {
    End();
    s_on = on;
}

void End() {
    FreeAll(false);
    s_ids.clear();
    s_on = false;
    s_ticked = false;
}

void Clear() {
    FreeAll(true);
    s_shownLogged = 0;
}

void Tick(Link* link, bool peerOnline, bool paused) {
    if (!s_on) return;
    const Clock::time_point now = Clock::now();
    double dt = s_ticked ? Seconds(now - s_lastTick) : 0.0;
    s_lastTick = now;
    s_ticked = true;
    if (dt > 0.25) dt = 0.25;   // a long frame (a load, a hitch): no jump in the walk cycle
    if (link != nullptr && peerOnline && link->Connected() && Seconds(now - s_lastSend) >= kSendEvery) {
        s_lastSend = now;
        SendOurs(link);
    }
    MoveTheirs(paused ? 0.0 : dt);
}

bool OnMessage(const Message& m) {
    if (m.type != "walkers") return false;
    if (!s_on) return true;
    std::string tmpl, list;
    if (!m.Get("tmpl", tmpl) || !m.Get("list", list) || tmpl.empty() || tmpl.size() > 160) return true;
    ::Object::ExternalFactory* f = ::Object::ExternalFactory::_singleton;
    if (f == nullptr) return true;
    ComponentDescription* desc = f->FindOrLoadAs<ComponentDescription>(System::Filename(tmpl.c_str()));
    if (!::Object::IsDerivedFromNullSafe(desc, ComponentDescription::TypeId())) return true;
    const ModelDescription* md = desc->FindDescription<ModelDescription>();
    if (md == nullptr || md->_meshGroups == nullptr || md->_meshGroupCount <= 0) return true;
    const Clock::time_point now = Clock::now();
    size_t at = 0;
    while (at < list.size()) {
        const size_t semi = list.find(';', at);
        const std::string entry = list.substr(at, semi == std::string::npos ? std::string::npos : semi - at);
        at = semi == std::string::npos ? list.size() : semi + 1;
        int64_t v[9] = {};
        if (!ParseEntry(entry, v) || v[0] <= 0) continue;
        const uint32_t id = static_cast<uint32_t>(v[0]);
        const Math::Vector3 pos(static_cast<float>(v[1]) / 100.0f, static_cast<float>(v[2]) / 100.0f,
                                static_cast<float>(v[3]) / 100.0f);
        const float yaw = static_cast<float>(v[4]) / 10.0f;
        float scale = static_cast<float>(v[7]) / 100.0f;
        if (scale < 0.3f || scale > 2.0f) scale = 1.0f;
        const float cloth = static_cast<float>(v[8] < 0 ? 0 : (v[8] > 64 ? 64 : v[8]));
        auto it = s_ghosts.find(id);
        if (it == s_ghosts.end()) {
            if (s_ghosts.size() >= kMaxShown) continue;
            Ghost gh;
            if (!MakeGhost(gh, md, static_cast<int>(v[6]), pos, yaw, scale, cloth, static_cast<int>(v[5]))) continue;
            gh.start = now;
            gh.seen = now;
            s_ghosts[id] = gh;
            continue;
        }
        Ghost& gh = it->second;
        // Glide on from where it is drawn now to the new sample.
        float t = static_cast<float>(Seconds(now - gh.start) / kGlide);
        if (t > 1.0f) t = 1.0f;
        if (t < 0.0f) t = 0.0f;
        gh.from = Lerp(gh.from, gh.to, t);
        gh.yawFrom = LerpYaw(gh.yawFrom, gh.yawTo, t);
        gh.to = pos;
        gh.yawTo = yaw;
        gh.start = now;
        gh.seen = now;
        gh.mesh->_color[0] = scale;
        gh.mesh->_color[1] = scale;
        gh.mesh->_color[2] = scale;
        gh.mesh->_color[3] = cloth;
        PlayClip(gh, static_cast<int>(v[5]));
    }
    if (s_ghosts.size() != s_shownLogged) {
        s_shownLogged = s_ghosts.size();
        fprintf(stderr, "[Neighbour] villagers: %zu of the neighbour's shown\n", s_shownLogged);
    }
    return true;
}

}  // namespace Walkers
}  // namespace Neighbour
