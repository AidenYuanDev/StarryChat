#pragma once

#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "connection.h"
#include "connection_pool.h"
#include "logging.h"
#include "model.h"
#include "pool_config.h"
#include "query_builder.h"
#include "transaction.h"

namespace StarryChat::Test {

// 数据库配置常量
struct DbConfig {
  static constexpr const char* HOST = "localhost";
  static constexpr int PORT = 3306;
  static constexpr const char* DATABASE = "starrychat_test";
  static constexpr const char* USERNAME = "root";  // 替换为你的数据库用户名
  static constexpr const char* PASSWORD = "";      // 替换为你的数据库密码
  static constexpr const char* CHARSET = "utf8mb4";
  static constexpr int MIN_CONNECTIONS = 5;
  static constexpr int MAX_CONNECTIONS = 20;
};

// 测试状态跟踪
class TestResult {
 public:
  void addSuccess(const std::string& testName) {
    std::cout << "[PASS] " << testName << std::endl;
    successes_.push_back(testName);
  }

  void addFailure(const std::string& testName, const std::string& reason) {
    std::cout << "[FAIL] " << testName << ": " << reason << std::endl;
    failures_.push_back({testName, reason});
  }

  void printSummary() const {
    std::cout << "\n===== TEST SUMMARY =====\n";
    std::cout << "Total: " << (successes_.size() + failures_.size())
              << " tests\n";
    std::cout << "Passed: " << successes_.size() << "\n";
    std::cout << "Failed: " << failures_.size() << "\n";

    if (!failures_.empty()) {
      std::cout << "\nFailed tests:\n";
      for (const auto& [name, reason] : failures_) {
        std::cout << " - " << name << ": " << reason << "\n";
      }
    }
  }

  bool allPassed() const { return failures_.empty(); }

 private:
  std::vector<std::string> successes_;
  std::vector<std::pair<std::string, std::string>> failures_;
};

// 测试执行器
class TestRunner {
 public:
  using TestFunction = std::function<void()>;

  void addTest(const std::string& name, TestFunction test) {
    tests_.push_back({name, test});
  }

  bool runAll() {
    TestResult result;

    for (const auto& [name, func] : tests_) {
      try {
        std::cout << "\nRunning test: " << name << "..." << std::endl;
        func();
        result.addSuccess(name);
      } catch (const std::exception& ex) {
        result.addFailure(name, ex.what());
      } catch (...) {
        result.addFailure(name, "Unknown error");
      }
    }

    result.printSummary();
    return result.allPassed();
  }

 private:
  std::vector<std::pair<std::string, TestFunction>> tests_;
};

// 创建测试连接池
inline orm::ConnectionPoolPtr createTestPool() {
  auto config = std::make_shared<orm::PoolConfig>();
  config->setHost(DbConfig::HOST)
      .setPort(DbConfig::PORT)
      .setDatabase(DbConfig::DATABASE)
      .setUsername(DbConfig::USERNAME)
      .setPassword(DbConfig::PASSWORD)
      .setCharset(DbConfig::CHARSET)
      .setMinPoolSize(DbConfig::MIN_CONNECTIONS)
      .setMaxPoolSize(DbConfig::MAX_CONNECTIONS)
      .setTestOnBorrow(true)
      .setIdleTimeout(60000);  // 1分钟

  auto pool = std::make_shared<orm::ConnectionPool>(config);
  pool->initialize();
  return pool;
}

// 获取单个测试连接
inline orm::ConnectionPtr getTestConnection() {
  static orm::ConnectionPoolPtr pool = createTestPool();
  return pool->getConnection();
}

// 重置测试数据库到初始状态
inline void resetDatabase() {
  auto conn = getTestConnection();

  try {
    // 从schema文件读取SQL脚本并执行
    std::string sqlScript = R"SQL(
            -- 确保表不存在（如果存在则先删除）
            DROP TABLE IF EXISTS messages;
            DROP TABLE IF EXISTS categories;
            DROP TABLE IF EXISTS users;
            
            -- 创建用户表
            CREATE TABLE users (
                id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
                username VARCHAR(100) NOT NULL UNIQUE,
                password VARCHAR(255) NOT NULL,
                email VARCHAR(100) NOT NULL UNIQUE,
                status TINYINT NOT NULL DEFAULT 1 COMMENT '1: 活跃, 0: 禁用',
                last_login_at DATETIME NULL,
                created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
                updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                
                INDEX idx_username (username),
                INDEX idx_email (email),
                INDEX idx_status (status)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
            
            -- 创建消息分类表
            CREATE TABLE categories (
                id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
                name VARCHAR(50) NOT NULL UNIQUE,
                description TEXT NULL,
                parent_id INT UNSIGNED NULL,
                display_order INT NOT NULL DEFAULT 0,
                created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
                updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                
                INDEX idx_parent_id (parent_id),
                INDEX idx_display_order (display_order),
                
                CONSTRAINT fk_category_parent FOREIGN KEY (parent_id) 
                    REFERENCES categories (id) ON DELETE SET NULL ON UPDATE CASCADE
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
            
            -- 创建消息表
            CREATE TABLE messages (
                id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
                user_id INT UNSIGNED NOT NULL,
                receiver_id INT UNSIGNED NULL COMMENT '如果为NULL则是公开消息',
                category_id INT UNSIGNED NOT NULL,
                content TEXT NOT NULL,
                attachment VARCHAR(255) NULL COMMENT '附件路径',
                is_read TINYINT(1) NOT NULL DEFAULT 0 COMMENT '0: 未读, 1: 已读',
                read_at DATETIME NULL COMMENT '已读时间',
                created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
                updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
                
                INDEX idx_user_id (user_id),
                INDEX idx_receiver_id (receiver_id),
                INDEX idx_category_id (category_id),
                INDEX idx_is_read (is_read),
                INDEX idx_created_at (created_at),
                
                CONSTRAINT fk_message_user FOREIGN KEY (user_id) 
                    REFERENCES users (id) ON DELETE CASCADE ON UPDATE CASCADE,
                CONSTRAINT fk_message_receiver FOREIGN KEY (receiver_id) 
                    REFERENCES users (id) ON DELETE CASCADE ON UPDATE CASCADE,
                CONSTRAINT fk_message_category FOREIGN KEY (category_id) 
                    REFERENCES categories (id) ON DELETE CASCADE ON UPDATE CASCADE
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
            
            -- 插入测试用户数据
            INSERT INTO users (username, password, email, status) VALUES
            ('admin', '$2y$10$92IXUNpkjO0rOQ5byMi.Ye4oKoEa3Ro9llC/.og/at2.uheWG/igi', 'admin@example.com', 1),
            ('test_user', '$2y$10$92IXUNpkjO0rOQ5byMi.Ye4oKoEa3Ro9llC/.og/at2.uheWG/igi', 'user@example.com', 1),
            ('inactive_user', '$2y$10$92IXUNpkjO0rOQ5byMi.Ye4oKoEa3Ro9llC/.og/at2.uheWG/igi', 'inactive@example.com', 0);
            
            -- 插入测试分类数据
            INSERT INTO categories (name, description, parent_id, display_order) VALUES
            ('公告', '系统公告和重要信息', NULL, 1),
            ('聊天', '普通聊天消息', NULL, 2),
            ('通知', '系统通知', NULL, 3),
            ('技术讨论', '技术相关话题', 2, 1),
            ('闲聊', '非技术闲聊', 2, 2);
            
            -- 插入测试消息数据
            INSERT INTO messages (user_id, receiver_id, category_id, content, is_read) VALUES
            (1, NULL, 1, '欢迎来到StarryChat聊天系统！', 1),
            (1, 2, 3, '你有一条新的系统通知', 0),
            (2, 1, 2, '管理员，我有一些问题需要咨询', 1),
            (2, NULL, 4, '有人了解C++中的智能指针吗？', 0),
            (1, 2, 5, '周末有什么计划？', 0),
            (2, NULL, 4, '我在学习网络编程，有推荐的资料吗？', 0),
            (3, NULL, 5, '大家好，我是新来的！', 1);
        )SQL";

    conn->executeScript(sqlScript);
    std::cout << "Database reset successfully." << std::endl;
  } catch (const std::exception& ex) {
    std::cerr << "Failed to reset database: " << ex.what() << std::endl;
    throw;
  }
}

// 性能测试辅助函数 - 计时执行
template <typename Func>
inline double measureExecutionTime(Func func) {
  auto start = std::chrono::high_resolution_clock::now();
  func();
  auto end = std::chrono::high_resolution_clock::now();

  std::chrono::duration<double, std::milli> duration = end - start;
  return duration.count();
}

// 断言辅助函数
inline void assertCondition(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error("Assertion failed: " + message);
  }
}

inline void assertEqual(const std::string& actual,
                        const std::string& expected,
                        const std::string& message) {
  if (actual != expected) {
    throw std::runtime_error(message + " - Expected: '" + expected +
                             "', Got: '" + actual + "'");
  }
}

inline void assertEqual(int actual, int expected, const std::string& message) {
  if (actual != expected) {
    throw std::runtime_error(message +
                             " - Expected: " + std::to_string(expected) +
                             ", Got: " + std::to_string(actual));
  }
}

// 初始化测试环境
inline void initTestEnvironment() {
  // 配置日志级别
  starry::Logger::setLogLevel(starry::LogLevel::ERROR);  // 只显示错误日志

  try {
    // 确保数据库已经设置
    resetDatabase();
  } catch (const std::exception& ex) {
    std::cerr << "Failed to initialize test environment: " << ex.what()
              << std::endl;
    throw;
  }
}

}  // namespace StarryChat::Test
