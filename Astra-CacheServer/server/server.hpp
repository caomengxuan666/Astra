#pragma once

#include "logger.hpp"
#include <asio.hpp>
#include <boost/asio/io_context.hpp>
#include <concurrent/task_queue.hpp>
#include <datastructures/lru_cache.hpp>
#include <memory>
#include <proto/redis_command_handler.hpp>

namespace Astra::apps {

    using namespace Astra::proto;
    class Session : public std::enable_shared_from_this<Session> {
    public:
        explicit Session(asio::ip::tcp::socket socket,
                         std::shared_ptr<LRUCache<std::string, std::string>> cache)
            : socket_(std::move(socket)),
              handler_(std::make_shared<RedisCommandHandler>(cache)),
              task_queue_(std::make_shared<concurrent::TaskQueue>(std::thread::hardware_concurrency())) {}

        void Start() {
            ZEN_LOG_INFO("Client connected from: {}", socket_.remote_endpoint().address().to_string());
            if (!stopped_) {
                auto self = shared_from_this();
                task_queue_->Post([self]() {
                    if (!self->stopped_) {
                        self->DoRead();
                    }
                });
            }
        }

        void Stop() {
            stopped_ = true;

            asio::error_code ec;
            (void) socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
            (void) socket_.cancel(ec);// 取消所有异步操作
            (void) socket_.close(ec);
        }

    private:
        enum class ParseState {
            ReadingArrayHeader,
            ReadingBulkHeader,
            ReadingBulkContent
        };

        void DoRead() {
            if (stopped_) return;// 如果已关闭，直接返回

            auto self = shared_from_this();
            asio::async_read_until(socket_, asio::dynamic_buffer(buffer_), "\r\n",
                                   [this, self](std::error_code ec, std::size_t bytes_transferred) {
                                       if (ec) {
                                           if (ec != asio::error::eof) {
                                               ZEN_LOG_WARN("Read error: {}", ec.message());
                                           }
                                           return;
                                       }

                                       std::string data = buffer_.substr(0, bytes_transferred);
                                       ZEN_LOG_DEBUG("Received raw data: {}", data);

                                       if (!stopped_) {
                                           self->ProcessBuffer();

                                           if (!stopped_) {
                                               self->DoRead();// 直接调用而不是 Post
                                           }
                                       }
                                   });
        }

        void ProcessBuffer() {
            while (true) {
                size_t pos = buffer_.find("\r\n");
                if (pos == std::string::npos)
                    return;

                std::string line(buffer_.substr(0, pos));
                buffer_.erase(0, pos + 2);

                ZEN_LOG_DEBUG("Parsing line: {}", line);

                switch (parse_state_) {
                    case ParseState::ReadingArrayHeader:
                        HandleArrayHeader(line);
                        break;
                    case ParseState::ReadingBulkHeader:
                        HandleBulkHeader(line);
                        break;
                    case ParseState::ReadingBulkContent:
                        // Only invoked if we have all the content in buffer
                        if (buffer_.size() >= static_cast<size_t>(current_bulk_size_ + 2)) {
                            HandleBulkContent();
                        }
                        return;// wait for more content if not enough
                }

                if (parse_state_ == ParseState::ReadingBulkContent &&
                    buffer_.size() < static_cast<size_t>(current_bulk_size_ + 2)) {
                    ReadBulkContent();
                    return;
                }
            }
        }

        void HandleArrayHeader(const std::string &line) {
            if (line.empty() || line[0] != '*') {
                WriteResponse("-ERR Protocol error: expected array header\r\n");
                return;
            }

            try {
                remaining_args_ = std::stoi(line.substr(1));
                argv_.clear();
                ZEN_LOG_DEBUG("Array header parsed: {} arguments", remaining_args_);
                parse_state_ = ParseState::ReadingBulkHeader;
            } catch (...) {
                WriteResponse("-ERR Invalid argument count\r\n");
            }
        }

        void HandleBulkHeader(const std::string &line) {
            if (line.empty() || line[0] != '$') {
                WriteResponse("-ERR Protocol error: expected bulk header\r\n");
                return;
            }

            try {
                current_bulk_size_ = std::stoi(line.substr(1));
                ZEN_LOG_DEBUG("Bulk string size: {}", current_bulk_size_);
                parse_state_ = ParseState::ReadingBulkContent;
                if (buffer_.size() >= static_cast<size_t>(current_bulk_size_ + 2)) {
                    HandleBulkContent();
                } else {
                    ReadBulkContent();
                }
            } catch (...) {
                WriteResponse("-ERR Invalid bulk length\r\n");
            }
        }

        void ReadBulkContent() {
            auto self = shared_from_this();
            size_t need = current_bulk_size_ + 2 - buffer_.size();

            ZEN_LOG_DEBUG("Waiting for {} more bytes for bulk content", need);

            asio::async_read(socket_, asio::dynamic_buffer(buffer_),
                             asio::transfer_exactly(need),
                             [this, self](std::error_code ec, size_t bytes_read) {
                                 if (!ec) {
                                     ZEN_LOG_DEBUG("Read bulk content: {} bytes", bytes_read);
                                     HandleBulkContent();
                                 } else {
                                     ZEN_LOG_WARN("Error reading bulk content: {}", ec.message());
                                 }
                             });
        }

        void HandleBulkContent() {
            std::string content = buffer_.substr(0, current_bulk_size_);
            buffer_.erase(0, current_bulk_size_ + 2);
            argv_.push_back(content);
            ZEN_LOG_DEBUG("Bulk content parsed: {}", content);

            if (--remaining_args_ > 0) {
                parse_state_ = ParseState::ReadingBulkHeader;
            } else {
                ProcessRequest();
                parse_state_ = ParseState::ReadingArrayHeader;
            }
        }

        void ProcessRequest() {
            std::string full_cmd;
            for (const auto &arg: argv_) {
                full_cmd += (full_cmd.empty() ? "" : " ") + arg;
            }
            ZEN_LOG_INFO("Received command: {}", full_cmd);

            auto res = task_queue_->Submit([this, self = shared_from_this()]() {
                try {
                    std::string response = handler_->ProcessCommand(argv_);
                    asio::post(socket_.get_executor(), [this, self, response]() {
                        WriteResponse(response);
                    });
                } catch (const std::exception &e) {
                    ZEN_LOG_ERROR("Error processing request: {}", e.what());
                }
            });
        }

        void WriteResponse(const std::string &response) {
            auto self = shared_from_this();
            task_queue_->Post([this, self, response]() {
                // 使用异步写入
                asio::async_write(socket_, asio::buffer(response),
                                  [self, response](const boost::system::error_code &ec, std::size_t /*bytes_transferred*/) {
                                      if (ec) {
                                          ZEN_LOG_WARN("Failed to send response: {}", ec.message());
                                          //self->Stop();// 可选：写入失败时关闭连接
                                      } else {
                                          ZEN_LOG_INFO("Sent response: {}", response);
                                      }
                                  });
            });
        }

        bool stopped_ = false;// 控制状态机是否已关闭
        asio::ip::tcp::socket socket_;
        std::string buffer_;
        std::shared_ptr<Astra::proto::RedisCommandHandler> handler_;
        std::shared_ptr<concurrent::TaskQueue> task_queue_ =
                std::make_shared<concurrent::TaskQueue>(std::thread::hardware_concurrency());

        ParseState parse_state_ = ParseState::ReadingArrayHeader;
        int remaining_args_ = 0;
        int current_bulk_size_ = 0;
        std::vector<std::string> argv_;
    };

    class AstraCacheServer {
    public:
        explicit AstraCacheServer(asio::io_context &context, size_t cache_size)
            : context_(context),
              cache_(std::make_shared<LRUCache<std::string, std::string>>(cache_size)),
              acceptor_(context) {
            task_queue_ = std::make_shared<concurrent::TaskQueue>(std::thread::hardware_concurrency());
            cache_->StartEvictionTask(*task_queue_);
        }

        void Start(unsigned short port) {
            asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port);
            acceptor_.open(endpoint.protocol());
            acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
            acceptor_.bind(endpoint);
            acceptor_.listen();
            ZEN_LOG_INFO("Server listening on port {}", port);

            //加载CACHE
            ZEN_LOG_INFO("Loading cache...");
            LoadCacheFromFile();

            // 每次 accept 使用新 socket
            auto new_socket = std::make_shared<asio::ip::tcp::socket>(context_);
            DoAccept(new_socket);
        }

        void Stop() {
            // 关闭acceptor
            asio::error_code ec;
            acceptor_.close(ec);

            // 停止所有活跃Session
            for (auto &session: active_sessions_) {
                session->Stop();// 确保停止所有异步操作
            }
            active_sessions_.clear();
            SaveToFile();
        }

        void SaveToFile(const std::string &filename = "cache_dump.rdb") {
            Astra::datastructures::SaveCacheToFile(*cache_.get(), filename);
        }

        void LoadCacheFromFile(const std::string &filename = "cache_dump.rdb") {
            Astra::datastructures::LoadCacheFromFile(*cache_.get(), filename);
        }

    private:
        void DoAccept(std::shared_ptr<asio::ip::tcp::socket> socket) {
            acceptor_.async_accept(*socket, [this, socket](std::error_code ec) {
                if (ec) {
                    if (ec == asio::error::operation_aborted) {
                        ZEN_LOG_INFO("Acceptor stopped, exiting accept loop");
                        return;// 不再继续监听新连接
                    }
                    ZEN_LOG_WARN("Accept error: {}", ec.message());
                    // 其他错误继续监听
                    auto new_socket = std::make_shared<asio::ip::tcp::socket>(context_);
                    DoAccept(new_socket);
                    return;
                }

                // 处理新连接...
                ZEN_LOG_INFO("New client accepted");

                // 创建 Session 并加入活跃列表
                auto session = std::make_shared<Session>(std::move(*socket), cache_);
                {
                    std::lock_guard<std::mutex> lock(sessions_mutex_);
                    active_sessions_.push_back(session);
                }

                task_queue_->Post([session]() {
                    session->Start();
                });

                // 继续监听下一个连接
                auto new_socket = std::make_shared<asio::ip::tcp::socket>(context_);
                DoAccept(new_socket);
            });
        }
        asio::io_context &context_;
        asio::ip::tcp::acceptor acceptor_;
        std::shared_ptr<LRUCache<std::string, std::string>> cache_;
        std::shared_ptr<concurrent::TaskQueue> task_queue_;
        std::vector<std::shared_ptr<Session>> active_sessions_;
        std::mutex sessions_mutex_;
    };

}// namespace Astra::apps
