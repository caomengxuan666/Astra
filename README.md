# Astra - 高性能Redis兼容缓存中间件

[![Build Status](https://example.com/build-status)](https://example.com)
[![License](https://img.shields.io/github/license/cmxAstra/Astra)](https://github.com/cmxAstra/Astra/blob/main/LICENSE)

## 项目概述
Astra 是一个基于 C++17 的高性能 Redis 兼容缓存中间件，采用模块化设计实现以下核心价值：
- 提供线程安全的 LRU 缓存实现
- 支持 Redis 协议的网络通信
- 实现命令模式的客户端 SDK
- 支持 TTL 过期机制与后台清理
- 提供分片锁机制提升并发性能

## 系统架构
![Architecture](docs/architecture.png)

### 核心模块
| 模块 | 功能 | 技术实现 |
|-------|-------|-------|
| **Core** | 基础类型定义与宏 | C++17 特性
| **Utils** | 日志系统/范围保护 | RAII 模式
| **DataStructures** | 无锁队列/LRU 缓存 | CAS 原子操作
| **concurrent** | 并发支持框架 | 线程池/任务队列/任务流
| **CacheServer** | Redis 协议解析 | Asio 异步 IO
| **Client SDK** | 命令模式封装 | 多态设计

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

```
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
```

### 启动服务
```bash
# 启动缓存服务器
$ ./build/bin/Astra-CacheServer 6379

# 运行示例客户端
$ ./build/bin/example_client
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
}
```

## 目录结构
```
Astra/
├── Astra-CacheServer/    # Redis兼容缓存服务
├── core/                 # 核心类型定义
├── utils/                # 工具类（日志/ScopeGuard）
├── concurrent/           # 并发支持（线程池/TaskQueue）
├── datastructures/       # 数据结构（LRU/无锁队列）
├── tests/                # 单元测试套件
├── third-party/          # 第三方依赖（Asio/fmt）
└── sdk/                  # 客户端SDK（命令模式实现）
```

## 贡献指南
请通过 [CONTRIBUTING.md](CONTRIBUTING.md) 获取完整的贡献指南，包含代码规范、提交要求和审查流程。

## 问题反馈
请通过 [GitHub Issues](https://github.com/cmxAstra/Astra/issues) 提交 bug 报告或功能请求

## 许可证
本项目采用 MIT License - 详见 [LICENSE](LICENSE) 文件