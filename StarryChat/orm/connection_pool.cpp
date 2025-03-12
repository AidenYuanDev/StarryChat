#include <algorithm>
#include <mariadb/conncpp/Driver.hpp>
#include <random>
#include "connection_pool.h"
#include "logging.h"
#include "result_set.h"

using namespace StarryChat::orm;

// PooledConnection 构造函数实现
ConnectionPool::PooledConnection::PooledConnection(ConnectionPtr conn)
    : connection(std::move(conn)),
      creationTime(std::chrono::steady_clock::now()),
      lastUsedTime(creationTime),
      isBroken(false) {}

// ConnectionPool 构造函数实现
ConnectionPool::ConnectionPool(PoolConfigPtr config)
    : config_(std::move(config)),
      totalConnections_(0),
      totalCreatedConnections_(0),
      totalClosedConnections_(0),
      waitingCount_(0),
      isClosed_(false),
      stopEviction_(false) {
  if (!config_) {
    LOG_ERROR << "Cannot create ConnectionPool with null config";
    throw std::invalid_argument("Config pointer cannot be null");
  }

  if (!config_->validate()) {
    LOG_ERROR << "Invalid connection pool configuration";
    throw std::invalid_argument("Invalid connection pool configuration");
  }

  // 启动空闲连接清理线程
  if (config_->getIdleTimeout() > 0) {
    stopEviction_ = false;
    evictionThread_ = std::thread(&ConnectionPool::evictionThreadFunc, this);
    LOG_INFO << "Connection eviction thread started";
  }

  LOG_INFO << "Connection pool created with min=" << config_->getMinPoolSize()
           << ", max=" << config_->getMaxPoolSize()
           << ", initial=" << config_->getInitialPoolSize();
}

ConnectionPool::~ConnectionPool() {
  try {
    close();
  } catch (const std::exception& ex) {
    LOG_ERROR << "Exception during connection pool destruction: " << ex.what();
  } catch (...) {
    LOG_ERROR << "Unknown exception during connection pool destruction";
  }
}

void ConnectionPool::initialize() {
  if (isClosed_) {
    LOG_ERROR << "Cannot initialize closed connection pool";
    throw std::runtime_error("Connection pool is closed");
  }

  warmUp();
}

PoolConfigPtr ConnectionPool::getConfig() const {
  return config_;
}

ConnectionPtr ConnectionPool::getConnection() {
  return getConnection(config_->getConnectionTimeout());
}

ConnectionPtr ConnectionPool::getConnection(int timeoutMs) {
  if (isClosed_) {
    LOG_ERROR << "Cannot get connection from closed pool";
    throw std::runtime_error("Connection pool is closed");
  }

  // 记录请求开始时间
  auto start = std::chrono::steady_clock::now();
  auto end = start + std::chrono::milliseconds(timeoutMs);

  // 增加等待计数
  ++waitingCount_;

  try {
    std::unique_lock<std::mutex> lock(mutex_);

    // 等待连接可用或超时
    while (true) {
      // 检查是否有空闲连接
      if (!idleConnections_.empty()) {
        // 获取一个空闲连接
        auto pooledConn = std::move(idleConnections_.front());
        idleConnections_.pop_front();

        // 验证连接有效性
        if (config_->getTestOnBorrow() && !validateConnection(pooledConn)) {
          LOG_WARN << "Connection validation failed, creating new connection";
          closeAndRemoveConnection(pooledConn);
          continue;
        }

        // 更新最后使用时间并添加到活跃连接集合
        pooledConn->lastUsedTime = std::chrono::steady_clock::now();
        activeConnections_.insert(pooledConn);

        --waitingCount_;
        return pooledConn->connection;
      }

      // 如果池未满，尝试创建新连接
      if (getPoolSize() < config_->getMaxPoolSize()) {
        try {
          auto pooledConn = createNewConnection();
          activeConnections_.insert(pooledConn);

          --waitingCount_;
          return pooledConn->connection;
        } catch (const std::exception& ex) {
          LOG_ERROR << "Failed to create new connection: " << ex.what();
          // 继续等待或超时
        }
      }

      // 检查是否超时
      if (timeoutMs <= 0) {
        // 无限等待
        condition_.wait(lock);
      } else {
        // 有超时限制
        auto now = std::chrono::steady_clock::now();
        if (now >= end) {
          LOG_ERROR << "Connection request timed out after " << timeoutMs
                    << "ms";
          --waitingCount_;
          throw std::runtime_error("Connection request timed out");
        }

        // 等待直到超时或有连接可用
        condition_.wait_until(lock, end);
      }

      // 再次检查池是否已关闭
      if (isClosed_) {
        LOG_ERROR << "Pool was closed while waiting for connection";
        --waitingCount_;
        throw std::runtime_error("Connection pool is closed");
      }
    }
  } catch (...) {
    // 确保在异常情况下减少等待计数
    --waitingCount_;
    throw;
  }

  // 这里不应该到达，但为了编译器不警告，返回nullptr
  return nullptr;
}

void ConnectionPool::releaseConnection(Connection* connection) {
  if (!connection) {
    LOG_WARN << "Attempted to release null connection";
    return;
  }

  if (isClosed_) {
    LOG_DEBUG
        << "Releasing connection to closed pool, connection will be destroyed";
    return;
  }

  std::unique_lock<std::mutex> lock(mutex_);

  // 在活跃连接中查找这个连接
  auto it = std::find_if(activeConnections_.begin(), activeConnections_.end(),
                         [connection](const PooledConnectionPtr& conn) {
                           return conn->connection.get() == connection;
                         });

  if (it == activeConnections_.end()) {
    LOG_WARN << "Attempted to release a connection not owned by this pool";
    return;
  }

  auto pooledConn = *it;
  activeConnections_.erase(it);

  // 检查连接是否应该被关闭
  bool shouldClose = false;

  // 检查连接有效性
  if (config_->getTestOnReturn() && !validateConnection(pooledConn)) {
    LOG_WARN << "Connection failed validation on return, will close it";
    shouldClose = true;
  }

  // 检查连接是否超过最大生命周期
  auto now = std::chrono::steady_clock::now();
  auto lifetime = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now - pooledConn->creationTime)
                      .count();

  if (config_->getMaxLifetime() > 0 && lifetime > config_->getMaxLifetime()) {
    LOG_DEBUG << "Connection exceeded max lifetime, will close it";
    shouldClose = true;
  }

  // 检查连接池是否超过最小大小且有等待请求
  if (getPoolSize() > config_->getMinPoolSize() &&
      idleConnections_.size() >= config_->getMinPoolSize()) {
    // 随机关闭一些连接以防止连接全部集中在同一时间过期
    static std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dis(1, 10);
    if (dis(gen) == 1) {
      LOG_DEBUG << "Randomly closing connection to prevent mass expiration";
      shouldClose = true;
    }
  }

  if (shouldClose) {
    closeAndRemoveConnection(pooledConn);
  } else {
    // 更新最后使用时间并放回空闲队列
    pooledConn->lastUsedTime = now;
    idleConnections_.push_back(std::move(pooledConn));
  }

  // 通知等待中的线程
  lock.unlock();
  condition_.notify_one();
}

void ConnectionPool::close() {
  LOG_INFO << "Closing connection pool";

  // 标记池为已关闭
  isClosed_ = true;

  // 停止空闲连接清理线程
  if (evictionThread_.joinable()) {
    stopEviction_ = true;
    evictionCondition_.notify_all();
    evictionThread_.join();
    LOG_DEBUG << "Connection eviction thread stopped";
  }

  // 关闭所有连接
  std::unique_lock<std::mutex> lock(mutex_);

  // 关闭活跃连接
  for (auto& conn : activeConnections_) {
    try {
      LOG_DEBUG << "Closing active connection";
      conn->isBroken = true;
      // 对活跃连接调用finalizer但不关闭，因为可能仍在使用
      if (config_->getConnectionFinalizer()) {
        config_->getConnectionFinalizer()(conn->connection->getRawConnection());
      }
    } catch (const std::exception& ex) {
      LOG_ERROR << "Exception while finalizing active connection: "
                << ex.what();
    }
  }

  // 清空活跃连接集合
  activeConnections_.clear();

  // 关闭空闲连接
  while (!idleConnections_.empty()) {
    auto conn = std::move(idleConnections_.front());
    idleConnections_.pop_front();

    try {
      LOG_DEBUG << "Closing idle connection";
      closeAndRemoveConnection(conn);
    } catch (const std::exception& ex) {
      LOG_ERROR << "Exception while closing idle connection: " << ex.what();
    }
  }

  // 重置计数器
  totalConnections_ = 0;

  // 通知所有等待的线程
  lock.unlock();
  condition_.notify_all();

  LOG_INFO << "Connection pool closed. Total created: "
           << totalCreatedConnections_
           << ", total closed: " << totalClosedConnections_;
}

int ConnectionPool::getActiveConnections() const {
  std::unique_lock<std::mutex> lock(mutex_);
  return activeConnections_.size();
}

int ConnectionPool::getIdleConnections() const {
  std::unique_lock<std::mutex> lock(mutex_);
  return idleConnections_.size();
}

int ConnectionPool::getTotalConnections() const {
  return totalConnections_;
}

int ConnectionPool::getConnectionRequestsQueued() const {
  return waitingCount_;
}

int ConnectionPool::getTotalCreatedConnections() const {
  return totalCreatedConnections_;
}

int ConnectionPool::getTotalClosedConnections() const {
  return totalClosedConnections_;
}

ConnectionPtr ConnectionPool::createConnection() {
  auto pooledConn = createNewConnection();

  std::unique_lock<std::mutex> lock(mutex_);
  activeConnections_.insert(pooledConn);
  return pooledConn->connection;
}

void ConnectionPool::warmUp() {
  LOG_INFO << "Warming up connection pool with "
           << config_->getInitialPoolSize() << " connections";

  std::unique_lock<std::mutex> lock(mutex_);

  try {
    // 创建初始连接
    for (int i = 0; i < config_->getInitialPoolSize(); ++i) {
      try {
        auto conn = createNewConnection();
        idleConnections_.push_back(std::move(conn));
      } catch (const std::exception& ex) {
        LOG_ERROR << "Failed to create connection during warmup: " << ex.what();
        // 继续尝试创建其他连接
      }
    }
  } catch (const std::exception& ex) {
    LOG_ERROR << "Exception during connection pool warmup: " << ex.what();
    throw;
  }

  LOG_INFO << "Connection pool warmup completed with "
           << idleConnections_.size() << " connections";
}

void ConnectionPool::evictIdleConnections() {
  if (isClosed_) {
    return;
  }

  std::unique_lock<std::mutex> lock(mutex_);

  auto now = std::chrono::steady_clock::now();
  auto idleTimeout = config_->getIdleTimeout();
  auto maxLifetime = config_->getMaxLifetime();

  // 保持至少最小池大小的连接
  int minSize = config_->getMinPoolSize();
  int extraConnections = getPoolSize() - minSize;

  if (extraConnections <= 0) {
    return;
  }

  LOG_DEBUG << "Checking for idle connections to evict. Current pool size: "
            << getPoolSize() << ", min size: " << minSize
            << ", extra: " << extraConnections;

  std::vector<PooledConnectionPtr> connectionsToClose;

  auto it = idleConnections_.begin();
  while (it != idleConnections_.end() && extraConnections > 0) {
    bool shouldRemove = false;

    // 检查空闲超时
    if (idleTimeout > 0) {
      auto idleTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now - (*it)->lastUsedTime)
                          .count();

      if (idleTime > idleTimeout) {
        LOG_DEBUG << "Connection idle for " << idleTime
                  << "ms, exceeds timeout of " << idleTimeout << "ms";
        shouldRemove = true;
      }
    }

    // 检查生命周期
    if (maxLifetime > 0) {
      auto lifetime = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now - (*it)->creationTime)
                          .count();

      if (lifetime > maxLifetime) {
        LOG_DEBUG << "Connection lifetime is " << lifetime
                  << "ms, exceeds max of " << maxLifetime << "ms";
        shouldRemove = true;
      }
    }

    // 如果应该移除
    if (shouldRemove) {
      connectionsToClose.push_back(std::move(*it));
      it = idleConnections_.erase(it);
      extraConnections--;
    } else {
      ++it;
    }
  }

  lock.unlock();

  // 关闭需要移除的连接
  for (auto& conn : connectionsToClose) {
    try {
      closeAndRemoveConnection(conn);
    } catch (const std::exception& ex) {
      LOG_ERROR << "Exception while closing idle connection: " << ex.what();
    }
  }

  if (!connectionsToClose.empty()) {
    LOG_INFO << "Evicted " << connectionsToClose.size() << " idle connections. "
             << "New pool size: " << getPoolSize();
  }
}

ConnectionPool::PooledConnectionPtr ConnectionPool::createNewConnection() {
  try {
    LOG_DEBUG << "Creating new database connection";

    sql::Driver* driver = sql::mariadb::get_driver_instance();
    sql::SQLString url;

    if (!config_->getUrl().empty()) {
      url = config_->getUrl().c_str();
    } else {
      url = config_->buildConnectionUrl().c_str();
    }

    sql::Properties properties;

    if (config_->getUrl().empty()) {
      // 如果没有URL，则设置连接属性
      properties["user"] = config_->getUsername().c_str();
      properties["password"] = config_->getPassword().c_str();

      if (!config_->getCharset().empty()) {
        properties["characterEncoding"] = config_->getCharset().c_str();
      }

      if (config_->getAutoReconnect()) {
        properties["autoReconnect"] = "true";
      }
    }

    // 创建MariaDB连接
    SqlConnectionPtr sqlConn;

    try {
      if (config_->getUrl().empty()) {
        // 构建连接字符串
        sql::SQLString host = config_->getHost().c_str();
        sql::SQLString user = config_->getUsername().c_str();
        sql::SQLString password = config_->getPassword().c_str();
        sql::SQLString database = config_->getDatabase().c_str();

        // 使用正确的connect方法签名
        sqlConn.reset(driver->connect(host, user, password));

        // 如果有数据库名，设置当前数据库
        if (!config_->getDatabase().empty()) {
          sqlConn->setSchema(database);
        }

      } else {
        sqlConn.reset(driver->connect(url, properties));
      }
    } catch (const SqlException& ex) {
      LOG_ERROR << "Failed to connect to database: " << ex.what();
      throw;
    }

    // 设置自动提交模式
    sqlConn->setAutoCommit(config_->getAutoCommit());

    // 创建我们的连接封装
    auto connection = std::make_shared<Connection>(std::move(sqlConn));
    auto pooledConn = std::make_shared<PooledConnection>(std::move(connection));

    // 更新计数器
    ++totalConnections_;
    ++totalCreatedConnections_;

    LOG_DEBUG << "New connection created successfully. Total: "
              << totalConnections_;

    return pooledConn;
  } catch (const std::exception& ex) {
    LOG_ERROR << "Failed to create database connection: " << ex.what();
    throw;
  }
}

bool ConnectionPool::validateConnection(PooledConnectionPtr& conn) {
  if (!conn || !conn->connection) {
    return false;
  }

  try {
    // 使用自定义验证器
    if (config_->getConnectionValidator()) {
      return config_->getConnectionValidator()(
          conn->connection->getRawConnection());
    }

    // 默认验证：执行测试查询
    auto result = conn->connection->executeQuery(config_->getTestQuery());
    return result && result->next();
  } catch (const std::exception& ex) {
    LOG_WARN << "Connection validation failed: " << ex.what();
    conn->isBroken = true;
    return false;
  }
}

void ConnectionPool::closeAndRemoveConnection(PooledConnectionPtr& conn) {
  if (!conn || !conn->connection) {
    return;
  }

  try {
    // 调用连接终结器
    if (config_->getConnectionFinalizer()) {
      config_->getConnectionFinalizer()(conn->connection->getRawConnection());
    }

    // 更新计数器
    --totalConnections_;
    ++totalClosedConnections_;

    LOG_DEBUG << "Connection closed. Total: " << totalConnections_;
  } catch (const std::exception& ex) {
    LOG_ERROR << "Exception while closing connection: " << ex.what();
  }

  // 清空连接指针，会触发Connection的析构
  conn->connection.reset();
}

void ConnectionPool::evictionThreadFunc() {
  LOG_DEBUG << "Connection eviction thread started";

  int checkInterval = std::min(config_->getIdleTimeout() / 2, 30000);
  if (checkInterval < 1000) {
    checkInterval = 1000;  // 最小1秒
  }

  while (!stopEviction_) {
    LOG_DEBUG << "Eviction thread running idle connection check";

    try {
      evictIdleConnections();
    } catch (const std::exception& ex) {
      LOG_ERROR << "Exception in connection eviction thread: " << ex.what();
    }

    // 等待直到下次检查时间或被通知停止
    std::unique_lock<std::mutex> lock(evictionMutex_);
    evictionCondition_.wait_for(lock, std::chrono::milliseconds(checkInterval),
                                [this] { return stopEviction_.load(); });
  }

  LOG_DEBUG << "Connection eviction thread exiting";
}

int ConnectionPool::getPoolSize() const {
  return activeConnections_.size() + idleConnections_.size();
}
