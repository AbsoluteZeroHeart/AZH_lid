# AZH_lid
# 高性能C++ Reactor模式TCP服务器框架
一个高性能的现代C++ TCP服务器框架，采用Reactor模式实现，具有事件驱动的IO、线程池管理以及内置的空闲连接超时处理功能。

## 核心特性
### 🚀 高性能设计
- **Reactor事件驱动模型**：基于epoll（Linux）实现事件循环，支持ET/LT模式，高效处理大量并发连接
- **非阻塞IO**：全链路非阻塞socket操作，结合socket flag（SOCK_NONBLOCK/SOCK_CLOEXEC）减少系统调用
- **线程池负载均衡**：IO线程池采用Round-Robin策略分发连接，充分利用多核CPU资源
- **fd资源保护**：内置idle fd机制处理EMFILE/ENFILE错误，避免fd耗尽导致服务不可用

### 🛠️ 完善的连接管理
- **自动空闲超时**：基于时间轮（Time Wheel）实现高效的空闲连接检测，支持自定义超时时间
- **安全的连接生命周期**：使用std::shared_ptr/std::weak_ptr管理连接资源，避免野指针和double free
- **优雅关闭**：支持半关闭（SHUT_WR）、连接状态原子管理，保证资源正确释放
- **连接统计**：提供连接数、空闲连接数等实时统计接口

### 🎯 易用性与可扩展性
- **现代C++特性**：基于C++17实现，使用智能指针、lambda、原子操作等现代特性
- **跨平台兼容**：针对Linux做优化，同时兼容类Unix系统（自动适配accept4/accept）
- **灵活的回调机制**：支持连接建立、消息接收、连接关闭等自定义回调
- **线程安全设计**：关键操作使用互斥锁/原子变量，支持跨线程安全调用

### 📝 完善的日志与调试
- **分级日志**：支持INFO/WARN/ERROR/DEBUG级别的日志输出，便于问题定位
- **线程命名**：Linux下自动设置线程名称，方便调试和性能分析
- **详细的错误处理**：捕获并记录系统调用错误，提供友好的错误信息

## 核心组件
| 组件 | 功能描述 |
|------|----------|
| `EventLoop` | 事件循环核心，管理epoll事件、处理IO回调、执行跨线程任务 |
| `Epoll` | epoll系统调用封装，提供add/mod/del/poll接口 |
| `Channel` | IO事件通道，关联fd和事件回调，是Reactor模式的核心载体 |
| `TcpServer` | 服务器核心类，管理Acceptor、线程池、连接生命周期 |
| `Acceptor` | 监听端口并接受新连接，负责socket创建、绑定、监听 |
| `TcpConnection` | 单个TCP连接的封装，处理读写事件、连接状态管理 |
| `EventLoopThreadPool` | IO线程池，管理多个EventLoop实例，实现连接分发 |
| `ConnectionTimeoutManager` | 基于时间轮的空闲连接管理器，高效检测超时连接 |

## 快速开始
### 编译要求
- C++17及以上编译器（GCC 8+/Clang 7+/MSVC 2019+）
- Linux/类Unix系统（epoll依赖）
- 支持pthread库

### 简单示例
```cpp
#include "TcpServer.hpp"
#include "EventLoop.hpp"
#include <iostream>

int main() {
    // 创建主线程EventLoop
    EventLoop base_loop;
    
    // 创建TCP服务器（绑定127.0.0.1:8080，4个IO线程）
    TcpServer server(&base_loop, "127.0.0.1", 8080, 4, "DemoServer");
    
    // 设置空闲连接超时（30秒）
    server.set_idle_timeout(30000);
    server.enable_idle_timeout(true);
    
    // 设置连接回调
    server.set_connected_callback([](const TcpConnectionPtr& conn) {
        std::cout << "New connection: " << conn->peer_ipport() << std::endl;
    });
    
    // 设置消息回调
    server.set_message_callback([](const TcpConnectionPtr& conn, InputBuffer& buf) {
        // 回显数据
        std::string data(buf.get_from_buf(), buf.length());
        conn->send(data.c_str(), data.length());
        buf.retrieve_all(); // 清空缓冲区
    });
    
    // 设置关闭回调
    server.set_close_callback([](const TcpConnectionPtr& conn) {
        std::cout << "Connection closed: " << conn->peer_ipport() << std::endl;
    });
    
    // 启动服务器
    server.start();
    
    // 运行主线程事件循环
    base_loop.loop();
    
    return 0;
}
```

## 架构设计
### 线程模型
- **Base线程**：运行主EventLoop，负责监听端口（Acceptor）、处理信号、管理线程池
- **IO线程**：线程池中的每个线程运行独立的EventLoop，负责处理TCP连接的读写事件
- **超时检测线程**：独立线程运行时间轮，检测并关闭空闲连接

### 数据流向
1. Acceptor在Base线程监听端口，接受新连接
2. 新连接通过Round-Robin策略分发到IO线程
3. IO线程的EventLoop处理连接的读写事件
4. 消息通过回调传递给上层业务逻辑
5. 空闲连接由时间轮检测并自动关闭

## 性能特性
- 支持上万并发连接（受限于系统fd限制）
- 低延迟的事件处理（epoll ET模式）
- 无锁化的连接分发（原子操作）
- 高效的内存管理（智能指针+对象池思想）

## 适用场景
- 高并发TCP服务端开发
- 游戏服务器
- 物联网设备通信服务
- 高性能API网关
- 实时数据传输服务

## 开源协议
本项目采用MIT开源协议，您可以自由使用、修改和分发，商业用途也无需授权。

## 贡献指南
1. Fork本仓库
2. 创建特性分支 (`git checkout -b feature/amazing-feature`)
3. 提交变更 (`git commit -m 'Add some amazing feature'`)
4. 推送到分支 (`git push origin feature/amazing-feature`)
5. 创建Pull Request

## 致谢
- 参考muduo、libevent等经典网络库的设计思想
- 采用现代C++最佳实践，注重代码的可维护性和性能平衡
