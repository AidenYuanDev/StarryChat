#include "config.h"
#include "db_manager.h"
#include "logging.h"

namespace StarryChat {

DBManager& DBManager::getInstance() {
  static DBManager instance;
  return instance;
}

bool DBManager::initialize() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (initialized_) {
    return true;  // 已经初始化过，直接返回
  }

  try {
    // 从配置中获取数据库设置
    auto& config = Config::getInstance();

    // 设置连接属性
    connectionProps_["hostName"] = config.getMariaDBHost();
    connectionProps_["port"] = std::to_string(config.getMariaDBPort());
    connectionProps_["userName"] = config.getMariaDBUsername();
    connectionProps_["password"] = config.getMariaDBPassword();
    connectionProps_["schema"] = config.getMariaDBDatabase();

    // 连接池配置
    connectionProps_["pool_max_size"] =
        std::to_string(config.getMariaDBPoolSize());
    connectionProps_["pool_idle_timeout"] = "300";  // 5分钟
    connectionProps_["pool_queue_timeout"] = "30";  // 30秒

    // 获取MariaDB驱动实例
    driver_ = sql::mariadb::get_driver_instance();

    // 测试连接
    auto testConn = getConnection();
    if (!testConn) {
      LOG_ERROR << "Failed to connect to database";
      return false;
    }

    initialized_ = true;
    LOG_INFO << "Database connection initialized successfully";
    return true;

  } catch (sql::SQLException& e) {
    LOG_ERROR << "Database initialization error: " << e.what()
              << ", Error code: " << e.getErrorCode()
              << ", SQL state: " << e.getSQLState();
    return false;
  } catch (std::exception& e) {
    LOG_ERROR << "Database initialization error: " << e.what();
    return false;
  }
}

std::shared_ptr<sql::Connection> DBManager::getConnection() {
  if (!initialized_) {
    LOG_ERROR << "Database not initialized. Call initialize() first.";
    return nullptr;
  }

  try {
    // 使用智能指针管理连接生命周期
    return std::shared_ptr<sql::Connection>(driver_->connect(connectionProps_));
  } catch (sql::SQLException& e) {
    LOG_ERROR << "Error getting database connection: " << e.what()
              << ", Error code: " << e.getErrorCode()
              << ", SQL state: " << e.getSQLState();
    return nullptr;
  }
}

void DBManager::shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (initialized_) {
    // 断开所有连接 - mariadb-connector-c++会处理连接池的关闭
    initialized_ = false;
    LOG_INFO << "Database connections shut down";
  }
}

std::unique_ptr<sql::ResultSet> DBManager::executeQuery(
    const std::string& sql) {
  auto conn = getConnection();
  if (!conn) {
    return nullptr;
  }

  try {
    std::unique_ptr<sql::Statement> stmt(conn->createStatement());
    return std::unique_ptr<sql::ResultSet>(stmt->executeQuery(sql));
  } catch (sql::SQLException& e) {
    LOG_ERROR << "Query execution error: " << e.what() << ", SQL: " << sql;
    return nullptr;
  }
}

int DBManager::executeUpdate(const std::string& sql) {
  auto conn = getConnection();
  if (!conn) {
    return -1;
  }

  try {
    std::unique_ptr<sql::Statement> stmt(conn->createStatement());
    return stmt->executeUpdate(sql);
  } catch (sql::SQLException& e) {
    LOG_ERROR << "Update execution error: " << e.what() << ", SQL: " << sql;
    return -1;
  }
}

std::unique_ptr<sql::PreparedStatement> DBManager::prepareStatement(
    const std::string& sql,
    bool returnGeneratedKeys) {
  auto conn = getConnection();
  if (!conn) {
    return nullptr;
  }

  try {
    if (returnGeneratedKeys) {
      return std::unique_ptr<sql::PreparedStatement>(
          conn->prepareStatement(sql, sql::Statement::RETURN_GENERATED_KEYS));
    } else {
      return std::unique_ptr<sql::PreparedStatement>(
          conn->prepareStatement(sql));
    }
  } catch (sql::SQLException& e) {
    LOG_ERROR << "Prepare statement error: " << e.what() << ", SQL: " << sql;
    return nullptr;
  }
}

}  // namespace StarryChat
