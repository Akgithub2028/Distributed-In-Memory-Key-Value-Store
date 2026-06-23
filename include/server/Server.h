#pragma once

#include <string>
#include <atomic>
#include "net/Connection.h"
#include "storage/StorageEngine.h"
#include "storage/AOFLogger.h"

namespace kvstore {
namespace server {

class Server {
public:
    Server(uint16_t port, std::string replica_host = "", uint16_t replica_port = 0);
    ~Server();

    void start();
    void stop();

private:
    void handle_client(net::Connection conn);
    void run_replica_worker();

    uint16_t port_;
    int server_fd_{-1};
    std::atomic<bool> running_{false};
    storage::StorageEngine storage_engine_;
    storage::AOFLogger aof_logger_{"appendonly.aof"};
    
    // Replication configuration
    std::string replica_host_;
    uint16_t replica_port_;
    bool is_replica_;
};

} // namespace server
} // namespace kvstore
