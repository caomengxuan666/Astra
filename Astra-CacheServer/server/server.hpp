#pragma once

#include "asio/any_io_executor.hpp"
#include "asio/strand.hpp"
#include "caching/AstraCacheStrategy.hpp"
#include "logger.hpp"
#include "persistence/persistence.hpp"
#include <asio.hpp>
#include <asio/io_context.hpp>
#include <concurrent/task_queue.hpp>
#include <datastructures/lru_cache.hpp>
#include <memory>
#include <proto/redis_command_handler.hpp>

namespace Astra::apps {

    using namespace Astra::proto;
    class Session : public std::enable_shared_from_this<Session> {
    public:
        explicit Session(asio::ip::tcp::socket socket,
                         std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache,
                         std::shared_ptr<concurrent::TaskQueue> global_task_queue)
            : socket_(std::move(socket)),
              strand_(asio::make_strand(socket_.get_executor())),// 添加strand确保单线程执行
              handler_(std::make_shared<RedisCommandHandler>(cache)),
              task_queue_(std::move(global_task_queue)) {}

        void Start() {
            if (!stopped_) {
                // 通过strand调度Start操作
                asio::post(strand_, [self = shared_from_this()]() {
                    self->DoRead();
                });
            }
        }

        void Stop() {
            asio::post(strand_, [self = shared_from_this()]() {
                if (self->stopped_) return;

                self->stopped_ = true;
                asio::error_code ec;
                (void) self->socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
                (void) self->socket_.cancel(ec);
                (void) self->socket_.close(ec);

                ZEN_LOG_INFO("Client disconnected");
            });
        }

    private:
        enum class ParseState {
            ReadingArrayHeader,
            ReadingBulkHeader,
            ReadingBulkContent
        };

        void DoRead() {
            if (stopped_) return;

            // 使用strand确保读操作在单线程执行
            asio::async_read_until(socket_, asio::dynamic_buffer(buffer_), "\r\n",
                                   asio::bind_executor(strand_,
                                                       [this, self = shared_from_this()](std::error_code ec, std::size_t bytes_transferred) {
                                                           if (ec) {
                                                               if (ec != asio::error::eof) {
                                                                   ZEN_LOG_WARN("Read error: {}", ec.message());
                                                               }
                                                               Stop();
                                                               return;
                                                           }

                                                           ProcessBuffer();

                                                           if (!stopped_) {
                                                               DoRead();
                                                           }
                                                       }));
        }

        // 返回已处理的字节数
        size_t ProcessBuffer() {
            if (parse_state_ == ParseState::ReadingArrayHeader) {
                // 查找第一个 '\r\n'
                size_t pos = buffer_.find("\r\n");
                if (pos == std::string::npos) return 0;

                std::string line = buffer_.substr(0, pos);
                buffer_.erase(0, pos + 2);// 删除该行和 \r\n

                HandleArrayHeader(line);
                return pos + 2;
            } else if (parse_state_ == ParseState::ReadingBulkHeader) {
                size_t pos = buffer_.find("\r\n");
                if (pos == std::string::npos) return 0;

                std::string line = buffer_.substr(0, pos);
                buffer_.erase(0, pos + 2);

                HandleBulkHeader(line);
                return pos + 2;
            } else if (parse_state_ == ParseState::ReadingBulkContent) {
                if (current_bulk_size_ < 0 || buffer_.size() < static_cast<size_t>(current_bulk_size_ + 2)) {
                    return 0;// 数据未就绪
                }

                std::string content = buffer_.substr(0, current_bulk_size_);
                buffer_.erase(0, current_bulk_size_ + 2);

                argv_.push_back(content);
                remaining_args_--;

                if (remaining_args_ > 0) {
                    parse_state_ = ParseState::ReadingBulkHeader;
                } else {
                    ProcessRequest();
                    parse_state_ = ParseState::ReadingArrayHeader;
                }

                return current_bulk_size_ + 2;
            }

            return 0;
        }

        void HandleArrayHeader(const std::string &line) {
            if (line.empty() || line[0] != '*') {
                WriteResponse("-ERR Protocol error: expected array header\r\n");
                return;
            }

            try {
                int64_t arg_count = std::stoll(line.substr(1));
                if (arg_count < 0) {
                    throw std::invalid_argument("Negative argument count");
                }

                remaining_args_ = static_cast<int>(arg_count);
                argv_.clear();
                argv_.reserve(remaining_args_);

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
                int64_t bulk_size = std::stoll(line.substr(1));
                if (bulk_size < -1) {// -1表示空值
                    throw std::invalid_argument("Invalid bulk size");
                }

                current_bulk_size_ = static_cast<int>(bulk_size);
                ZEN_LOG_DEBUG("Bulk string size: {}", current_bulk_size_);

                parse_state_ = ParseState::ReadingBulkContent;

                // 处理空值情况
                if (current_bulk_size_ == -1) {
                    argv_.push_back("");// 空字符串表示nil
                    if (--remaining_args_ > 0) {
                        parse_state_ = ParseState::ReadingBulkHeader;
                    } else {
                        ProcessRequest();
                        parse_state_ = ParseState::ReadingArrayHeader;
                    }
                    return;
                }

                // 检查是否已有足够数据
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
            if (stopped_ || current_bulk_size_ < 0) return;

            size_t required = static_cast<size_t>(current_bulk_size_ + 2);// +2 是为了\r\n
            size_t have = buffer_.size();

            if (have >= required) {
                HandleBulkContent();
                return;
            }

            size_t need = required - have;
            ZEN_LOG_DEBUG("Waiting for {} more bytes for bulk content", need);

            // 使用strand确保读操作在单线程执行
            asio::async_read(socket_, asio::dynamic_buffer(buffer_),
                             asio::transfer_exactly(need),
                             asio::bind_executor(strand_,
                                                 [this, self = shared_from_this()](std::error_code ec, size_t bytes_read) {
                                                     if (stopped_) return;

                                                     if (!ec) {
                                                         ZEN_LOG_DEBUG("Read bulk content: {} bytes", bytes_read);
                                                         HandleBulkContent();
                                                     } else {
                                                         ZEN_LOG_WARN("Error reading bulk content: {}", ec.message());
                                                         WriteResponse("-ERR Connection error\r\n");
                                                         parse_state_ = ParseState::ReadingArrayHeader;
                                                         DoRead();// 继续读取下一个命令
                                                     }
                                                 }));
        }

        void HandleBulkContent() {
            if (stopped_ || current_bulk_size_ < 0) return;

            // 确保有足够的数据
            if (buffer_.size() < static_cast<size_t>(current_bulk_size_ + 2)) {
                ZEN_LOG_ERROR("Insufficient data for bulk content");
                WriteResponse("-ERR Internal server error\r\n");
                parse_state_ = ParseState::ReadingArrayHeader;
                return;
            }

            // 提取bulk内容（不包括最后的\r\n）
            std::string content = buffer_.substr(0, current_bulk_size_);
            // 移除内容及\r\n
            buffer_.erase(0, current_bulk_size_ + 2);

            argv_.push_back(content);
            ZEN_LOG_DEBUG("Bulk content parsed: {}", content);

            // 检查是否还有更多参数需要读取
            if (--remaining_args_ > 0) {
                parse_state_ = ParseState::ReadingBulkHeader;
                // 继续处理缓冲区中的数据
                ProcessBuffer();
            } else {
                // 所有参数都已读取，处理请求
                ProcessRequest();
                parse_state_ = ParseState::ReadingArrayHeader;
            }
        }

        void ProcessRequest() {
            std::string full_cmd;
            for (const auto &arg: argv_) {
                full_cmd += (full_cmd.empty() ? "" : " ") + arg;
            }
            ZEN_LOG_TRACE("Received command: {}", full_cmd);

            // 复制参数，避免跨线程访问
            auto args_copy = argv_;
            auto self = shared_from_this();

            // 提交命令处理任务到线程池
            (void) task_queue_->Submit([self, args_copy]() {
                try {
                    std::string response = self->handler_->ProcessCommand(args_copy);
                    // 确保响应在strand中发送
                    asio::post(self->strand_, [self, response]() {
                        self->WriteResponse(response);
                    });
                } catch (const std::exception &e) {
                    std::string error_msg = "-ERR " + std::string(e.what()) + "\r\n";
                    asio::post(self->strand_, [self, error_msg]() {
                        self->WriteResponse(error_msg);
                    });
                    ZEN_LOG_ERROR("Error processing request: {}", e.what());
                }
            });
        }

        void WriteResponse(const std::string &response) {
            if (stopped_) return;

            auto self = shared_from_this();
            auto response_copy = std::make_shared<std::string>(response);

            asio::async_write(socket_, asio::buffer(*response_copy),
                              asio::bind_executor(strand_, [self, response_copy](const asio::error_code &ec, std::size_t) {
                                  if (ec) {
                                      ZEN_LOG_WARN("Failed to send response: {}", ec.message());
                                      self->Stop();
                                  } else {
                                      ZEN_LOG_DEBUG("Sent response: {}", *response_copy);
                                  }
                              }));
        }

        std::shared_ptr<concurrent::TaskQueue> task_queue_;
        bool stopped_ = false;
        asio::ip::tcp::socket socket_;
        asio::strand<asio::any_io_executor> strand_;// 用于确保单线程执行
        std::string buffer_;
        std::shared_ptr<Astra::proto::RedisCommandHandler> handler_;

        ParseState parse_state_ = ParseState::ReadingArrayHeader;
        int remaining_args_ = 0;
        int current_bulk_size_ = 0;
        std::vector<std::string> argv_;
    };

    class AstraCacheServer {
    public:
        explicit AstraCacheServer(asio::io_context &context, size_t cache_size,
                                  const std::string &persistent_file)
            : context_(context),
              cache_(std::make_shared<AstraCache<LRUCache, std::string, std::string>>(cache_size)),
              acceptor_(context), persistence_db_name_(persistent_file) {
            // 服务模式下使用单线程任务队列避免并发问题
            const int thread_count = std::thread::hardware_concurrency() / 2;
            task_queue_ = std::make_shared<concurrent::TaskQueue>(thread_count);
        }

        void Start(unsigned short port) {
            asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port);
            acceptor_.open(endpoint.protocol());
            acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
            acceptor_.bind(endpoint);
            acceptor_.listen();
            ZEN_LOG_INFO("Server listening on port {}", port);

            LoadCacheFromFile(persistence_db_name_);

            // 开始接受连接
            DoAccept();
        }

        void Stop() {
            // 关闭 acceptor
            asio::error_code ec;
            acceptor_.close(ec);

            // 停止所有活跃会话
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            for (auto &session: active_sessions_) {
                session->Stop();
            }
            active_sessions_.clear();

            // 停止任务队列
            task_queue_->Stop();

            // 保存缓存
            SaveToFile(persistence_db_name_);
        }

        void SaveToFile(const std::string &filename) {
            if (!enable_persistence_) return;
            Astra::Persistence::SaveCacheToFile(*cache_.get(), filename);
        }

        void LoadCacheFromFile(const std::string &filename) {
            if (!enable_persistence_) return;
            // 加载缓存
            ZEN_LOG_INFO("Loading cache from {}", persistence_db_name_);
            Astra::Persistence::LoadCacheFromFile(*cache_.get(), filename);
        }

        void setEnablePersistence(bool enable) {
            enable_persistence_ = enable;
        }

    private:
        void DoAccept() {
            // 创建新的socket并异步接受连接
            acceptor_.async_accept(
                    asio::make_strand(context_),// 使用strand确保accept在单线程执行
                    [this](std::error_code ec, asio::ip::tcp::socket socket) {
                        if (!ec) {
                            ZEN_LOG_INFO("New client accepted from: {}",
                                         socket.remote_endpoint().address().to_string());

                            // 创建会话并添加到活跃会话列表
                            auto session = std::make_shared<Session>(
                                    std::move(socket), cache_, task_queue_);
                            {
                                std::lock_guard<std::mutex> lock(sessions_mutex_);
                                active_sessions_.push_back(session);
                            }

                            // 启动会话
                            session->Start();
                        } else if (ec != asio::error::operation_aborted) {
                            ZEN_LOG_WARN("Accept error: {}", ec.message());
                        }

                        // 继续接受下一个连接
                        if (!acceptor_.is_open()) {
                            ZEN_LOG_INFO("Acceptor closed, stopping accept loop");
                            return;
                        }
                        DoAccept();
                    });
        }

        std::string persistence_db_name_;
        asio::io_context &context_;
        asio::ip::tcp::acceptor acceptor_;
        std::shared_ptr<AstraCache<LRUCache, std::string, std::string>> cache_;
        std::shared_ptr<concurrent::TaskQueue> task_queue_;
        std::vector<std::shared_ptr<Session>> active_sessions_;
        std::mutex sessions_mutex_;
        bool enable_persistence_ = false;
    };

}// namespace Astra::apps
