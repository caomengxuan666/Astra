#pragma once
#include "Singleton.h"
#include <asio.hpp>
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
    //因为仅仅执行work.reset并不能让iocontext从run的状态中退出
    //当iocontext已经绑定了读或写的监听事件后，还需要手动stop该服务。
    for (auto &work: _works) {
        //把服务先停止
        work->get_io_context().stop();
        work.reset();
    }

    for (auto &t: _threads) {
        t.join();
    }
}
