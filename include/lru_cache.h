#pragma once
#include <string>
#include <list>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <cstddef>

/*
 * LRUCache
 * --------
 * O(1) get/put using doubly-linked list + unordered_map.
 *
 * Data structure:
 *   list<pair<key, value>>          ← MRU at front, LRU at back
 *   unordered_map<key, list::iter>  ← O(1) lookup to list node
 *
 * On GET hit  → splice node to front (O(1) because we have the iterator)
 * On PUT miss → push to front; if full, pop back and erase from map
 * On PUT hit  → update value, splice to front
 *
 * Thread safety: single mutex guards both map and list.
 * Interview: "Why not per-key locking?" → list splice needs global order;
 * fine-grained locking buys little for a cache that's mostly I/O-bound.
 */
class LRUCache {
public:
    explicit LRUCache(size_t capacity);

    std::optional<std::string> get(const std::string& key);
    void put(const std::string& key, const std::string& value);

    // Stats — safe to call without lock (atomic reads are fine for logging)
    size_t size()   const;
    size_t hits()   const { return hit_count; }
    size_t misses() const { return miss_count; }

private:
    using Entry     = std::pair<std::string, std::string>;
    using ListIter  = std::list<Entry>::iterator;

    size_t capacity;
    std::list<Entry> cache_list;  // MRU → LRU
    std::unordered_map<std::string, ListIter> cache_map;
    mutable std::mutex mtx;
    size_t hit_count{0};
    size_t miss_count{0};
};
