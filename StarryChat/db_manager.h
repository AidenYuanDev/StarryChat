#pragma once

#include <mariadb/conncpp.hpp>
#include <memory>
#include <mutex>
#include <string>
#include "logging.h"

namespace StarryChat {

/**
 * 数据库管理类 - 负责管理MariaDB连接
 * 使用单例模式设计，提供连接池管理
 */
class DBManager {
 public:
  /**
   * 获取DBManager单例
   */
  static DBManager& getInstance();

  /**
   * 禁止拷贝和移动
   */
  DBManager(const DBManager&) = delete;
  DBManager& operator=(const DBManager&) = delete;
  DBManager(DBManager&&) = delete;
  DBManager& operator=(DBManager&&) = delete;

  /**
   * 初始化数据库连接池
   * 从配置文件读取数据库设置并创建连接池
   * @return 初始化是否成功
   */
  bool initialize();

  /**
   * 获取数据库连接
   * 从连接池获取一个可用连接
   * @return 数据库连接的智能指针
   */
  std::shared_ptr<sql::Connection> getConnection();

  /**
   * 关闭数据库连接池
   * 在程序结束时调用，释放资源
   */
  void shutdown();

  /**
   * 执行事务
   * @param func 事务中执行的函数
   * @return 事务是否成功
   */
  template <typename Func>
  bool executeTransaction(Func func) {
    auto conn = getConnection();
    if (!conn)
      return false;

    try {
      conn->setAutoCommit(false);

      bool result = func(conn);

      if (result) {
        conn->commit();
        return true;
      } else {
        conn->rollback();
        return false;
      }
    } catch (sql::SQLException& e) {
      conn->rollback();
      // 记录错误
      return false;
    }
  }

  /**
   * 执行SQL查询并返回结果集
   * @param sql SQL语句
   * @param resultSet 结果集引用
   * @param params 绑定参数
   * @return 是否执行成功
   */
  template <typename... Args>
  static bool executeQuery(const std::string& sql,
                           std::unique_ptr<sql::ResultSet>& resultSet,
                           Args&&... params) {
    try {
      auto conn = getInstance().getConnection();
      if (!conn) {
        LOG_ERROR << "Database connection failed";
        return false;
      }

      std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(sql));
      bindParameters(stmt.get(), 1, std::forward<Args>(params)...);

      // 执行查询并保存结果集
      resultSet = std::unique_ptr<sql::ResultSet>(stmt->executeQuery());
      return true;
    } catch (sql::SQLException& e) {
      LOG_ERROR << "SQL error: " << e.what()
                << ", Error code: " << e.getErrorCode()
                << ", SQL state: " << e.getSQLState() << ", Query: " << sql;
      return false;
    } catch (std::exception& e) {
      LOG_ERROR << "Database error: " << e.what() << ", Query: " << sql;
      return false;
    }
  }

  /**
   * 执行SQL更新操作
   * @param sql SQL语句
   * @param params 绑定参数
   * @return 是否执行成功（受影响行数>0）
   */
  template <typename... Args>
  static bool executeUpdate(const std::string& sql, Args&&... params) {
    try {
      auto conn = getInstance().getConnection();
      if (!conn) {
        LOG_ERROR << "Database connection failed";
        return false;
      }

      std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(sql));
      bindParameters(stmt.get(), 1, std::forward<Args>(params)...);

      // 执行更新操作
      return stmt->executeUpdate() > 0;
    } catch (sql::SQLException& e) {
      LOG_ERROR << "SQL error: " << e.what()
                << ", Error code: " << e.getErrorCode()
                << ", SQL state: " << e.getSQLState() << ", Query: " << sql;
      return false;
    } catch (std::exception& e) {
      LOG_ERROR << "Database error: " << e.what() << ", Query: " << sql;
      return false;
    }
  }

  /**
   * 执行SQL更新操作并返回生成的主键
   * @param sql SQL语句
   * @param generatedId 输出参数，存储生成的主键
   * @param params 绑定参数
   * @return 是否执行成功
   */
  template <typename... Args>
  static bool executeUpdateWithGeneratedKey(const std::string& sql,
                                            uint64_t& generatedId,
                                            Args&&... params) {
    try {
      auto conn = getInstance().getConnection();
      if (!conn) {
        LOG_ERROR << "Database connection failed";
        return false;
      }

      std::unique_ptr<sql::PreparedStatement> stmt(
          conn->prepareStatement(sql, sql::Statement::RETURN_GENERATED_KEYS));
      bindParameters(stmt.get(), 1, std::forward<Args>(params)...);

      // 执行更新操作
      if (stmt->executeUpdate() > 0) {
        // 获取生成的主键
        std::unique_ptr<sql::ResultSet> rs(stmt->getGeneratedKeys());
        if (rs->next()) {
          generatedId = rs->getUInt64(1);
          return true;
        }
      }
      return false;
    } catch (sql::SQLException& e) {
      LOG_ERROR << "SQL error: " << e.what()
                << ", Error code: " << e.getErrorCode()
                << ", SQL state: " << e.getSQLState() << ", Query: " << sql;
      return false;
    } catch (std::exception& e) {
      LOG_ERROR << "Database error: " << e.what() << ", Query: " << sql;
      return false;
    }
  }

 private:
  /**
   * 私有构造函数，实现单例模式
   */
  DBManager() = default;
  ~DBManager() = default;

  // MariaDB驱动和连接属性
  sql::Driver* driver_{nullptr};
  sql::Properties connectionProps_;

  // 连接池状态
  bool initialized_{false};
  std::mutex mutex_;

  // 参数绑定辅助函数
  template <typename T, typename... Rest>
  static void bindParameters(sql::PreparedStatement* stmt,
                             int index,
                             T&& value,
                             Rest&&... rest) {
    bindParameter(stmt, index, std::forward<T>(value));
    bindParameters(stmt, index + 1, std::forward<Rest>(rest)...);
  }

  // 递归终止
  static void bindParameters(sql::PreparedStatement*, int) {}

  // 为各种类型提供绑定实现
  static void bindParameter(sql::PreparedStatement* stmt,
                            int index,
                            int value) {
    stmt->setInt(index, value);
  }

  static void bindParameter(sql::PreparedStatement* stmt,
                            int index,
                            uint64_t value) {
    stmt->setUInt64(index, value);
  }

  static void bindParameter(sql::PreparedStatement* stmt,
                            int index,
                            const std::string& value) {
    stmt->setString(index, value);
  }

  static void bindParameter(sql::PreparedStatement* stmt,
                            int index,
                            double value) {
    stmt->setDouble(index, value);
  }

  static void bindParameter(sql::PreparedStatement* stmt,
                            int index,
                            bool value) {
    stmt->setBoolean(index, value);
  }

  // 处理nullptr的特例
  static void bindParameter(sql::PreparedStatement* stmt,
                            int index,
                            std::nullptr_t) {
    stmt->setNull(index, sql::DataType::VARCHAR);
  }

  // 枚举类型自动处理
  template <typename E>
  static typename std::enable_if<std::is_enum<E>::value>::type
  bindParameter(sql::PreparedStatement* stmt, int index, E value) {
    stmt->setInt(index, static_cast<int>(value));
  }
};

}  // namespace StarryChat
