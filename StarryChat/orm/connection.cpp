#include <regex>
#include <sstream>
#include "connection.h"
#include "logging.h"
#include "result_set.h"

using namespace StarryChat::orm;

Connection::Connection(SqlConnectionPtr sqlConnection)
    : sqlConnection_(std::move(sqlConnection)) {
  if (!sqlConnection_) {
    LOG_ERROR << "Cannot create Connection with null SqlConnection";
    throw std::invalid_argument("SqlConnection pointer cannot be null");
  }
  LOG_DEBUG << "Connection created";
}

Connection::~Connection() {
  LOG_DEBUG << "Connection destroyed";
}

sql::Connection* Connection::getRawConnection() const {
  checkConnection();
  return sqlConnection_.get();
}

ResultSetPtr Connection::executeQuery(const std::string& sql) {
  checkConnection();
  try {
    LOG_DEBUG << "Executing query: " << sql;
    SqlStatementPtr stmt(sqlConnection_->createStatement());
    SqlResultSetPtr resultSet(stmt->executeQuery(sql));
    return std::make_unique<ResultSet>(std::move(resultSet));
  } catch (const SqlException& ex) {
    LOG_ERROR << "Failed to execute query: " << ex.what() << ", SQL: " << sql;
    throw;
  }
}

bool Connection::executeUpdate(const std::string& sql) {
  return executeUpdateWithRowCount(sql) > 0;
}

int Connection::executeUpdateWithRowCount(const std::string& sql) {
  checkConnection();
  try {
    LOG_DEBUG << "Executing update: " << sql;
    SqlStatementPtr stmt(sqlConnection_->createStatement());
    int result = stmt->executeUpdate(sql);
    LOG_DEBUG << "Execute update result: " << result;
    return result;
  } catch (const SqlException& ex) {
    LOG_ERROR << "Failed to execute update: " << ex.what() << ", SQL: " << sql;
    throw;
  }
}

SqlPreparedStatementPtr Connection::prepareStatement(const std::string& sql) {
  checkConnection();
  try {
    LOG_DEBUG << "Preparing statement: " << sql;
    return SqlPreparedStatementPtr(sqlConnection_->prepareStatement(sql));
  } catch (const SqlException& ex) {
    LOG_ERROR << "Failed to prepare statement: " << ex.what()
              << ", SQL: " << sql;
    throw;
  }
}

void Connection::setAutoCommit(bool autoCommit) {
  checkConnection();
  try {
    sqlConnection_->setAutoCommit(autoCommit);
    LOG_DEBUG << "Auto commit set to: " << (autoCommit ? "true" : "false");
  } catch (const SqlException& ex) {
    LOG_ERROR << "Failed to set auto commit: " << ex.what();
    throw;
  }
}

bool Connection::getAutoCommit() {
  checkConnection();
  try {
    bool result = sqlConnection_->getAutoCommit();
    LOG_DEBUG << "Auto commit is: " << (result ? "true" : "false");
    return result;
  } catch (const SqlException& ex) {
    LOG_ERROR << "Failed to get auto commit: " << ex.what();
    throw;
  }
}

void Connection::commit() {
  checkConnection();
  try {
    sqlConnection_->commit();
    LOG_DEBUG << "Transaction committed";
  } catch (const SqlException& ex) {
    LOG_ERROR << "Failed to commit transaction: " << ex.what();
    throw;
  }
}

void Connection::rollback() {
  checkConnection();
  try {
    sqlConnection_->rollback();
    LOG_DEBUG << "Transaction rolled back";
  } catch (const SqlException& ex) {
    LOG_ERROR << "Failed to rollback transaction: " << ex.what();
    throw;
  }
}

bool Connection::isValid(int timeout) {
  if (!sqlConnection_) {
    return false;
  }

  try {
    return sqlConnection_->isValid(timeout);
  } catch (const SqlException& ex) {
    LOG_ERROR << "Failed to check connection validity: " << ex.what();
    return false;
  }
}

uint64_t Connection::getLastInsertId() {
  checkConnection();
  try {
    ResultSetPtr rs = executeQuery("SELECT LAST_INSERT_ID()");
    if (rs && rs->next()) {
      return rs->get<uint64_t>(0);
    }
    return 0;
  } catch (const SqlException& ex) {
    LOG_ERROR << "Failed to get last insert ID: " << ex.what();
    throw;
  }
}

void Connection::executeScript(const std::string& sql) {
  checkConnection();
  // 简单的脚本执行实现 - 分割SQL语句并逐一执行
  // 注意：这个实现不处理引号内的分号等复杂情况

  std::stringstream ss(sql);
  std::string statement;
  std::vector<std::string> statements;

  while (std::getline(ss, statement, ';')) {
    // 跳过空语句
    statement = std::regex_replace(statement, std::regex("^\\s+|\\s+$"), "");
    if (!statement.empty()) {
      statements.push_back(statement);
    }
  }

  try {
    for (const auto& stmt : statements) {
      executeUpdate(stmt);
    }
    LOG_DEBUG << "Script executed successfully with " << statements.size()
              << " statements";
  } catch (const SqlException& ex) {
    LOG_ERROR << "Failed to execute script: " << ex.what();
    throw;
  }
}

Connection::Connection(Connection&& other) noexcept
    : sqlConnection_(std::move(other.sqlConnection_)) {}

Connection& Connection::operator=(Connection&& other) noexcept {
  if (this != &other) {
    sqlConnection_ = std::move(other.sqlConnection_);
  }
  return *this;
}

void Connection::checkConnection() const {
  if (!sqlConnection_) {
    LOG_ERROR << "Connection is null";
    throw std::runtime_error("Connection is not initialized");
  }
}
