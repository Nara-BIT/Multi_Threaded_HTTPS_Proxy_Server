#include "lru_cache.h"

LRUCache::LRUCache(size_t capacity) : capacity(capacity) {}

std::optional<std::string> LRUCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx);

    auto it = cache_map.find(key);
    if (it == cache_map.end()) {
        ++miss_count;
        return std::nullopt;
    }

    // Move accessed node to front (most-recently-used position)
    // std::list::splice is O(1) — no data copy, just pointer rewiring
    cache_list.splice(cache_list.begin(), cache_list, it->second);
    ++hit_count;
    return it->second->second;  // Return the cached value
}

void LRUCache::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mtx);

    auto it = cache_map.find(key);
    if (it != cache_map.end()) {
        // Key exists: update value and move to front
        it->second->second = value;
        cache_list.splice(cache_list.begin(), cache_list, it->second);
        return;
    }

    // Evict LRU entry if at capacity
    if (cache_list.size() >= capacity) {
        const std::string& lru_key = cache_list.back().first;
        cache_map.erase(lru_key);
        cache_list.pop_back();
    }

    // Insert new entry at front (MRU position)
    cache_list.emplace_front(key, value);
    cache_map[key] = cache_list.begin();
}

size_t LRUCache::size() const {
    std::lock_guard<std::mutex> lock(mtx);
    return cache_list.size();
}
