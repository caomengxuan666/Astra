#include "server/session.hpp"
#include "core/astra.hpp"
#include "logger.hpp"
#include "proto/redis_command_handler.hpp"
#include "proto/resp_builder.hpp"
#include "proto/ProtocolParser.hpp"
#include "server/stats_event.h"
#include <asio/post.hpp>
#include <fmt/format.h>
#include <utils/uuid_utils.h>

namespace Astra::apps {

    // PubSubSession实现
    PubSubSession::PubSubSession(std::shared_ptr<ChannelManager> channel_manager) 
        : channel_manager_(std::move(channel_manager)) {}

    void PubSubSession::PushMessage(const std::string &channel, const std::string &message, const std::string &pattern) {
        // PubSubSession不负责实际的消息推送操作
        // 消息推送由Session类通过msg_queue_和DoWriteMessages()方法处理
        // 此方法仅为满足接口契约而存在
    }

    void PubSubSession::PushMessage(const std::string &channel, const std::string &message) {
        // 调用三个参数版本的PushMessage方法，pattern字段为空
        PushMessage(channel, message, "");
    }

    void PubSubSession::AddSubscribedPattern(const std::string &pattern) {
        subscribed_patterns_.insert(pattern);
    }

    void PubSubSession::RemoveSubscribedPattern(const std::string &pattern) {
        subscribed_patterns_.erase(pattern);
    }

    void PubSubSession::ClearSubscribedPatterns() {
        // 不再直接调用channel_manager_的方法，这些应该在Session类中处理
        subscribed_patterns_.clear();
    }

    const std::unordered_set<std::string> &PubSubSession::GetSubscribedPatterns() const {
        return subscribed_patterns_;
    }

    const std::unordered_set<std::string> &PubSubSession::GetSubscribedChannels() const {
        return subscribed_channels_;
    }

    void PubSubSession::Subscribe(const std::string& channel) {
        subscribed_channels_.insert(channel);
    }

    void PubSubSession::Unsubscribe(const std::string& channel) {
        subscribed_channels_.erase(channel);
    }

    void PubSubSession::UnsubscribeAll() {
        subscribed_channels_.clear();
    }

    void PubSubSession::PUnsubscribeAll() {
        subscribed_patterns_.clear();
    }

    bool PubSubSession::HasSubscriptions() const {
        return !subscribed_channels_.empty() || !subscribed_patterns_.empty();
    }

    // 构造函数实现
    Session::Session(
            asio::ip::tcp::socket socket,
            std::shared_ptr<datastructures::AstraCache<datastructures::LRUCache, std::string, std::string>> cache,
            std::shared_ptr<concurrent::TaskQueue> global_task_queue,
            std::shared_ptr<apps::ChannelManager> channel_manager) : socket_(std::move(socket)),
                                                                     strand_(asio::make_strand(socket_.get_executor())),
                                                                     handler_(std::make_shared<proto::RedisCommandHandler>(cache, channel_manager, weak_from_this())),
                                                                     task_queue_(std::move(global_task_queue)),
                                                                     parser_(std::make_shared<proto::ProtocolParser>()),
                                                                     pubsub_session_(std::make_shared<PubSubSession>(channel_manager)),
                                                                     session_mode_(SessionMode::CacheMode),
                                                                     channel_manager_(std::move(channel_manager)),
                                                                     stopped_(false),
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
        auto self = shared_from_this();

        // 1. 清理频道订阅
        for (const auto &channel: pubsub_session_->GetSubscribedChannels()) {
            channel_manager_->Unsubscribe(channel, self);
        }

        // 2. 清理模式订阅（关键修复）
        for (const auto &pattern: pubsub_session_->GetSubscribedPatterns()) {
            channel_manager_->PUnsubscribe(pattern, self);
        }

        // 发送连接关闭事件
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
        return pubsub_session_->GetSubscribedChannels();
    }

    // 切换会话模式（Cache/PubSub）
    void Session::SwitchMode(SessionMode new_mode) {
        asio::post(strand_, [self = shared_from_this(), new_mode]() {
            self->session_mode_ = new_mode;
            ZEN_LOG_DEBUG("Session switched to mode: {}",
                          new_mode == SessionMode::CacheMode ? "CacheMode" : "PubSubMode");
            self->buffer_.clear();
            self->argv_.clear();
            self->parser_->Reset();
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
        size_t processed = parser_->ProcessBuffer(buffer_, argv_);
        
        // Check if parsing is complete and process the request
        if (parser_->GetParseState() == proto::ProtocolParser::ParseState::ReadingArrayHeader && 
            parser_->GetRemainingArgs() == 0 && !argv_.empty()) {
            ProcessRequest();
        }
        
        return processed;
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
        
        // 清空参数向量，为下一次解析做准备
        argv_.clear();
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
                    pubsub_session_->Subscribe(channel);
                    channel_manager_->Subscribe(channel, shared_from_this());
                }
                response = proto::RespBuilder::SubscribeResponse(pubsub_session_->GetSubscribedChannels());
                need_switch_mode = true;
            }
        } else if (cmd == "UNSUBSCRIBE") {
            std::unordered_set<std::string> unsubscribed;
            if (argv_.size() < 2) {
                // 无参数：取消所有订阅
                unsubscribed = pubsub_session_->GetSubscribedChannels();
                for (const auto &channel: pubsub_session_->GetSubscribedChannels()) {
                    channel_manager_->Unsubscribe(channel, shared_from_this());
                }
                pubsub_session_->UnsubscribeAll();
            } else {
                // 有参数：取消指定频道
                for (size_t i = 1; i < argv_.size(); ++i) {
                    const std::string &channel = argv_[i];
                    if (pubsub_session_->GetSubscribedChannels().count(channel)) {
                        unsubscribed.insert(channel);
                        pubsub_session_->Unsubscribe(channel);
                        channel_manager_->Unsubscribe(channel, shared_from_this());
                    }
                }
            }
            response = proto::RespBuilder::UnsubscribeResponse(unsubscribed);
            need_switch_mode = pubsub_session_->GetSubscribedChannels().empty() && pubsub_session_->GetSubscribedPatterns().empty();
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
                    pubsub_session_->AddSubscribedPattern(pattern);
                    channel_manager_->PSubscribe(pattern, shared_from_this());

                    // 为每个模式单独生成响应，传入当前会话的订阅总数（i 是当前已处理的模式数）
                    std::unordered_set<std::string> single_pattern{pattern};
                    response += proto::RespBuilder::PSubscribeResponse(
                            single_pattern,
                            pubsub_session_->GetSubscribedPatterns().size()// 当前会话的总订阅数（递增）
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

            if (argv_.size() < 2) {
                // 无参数：取消所有模式（此处正常）
                unsubscribed_patterns = pubsub_session_->GetSubscribedPatterns();
                for (const auto& pattern : pubsub_session_->GetSubscribedPatterns()) {
                    channel_manager_->PUnsubscribe(pattern, shared_from_this());
                }
                pubsub_session_->PUnsubscribeAll();
            } else {
                // 有参数：取消指定模式（关键修复）
                for (size_t i = 1; i < argv_.size(); ++i) {
                    const std::string &pattern = argv_[i];// 获取用户输入的模式（如 "news*"）
                    if (pubsub_session_->GetSubscribedPatterns().count(pattern)) {
                        unsubscribed_patterns.insert(pattern);// 记录被取消的模式
                        pubsub_session_->RemoveSubscribedPattern(pattern);
                        channel_manager_->PUnsubscribe(pattern, shared_from_this());
                    }
                }
            }

            // 构建响应时，传入实际被取消的模式（unsubscribed_patterns）
            response = proto::RespBuilder::PUnsubscribeResponse(
                    unsubscribed_patterns,// 此处必须传入非空集合（包含 "news*"）
                    pubsub_session_->GetSubscribedPatterns().size());

            // 切换模式（原有逻辑）
            if (pubsub_session_->GetSubscribedChannels().empty() && pubsub_session_->GetSubscribedPatterns().empty()) {
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
        auto self = shared_from_this();
        
        // 清理频道订阅
        for (const auto &channel: pubsub_session_->GetSubscribedChannels()) {
            channel_manager_->Unsubscribe(channel, self);
        }
        pubsub_session_->UnsubscribeAll();

        // 清理模式订阅
        for (const auto &pattern: pubsub_session_->GetSubscribedPatterns()) {
            channel_manager_->PUnsubscribe(pattern, self);
        }
        pubsub_session_->PUnsubscribeAll();
    }

}// namespace Astra::apps