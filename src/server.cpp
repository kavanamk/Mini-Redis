#include "server.h"
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

std::vector<std::string> parse(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream stream(line);
    std::string token;
    while (stream >> token) tokens.push_back(token);
    return tokens;
}

void handle_client(int client_fd, KeyValueStore& store) {
    std::cout << "Client connected on fd=" << client_fd << "\n";
    char buf[1024];

    while (true) {
        memset(buf, 0, sizeof(buf));
        int bytes = recv(client_fd, buf, sizeof(buf) - 1, 0);

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
                store.set(tokens[1], tokens[2], std::stoi(tokens[3]));
                response = "OK\n";
            } else {
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

int setup_server(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "socket() failed\n";
        exit(1);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

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
