#include "logging.h"
#include "pool_config.h"

using namespace StarryChat::orm;

PoolConfig::PoolConfig()
    : host_("localhost"),
      port_(3306),
      database_(""),
      username_(""),
      password_(""),
      charset_("utf8mb4"),
      url_(""),
      minPoolSize_(DEFAULT_MIN_POOL_SIZE),
      maxPoolSize_(DEFAULT_MAX_POOL_SIZE),
      initialPoolSize_(DEFAULT_MIN_POOL_SIZE),
      queueSize_(DEFAULT_QUEUE_SIZE),
      connectionTimeout_(DEFAULT_CONNECTION_TIMEOUT),
      idleTimeout_(DEFAULT_IDLE_TIMEOUT),
      maxLifetime_(DEFAULT_MAX_LIFETIME),
      testQuery_("SELECT 1"),
      testOnBorrow_(true),
      testOnReturn_(false),
      testWhileIdle_(true),
      autoCommit_(true),
      autoReconnect_(true),
      maxRetries_(3),
      connectionValidator_([](SqlConnection* conn) -> bool {
        try {
          return conn && conn->isValid();
        } catch (...) {
          return false;
        }
      }),
      connectionFinalizer_([](SqlConnection*) {
        // 默认不做任何操作
      }) {}

// 连接参数设置
PoolConfig& PoolConfig::setHost(const std::string& host) {
  host_ = host;
  url_ = "";  // 清除URL以确保优先使用分开的连接参数
  return *this;
}

PoolConfig& PoolConfig::setPort(int port) {
  if (port <= 0 || port > 65535) {
    LOG_WARN << "Invalid port number: " << port << ", using default: " << port_;
  } else {
    port_ = port;
    url_ = "";  // 清除URL
  }
  return *this;
}

PoolConfig& PoolConfig::setDatabase(const std::string& database) {
  database_ = database;
  url_ = "";  // 清除URL
  return *this;
}

PoolConfig& PoolConfig::setUsername(const std::string& username) {
  username_ = username;
  url_ = "";  // 清除URL
  return *this;
}

PoolConfig& PoolConfig::setPassword(const std::string& password) {
  password_ = password;
  url_ = "";  // 清除URL
  return *this;
}

PoolConfig& PoolConfig::setCharset(const std::string& charset) {
  charset_ = charset;
  url_ = "";  // 清除URL
  return *this;
}

PoolConfig& PoolConfig::setUrl(const std::string& url) {
  url_ = url;
  return *this;
}

// 池大小设置
PoolConfig& PoolConfig::setMinPoolSize(int size) {
  if (size < 0) {
    LOG_WARN << "Invalid min pool size: " << size
             << ", using default: " << minPoolSize_;
  } else {
    minPoolSize_ = size;
    if (minPoolSize_ > maxPoolSize_) {
      LOG_WARN << "Min pool size " << minPoolSize_
               << " is greater than max pool size " << maxPoolSize_
               << ", setting max pool size to " << minPoolSize_;
      maxPoolSize_ = minPoolSize_;
    }
  }
  return *this;
}

PoolConfig& PoolConfig::setMaxPoolSize(int size) {
  if (size <= 0) {
    LOG_WARN << "Invalid max pool size: " << size
             << ", using default: " << maxPoolSize_;
  } else {
    maxPoolSize_ = size;
    if (minPoolSize_ > maxPoolSize_) {
      LOG_WARN << "Min pool size " << minPoolSize_
               << " is greater than max pool size " << maxPoolSize_
               << ", setting min pool size to " << maxPoolSize_;
      minPoolSize_ = maxPoolSize_;
    }
  }
  return *this;
}

PoolConfig& PoolConfig::setInitialPoolSize(int size) {
  if (size < 0) {
    LOG_WARN << "Invalid initial pool size: " << size
             << ", using default: " << initialPoolSize_;
  } else {
    initialPoolSize_ = size;
    if (initialPoolSize_ > maxPoolSize_) {
      LOG_WARN << "Initial pool size " << initialPoolSize_
               << " is greater than max pool size " << maxPoolSize_
               << ", setting initial pool size to " << maxPoolSize_;
      initialPoolSize_ = maxPoolSize_;
    }
  }
  return *this;
}

PoolConfig& PoolConfig::setQueueSize(int size) {
  if (size <= 0) {
    LOG_WARN << "Invalid queue size: " << size
             << ", using default: " << queueSize_;
  } else {
    queueSize_ = size;
  }
  return *this;
}

// 超时设置
PoolConfig& PoolConfig::setConnectionTimeout(int milliseconds) {
  if (milliseconds < 0) {
    LOG_WARN << "Invalid connection timeout: " << milliseconds
             << ", using default: " << connectionTimeout_;
  } else {
    connectionTimeout_ = milliseconds;
  }
  return *this;
}

PoolConfig& PoolConfig::setIdleTimeout(int milliseconds) {
  if (milliseconds < 0) {
    LOG_WARN << "Invalid idle timeout: " << milliseconds
             << ", using default: " << idleTimeout_;
  } else {
    idleTimeout_ = milliseconds;
  }
  return *this;
}

PoolConfig& PoolConfig::setMaxLifetime(int milliseconds) {
  if (milliseconds < 0) {
    LOG_WARN << "Invalid max lifetime: " << milliseconds
             << ", using default: " << maxLifetime_;
  } else {
    maxLifetime_ = milliseconds;
  }
  return *this;
}

// 连接测试设置
PoolConfig& PoolConfig::setTestQuery(const std::string& query) {
  testQuery_ = query;
  return *this;
}

PoolConfig& PoolConfig::setTestOnBorrow(bool test) {
  testOnBorrow_ = test;
  return *this;
}

PoolConfig& PoolConfig::setTestOnReturn(bool test) {
  testOnReturn_ = test;
  return *this;
}

PoolConfig& PoolConfig::setTestWhileIdle(bool test) {
  testWhileIdle_ = test;
  return *this;
}

// 行为设置
PoolConfig& PoolConfig::setAutoCommit(bool autoCommit) {
  autoCommit_ = autoCommit;
  return *this;
}

PoolConfig& PoolConfig::setAutoReconnect(bool autoReconnect) {
  autoReconnect_ = autoReconnect;
  return *this;
}

PoolConfig& PoolConfig::setMaxRetries(int retries) {
  if (retries < 0) {
    LOG_WARN << "Invalid max retries: " << retries
             << ", using default: " << maxRetries_;
  } else {
    maxRetries_ = retries;
  }
  return *this;
}

// 连接验证和清理
PoolConfig& PoolConfig::setConnectionValidator(ConnectionValidator validator) {
  if (validator) {
    connectionValidator_ = std::move(validator);
  } else {
    LOG_WARN << "Null connection validator provided, using default";
  }
  return *this;
}

PoolConfig& PoolConfig::setConnectionFinalizer(ConnectionFinalizer finalizer) {
  if (finalizer) {
    connectionFinalizer_ = std::move(finalizer);
  } else {
    LOG_WARN << "Null connection finalizer provided, using default";
  }
  return *this;
}

// 获取配置参数
std::string PoolConfig::getHost() const {
  return host_;
}

int PoolConfig::getPort() const {
  return port_;
}

std::string PoolConfig::getDatabase() const {
  return database_;
}

std::string PoolConfig::getUsername() const {
  return username_;
}

std::string PoolConfig::getPassword() const {
  return password_;
}

std::string PoolConfig::getCharset() const {
  return charset_;
}

std::string PoolConfig::getUrl() const {
  return url_;
}

int PoolConfig::getMinPoolSize() const {
  return minPoolSize_;
}

int PoolConfig::getMaxPoolSize() const {
  return maxPoolSize_;
}

int PoolConfig::getInitialPoolSize() const {
  return initialPoolSize_;
}

int PoolConfig::getQueueSize() const {
  return queueSize_;
}

int PoolConfig::getConnectionTimeout() const {
  return connectionTimeout_;
}

int PoolConfig::getIdleTimeout() const {
  return idleTimeout_;
}

int PoolConfig::getMaxLifetime() const {
  return maxLifetime_;
}

std::string PoolConfig::getTestQuery() const {
  return testQuery_;
}

bool PoolConfig::getTestOnBorrow() const {
  return testOnBorrow_;
}

bool PoolConfig::getTestOnReturn() const {
  return testOnReturn_;
}

bool PoolConfig::getTestWhileIdle() const {
  return testWhileIdle_;
}

bool PoolConfig::getAutoCommit() const {
  return autoCommit_;
}

bool PoolConfig::getAutoReconnect() const {
  return autoReconnect_;
}

int PoolConfig::getMaxRetries() const {
  return maxRetries_;
}

ConnectionValidator PoolConfig::getConnectionValidator() const {
  return connectionValidator_;
}

ConnectionFinalizer PoolConfig::getConnectionFinalizer() const {
  return connectionFinalizer_;
}

std::string PoolConfig::buildConnectionUrl() const {
  // 如果已有URL，则直接返回
  if (!url_.empty()) {
    return url_;
  }

  // 构建连接URL
  std::stringstream url;
  url << "jdbc:mariadb://";
  url << host_ << ":" << port_;

  if (!database_.empty()) {
    url << "/" << database_;
  }

  // 添加基本参数
  url << "?user=" << username_;
  url << "&password=" << password_;
  url << "&charset=" << charset_;

  // 添加其他连接参数
  if (autoReconnect_) {
    url << "&autoReconnect=true";
  }

  return url.str();
}

bool PoolConfig::validate() const {
  bool valid = true;

  // 检查必要的连接参数
  if (url_.empty()) {
    if (host_.empty()) {
      LOG_ERROR << "Host is not set";
      valid = false;
    }

    if (username_.empty()) {
      LOG_ERROR << "Username is not set";
      valid = false;
    }
  }

  // 检查池大小设置
  if (minPoolSize_ > maxPoolSize_) {
    LOG_ERROR << "Min pool size " << minPoolSize_
              << " is greater than max pool size " << maxPoolSize_;
    valid = false;
  }

  if (initialPoolSize_ > maxPoolSize_) {
    LOG_ERROR << "Initial pool size " << initialPoolSize_
              << " is greater than max pool size " << maxPoolSize_;
    valid = false;
  }

  return valid;
}

PoolConfigPtr PoolConfig::getDefaultConfig() {
  return std::make_shared<PoolConfig>();
}
