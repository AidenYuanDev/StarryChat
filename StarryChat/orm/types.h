#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mariadb/conncpp.hpp>
#include <mariadb/conncpp/Connection.hpp>
#include <mariadb/conncpp/Driver.hpp>
#include <mariadb/conncpp/Statement.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

namespace StarryChat::orm {

using String = std::string;

using TimePoint = std::chrono::system_clock::time_point;

using SqlValue = std::variant<std::nullptr_t,
                              int,
                              int64_t,
                              uint64_t,
                              double,
                              String,
                              bool,
                              TimePoint>;

using RowData = std::unordered_map<String, SqlValue>;
;

using SqlDriver = sql::Driver;
using SqlConnection = sql::Connection;
using SqlStatement = sql::Statement;
using SqlPreparedStatement = sql::PreparedStatement;
using SqlResultSet = sql::ResultSet;
using SqlException = sql::SQLException;
using SqlString = sql::SQLString;

using SqlConnectionPtr = std::shared_ptr<SqlConnection>;
using SqlStatementPtr = std::unique_ptr<SqlStatement>;
using SqlPreparedStatementPtr = std::unique_ptr<SqlPreparedStatement>;
using SqlResultSetPtr = std::unique_ptr<SqlResultSet>;

class Connection;
class ConnectionPool;
class PoolConfig;
class QueryBuilder;
class ResultSet;
class Model;
class Transaction;
class FieldMapper;

using ConnectionPtr = std::shared_ptr<Connection>;
using ConnectionPoolPtr = std::shared_ptr<ConnectionPool>;
using PoolConfigPtr = std::shared_ptr<PoolConfig>;
using QueryBuilderPtr = std::shared_ptr<QueryBuilder>;
using ResultSetPtr = std::unique_ptr<ResultSet>;
using ModelPtr = std::shared_ptr<Model>;
using TransactionPtr = std::unique_ptr<Transaction>;

using ConnectionValidator = std::function<bool(SqlConnection*)>;
using ConnectionFinalizer = std::function<void(SqlConnection*)>;

using RowHandler = std::function<void(const RowData&)>;

constexpr int DEFAULT_MIN_POOL_SIZE = 5;
constexpr int DEFAULT_MAX_POOL_SIZE = 20;
constexpr int DEFAULT_QUEUE_SIZE = 1000;

constexpr int DEFAULT_CONNECTION_TIMEOUT = 5000;  // 5 seconds
constexpr int DEFAULT_IDLE_TIMEOUT = 600000;      // 10 minutes
constexpr int DEFAULT_MAX_LIFETIME = 3600000;     // 1 hour

}  // namespace StarryChat::orm
