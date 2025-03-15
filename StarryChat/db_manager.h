#pragma once

#include <mariadb/conncpp.hpp>
#include <memory>
#include <mutex>
#include <string>

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
   * 执行查询语句
   * @param sql 要执行的SQL查询语句
   * @return 结果集的智能指针
   */
  std::unique_ptr<sql::ResultSet> executeQuery(const std::string& sql);

  /**
   * 执行更新语句
   * @param sql 要执行的SQL更新语句
   * @return 受影响的行数
   */
  int executeUpdate(const std::string& sql);

  /**
   * 创建预处理语句
   * @param sql 预处理语句的SQL
   * @param returnGeneratedKeys 是否返回生成的主键
   * @return 预处理语句的智能指针
   */
  std::unique_ptr<sql::PreparedStatement> prepareStatement(
      const std::string& sql,
      bool returnGeneratedKeys = false);

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
};

}  // namespace StarryChat
