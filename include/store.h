#pragma once
#include <atomic>
#include <chrono>
#include <list>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>

using Clock     = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

struct Entry {
    std::string value;
    std::optional<TimePoint> expires_at;
    std::list<std::pair<std::string,std::string>>::iterator lru_pos;
};

class KeyValueStore {
private:
    int capacity;
    std::unordered_map<std::string, Entry> data;
    mutable std::shared_timed_mutex rw_mutex;
    std::atomic<bool> running{true};
    std::thread reaper_thread;
    std::list<std::pair<std::string,std::string>> lru_order;

    bool is_expired(const Entry& e) const;
    void move_to_front(const std::string& key);
    void evict_lru();
    void reaper_loop();

public:
    explicit KeyValueStore(int cap);
    ~KeyValueStore();

    void set(const std::string& key,
             const std::string& val,
             std::optional<int> ttl_seconds = std::nullopt);

    std::optional<std::string> get(const std::string& key);
    void del(const std::string& key);
    bool exists(const std::string& key);
};
