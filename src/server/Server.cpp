#include "server/Server.h"
#include "server/CommandDispatcher.h"
#include "protocol/RESP.h"
#include "util/Logger.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include "server/Metrics.h"

namespace kvstore {
namespace server {

Server::Server(uint16_t port, std::string replica_host, uint16_t replica_port) 
    : port_(port), replica_host_(replica_host), replica_port_(replica_port) {
    is_replica_ = !replica_host_.empty() && replica_port_ > 0;
}

Server::~Server() {
    stop();
}

void Server::start() {
    running_ = true;
    
    // Load AOF before accepting any connections
    util::Logger::info("Loading AOF file from " + aof_logger_.get_filepath() + "...");
    std::ifstream aof_stream(aof_logger_.get_filepath(), std::ios::binary);
    if (aof_stream.is_open()) {
        std::stringstream buffer;
        buffer << aof_stream.rdbuf();
        std::string aof_content = buffer.str();
        
        size_t consumed = 0;
        int loaded_commands = 0;
        while (!aof_content.empty()) {
            auto req_opt = protocol::RESPParser::parse(aof_content, consumed);
            if (!req_opt) break; // Incomplete or corrupt end
            CommandDispatcher::dispatch(req_opt.value(), storage_engine_, nullptr, is_replica_, true); // no AOF logging on replay, bypass readonly
            aof_content.erase(0, consumed);
            loaded_commands++;
        }
        util::Logger::info("AOF replay complete. Loaded " + std::to_string(loaded_commands) + " commands.");
    } else {
        util::Logger::info("No AOF file found. Starting fresh.");
    }

    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ == -1) {
        util::Logger::error("Failed to create socket");
        return;
    }

    int opt = 1;
    if (::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        util::Logger::error("Failed to set SO_REUSEADDR");
        return;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (::bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
        util::Logger::error("Bind failed on port " + std::to_string(port_));
        return;
    }

    if (::listen(server_fd_, 128) < 0) {
        util::Logger::error("Listen failed");
        return;
    }

    if (is_replica_) {
        std::thread replica_thread(&Server::run_replica_worker, this);
        replica_thread.detach();
    }

    running_ = true;
    util::Logger::info("Server started, listening on port " + std::to_string(port_));

    while (running_) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) {
            if (running_) {
                util::Logger::error("Accept failed");
            }
            continue;
        }

        util::Logger::info("Accepted new connection, fd: " + std::to_string(client_fd));
        Metrics::connected_clients.fetch_add(1, std::memory_order_relaxed);
        
        std::thread client_thread([this, fd = client_fd]() {
            this->handle_client(net::Connection(fd));
            Metrics::connected_clients.fetch_sub(1, std::memory_order_relaxed);
        });
        client_thread.detach();
    }
}

void Server::stop() {
    running_ = false;
    if (server_fd_ != -1) {
        ::close(server_fd_);
        server_fd_ = -1;
    }
}

void Server::handle_client(net::Connection conn) {
    char read_buf[4096];
    std::string client_buffer;

    while (running_) {
        int bytes = conn.read_bytes(read_buf, sizeof(read_buf));
        if (bytes <= 0) {
            util::Logger::info("Client disconnected, fd: " + std::to_string(conn.fd()));
            break;
        }

        client_buffer.append(read_buf, bytes);

        size_t consumed = 0;
        while (true) {
            auto req_opt = protocol::RESPParser::parse(client_buffer, consumed);
            if (!req_opt) {
                // Incomplete frame, need more data
                break;
            }

            client_buffer.erase(0, consumed);

            // Dispatch command
            auto result = CommandDispatcher::dispatch(req_opt.value(), storage_engine_, &aof_logger_, is_replica_, false);

            // Serialize response and send
            std::string serialized_resp = result.response.serialize();
            if (!conn.write_bytes(serialized_resp)) {
                util::Logger::error("Failed to write to client fd: " + std::to_string(conn.fd()));
                return;
            }

            if (result.is_psync) {
                // Primary behavior: Stream AOF forever
                util::Logger::info("Starting PSYNC stream to replica at offset " + std::to_string(result.psync_offset));
                std::ifstream aof_stream(aof_logger_.get_filepath(), std::ios::binary);
                aof_stream.seekg(result.psync_offset);
                
                char stream_buf[4096];
                while (running_) {
                    aof_stream.read(stream_buf, sizeof(stream_buf));
                    size_t read_bytes = aof_stream.gcount();
                    if (read_bytes > 0) {
                        if (!conn.write_bytes(std::string(stream_buf, read_bytes))) {
                            util::Logger::info("Replica disconnected during PSYNC stream");
                            return;
                        }
                    } else {
                        aof_stream.clear(); // Clear EOF
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                }
                return;
            }

            if (result.close_connection) {
                util::Logger::info("Closing client connection gracefully, fd: " + std::to_string(conn.fd()));
                return;
            }
        }
    }
}

void Server::run_replica_worker() {
    util::Logger::info("Starting replication worker to " + replica_host_ + ":" + std::to_string(replica_port_));
    
    while (running_) {
        int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (client_fd < 0) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(replica_port_);
        if (inet_pton(AF_INET, replica_host_.c_str(), &serv_addr.sin_addr) <= 0) {
            ::close(client_fd);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        if (::connect(client_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            ::close(client_fd);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        
        net::Connection conn(client_fd);
        
        size_t current_offset = aof_logger_.get_file_size();
        std::string psync_cmd = "*2\r\n$5\r\nPSYNC\r\n$" + std::to_string(std::to_string(current_offset).size()) + "\r\n" + std::to_string(current_offset) + "\r\n";
        
        if (!conn.write_bytes(psync_cmd)) {
            continue;
        }
        
        char read_buf[4096];
        std::string client_buffer;
        bool in_stream = false;
        
        while (running_) {
            int bytes = conn.read_bytes(read_buf, sizeof(read_buf));
            if (bytes <= 0) break; // Disconnected, outer loop will reconnect
            
            if (!in_stream) {
                client_buffer.append(read_buf, bytes);
                // Look for +CONTINUE\r\n
                size_t pos = client_buffer.find("\r\n");
                if (pos != std::string::npos) {
                    std::string reply = client_buffer.substr(0, pos);
                    if (reply == "+CONTINUE") {
                        in_stream = true;
                        util::Logger::info("PSYNC accepted by primary. Streaming started.");
                        std::string remainder = client_buffer.substr(pos + 2);
                        if (!remainder.empty()) {
                            aof_logger_.log_raw(remainder);
                            
                            size_t consumed = 0;
                            while (true) {
                                auto req_opt = protocol::RESPParser::parse(remainder, consumed);
                                if (!req_opt) break;
                                CommandDispatcher::dispatch(req_opt.value(), storage_engine_, nullptr, is_replica_, true);
                                remainder.erase(0, consumed);
                                consumed = 0;
                            }
                            client_buffer = remainder; // Keep incomplete frames
                        } else {
                            client_buffer.clear();
                        }
                    } else {
                        util::Logger::error("Failed PSYNC: " + reply);
                        break;
                    }
                }
            } else {
                std::string data(read_buf, bytes);
                aof_logger_.log_raw(data);
                
                client_buffer.append(data);
                size_t consumed = 0;
                while (true) {
                    auto req_opt = protocol::RESPParser::parse(client_buffer, consumed);
                    if (!req_opt) break;
                    CommandDispatcher::dispatch(req_opt.value(), storage_engine_, nullptr, is_replica_, true);
                    client_buffer.erase(0, consumed);
                    consumed = 0;
                }
            }
        }
        util::Logger::info("Replica connection lost. Reconnecting in 1 second...");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace server
} // namespace kvstore
