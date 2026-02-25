#pragma once
#include "store.h"

// Parse a space-separated command string into tokens
std::vector<std::string> parse(const std::string& line);

// Handle a single client connection — runs in its own thread
void handle_client(int client_fd, KeyValueStore& store);

// Create, bind, and listen on a port — returns server_fd
int setup_server(int port);
