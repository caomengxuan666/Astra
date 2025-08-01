# Astra - 高性能Redis兼容缓存中间件

[![Build Status](https://https://github.com/caomengxuan666/Astra/build-status)](https://github.com/caomengxuan666/Astra)
[![License](https://img.shields.io/github/license/caomengxuan666/Astra)](https://github.com/caomengxuan666/Astra/blob/master/LICENSE)

## 项目概述
Astra 是一个基于 C++17 的高性能 Redis 兼容缓存中间件，采用模块化设计实现以下核心价值：
- 提供线程安全的 LRU 缓存实现
- 支持 Redis 协议的网络通信
- 实现命令模式的客户端 SDK，包含C++ SDK、C SDK、LabView SDK，兼容hiredis 库
- 支持 TTL 过期机制与后台清理
- 在Windows下支持服务模式启动
- 支供发布/订阅模式和Lua脚本执行
- 支持rdb快照保存和恢复

## 项目截图

![alt text](snapshots/{734A5CB7-AED1-4D02-BFF0-50F80F7A0A6F}.png)

### 项目统计
- [代码统计报告](code_stats_reports/report.html) - 详细代码行数、文件数量等统计信息
- [交互式图表](code_stats_reports/interactive_chart.html) - 可交互的代码统计图表
![语言统计](code_stats_reports/bar_chart.png)

### 核心模块
| 模块 | 功能 | 技术实现 |
|-------|-------|-------|
| **Core** | 基础类型定义与宏 | C++17 特性
| **Utils** | 日志系统/范围保护 | RAII 模式
| **DataStructures** | 无锁队列/LRU 缓存 | CAS 原子操作
| **concurrent** | 并发支持框架 | 线程池/任务队列/任务流
| **CacheServer** | Redis 协议解析 | Asio 异步 IO
| **Client SDK** | 命令模式封装 | 多态设计

### 支持的Redis命令

#### 键值命令
- GET, SET, DEL, EXISTS, KEYS, TTL, MGET, MSET

#### 数值命令
- INCR, DECR

#### 连接命令
- PING

#### 服务命令
- COMMAND, INFO

#### 发布/订阅命令
- SUBSCRIBE, UNSUBSCRIBE, PUBLISH, PSUBSCRIBE, PUNSUBSCRIBE

#### Lua脚本命令
- EVAL, EVALSHA

### 并发模块设计
`concurrent` 模块提供完整的并发解决方案：

1. **ThreadPool 线程池**
   - 优先级调度支持（小值优先）
   - 工作窃取算法优化负载均衡
   - 完整生命周期管理（Stop/Join/Resume）
   - 支持局部任务队列与全局任务队列混合模式

2. **TaskQueue 任务队列**
   - 支持带回调的任务提交
   - 提供Post/Submit接口
   - 基于线程池实现异步任务调度

3. **任务流编排**
   - **SeriesWork**：串行任务组（类似Promise链式调用）
   - **ParallelWork**：并行任务组
   - **TaskPipeline**：带共享上下文的状态化任务链

```cpp
// 示例：并发任务编排
auto queue = TaskQueue::Create();

// 串行任务
SeriesWork::Create(queue)
    ->Then([]{ std::cout << "Step 1"; })
    ->Then([]{ std::cout << "Step 2"; })
    ->Run();

// 并行任务
ParallelWork::Create(queue)
    ->Add([]{ /* 任务A */ })
    ->Add([]{ /* 任务B */ })
    ->Run();
```

## 技术亮点
- **六大技术特性**:
  - 异步网络模型（基于 Asio + 自定义线程池）
  - 分片锁机制提升并发性能
  - 无锁数据结构优化访问效率
  - 完整协议支持（Redis RESP 兼容）
  - 可扩展的命令模式设计
  - 多级任务队列优化资源调度

## 客户端SDK

Astra提供了多种语言的客户端SDK，方便开发者集成到自己的应用中：

### C++ SDK
面向C++开发者，提供完整的Astra功能访问接口，支持所有Redis兼容命令。

### C SDK
面向C开发者，提供C语言接口访问Astra功能，兼容标准C语言规范。

### LabVIEW SDK
面向LabVIEW开发者，提供LabVIEW环境下的Astra访问接口，方便在LabVIEW中集成缓存功能。

## Windows服务模式

Astra支持在Windows系统中以服务模式运行，提供后台持久化运行能力：
- 支持安装为Windows服务
- 支持启动、停止服务
- 支持设置自动启动
- 服务模式下稳定运行，支持系统重启后自动启动

## 快速入门
### 构建要求
- C++17 兼容编译器（GCC 9+/Clang 10+）
- CMake 3.14+
- 系统依赖：libfmt-dev libssl-dev

### 构建步骤
```bash
# 克隆项目
$ git clone https://github.com/caomengxuan666/Astra.git
$ cd Astra

# 构建项目
$ mkdir build && cd build
$ cmake ..
$ make -j$(nproc)

# 安装项目
$ sudo make install
```

### 启动服务
```bash
# 启动缓存服务器
$ Astra-CacheServer -p 6379

# 运行示例客户端
$ ./build/bin/example_client

# 在windows下安装服务模式
$ Astra-CacheServer.exe install

# 启动服务
$ Astra-CacheServer.exe start

# 停止服务
$ Astra-CacheServer.exe stop

# 设置服务自动启动
$ Astra-CacheServer.exe autostart
```

## 功能演示
```cpp
#include "sdk/astra_client.hpp"

int main() {
    Astra::Client::AstraClient client("127.0.0.1", 8080);
    
    // 基础操作
    client.Set("key", "value");
    auto val = client.Get("key");
    
    // 带TTL的缓存
    client.Set("temp_key", "value", std::chrono::seconds(10));
    auto ttl = client.TTL("temp_key");
    
    // 原子操作
    client.Incr("counter");
    auto count = client.Get("counter");
    
    // 批量操作
    std::vector<std::pair<std::string, std::string>> kvs = {
        {"key1", "value1"},
        {"key2", "value2"}
    };
    client.MSet(kvs);
    
    std::vector<std::string> keys = {"key1", "key2"};
    auto values = client.MGet(keys);
    
    // 发布/订阅
    // 订阅需要单独的客户端实例
    // 发布消息
    client.Publish("channel", "Hello, Astra!");
}
```

## 目录结构
```
Astra/
├── Astra-CacheServer/    # Redis兼容缓存服务
│   ├── platform/         # 平台相关代码
│   │   └── windows/      # Windows服务模式实现
│   ├── sdk/              # 客户端SDK（多种语言实现）
│   └── ...               # 服务器核心代码
├── core/                 # 核心类型定义
├── utils/                # 工具类（日志/ScopeGuard）
├── concurrent/           # 并发支持（线程池/TaskQueue）
├── datastructures/       # 数据结构（LRU/无锁队列）
├── tests/                # GTest单元测试
├── third-party/          # 第三方依赖（Asio/fmt）
└── benchmark/            # 性能测试

```

### 常见问题

#### Q: 为什么会出现 "Failed to send response: 远程主机强迫关闭了一个现有的连接"?

**A:** 这是因为客户端在调用 `DEL` 方法后立即退出了，导致服务器发送的消息被认为"丢失"了，因为'DEL'本身会返回一个'OK'。但这完全不影响程序的正常运行。客户端本身不需要一直阻塞以等待服务器确认删除数据的成功与否。正常情况下删除操作也不会失败。

## 贡献指南
请通过 [CONTRIBUTING.md](CONTRIBUTING.md) 获取完整的贡献指南，包含代码规范、提交要求和审查流程。

## 问题反馈
请通过 [GitHub Issues](https://github.com/caomengxuan666/Astra/issues) 提交 bug 报告或功能请求

## 许可证
本项目采用 MIT License - 详见 [LICENSE](LICENSE) 文件