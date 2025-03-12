#pragma once

#include "connection.h"
#include "pool_config.h"
#include "types.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace StarryChat::orm {

/**
 * @brief 数据库连接池类
 *
 * 管理数据库连接的创建、获取和回收，支持连接生命周期管理、
 * 连接验证和空闲连接清理等功能。
 */
class ConnectionPool : public std::enable_shared_from_this<ConnectionPool> {
 public:
  // 构造函数，使用连接池配置初始化
  explicit ConnectionPool(PoolConfigPtr config);

  // 析构函数，释放所有连接
  ~ConnectionPool();

  // 初始化连接池
  void initialize();

  // 获取连接池配置
  PoolConfigPtr getConfig() const;

  // 获取连接（阻塞直到获取成功或超时）
  ConnectionPtr getConnection();

  // 获取连接（带超时时间）
  ConnectionPtr getConnection(int timeoutMs);

  // 归还连接（由Connection对象析构时自动调用）
  void releaseConnection(Connection* connection);

  // 关闭连接池，释放所有连接
  void close();

  // 获取连接池状态
  int getActiveConnections() const;
  int getIdleConnections() const;
  int getTotalConnections() const;
  int getConnectionRequestsQueued() const;

  // 获取创建的总连接数和关闭的总连接数
  int getTotalCreatedConnections() const;
  int getTotalClosedConnections() const;

  // 强制创建新连接（用于测试）
  ConnectionPtr createConnection();

  // 预热连接池，创建初始连接
  void warmUp();

  // 清理空闲连接
  void evictIdleConnections();

  // 禁止拷贝
  ConnectionPool(const ConnectionPool&) = delete;
  ConnectionPool& operator=(const ConnectionPool&) = delete;

 private:
  // 打包的连接对象，包含创建时间和最后使用时间等信息
  struct PooledConnection {
    ConnectionPtr connection;
    std::chrono::steady_clock::time_point creationTime;
    std::chrono::steady_clock::time_point lastUsedTime;
    bool isBroken;

    explicit PooledConnection(ConnectionPtr conn);
    ~PooledConnection() = default;
  };

  using PooledConnectionPtr = std::shared_ptr<PooledConnection>;

  // 连接池配置
  PoolConfigPtr config_;

  // 空闲连接队列
  std::deque<PooledConnectionPtr> idleConnections_;

  // 活跃连接集合
  std::unordered_set<PooledConnectionPtr> activeConnections_;

  // 连接计数器
  std::atomic<int> totalConnections_;
  std::atomic<int> totalCreatedConnections_;
  std::atomic<int> totalClosedConnections_;

  // 等待队列大小
  std::atomic<int> waitingCount_;

  // 是否已关闭
  std::atomic<bool> isClosed_;

  // 保护连接池状态的互斥锁
  mutable std::mutex mutex_;

  // 等待连接的条件变量
  std::condition_variable condition_;

  // 空闲连接清理线程
  std::thread evictionThread_;
  std::mutex evictionMutex_;
  std::condition_variable evictionCondition_;
  std::atomic<bool> stopEviction_;

  // 创建新连接
  PooledConnectionPtr createNewConnection();

  // 验证连接有效性
  bool validateConnection(PooledConnectionPtr& conn);

  // 处理连接获取超时
  ConnectionPtr handleTimeout();

  // 关闭并移除连接
  void closeAndRemoveConnection(PooledConnectionPtr& conn);

  // 空闲连接清理线程函数
  void evictionThreadFunc();

  // 获取当前连接池大小
  int getPoolSize() const;
};

}  // namespace StarryChat::orm
