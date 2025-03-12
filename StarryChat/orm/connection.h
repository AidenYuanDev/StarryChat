#pragma once

#include <string>
#include "types.h"

namespace StarryChat::orm {

class Connection {
 public:
  // 构造函数，接收一个原始SQL连接
  explicit Connection(SqlConnectionPtr sqlConnection);

  // 析构函数
  ~Connection();

  // 获取底层SQL连接
  sql::Connection* getRawConnection() const;

  // 执行查询并返回结果集
  ResultSetPtr executeQuery(const std::string& sql);

  // 执行不返回结果集的操作，如INSERT, UPDATE, DELETE
  bool executeUpdate(const std::string& sql);

  // 执行不返回结果集的操作，返回受影响的行数
  int executeUpdateWithRowCount(const std::string& sql);

  // 创建预处理语句
  SqlPreparedStatementPtr prepareStatement(const std::string& sql);

  // 设置自动提交模式
  void setAutoCommit(bool autoCommit);

  // 获取自动提交模式
  bool getAutoCommit();

  // 手动提交事务
  void commit();

  // 手动回滚事务
  void rollback();

  // 检查连接是否有效
  bool isValid(int timeout = 0);

  // 获取最后一个插入的ID
  uint64_t getLastInsertId();

  // 执行多条SQL语句
  void executeScript(const std::string& sql);

  // 防止拷贝
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  // 允许移动
  Connection(Connection&&) noexcept;
  Connection& operator=(Connection&&) noexcept;

 private:
  SqlConnectionPtr sqlConnection_;

  // 检查连接是否为空
  void checkConnection() const;
};

}  // namespace StarryChat::orm
