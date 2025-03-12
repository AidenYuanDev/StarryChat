#include <mariadb/conncpp.hpp>
#include "logging.h"
#include "transaction.h"

using namespace StarryChat::orm;

Transaction::Transaction(ConnectionPtr connection, IsolationLevel level)
    : connection_(connection) {
  if (!connection_) {
    LOG_ERROR << "Cannot create Transaction with null Connection";
    throw std::invalid_argument("Connection pointer cannot be null");
  }

  setIsolationLevel(level);
  begin();
}

Transaction::~Transaction() {
  if (isActive()) {
    try {
      LOG_WARN << "Transaction was not explicitly committed or rolled back, "
                  "rolling back automatically";
      rollback();
    } catch (const SqlException& ex) {
      LOG_ERROR << "Failed to rollback transaction in destructor: "
                << ex.what();
      // Cannot throw from destructor, just log the error
    }
  }
}

void Transaction::commit() {
  if (!isActive()) {
    LOG_ERROR << "Cannot commit transaction: transaction is not active";
    throw std::logic_error("Transaction is not active");
  }

  try {
    connection_->commit();
    active_ = false;
    committed_ = true;
    LOG_DEBUG << "Transaction committed successfully";
  } catch (const SqlException& ex) {
    LOG_ERROR << "Failed to commit transaction: " << ex.what();
    throw;
  }
}

void Transaction::rollback() {
  if (!isActive()) {
    LOG_ERROR << "Cannot rollback transaction: transaction is not active";
    throw std::logic_error("Transaction is not active");
  }

  try {
    connection_->rollback();
    active_ = false;
    rolledBack_ = true;
    LOG_DEBUG << "Transaction rolled back successfully";
  } catch (const SqlException& ex) {
    LOG_ERROR << "Failed to rollback transaction: " << ex.what();
    throw;
  }
}

bool Transaction::isActive() const {
  return active_;
}

bool Transaction::isCommitted() const {
  return committed_;
}

bool Transaction::isRolledBack() const {
  return rolledBack_;
}

ConnectionPtr Transaction::getConnection() const {
  return connection_;
}

Transaction::Transaction(Transaction&& other) noexcept
    : connection_(std::move(other.connection_)),
      active_(other.active_),
      committed_(other.committed_),
      rolledBack_(other.rolledBack_) {
  other.active_ = false;
  other.committed_ = false;
  other.rolledBack_ = false;
}

Transaction& Transaction::operator=(Transaction&& other) noexcept {
  if (this != &other) {
    // 如果当前事务仍然活跃，尝试回滚
    if (isActive()) {
      try {
        rollback();
      } catch (const SqlException& ex) {
        LOG_ERROR << "Failed to rollback transaction in move assignment: "
                  << ex.what();
        // 继续，不抛出异常
      }
    }

    connection_ = std::move(other.connection_);
    active_ = other.active_;
    committed_ = other.committed_;
    rolledBack_ = other.rolledBack_;

    other.active_ = false;
    other.committed_ = false;
    other.rolledBack_ = false;
  }
  return *this;
}

void Transaction::setIsolationLevel(IsolationLevel level) {
  try {
    sql::Connection* rawConn = connection_->getRawConnection();

    switch (level) {
      case IsolationLevel::READ_UNCOMMITTED:
        rawConn->setTransactionIsolation(sql::TRANSACTION_READ_UNCOMMITTED);
        break;
      case IsolationLevel::READ_COMMITTED:
        rawConn->setTransactionIsolation(sql::TRANSACTION_READ_COMMITTED);
        break;
      case IsolationLevel::REPEATABLE_READ:
        rawConn->setTransactionIsolation(sql::TRANSACTION_REPEATABLE_READ);
        break;
      case IsolationLevel::SERIALIZABLE:
        rawConn->setTransactionIsolation(sql::TRANSACTION_SERIALIZABLE);
        break;
      default:
        rawConn->setTransactionIsolation(sql::TRANSACTION_REPEATABLE_READ);
        break;
    }

    LOG_DEBUG << "Transaction isolation level set successfully";
  } catch (const SqlException& ex) {
    LOG_ERROR << "Failed to set transaction isolation level: " << ex.what();
    throw;
  }
}

void Transaction::begin() {
  try {
    connection_->setAutoCommit(false);
    active_ = true;
    LOG_DEBUG << "Transaction started successfully";
  } catch (const SqlException& ex) {
    LOG_ERROR << "Failed to start transaction: " << ex.what();
    throw;
  }
}
