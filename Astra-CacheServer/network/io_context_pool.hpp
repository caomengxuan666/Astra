#pragma once
#include "Singleton.h"
#include "logger.hpp"
#include <asio.hpp>
#include <iostream>
#include <thread>
#include <vector>
/**
 * @author       : caomengxuan666
 * @brief        : ASIO io_service 线程池
**/
class AsioIOServicePool : public Singleton<AsioIOServicePool> {
    friend Singleton<AsioIOServicePool>;

public:
    using IOService = asio::io_context;
    using Work = asio::io_context::work;
    using WorkPtr = std::unique_ptr<Work>;
    ~AsioIOServicePool();
    AsioIOServicePool(const AsioIOServicePool &) = delete;
    AsioIOServicePool &operator=(const AsioIOServicePool &) = delete;

    /**
     * @author       : caomengxuan666
     * @brief        : 使用 round-robin 的方式返回一个 io_service
     * @return        {*}
    **/
    asio::io_context &GetIOService();
    /**
     * @author       : caomengxuan666
     * @brief        : 使得 io_service从run的状态退出
     * @return        {*}
    **/
    void Stop();

private:
    /**
     * @author       : caomengxuan666
     * @brief        : 构造函数，初始化线程池，每个线程内部启动一个io_service
     * @param         {size_t} size:
     * @return        {*}
    **/
    AsioIOServicePool(std::size_t size = 2 /*std::thread::hardware_concurrency()*/);
    std::vector<IOService> _ioServices;
    std::vector<WorkPtr> _works;
    std::vector<std::thread> _threads;
    std::size_t _nextIOService;
};

inline AsioIOServicePool::AsioIOServicePool(std::size_t size) : _ioServices(size),
                                                                _works(size), _nextIOService(0) {
    for (std::size_t i = 0; i < size; ++i) {
        _works[i] = std::unique_ptr<Work>(new Work(_ioServices[i]));
    }

    //遍历多个ioservice，创建多个线程，每个线程内部启动ioservice
    for (std::size_t i = 0; i < _ioServices.size(); ++i) {
        _threads.emplace_back([this, i]() {
            _ioServices[i].run();
        });
    }
}

inline AsioIOServicePool::~AsioIOServicePool() {
    Stop();
}

inline asio::io_context &AsioIOServicePool::GetIOService() {
    auto &service = _ioServices[_nextIOService++];
    if (_nextIOService == _ioServices.size()) {
        _nextIOService = 0;
    }
    return service;
}

inline void AsioIOServicePool::Stop() {
    for (auto &work: _works) {
        work.reset();
    }

    for (auto &io: _ioServices) {
        io.stop();
    }

    for (auto &t: _threads) {
        if (t.joinable() && t.get_id() != std::this_thread::get_id()) {
            try {
                t.join();// 只 join 其他线程
            } catch (const std::system_error &e) {
                //ZEN_LOG_WARN("Failed to join thread {}: {}", t.get_id(), e.what());
            }
        } else {
            //ZEN_LOG_DEBUG("Skipping current thread {}", t.get_id());
        }
    }
}