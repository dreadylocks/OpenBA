// ============================================================================
// src/ext/neighbour/town.cpp -- NOT RETAIL. ext.neighbour's parcels in the town (town.h)
// ============================================================================
// Our own code over the game's objects (the guards are ours to place: none of this is a
// faithful body). Main thread only.
// ============================================================================

#include "ext/neighbour/town.h"

#include <algorithm>
#include <map>
#include <new>

#include "Component/CharacterComponent.h"
#include "Component/CitizenComponent.h"
#include "Component/CitizenDescription.h"
#include "Component/ComponentDescription.h"
#include "Component/ModelComponent.h"
#include "Component/RawMaterialDescription.h"
#include "Component/ResidenceComponent.h"
#include "Component/StorageComponent.h"
#include "Entity/CitizenBehavior.h"
#include "Entity/ComponentEntity.h"
#include "Entity/EntityList.h"
#include "Game/GameUpdateLoop.h"
#include "Game/ProfessionList.h"
#include "object/External.h"
#include "system/Filename.h"
#include "system/MemoryAllocator.h"
#include "system/String.h"
#include "ui/StringTable.h"

namespace Neighbour {
namespace Town {

namespace {

CitizenBehavior* TownMgr() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    return g != nullptr ? g->m_citizenList : nullptr;
}

const RawMaterialDescription* RawMaterial(const ComponentDescription* res) {
    return res != nullptr ? res->FindDescription<RawMaterialDescription>() : nullptr;
}

std::string PathOf(const ComponentDescription* res) {
    const System::Filename p = res->GetPath();
    const char* s = p.AsString()._data;
    return s != nullptr ? s : std::string();
}

// Every StorageComponent of the town (townMgr +0x00, the storage entities).
template <class Fn>
void ForEachStorage(Fn fn) {
    CitizenBehavior* tm = TownMgr();
    if (tm == nullptr) return;
    for (int i = 0; i < tm->field_0x00._size; ++i) {
        ComponentEntity* e = tm->field_0x00._data[i].Get();
        if (e == nullptr) continue;
        StorageComponent* st = e->FindComponent<StorageComponent>();
        if (st != nullptr) fn(*st);
    }
}

bool IsChild(const CitizenComponent* c) {
    return c->description != nullptr && c->age < c->description->adultAge;
}

// A house's family, its citizens adults first.
struct FamilyRef {
    std::vector<CitizenComponent*> members;
};

// Every non-empty family of the town, in house order.
std::vector<FamilyRef> Families() {
    std::vector<FamilyRef> out;
    CitizenBehavior* tm = TownMgr();
    if (tm == nullptr) return out;
    for (int h = 0; h < tm->field_0x20._size; ++h) {
        ComponentEntity* house = tm->field_0x20._data[h].Get();
        ResidenceComponent* rc = house != nullptr ? house->FindComponent<ResidenceComponent>() : nullptr;
        if (rc == nullptr) continue;
        for (int f = 0; f < rc->residentSlots._size; ++f) {
            const ResidenceComponent::Family& fam = rc->residentSlots._data[f];
            FamilyRef ref;
            for (int m = 0; m < fam._size; ++m) {
                CitizenComponent* c = fam._data[m].Get();
                if (c != nullptr && c->_entity.Get() != nullptr) ref.members.push_back(c);
            }
            if (ref.members.empty()) continue;
            std::stable_sort(ref.members.begin(), ref.members.end(),
                             [](const CitizenComponent* a, const CitizenComponent* b) { return !IsChild(a) && IsChild(b); });
            out.push_back(std::move(ref));
        }
    }
    return out;
}

// The family that would go: the smallest one that has an adult (a small household leaves the
// least behind), the last such in house order on a tie.
const FamilyRef* PickFamily(const std::vector<FamilyRef>& fams) {
    if (fams.size() < 2) return nullptr;   // the town keeps at least one family
    const FamilyRef* best = nullptr;
    for (const FamilyRef& f : fams) {
        bool adult = false;
        for (const CitizenComponent* c : f.members) adult |= !IsChild(c);
        if (!adult || static_cast<int>(f.members.size()) > kMaxFamily) continue;
        if (best == nullptr || f.members.size() <= best->members.size()) best = &f;
    }
    return best;
}

std::vector<Member> Describe(const FamilyRef& f) {
    std::vector<Member> out;
    for (const CitizenComponent* c : f.members) {
        Member m;
        m.age = c->age;
        m.gender = c->gender ? 1 : 0;
        m.child = IsChild(c);
        out.push_back(m);
    }
    return out;
}

}  // namespace

bool InGame() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    return g != nullptr && g->m_citizenList != nullptr && g->m_entityList != nullptr;
}

std::vector<Stock> ListGoods() {
    std::map<const ComponentDescription*, int> sums;
    ForEachStorage([&](StorageComponent& st) {
        for (int i = 0; i < st.storageSlots._size; ++i) {
            const StorageComponent::Item& it = st.storageSlots._data[i];
            if (it._resource == nullptr || RawMaterial(it._resource) == nullptr) continue;
            const int avail = it._count - it._reserve;
            if (avail > 0) sums[it._resource] += avail;
        }
    });
    std::vector<Stock> out;
    for (const auto& kv : sums) {
        Stock s;
        s.resource = const_cast<ComponentDescription*>(kv.first);
        s.path = PathOf(kv.first);
        s.available = kv.second;
        if (!s.path.empty()) out.push_back(std::move(s));
    }
    std::sort(out.begin(), out.end(), [](const Stock& a, const Stock& b) { return a.path < b.path; });
    return out;
}

std::string DisplayName(const ComponentDescription* res) {
    const RawMaterialDescription* raw = RawMaterial(res);
    if (raw != nullptr && raw->_stringTable != nullptr && raw->_name._data != nullptr) {
        const System::String text = raw->_stringTable->GetText(raw->_name);
        if (text._data != nullptr && text._data[0] != '\0') return text._data;
    }
    if (res == nullptr) return "?";
    std::string path = PathOf(res);
    const size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) path = path.substr(slash + 1);
    const size_t dot = path.find('.');
    if (dot != std::string::npos) path = path.substr(0, dot);
    return path;
}

int TakeGoods(const ComponentDescription* res, int count) {
    if (res == nullptr || count <= 0) return 0;
    int taken = 0;
    ForEachStorage([&](StorageComponent& st) {
        if (taken >= count) return;
        const int avail = st.QueryAvailable(res);
        const int take = std::min(avail, count - taken);
        if (take > 0) {
            st.SubtractItem(res, take);
            taken += take;
        }
    });
    return taken;
}

int GiveGoods(const std::string& path, int count) {
    if (count <= 0 || !InGame()) return 0;
    ::Object::ExternalFactory* f = ::Object::ExternalFactory::_singleton;
    if (f == nullptr) return -1;
    ComponentDescription* res = f->FindOrLoadAs<ComponentDescription>(System::Filename(path.c_str()));
    if (!::Object::IsDerivedFromNullSafe(res, ComponentDescription::TypeId()) || RawMaterial(res) == nullptr)
        return -1;
    int given = 0;
    ForEachStorage([&](StorageComponent& st) {
        if (given >= count || !st.AcceptsMaterial(res)) return;
        // The most that fits here: CanAdd is monotonic in the count, so search for it.
        int lo = 0, hi = count - given;
        while (lo < hi) {
            const int mid = lo + (hi - lo + 1) / 2;
            if (st.CanAdd(res, mid)) lo = mid; else hi = mid - 1;
        }
        if (lo > 0) {
            st.AddItem(res, lo);
            given += lo;
        }
    });
    if (given > 0) TownMgr()->RegisterRawMaterial(res);
    return given;
}

std::vector<Member> PreviewFamily(int* familyCount) {
    const std::vector<FamilyRef> fams = Families();
    if (familyCount != nullptr) *familyCount = static_cast<int>(fams.size());
    const FamilyRef* pick = PickFamily(fams);
    return pick != nullptr ? Describe(*pick) : std::vector<Member>();
}

std::vector<Member> SendFamily() {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    if (!InGame()) return {};
    const std::vector<FamilyRef> fams = Families();
    const FamilyRef* pick = PickFamily(fams);
    if (pick == nullptr) return {};
    const std::vector<Member> members = Describe(*pick);
    // Children first: a child whose last parent is removed before it is buried, not moved.
    for (int pass = 0; pass < 2; ++pass) {
        for (CitizenComponent* c : pick->members) {
            if (IsChild(c) != (pass == 0)) continue;
            ComponentEntity* e = c->_entity.Get();
            if (e != nullptr) g->m_entityList->QueueDestroy(&e);
        }
    }
    return members;
}

int ReceiveFamily(const std::vector<Member>& members) {
    GameUpdateLoop* g = GameUpdateLoop::_singleton;
    CitizenBehavior* tm = TownMgr();
    if (!InGame() || tm->citizens._size == 0 || members.empty()) return 0;

    // A townsperson: its template (as a birth copies the mother's) and where it stands.
    CitizenComponent* model = nullptr;
    Math::Vector3 pos(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < tm->citizens._size && model == nullptr; ++i) {
        CitizenComponent* c = tm->citizens._data[i].Get();
        if (c == nullptr || c->_entity.Get() == nullptr || c->_entity->_description == nullptr) continue;
        CharacterComponent* ch = c->character.Get();
        ModelComponent* mc = ch != nullptr ? ch->modelComponent.Get() : nullptr;
        if (mc == nullptr) continue;
        pos = mc->transform.translation;
        model = c;
    }
    if (model == nullptr) return 0;
    ComponentDescription* tmpl = model->_entity->_description;
    Profession* prof = g->m_professionList != nullptr ? g->m_professionList->field_0x30 : nullptr;

    CitizenComponent* firstAdult = nullptr;
    CitizenComponent* secondAdult = nullptr;
    int made = 0;
    for (const Member& m : members) {
        void* mem = System::MemoryAllocator::MemAlloc(0x48, 8);
        if (mem == nullptr) break;
        ComponentEntity* ent = ::new (mem) ComponentEntity(tmpl);
        CitizenComponent* cit = ent->FindComponent<CitizenComponent>();
        if (cit == nullptr) break;   // never for a citizen's template
        CitizenComponent* parent = m.child ? (secondAdult != nullptr ? secondAdult : firstAdult) : nullptr;
        cit->Initialize(&pos, static_cast<uint32_t>(m.gender), m.child ? '\x01' : '\0', m.age, '\0', parent, prof);
        Object::InternalPtr<ComponentEntity> reg = ent;
        g->m_entityList->RegisterEntity(&reg, tmpl->_category);
        if (!m.child) {
            if (firstAdult == nullptr) firstAdult = cit;
            else if (secondAdult == nullptr) {
                secondAdult = cit;
                firstAdult->spouse = secondAdult;   // as ImmigrationComponent::Accept pairs them
                secondAdult->spouse = firstAdult;
            }
        }
        ++made;
    }
    return made;
}

}  // namespace Town
}  // namespace Neighbour
