#include "store.h"
#include <iostream>

bool KeyValueStore::is_expired(const Entry& e) const {
    if (!e.expires_at) return false;
    return Clock::now() > *e.expires_at;
}

void KeyValueStore::move_to_front(const std::string& key) {
    lru_order.splice(lru_order.begin(), lru_order, data[key].lru_pos);
}

void KeyValueStore::evict_lru() {
    auto lru = lru_order.back();
    std::cout << "[evict] " << lru.first << "\n";
    data.erase(lru.first);
    lru_order.pop_back();
}

void KeyValueStore::reaper_loop() {
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::unique_lock<std::shared_timed_mutex> lock(rw_mutex);
        for (auto it = data.begin(); it != data.end(); ) {
            if (is_expired(it->second)) {
                lru_order.erase(it->second.lru_pos);
                it = data.erase(it);
            } else {
                ++it;
            }
        }
    }
}

KeyValueStore::KeyValueStore(int cap) : capacity(cap) {
    reaper_thread = std::thread(&KeyValueStore::reaper_loop, this);
}

KeyValueStore::~KeyValueStore() {
    running = false;
    reaper_thread.join();
}

void KeyValueStore::set(const std::string& key,
                        const std::string& val,
                        std::optional<int> ttl_seconds) {

    std::unique_lock<std::shared_timed_mutex> lock(rw_mutex);

    std::optional<TimePoint> expires_at = std::nullopt;
    if (ttl_seconds.has_value())
        expires_at = Clock::now() + std::chrono::seconds(*ttl_seconds);

    if (data.count(key)) {
        data[key].value      = val;
        data[key].expires_at = expires_at;
        move_to_front(key);
        return;
    }

    if ((int)data.size() >= capacity)
        evict_lru();

    lru_order.push_front({key, val});
    Entry e;
    e.value      = val;
    e.expires_at = expires_at;
    e.lru_pos    = lru_order.begin();
    data[key]    = e;
}

std::optional<std::string> KeyValueStore::get(const std::string& key) {
    std::shared_lock<std::shared_timed_mutex> lock(rw_mutex);
    auto it = data.find(key);
    if (it == data.end())       return std::nullopt;
    if (is_expired(it->second)) return std::nullopt;
    move_to_front(key);
    return it->second.value;
}

void KeyValueStore::del(const std::string& key) {
    std::unique_lock<std::shared_timed_mutex> lock(rw_mutex);
    auto it = data.find(key);
    if (it == data.end()) return;
    lru_order.erase(it->second.lru_pos);
    data.erase(it);
}

bool KeyValueStore::exists(const std::string& key) {
    std::shared_lock<std::shared_timed_mutex> lock(rw_mutex);
    auto it = data.find(key);
    if (it == data.end())       return false;
    if (is_expired(it->second)) return false;
    return true;
}
