#pragma once

#include <memory>
#include "connection.h"
#include "types.h"

namespace StarryChat::orm {

// 定义事务隔离级别
enum class IsolationLevel {
  READ_UNCOMMITTED,
  READ_COMMITTED,
  REPEATABLE_READ,
  SERIALIZABLE
};

class Transaction {
 public:
  // 构造函数，开始一个新事务
  explicit Transaction(ConnectionPtr connection,
                       IsolationLevel level = IsolationLevel::REPEATABLE_READ);

  // 析构函数，如果事务仍然活跃，则自动回滚
  ~Transaction();

  // 提交事务
  void commit();

  // 回滚事务
  void rollback();

  // 检查事务状态
  bool isActive() const;
  bool isCommitted() const;
  bool isRolledBack() const;

  // 获取关联的连接
  ConnectionPtr getConnection() const;

  // 禁用拷贝
  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;

  // 允许移动
  Transaction(Transaction&&) noexcept;
  Transaction& operator=(Transaction&&) noexcept;

 private:
  ConnectionPtr connection_;
  bool active_ = false;
  bool committed_ = false;
  bool rolledBack_ = false;

  // 设置事务隔离级别
  void setIsolationLevel(IsolationLevel level);

  // 开始事务
  void begin();
};

// 辅助函数：使用事务执行一个函数
template <typename Func>
auto withTransaction(ConnectionPtr conn, Func&& func)
    -> decltype(func(std::declval<Transaction&>())) {
  Transaction transaction(conn);
  try {
    auto result = func(transaction);
    transaction.commit();
    return result;
  } catch (...) {
    // 任何异常都会导致回滚
    // 不需要显式调用rollback()，析构函数会处理
    throw;
  }
}

// 无返回值版本
template <typename Func,
          typename = std::enable_if_t<std::is_void_v<
              decltype(std::declval<Func>()(std::declval<Transaction&>()))>>>
void withTransaction(ConnectionPtr conn, Func&& func) {
  Transaction transaction(conn);
  try {
    func(transaction);
    transaction.commit();
  } catch (...) {
    // 不需要显式调用rollback()，析构函数会处理
    throw;
  }
}

}  // namespace StarryChat::orm
