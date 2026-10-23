#pragma once
// Pure C++ (no Metal types): the pipeline "fast cache" of the Metal backend, keyed by the RAW addresses of the
// objects a draw binds (vertex/pixel function, vertex layout, blend descriptor). A freed object's address can be
// handed to a new one, so every entry remembers the addresses in its key and Evict(address) drops all of them;
// the Video::*Data destructors that own those objects call it. Thread safe: destructors run on loader/main threads
// while the render thread looks entries up.
#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
namespace VideoMetal {
template <class Value>
class FastPipelineCache {
public:
    bool Find(const std::string& key, Value& out) const {
        std::lock_guard<std::mutex> guard(mutex_);
        auto it = entries_.find(key);
        if (it == entries_.end()) {
            return false;
        }
        out = it->second.value;
        return true;
    }
    // owners: the addresses baked into `key` (null ones are ignored).
    void Insert(const std::string& key, Value value, const std::vector<const void*>& owners) {
        std::lock_guard<std::mutex> guard(mutex_);
        Remove(key);
        Entry e;
        e.value = std::move(value);
        for (auto o : owners) {
            if (o && std::find(e.owners.begin(), e.owners.end(), o) == e.owners.end()) {
                e.owners.push_back(o);
            }
        }
        for (auto o : e.owners) {
            byOwner_.emplace(o, key);
        }
        entries_.emplace(key, std::move(e));
    }
    // Drops every entry whose key contains `owner`; returns how many.
    size_t Evict(const void* owner) {
        if (!owner) {
            return 0;
        }
        std::lock_guard<std::mutex> guard(mutex_);
        std::vector<std::string> doomed;
        auto range = byOwner_.equal_range(owner);
        for (auto it = range.first; it != range.second; ++it) {
            doomed.push_back(it->second);
        }
        for (auto& k : doomed) {
            Remove(k);
        }
        return doomed.size();
    }
    void Clear() {
        std::lock_guard<std::mutex> guard(mutex_);
        entries_.clear();
        byOwner_.clear();
    }
    size_t Size() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return entries_.size();
    }

private:
    struct Entry {
        Value value{};
        std::vector<const void*> owners;
    };
    void Remove(const std::string& key) {
        auto it = entries_.find(key);
        if (it == entries_.end()) {
            return;
        }
        for (auto o : it->second.owners) {
            auto range = byOwner_.equal_range(o);
            for (auto i = range.first; i != range.second; ++i) {
                if (i->second == key) {
                    byOwner_.erase(i);
                    break;
                }
            }
        }
        entries_.erase(it);
    }
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
    std::unordered_multimap<const void*, std::string> byOwner_;
};
}  // namespace VideoMetal
