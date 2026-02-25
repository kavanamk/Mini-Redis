#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <list>
#include <netinet/in.h>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

using Clock     = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

struct Entry {
    std::string value;
    std::optional<TimePoint> expires_at;
    std::list<std::pair<std::string,std::string>>::iterator lru_pos;
};

// =============================================
// KeyValueStore — full from Lessons 1-4
// =============================================
class KeyValueStore {
private:
    int capacity;
    std::unordered_map<std::string, Entry> data;
    mutable std::shared_timed_mutex rw_mutex;
    std::atomic<bool> running{true};
    std::thread reaper_thread;
    std::list<std::pair<std::string,std::string>> lru_order;

    bool is_expired(const Entry& e) const {
        if (!e.expires_at) return false;
        return Clock::now() > *e.expires_at;
    }

    void move_to_front(const std::string& key) {
        lru_order.splice(lru_order.begin(), lru_order, data[key].lru_pos);
    }

    void evict_lru() {
        auto lru = lru_order.back();
        std::cout << "[evict] " << lru.first << "\n";
        data.erase(lru.first);
        lru_order.pop_back();
    }

    void reaper_loop() {
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

public:
    KeyValueStore(int cap) : capacity(cap) {
        reaper_thread = std::thread(&KeyValueStore::reaper_loop, this);
    }

    ~KeyValueStore() {
        running = false;
        reaper_thread.join();
    }

    void set(const std::string& key,
             const std::string& val,
             std::optional<int> ttl_seconds = std::nullopt) {

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

    std::optional<std::string> get(const std::string& key) {
        std::shared_lock<std::shared_timed_mutex> lock(rw_mutex);
        auto it = data.find(key);
        if (it == data.end())       return std::nullopt;
        if (is_expired(it->second)) return std::nullopt;
        move_to_front(key);
        return it->second.value;
    }

    void del(const std::string& key) {
        std::unique_lock<std::shared_timed_mutex> lock(rw_mutex);
        auto it = data.find(key);
        if (it == data.end()) return;
        lru_order.erase(it->second.lru_pos);
        data.erase(it);
    }

    bool exists(const std::string& key) {
        std::shared_lock<std::shared_timed_mutex> lock(rw_mutex);
        auto it = data.find(key);
        if (it == data.end())       return false;
        if (is_expired(it->second)) return false;
        return true;
    }
};

// =============================================
// TCP Server
// =============================================

// Split "SET name alice" → ["SET", "name", "alice"]
std::vector<std::string> parse(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream stream(line);
    std::string token;
    while (stream >> token) tokens.push_back(token);
    return tokens;
}

// Runs in its own thread — handles one client for its lifetime
void handle_client(int client_fd, KeyValueStore& store) {
    std::cout << "Client connected on fd=" << client_fd << "\n";

    char buf[1024];

    while (true) {
        memset(buf, 0, sizeof(buf));
        int bytes = recv(client_fd, buf, sizeof(buf) - 1, 0);

        // 0 = client closed connection, -1 = error
        if (bytes <= 0) {
            std::cout << "Client disconnected fd=" << client_fd << "\n";
            break;
        }

        std::string line(buf, bytes);
        auto tokens = parse(line);

        if (tokens.empty()) continue;

        std::string response;
        std::string cmd = tokens[0];

        if (cmd == "SET") {
            if (tokens.size() < 3) {
                response = "ERROR usage: SET key value [ttl]\n";
            } else if (tokens.size() == 4) {
                // SET key value ttl
                store.set(tokens[1], tokens[2], std::stoi(tokens[3]));
                response = "OK\n";
            } else {
                // SET key value
                store.set(tokens[1], tokens[2]);
                response = "OK\n";
            }

        } else if (cmd == "GET") {
            if (tokens.size() < 2) {
                response = "ERROR usage: GET key\n";
            } else {
                auto val = store.get(tokens[1]);
                response = val.has_value() ? *val + "\n" : "(nil)\n";
            }

        } else if (cmd == "DEL") {
            if (tokens.size() < 2) {
                response = "ERROR usage: DEL key\n";
            } else {
                store.del(tokens[1]);
                response = "OK\n";
            }

        } else if (cmd == "EXISTS") {
            if (tokens.size() < 2) {
                response = "ERROR usage: EXISTS key\n";
            } else {
                response = store.exists(tokens[1]) ? "1\n" : "0\n";
            }

        } else {
            response = "ERROR unknown command: " + cmd + "\n";
        }

        send(client_fd, response.c_str(), response.size(), 0);
    }

    close(client_fd);
}

// Create, bind, and listen on a port — returns server fd
int setup_server(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "socket() failed\n";
        exit(1);
    }

    // Allow port reuse immediately after server restart
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;   // accept from any IP
    addr.sin_port        = htons(port);  // convert to network byte order

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "bind() failed\n";
        exit(1);
    }

    if (listen(server_fd, 10) < 0) {
        std::cerr << "listen() failed\n";
        exit(1);
    }

    return server_fd;
}

int main() {
    KeyValueStore store(10);  // capacity of 10 keys

    int server_fd = setup_server(6379);  // Redis default port
    std::cout << "Server listening on port 6379\n";
    std::cout << "Connect with: nc localhost 6379\n";
    std::cout << "Commands: SET key value [ttl]  |  GET key  |  DEL key  |  EXISTS key\n\n";

    // Accept loop — runs forever
    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            std::cerr << "accept() failed\n";
            continue;
        }

        // Each client gets its own thread — detach so it cleans itself up
        std::thread(handle_client, client_fd, std::ref(store)).detach();
    }

    close(server_fd);
    return 0;
}
