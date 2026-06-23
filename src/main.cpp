#include "server/Server.h"
#include "util/Logger.h"
#include <csignal>
#include <string>

kvstore::server::Server* global_server = nullptr;

void signal_handler(int signal) {
    if (global_server) {
        kvstore::util::Logger::info("Received signal " + std::to_string(signal) + ", stopping server...");
        global_server->stop();
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    uint16_t port = 6379;
    std::string replica_host = "";
    uint16_t replica_port = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--replicaof" && i + 2 < argc) {
            replica_host = argv[++i];
            replica_port = std::stoi(argv[++i]);
        }
    }

    kvstore::util::Logger::info("Starting Redis-like KV Store (Phase 6)");
    kvstore::server::Server server(port, replica_host, replica_port);
    global_server = &server;
    
    server.start();

    return 0;
}
