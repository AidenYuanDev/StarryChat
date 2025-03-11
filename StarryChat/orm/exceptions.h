#pragma once

#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace StarryChat::orm {

// 基础异常类
class DatabaseException : public std::exception {
 public:
  explicit DatabaseException(std::string message)
      : message_(std::move(message)) {}

  const char* what() const noexcept override { return message_.c_str(); }

  const std::string& getMessage() const noexcept { return message_; }

  virtual ~DatabaseException() = default;

 protected:
  std::string message_;
};

//===== 连接相关异常 =====
class ConnectionException : public DatabaseException {
 public:
  using DatabaseException::DatabaseException;
};

class ConnectionFailedException : public ConnectionException {
 public:
  ConnectionFailedException(const std::string& host,
                            int port,
                            const std::string& reason)
      : ConnectionException("Failed to connect to " + host + ":" +
                            std::to_string(port) + " - " + reason),
        host_(host),
        port_(port),
        reason_(reason) {}

  const std::string& getHost() const { return host_; }
  int getPort() const { return port_; }
  const std::string& getReason() const { return reason_; }

 private:
  std::string host_;
  int port_;
  std::string reason_;
};

class ConnectionTimeoutException : public ConnectionException {
 public:
  ConnectionTimeoutException(const std::string& host, int port, int timeout)
      : ConnectionException("Connection to " + host + ":" +
                            std::to_string(port) + " timed out after " +
                            std::to_string(timeout) + "ms"),
        host_(host),
        port_(port),
        timeout_(timeout) {}

  const std::string& getHost() const { return host_; }
  int getPort() const { return port_; }
  int getTimeout() const { return timeout_; }

 private:
  std::string host_;
  int port_;
  int timeout_;
};

class ConnectionClosedException : public ConnectionException {
 public:
  using ConnectionException::ConnectionException;
};

//===== 查询相关异常 =====

class QueryException : public DatabaseException {
 public:
  QueryException(std::string message, std::string sql)
      : DatabaseException(std::move(message)), sql_(std::move(sql)) {}

  const std::string& getSql() const { return sql_; }

 private:
  std::string sql_;
};

class QuerySyntaxException : public QueryException {
 public:
  using QueryException::QueryException;
};

class QueryExecutionException : public QueryException {
 public:
  QueryExecutionException(const std::string& message,
                          const std::string& sql,
                          int errorCode)
      : QueryException(message, sql), errorCode_(errorCode) {}

  int getErrorCode() const { return errorCode_; }

 private:
  int errorCode_;
};

class QueryTimeoutException : public QueryException {
 public:
  QueryTimeoutException(const std::string& sql, int timeout)
      : QueryException(
            "Query execution timed out after " + std::to_string(timeout) + "ms",
            sql),
        timeout_(timeout) {}

  int getTimeout() const { return timeout_; }

 private:
  int timeout_;
};

//===== 约束异常 =====

class ConstraintViolationException : public QueryException {
 public:
  using QueryException::QueryException;
};

class DuplicateEntryException : public ConstraintViolationException {
 public:
  DuplicateEntryException(const std::string& sql,
                          const std::string& table,
                          const std::string& column,
                          const std::string& value)
      : ConstraintViolationException("Duplicate entry '" + value +
                                         "' for key '" + table + "." + column +
                                         "'",
                                     sql),
        table_(table),
        column_(column),
        value_(value) {}

  const std::string& getTable() const { return table_; }
  const std::string& getColumn() const { return column_; }
  const std::string& getValue() const { return value_; }

 private:
  std::string table_;
  std::string column_;
  std::string value_;
};

class ForeignKeyConstraintException : public ConstraintViolationException {
 public:
  ForeignKeyConstraintException(const std::string& sql,
                                const std::string& table,
                                const std::string& constraint)
      : ConstraintViolationException(
            "Foreign key constraint failed on table '" + table + "' (" +
                constraint + ")",
            sql),
        table_(table),
        constraint_(constraint) {}

  const std::string& getTable() const { return table_; }
  const std::string& getConstraint() const { return constraint_; }

 private:
  std::string table_;
  std::string constraint_;
};

class NotNullConstraintException : public ConstraintViolationException {
 public:
  NotNullConstraintException(const std::string& sql,
                             const std::string& table,
                             const std::string& column)
      : ConstraintViolationException(
            "Column '" + table + "." + column + "' cannot be null",
            sql),
        table_(table),
        column_(column) {}

  const std::string& getTable() const { return table_; }
  const std::string& getColumn() const { return column_; }

 private:
  std::string table_;
  std::string column_;
};

//===== 事务相关异常 =====

class TransactionException : public DatabaseException {
 public:
  using DatabaseException::DatabaseException;
};

class TransactionBeginException : public TransactionException {
 public:
  using TransactionException::TransactionException;
};

class TransactionCommitException : public TransactionException {
 public:
  using TransactionException::TransactionException;
};

class TransactionRollbackException : public TransactionException {
 public:
  using TransactionException::TransactionException;
};

class NestedTransactionException : public TransactionException {
 public:
  using TransactionException::TransactionException;
};

//===== 连接池相关异常 =====

class PoolException : public DatabaseException {
 public:
  using DatabaseException::DatabaseException;
};

class PoolInitializationException : public PoolException {
 public:
  using PoolException::PoolException;
};

class PoolExhaustedException : public PoolException {
 public:
  PoolExhaustedException(int maxConnections)
      : PoolException("Connection pool exhausted (max: " +
                      std::to_string(maxConnections) + ")"),
        maxConnections_(maxConnections) {}

  int getMaxConnections() const { return maxConnections_; }

 private:
  int maxConnections_;
};

class PoolShutdownException : public PoolException {
 public:
  using PoolException::PoolException;
};

//===== ORM相关异常 =====

class ModelException : public DatabaseException {
 public:
  ModelException(std::string message, std::string model)
      : DatabaseException(std::move(message)), model_(std::move(model)) {}

  const std::string& getModel() const { return model_; }

 private:
  std::string model_;
};

class InvalidFieldException : public ModelException {
 public:
  InvalidFieldException(const std::string& model, const std::string& field)
      : ModelException("Invalid field '" + field + "' in model '" + model + "'",
                       model),
        field_(field) {}

  const std::string& getField() const { return field_; }

 private:
  std::string field_;
};

class ModelNotFoundException : public ModelException {
 public:
  ModelNotFoundException(const std::string& model, const std::string& id)
      : ModelException("Model '" + model + "' with ID '" + id + "' not found",
                       model),
        id_(id) {}

  const std::string& getId() const { return id_; }

 private:
  std::string id_;
};

class RelationException : public ModelException {
 public:
  RelationException(const std::string& model, const std::string& relation)
      : ModelException(
            "Invalid relation '" + relation + "' in model '" + model + "'",
            model),
        relation_(relation) {}

  const std::string& getRelation() const { return relation_; }

 private:
  std::string relation_;
};

//===== 工具函数 =====

// 从MySQL错误代码创建适当的异常
std::unique_ptr<DatabaseException> createExceptionFromMySqlError(
    int errorCode,
    const std::string& errorMessage,
    const std::string& sql = "");

}  // namespace StarryChat::orm
