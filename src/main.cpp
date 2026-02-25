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

#include "store.h"
#include "server.h"

int main() {
    KeyValueStore store(10);

    int server_fd = setup_server(6379);
    std::cout << "Server listening on port 6379\n";
    std::cout << "Connect with: nc localhost 6379\n";
    std::cout << "Commands: SET key value [ttl]  |  GET key  |  DEL key  |  EXISTS key\n\n";

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            std::cerr << "accept() failed\n";
            continue;
        }
        std::thread(handle_client, client_fd, std::ref(store)).detach();
    }

    close(server_fd);
    return 0;
}
