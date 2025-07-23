#include "server/session.hpp"
#include "core/astra.hpp"
#include "logger.hpp"
#include "proto/redis_command_handler.hpp"
#include "proto/resp_builder.hpp"
#include <asio/post.hpp>
#include <fmt/format.h>
#include <utils/uuid_utils.h>

namespace Astra::apps {

    // 构造函数实现
    Session::Session(
            asio::ip::tcp::socket socket,
            std::shared_ptr<datastructures::AstraCache<datastructures::LRUCache, std::string, std::string>> cache,
            std::shared_ptr<concurrent::TaskQueue> global_task_queue,
            std::shared_ptr<apps::ChannelManager> channel_manager) : socket_(std::move(socket)),
                                                                     strand_(asio::make_strand(socket_.get_executor())),
                                                                     handler_(std::make_shared<proto::RedisCommandHandler>(cache, channel_manager, weak_from_this())),
                                                                     task_queue_(std::move(global_task_queue)),
                                                                     session_mode_(SessionMode::CacheMode),
                                                                     channel_manager_(std::move(channel_manager)),
                                                                     stopped_(false),
                                                                     parse_state_(ParseState::ReadingArrayHeader),
                                                                     remaining_args_(0),
                                                                     current_bulk_size_(0),
                                                                     is_writing_(false) {
        // 使用UUID工具类生成session_id_
        auto generator = Astra::utils::UuidUtils::GetInstance().get_generator();

        if (generator != nullptr) [[likely]] {
            session_id_ = generator->generate();// 正常路径：使用对象池中的生成器
        } else {
            // 降级方案：极端情况下的备选逻辑（大概率不执行）
            static std::mt19937_64 fallback_rng(std::random_device{}());
            auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
            session_id_ = std::to_string(timestamp) + "_" + std::to_string(fallback_rng());
        }


        // 发送连接建立事件（使用生成的session_id_）
        stats::emitConnectionOpened(session_id_);
    }

    Session::~Session() {
        ZEN_LOG_DEBUG("Session destroyed, cleaning up subscriptions");

        // 1. 清理频道订阅
        for (const auto &channel: subscribed_channels_) {
            channel_manager_->Unsubscribe(channel, shared_from_this());
        }
        subscribed_channels_.clear();

        // 2. 清理模式订阅（关键修复）
        for (const auto &pattern: subscribed_patterns_) {
            channel_manager_->PUnsubscribe(pattern, shared_from_this());
        }
        subscribed_patterns_.clear();

        //  发送连接关闭事件
        stats::emitConnectionClosed(session_id_);

        ZEN_LOG_DEBUG("All subscriptions cleaned up");
    }

    // 启动会话
    void Session::Start() {
        if (!stopped_) {
            asio::post(strand_, [self = shared_from_this()]() {
                if (self->session_mode_ == SessionMode::CacheMode) {
                    self->DoRead();
                } else {
                    self->DoReadPubSub();
                }
            });
        }
    }

    // 停止会话
    void Session::Stop() {
        asio::post(strand_, [self = shared_from_this()]() {
            if (self->stopped_) return;

            self->stopped_ = true;


            asio::error_code ec;
            ZEN_LOG_DEBUG("Session stopping, closing socket");
            (void) self->socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
            (void) self->socket_.cancel(ec);
            (void) self->socket_.close(ec);
            self->CleanupSubscriptions();// 清理订阅关系
            ZEN_LOG_INFO("Client disconnected");
        });
    }

    // 推送消息到会话队列
    void Session::PushMessage(const std::string &channel, const std::string &message, const std::string &pattern) {
        ZEN_LOG_DEBUG("Pushing message to queue (channel: '{}', pattern: '{}')", channel, pattern);
        // 只传入3个参数，匹配PubSubMessage的构造函数
        bool pushed = msg_queue_.emplace(channel, message, pattern);
        if (!pushed) {
            ZEN_LOG_ERROR("Failed to push message to queue (overflow)");
        } else {
            TriggerMessageWrite();
        }
    }

    // 普通消息（无模式）的重载版本
    void Session::PushMessage(const std::string &channel, const std::string &message) {
        // 同样只传入3个参数，模式字段为空
        PushMessage(channel, message, "");
    }


    // 获取订阅的频道列表
    const std::unordered_set<std::string> &Session::GetSubscribedChannels() const {
        return subscribed_channels_;
    }

    // 切换会话模式（Cache/PubSub）
    void Session::SwitchMode(SessionMode new_mode) {
        asio::post(strand_, [self = shared_from_this(), new_mode]() {
            self->session_mode_ = new_mode;
            ZEN_LOG_DEBUG("Session switched to mode: {}",
                          new_mode == SessionMode::CacheMode ? "CacheMode" : "PubSubMode");
            self->parse_state_ = ParseState::ReadingArrayHeader;
            self->buffer_.clear();
            self->argv_.clear();
            if (new_mode == SessionMode::CacheMode) {
                self->DoRead();
            } else {
                self->DoReadPubSub();
            }
        });
    }

    // 读取Cache模式下的命令
    void Session::DoRead() {
        if (stopped_) return;

        asio::async_read_until(socket_, asio::dynamic_buffer(buffer_), "\r\n",
                               asio::bind_executor(strand_, [this, self = shared_from_this()](std::error_code ec, std::size_t) {
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

    // 读取PubSub模式下的命令
    void Session::DoReadPubSub() {
        if (stopped_) return;

        // 优先处理待发送消息
        if (!msg_queue_.Empty()) {
            TriggerMessageWrite();
        }

        // 再读取新命令
        asio::async_read_until(socket_, asio::dynamic_buffer(buffer_), "\r\n",
                               asio::bind_executor(strand_, [this, self = shared_from_this()](asio::error_code ec, size_t) {
                                   if (ec) {
                                       ZEN_LOG_WARN("PubSub read error: {}", ec.message());
                                       CleanupSubscriptions();
                                       Stop();
                                       return;
                                   }

                                   ProcessBuffer();
                                   DoReadPubSub();
                               }));
    }

    // 处理缓冲区中的协议数据
    size_t Session::ProcessBuffer() {
        if (parse_state_ == ParseState::ReadingArrayHeader) {
            size_t pos = buffer_.find("\r\n");
            if (pos == std::string::npos) return 0;

            std::string line = buffer_.substr(0, pos);
            buffer_.erase(0, pos + 2);

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
                return 0;
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

    // 处理数组头（如 *3\r\n）
    void Session::HandleArrayHeader(const std::string &line) {
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

    // 处理批量数据头（如 $5\r\n）
    void Session::HandleBulkHeader(const std::string &line) {
        if (line.empty() || line[0] != '$') {
            WriteResponse("-ERR Protocol error: expected bulk header\r\n");
            return;
        }

        try {
            int64_t bulk_size = std::stoll(line.substr(1));
            if (bulk_size < -1) {
                throw std::invalid_argument("Invalid bulk size");
            }

            current_bulk_size_ = static_cast<int>(bulk_size);
            ZEN_LOG_DEBUG("Bulk string size: {}", current_bulk_size_);

            parse_state_ = ParseState::ReadingBulkContent;

            if (current_bulk_size_ == -1) {
                argv_.push_back("");
                if (--remaining_args_ > 0) {
                    parse_state_ = ParseState::ReadingBulkHeader;
                } else {
                    ProcessRequest();
                    parse_state_ = ParseState::ReadingArrayHeader;
                }
                return;
            }

            if (buffer_.size() >= static_cast<size_t>(current_bulk_size_ + 2)) {
                HandleBulkContent();
            } else {
                ReadBulkContent();
            }
        } catch (...) {
            WriteResponse("-ERR Invalid bulk length\r\n");
        }
    }

    // 异步读取批量数据内容
    void Session::ReadBulkContent() {
        if (stopped_ || current_bulk_size_ < 0) return;

        size_t required = static_cast<size_t>(current_bulk_size_ + 2);
        size_t have = buffer_.size();

        if (have >= required) {
            HandleBulkContent();
            return;
        }

        size_t need = required - have;
        ZEN_LOG_DEBUG("Waiting for {} more bytes for bulk content", need);

        asio::async_read(socket_, asio::dynamic_buffer(buffer_),
                         asio::transfer_exactly(need),
                         asio::bind_executor(strand_, [this, self = shared_from_this()](std::error_code ec, size_t bytes_read) {
                             if (stopped_) return;

                             if (!ec) {
                                 ZEN_LOG_DEBUG("Read bulk content: {} bytes", bytes_read);
                                 HandleBulkContent();
                             } else {
                                 ZEN_LOG_WARN("Error reading bulk content: {}", ec.message());
                                 WriteResponse("-ERR Connection error\r\n");
                                 parse_state_ = ParseState::ReadingArrayHeader;
                                 DoRead();
                             }
                         }));
    }

    // 处理批量数据内容
    void Session::HandleBulkContent() {
        if (stopped_ || current_bulk_size_ < 0) return;

        if (buffer_.size() < static_cast<size_t>(current_bulk_size_ + 2)) {
            ZEN_LOG_ERROR("Insufficient data for bulk content");
            WriteResponse("-ERR Internal server error\r\n");
            parse_state_ = ParseState::ReadingArrayHeader;
            return;
        }

        std::string content = buffer_.substr(0, current_bulk_size_);
        buffer_.erase(0, current_bulk_size_ + 2);

        argv_.push_back(content);
        ZEN_LOG_DEBUG("Bulk content parsed: {}", content);

        if (--remaining_args_ > 0) {
            parse_state_ = ParseState::ReadingBulkHeader;
            ProcessBuffer();// 继续解析剩余参数
        } else {
            ProcessRequest();
            parse_state_ = ParseState::ReadingArrayHeader;
        }
    }

    // 处理完整请求
    void Session::ProcessRequest() {
        std::string full_cmd;
        for (const auto &arg: argv_) {
            full_cmd += (full_cmd.empty() ? "" : " ") + arg;
        }
        ZEN_LOG_TRACE("Received command: {}", full_cmd);

        const std::string &cmd = argv_[0];
        bool is_pubsub_cmd = (cmd == "SUBSCRIBE" || cmd == "UNSUBSCRIBE" || cmd == "PUBLISH" || cmd == "PSUBSCRIBE" || cmd == "PUNSUBSCRIBE");

        if (is_pubsub_cmd) {
            HandlePubSubCommand();// 直接处理PubSub命令（需 Strand 保护）
        } else {
            auto args_copy = argv_;
            auto self = shared_from_this();
            // 非PubSub命令提交到任务队列异步处理
            (void) task_queue_->Submit([self, args_copy]() {
                try {
                    std::string response = self->handler_->ProcessCommand(args_copy);
                    asio::post(self->strand_, [self, response]() {
                        self->WriteResponse(response);
                    });
                } catch (const std::exception &e) {
                    std::string error_msg = proto::RespBuilder::Error(e.what());
                    asio::post(self->strand_, [self, error_msg]() {
                        self->WriteResponse(error_msg);
                    });
                    ZEN_LOG_ERROR("Error processing request: {}", e.what());
                }
            });
        }
    }

    // 处理PubSub命令（SUBSCRIBE/UNSUBSCRIBE/PUBLISH）
    void Session::HandlePubSubCommand() {
        const std::string &cmd = argv_[0];
        std::string response;
        bool need_switch_mode = false;

        if (cmd == "SUBSCRIBE") {
            if (argv_.size() < 2) {
                response = proto::RespBuilder::Error("SUBSCRIBE requires at least one channel");
            } else {
                for (size_t i = 1; i < argv_.size(); ++i) {
                    const std::string &channel = argv_[i];
                    subscribed_channels_.insert(channel);
                    channel_manager_->Subscribe(channel, shared_from_this());
                }
                response = proto::RespBuilder::SubscribeResponse(subscribed_channels_);
                need_switch_mode = true;
            }
        } else if (cmd == "UNSUBSCRIBE") {
            std::unordered_set<std::string> unsubscribed;
            if (argv_.size() < 2) {
                // 无参数：取消所有订阅
                unsubscribed = subscribed_channels_;
                for (const auto &channel: subscribed_channels_) {
                    channel_manager_->Unsubscribe(channel, shared_from_this());
                }
                subscribed_channels_.clear();
            } else {
                // 有参数：取消指定频道
                for (size_t i = 1; i < argv_.size(); ++i) {
                    const std::string &channel = argv_[i];
                    if (subscribed_channels_.count(channel)) {
                        unsubscribed.insert(channel);
                        subscribed_channels_.erase(channel);
                        channel_manager_->Unsubscribe(channel, shared_from_this());
                    }
                }
            }
            response = proto::RespBuilder::UnsubscribeResponse(unsubscribed);
            need_switch_mode = subscribed_channels_.empty();
        } else if (cmd == "PUBLISH") {
            if (argv_.size() != 3) {
                response = proto::RespBuilder::Error("PUBLISH requires channel and message");
            } else {
                const std::string &channel = argv_[1];
                const std::string &message = argv_[2];
                size_t count = channel_manager_->Publish(channel, message);
                response = proto::RespBuilder::Integer(count);
            }
            need_switch_mode = false;
        } else if (cmd == "PSUBSCRIBE") {
            if (argv_.size() < 2) {
                response = proto::RespBuilder::Error("PSUBSCRIBE requires at least one pattern");
            } else {
                std::string response;// 单独收集每个模式的响应
                for (size_t i = 1; i < argv_.size(); ++i) {
                    const std::string &pattern = argv_[i];
                    subscribed_patterns_.insert(pattern);
                    channel_manager_->PSubscribe(pattern, shared_from_this());

                    // 为每个模式单独生成响应，传入当前会话的订阅总数（i 是当前已处理的模式数）
                    std::unordered_set<std::string> single_pattern{pattern};
                    response += proto::RespBuilder::PSubscribeResponse(
                            single_pattern,
                            subscribed_patterns_.size()// 当前会话的总订阅数（递增）
                    );
                }
                SwitchMode(SessionMode::PubSubMode);
                // 最终响应是所有模式的响应拼接
                asio::post(strand_, [self = shared_from_this(), response]() {
                    self->WriteResponse(response);
                });
            }
        }
        // 处理 PUNSUBSCRIBE 命令
        else if (cmd == "PUNSUBSCRIBE") {
            std::unordered_set<std::string> unsubscribed_patterns;
            const auto &subscribed_patterns = GetSubscribedPatterns();

            if (argv_.size() < 2) {
                // 无参数：取消所有模式（此处正常）
                unsubscribed_patterns = subscribed_patterns;
                ClearSubscribedPatterns();
            } else {
                // 有参数：取消指定模式（关键修复）
                for (size_t i = 1; i < argv_.size(); ++i) {
                    const std::string &pattern = argv_[i];// 获取用户输入的模式（如 "news*"）
                    if (subscribed_patterns.count(pattern)) {
                        unsubscribed_patterns.insert(pattern);// 记录被取消的模式
                        RemoveSubscribedPattern(pattern);
                        channel_manager_->PUnsubscribe(pattern, shared_from_this());
                    }
                }
            }

            // 构建响应时，传入实际被取消的模式（unsubscribed_patterns）
            response = proto::RespBuilder::PUnsubscribeResponse(
                    unsubscribed_patterns,// 此处必须传入非空集合（包含 "news*"）
                    GetSubscribedPatterns().size());

            // 切换模式（原有逻辑）
            if (GetSubscribedChannels().empty() && GetSubscribedPatterns().empty()) {
                SwitchMode(SessionMode::CacheMode);
            }
        }
        // 提交响应到 Strand 执行
        asio::post(strand_, [self = shared_from_this(), response]() {
            self->WriteResponse(response);
        });
    }


    // 触发消息写入（PubSub 模式下推送消息）
    void Session::TriggerMessageWrite() {
        asio::post(strand_, [self = shared_from_this()]() {// 修正lambda捕获格式
            bool expected = false;
            // 原子操作判断是否正在写入，避免重复触发
            if (self->is_writing_.compare_exchange_strong(expected, true)) {
                self->DoWriteMessages();
            } else {
                ZEN_LOG_DEBUG("Message write already in progress, will process later");
            }
        });
    }

    // 执行消息写入（PubSub 模式下批量发送队列中的消息）
    // session.cpp
    void Session::DoWriteMessages() {
        ZEN_LOG_DEBUG("Entering DoWriteMessages (session mode: PubSubMode)");

        std::vector<PubSubMessage> messages;
        PubSubMessage msg;
        size_t count = 0;
        // 从队列读取 PubSubMessage（类型匹配）
        while (msg_queue_.pop(msg)) {
            messages.push_back(std::move(msg));
            count++;
        }
        ZEN_LOG_DEBUG("Popped {} messages from queue", count);

        if (messages.empty()) {
            is_writing_ = false;
            ZEN_LOG_DEBUG("No messages to write, exiting");
            return;
        }

        // 构建响应：区分普通消息（message）和模式消息（pmessage）
        std::string response;
        for (const auto &msg: messages) {
            if (msg.pattern.empty()) {
                // 普通消息：使用 message 响应
                response += proto::RespBuilder::MessageResponse("message", msg.channel, msg.content);
            } else {
                // 模式消息：使用 pmessage 响应（调用重载的 MessageResponse）
                response += proto::RespBuilder::MessageResponse("pmessage", msg.pattern, msg.channel, msg.content);
            }
        }
        ZEN_LOG_DEBUG("Built message response ({} bytes)", response.size());

        // 异步写入消息
        auto self = shared_from_this();
        auto resp_ptr = std::make_shared<std::string>(response);
        asio::async_write(socket_, asio::buffer(*resp_ptr),
                          asio::bind_executor(strand_, [self, resp_ptr](asio::error_code ec, size_t bytes_sent) {
                              self->is_writing_ = false;
                              if (ec) {
                                  ZEN_LOG_ERROR("Failed to send message: {} (sent {} bytes)", ec.message(), bytes_sent);
                                  self->Stop();
                              } else {
                                  ZEN_LOG_DEBUG("Successfully sent {} bytes to subscriber", bytes_sent);
                                  if (!self->msg_queue_.empty()) {
                                      self->TriggerMessageWrite();
                                  }
                              }
                          }));
    }

    // 写入响应到客户端
    void Session::WriteResponse(const std::string &response) {
        if (stopped_) return;

        auto self = shared_from_this();
        auto response_copy = std::make_shared<std::string>(response);// 延长响应生命周期
        asio::async_write(socket_, asio::buffer(*response_copy),
                          asio::bind_executor(strand_, [self, response_copy](asio::error_code ec, size_t) {
                              if (ec) {
                                  ZEN_LOG_WARN("Failed to send response: {}", ec.message());
                                  self->Stop();
                              } else {
                                  ZEN_LOG_DEBUG("Sent response: {}", *response_copy);
                              }
                          }));
    }

    // 清理所有订阅关系（会话停止时调用）
    void Session::CleanupSubscriptions() {
        // 清理频道订阅
        for (const auto &channel: subscribed_channels_) {
            channel_manager_->Unsubscribe(channel, shared_from_this());
        }
        subscribed_channels_.clear();

        // 清理模式订阅
        for (const auto &pattern: subscribed_patterns_) {
            channel_manager_->PUnsubscribe(pattern, shared_from_this());
        }
        subscribed_patterns_.clear();
    }


}// namespace Astra::apps