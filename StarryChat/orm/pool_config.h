#pragma once

#include <string>
#include "types.h"

namespace StarryChat::orm {

/**
 * @brief 数据库连接池配置类
 *
 * 用于管理数据库连接池的各项配置参数，包括连接信息、
 * 池大小、超时设置、连接验证和回收等行为配置。
 */
class PoolConfig {
 public:
  // 构造函数，设置默认值
  PoolConfig();

  // 连接参数设置
  PoolConfig& setHost(const std::string& host);
  PoolConfig& setPort(int port);
  PoolConfig& setDatabase(const std::string& database);
  PoolConfig& setUsername(const std::string& username);
  PoolConfig& setPassword(const std::string& password);
  PoolConfig& setCharset(const std::string& charset);

  // 连接URL（可选，与上面的连接参数二选一）
  PoolConfig& setUrl(const std::string& url);

  // 池大小设置
  PoolConfig& setMinPoolSize(int size);
  PoolConfig& setMaxPoolSize(int size);
  PoolConfig& setInitialPoolSize(int size);
  PoolConfig& setQueueSize(int size);

  // 超时设置
  PoolConfig& setConnectionTimeout(int milliseconds);
  PoolConfig& setIdleTimeout(int milliseconds);
  PoolConfig& setMaxLifetime(int milliseconds);

  // 连接测试设置
  PoolConfig& setTestQuery(const std::string& query);
  PoolConfig& setTestOnBorrow(bool test);
  PoolConfig& setTestOnReturn(bool test);
  PoolConfig& setTestWhileIdle(bool test);

  // 行为设置
  PoolConfig& setAutoCommit(bool autoCommit);
  PoolConfig& setAutoReconnect(bool autoReconnect);
  PoolConfig& setMaxRetries(int retries);

  // 连接验证和清理
  PoolConfig& setConnectionValidator(ConnectionValidator validator);
  PoolConfig& setConnectionFinalizer(ConnectionFinalizer finalizer);

  // 获取配置参数
  std::string getHost() const;
  int getPort() const;
  std::string getDatabase() const;
  std::string getUsername() const;
  std::string getPassword() const;
  std::string getCharset() const;
  std::string getUrl() const;

  int getMinPoolSize() const;
  int getMaxPoolSize() const;
  int getInitialPoolSize() const;
  int getQueueSize() const;

  int getConnectionTimeout() const;
  int getIdleTimeout() const;
  int getMaxLifetime() const;

  std::string getTestQuery() const;
  bool getTestOnBorrow() const;
  bool getTestOnReturn() const;
  bool getTestWhileIdle() const;

  bool getAutoCommit() const;
  bool getAutoReconnect() const;
  int getMaxRetries() const;

  ConnectionValidator getConnectionValidator() const;
  ConnectionFinalizer getConnectionFinalizer() const;

  // 生成连接URL（如果未设置）
  std::string buildConnectionUrl() const;

  // 验证配置有效性
  bool validate() const;

  // 获取默认配置
  static PoolConfigPtr getDefaultConfig();

 private:
  // 连接参数
  std::string host_;
  int port_;
  std::string database_;
  std::string username_;
  std::string password_;
  std::string charset_;
  std::string url_;

  // 池大小参数
  int minPoolSize_;
  int maxPoolSize_;
  int initialPoolSize_;
  int queueSize_;

  // 超时参数
  int connectionTimeout_;
  int idleTimeout_;
  int maxLifetime_;

  // 连接测试参数
  std::string testQuery_;
  bool testOnBorrow_;
  bool testOnReturn_;
  bool testWhileIdle_;

  // 行为参数
  bool autoCommit_;
  bool autoReconnect_;
  int maxRetries_;

  // 连接验证和清理
  ConnectionValidator connectionValidator_;
  ConnectionFinalizer connectionFinalizer_;
};

}  // namespace StarryChat::orm
