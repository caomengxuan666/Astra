#include <iostream>
#include <hiredis/hiredis.h>

void check_reply(redisReply* reply) {
    if (!reply) {
        std::cerr << "Error: Received null reply\n";
        return;
    }

    switch (reply->type) {
        case REDIS_REPLY_INTEGER:
            std::cout << "Integer: " << reply->integer << "\n";
            break;
        case REDIS_REPLY_STATUS:
            std::cout << "Status: " << reply->str << "\n";
            break;
        case REDIS_REPLY_ERROR:
            std::cerr << "Redis Error: " << reply->str << "\n";
            break;
        case REDIS_REPLY_STRING:
            std::cout << "String: " << reply->str << "\n";
            break;
        case REDIS_REPLY_ARRAY:
            std::cout << "Array with " << reply->elements << " elements:\n";
            for (size_t i = 0; i < reply->elements; ++i) {
                std::cout << "  [" << i << "] ";
                check_reply(reply->element[i]);
            }
            break;
        default:
            std::cout << "Unknown reply type: " << reply->type << "\n";
    }
}

int main() {
    // 连接 Redis 服务器
    redisContext *c = redisConnect("127.0.0.1", 6379);
    if (c == nullptr || c->err) {
        if (c) {
            std::cerr << "Error connecting to Redis: " << c->errstr << "\n";
            redisFree(c);
        } else {
            std::cerr << "Connection error: can't allocate redis context\n";
        }
        return 1;
    }
    std::cout << "Connected to Redis\n";

    // PING
    {
        redisReply *reply = (redisReply*)redisCommand(c, "PING");
        std::cout << "PING response: ";
        check_reply(reply);
        freeReplyObject(reply);
    }

    // SET
    {
        redisReply *reply = (redisReply*)redisCommand(c, "SET test_key HelloHiredis");
        std::cout << "SET response: ";
        check_reply(reply);
        freeReplyObject(reply);
    }

    // GET
    {
        redisReply *reply = (redisReply*)redisCommand(c, "GET test_key");
        std::cout << "GET test_key: ";
        check_reply(reply);
        freeReplyObject(reply);
    }

    // EXISTS
    {
        redisReply *reply = (redisReply*)redisCommand(c, "EXISTS test_key");
        std::cout << "EXISTS test_key: ";
        check_reply(reply);
        freeReplyObject(reply);
    }

    // TTL (expect -1 since no EXPIRE set)
    {
        redisReply *reply = (redisReply*)redisCommand(c, "TTL test_key");
        std::cout << "TTL test_key: ";
        check_reply(reply);
        freeReplyObject(reply);
    }

    // INCR / DECR
    {
        redisReply *reply = (redisReply*)redisCommand(c, "INCR test_counter");
        std::cout << "INCR test_counter: ";
        check_reply(reply);
        freeReplyObject(reply);

        reply = (redisReply*)redisCommand(c, "DECR test_counter");
        std::cout << "DECR test_counter: ";
        check_reply(reply);
        freeReplyObject(reply);
    }

    // KEYS *
    {
        redisReply *reply = (redisReply*)redisCommand(c, "KEYS *");
        std::cout << "KEYS *: ";
        check_reply(reply);
        freeReplyObject(reply);
    }

    // INFO
    {
        redisReply *reply = (redisReply*)redisCommand(c, "INFO");
        std::cout << "INFO output:\n";
        if (reply && reply->type == REDIS_REPLY_STRING) {
            std::cout << reply->str << "\n";
        }
        freeReplyObject(reply);
    }

    // DEL
    {
        redisReply *reply = (redisReply*)redisCommand(c, "DEL test_key");
        std::cout << "DEL test_key: ";
        check_reply(reply);
        freeReplyObject(reply);
    }

    // 断开连接
    redisFree(c);
    std::cout << "Disconnected from Redis.\n";

    return 0;
}