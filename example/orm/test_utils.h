#pragma once

#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "connection.h"
#include "connection_pool.h"
#include "logging.h"
#include "pool_config.h"
#include "query_builder.h"
#include "transaction.h"

namespace StarryChat::Test {

// 颜色输出辅助（让测试结果更直观）
struct ConsoleColor {
  static constexpr const char* RESET = "\033[0m";
  static constexpr const char* RED = "\033[31m";
  static constexpr const char* GREEN = "\033[32m";
  static constexpr const char* YELLOW = "\033[33m";
  static constexpr const char* BLUE = "\033[34m";
  static constexpr const char* MAGENTA = "\033[35m";
};

// 数据库配置
struct DbConfig {
  static constexpr const char* HOST = "localhost";
  static constexpr int PORT = 3306;
  static constexpr const char* DATABASE = "starrychat_test";
  static constexpr const char* USERNAME = "root";  // 请替换为你的用户名
  static constexpr const char* PASSWORD = "";      // 请替换为你的密码
  static constexpr const char* CHARSET = "utf8mb4";
  static constexpr int MIN_CONNECTIONS = 2;  // 简化版使用更少的连接
  static constexpr int MAX_CONNECTIONS = 5;
};

// 简单的测试状态计数器
static int total_tests = 0;
static int passed_tests = 0;
static int failed_tests = 0;

// 获取单个测试连接
inline orm::ConnectionPtr getTestConnection() {
  try {
    // 使用单例模式确保连接池只创建一次
    static std::shared_ptr<orm::ConnectionPool> pool = nullptr;

    if (!pool) {
      auto config = std::make_shared<orm::PoolConfig>();
      config->setHost(DbConfig::HOST)
          .setPort(DbConfig::PORT)
          .setDatabase(DbConfig::DATABASE)
          .setUsername(DbConfig::USERNAME)
          .setPassword(DbConfig::PASSWORD)
          .setCharset(DbConfig::CHARSET)
          .setMinPoolSize(DbConfig::MIN_CONNECTIONS)
          .setMaxPoolSize(DbConfig::MAX_CONNECTIONS);

      pool = std::make_shared<orm::ConnectionPool>(config);
      pool->initialize();
      std::cout << "Connection pool initialized." << std::endl;
    }

    return pool->getConnection();
  } catch (const std::exception& ex) {
    std::cerr << "Failed to get test connection: " << ex.what() << std::endl;
    throw;
  }
}

// 重置测试数据库
inline void resetDatabase() {
  try {
    auto conn = getTestConnection();

    // 嵌入简化的schema脚本
    std::string sql = R"SQL(
-- 删除已存在的表（如果有）
DROP TABLE IF EXISTS user_configs;
DROP TABLE IF EXISTS users;

-- 创建用户表 - 基础表用于测试大部分ORM功能
CREATE TABLE users (
    id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(100) NOT NULL,
    email VARCHAR(100) NULL,
    status TINYINT NOT NULL DEFAULT 1,  -- 1=活跃，0=禁用
    login_count INT UNSIGNED DEFAULT 0,
    last_login_at DATETIME NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    
    UNIQUE INDEX idx_username (username),
    INDEX idx_status (status)
);

-- 创建用户配置表 - 用于测试简单的一对一关系
CREATE TABLE user_configs (
    id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id INT UNSIGNED NOT NULL,
    theme VARCHAR(50) DEFAULT 'default',
    notification_enabled TINYINT(1) DEFAULT 1,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    
    UNIQUE INDEX idx_user_id (user_id),
    CONSTRAINT fk_config_user FOREIGN KEY (user_id) 
        REFERENCES users (id) ON DELETE CASCADE ON UPDATE CASCADE
);

-- 插入一些基本测试数据
INSERT INTO users (username, email, status, login_count) VALUES
('test_user', 'test@example.com', 1, 5),
('admin', 'admin@example.com', 1, 10),
('inactive', 'inactive@example.com', 0, 2);

INSERT INTO user_configs (user_id, theme, notification_enabled) VALUES
(1, 'light', 1),
(2, 'dark', 1),
(3, 'default', 0);
)SQL";

    // 执行重置脚本
    conn->executeScript(sql);
    std::cout << ConsoleColor::GREEN << "Database reset successfully."
              << ConsoleColor::RESET << std::endl;
  } catch (const std::exception& ex) {
    std::cerr << ConsoleColor::RED << "Failed to reset database: " << ex.what()
              << ConsoleColor::RESET << std::endl;
    throw;
  }
}

// 简化的测试运行器
inline bool runTest(const std::string& testName,
                    std::function<void()> testFunc) {
  total_tests++;
  std::cout << ConsoleColor::BLUE << "\n[TEST] " << testName
            << ConsoleColor::RESET << std::endl;

  try {
    testFunc();
    std::cout << ConsoleColor::GREEN << "[PASS] " << testName
              << ConsoleColor::RESET << std::endl;
    passed_tests++;
    return true;
  } catch (const std::exception& ex) {
    std::cout << ConsoleColor::RED << "[FAIL] " << testName << ": " << ex.what()
              << ConsoleColor::RESET << std::endl;
    failed_tests++;
    return false;
  } catch (...) {
    std::cout << ConsoleColor::RED << "[FAIL] " << testName << ": Unknown error"
              << ConsoleColor::RESET << std::endl;
    failed_tests++;
    return false;
  }
}

// 打印测试结果摘要
inline void printTestSummary() {
  std::cout << "\n"
            << ConsoleColor::MAGENTA
            << "===== TEST SUMMARY =====" << ConsoleColor::RESET << std::endl;
  std::cout << "Total tests: " << total_tests << std::endl;
  std::cout << ConsoleColor::GREEN << "Passed: " << passed_tests
            << ConsoleColor::RESET << std::endl;

  if (failed_tests > 0) {
    std::cout << ConsoleColor::RED << "Failed: " << failed_tests
              << ConsoleColor::RESET << std::endl;
  } else {
    std::cout << "Failed: 0" << std::endl;
  }

  std::cout << ConsoleColor::MAGENTA
            << "======================" << ConsoleColor::RESET << std::endl;
}

// 简化的断言函数
inline void assertEquals(const std::string& actual,
                         const std::string& expected,
                         const std::string& message = "") {
  if (actual != expected) {
    throw std::runtime_error(
        message.empty() ? ("Expected '" + expected + "', got '" + actual + "'")
                        : (message + " (Expected '" + expected + "', got '" +
                           actual + "')"));
  }
}

inline void assertEquals(int actual,
                         int expected,
                         const std::string& message = "") {
  if (actual != expected) {
    throw std::runtime_error(
        message.empty() ? ("Expected " + std::to_string(expected) + ", got " +
                           std::to_string(actual))
                        : (message + " (Expected " + std::to_string(expected) +
                           ", got " + std::to_string(actual) + ")"));
  }
}

inline void assertTrue(bool condition,
                       const std::string& message = "Assertion failed") {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

inline void assertFalse(bool condition,
                        const std::string& message = "Assertion failed") {
  if (condition) {
    throw std::runtime_error(message);
  }
}

inline void assertNotNull(
    const void* ptr,
    const std::string& message = "Expected non-null value") {
  if (ptr == nullptr) {
    throw std::runtime_error(message);
  }
}

// 性能测试辅助函数
template <typename Func>
inline double measureExecutionTime(Func func) {
  auto start = std::chrono::high_resolution_clock::now();
  func();
  auto end = std::chrono::high_resolution_clock::now();

  std::chrono::duration<double, std::milli> duration = end - start;
  return duration.count();
}

// 初始化测试环境
inline void initTestEnvironment() {
  // 配置日志级别 - 仅显示错误，避免日志干扰测试输出
  starry::Logger::setLogLevel(starry::LogLevel::ERROR);

  std::cout << ConsoleColor::YELLOW << "Initializing test environment..."
            << ConsoleColor::RESET << std::endl;
  resetDatabase();
  std::cout << ConsoleColor::YELLOW << "Test environment ready."
            << ConsoleColor::RESET << std::endl;
}

}  // namespace StarryChat::Test
