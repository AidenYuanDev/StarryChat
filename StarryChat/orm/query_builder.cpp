#include <sstream>
#include "connection.h"
#include "logging.h"
#include "query_builder.h"
#include "result_set.h"

using namespace StarryChat::orm;

// 静态创建方法
QueryBuilder::Ptr QueryBuilder::create() {
  return std::shared_ptr<QueryBuilder>(new QueryBuilder());
}

// 构造函数
QueryBuilder::QueryBuilder() : type_(QueryType::SELECT), distinct_(false) {}

// 指定查询的表名
QueryBuilder::Ptr QueryBuilder::table(const std::string& tableName) {
  table_ = tableName;
  return shared_from_this();
}

// SELECT查询相关
QueryBuilder::Ptr QueryBuilder::select(
    const std::vector<std::string>& columns) {
  type_ = QueryType::SELECT;
  columns_ = columns;
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::select(const std::string& column) {
  type_ = QueryType::SELECT;
  columns_.clear();
  columns_.push_back(column);
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::selectRaw(const std::string& expression) {
  type_ = QueryType::SELECT;
  columns_.clear();
  columns_.push_back(expression);
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::distinct() {
  distinct_ = true;
  return shared_from_this();
}

// FROM子句
QueryBuilder::Ptr QueryBuilder::from(const std::string& table) {
  table_ = table;
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::from(const Ptr& subQuery,
                                     const std::string& alias) {
  fromSubQuery_ = subQuery;
  fromAlias_ = alias;
  return shared_from_this();
}

// JOIN子句
QueryBuilder::Ptr QueryBuilder::join(const std::string& table,
                                     const std::string& first,
                                     const std::string& op,
                                     const std::string& second,
                                     JoinType type) {
  JoinClause join;
  join.table = table;
  join.first = first;
  join.op = op;
  join.second = second;
  join.type = type;
  joins_.push_back(join);
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::leftJoin(const std::string& table,
                                         const std::string& first,
                                         const std::string& op,
                                         const std::string& second) {
  return join(table, first, op, second, JoinType::LEFT);
}

QueryBuilder::Ptr QueryBuilder::rightJoin(const std::string& table,
                                          const std::string& first,
                                          const std::string& op,
                                          const std::string& second) {
  return join(table, first, op, second, JoinType::RIGHT);
}

QueryBuilder::Ptr QueryBuilder::innerJoin(const std::string& table,
                                          const std::string& first,
                                          const std::string& op,
                                          const std::string& second) {
  return join(table, first, op, second, JoinType::INNER);
}

// WHERE子句
QueryBuilder::Ptr QueryBuilder::where(const std::string& column,
                                      const std::string& op,
                                      const ParamValue& value) {
  WhereClause where;
  where.type = WhereClause::Type::BASIC;
  where.column = column;
  where.op = op;
  where.values.push_back(value);
  where.isOr = false;
  wheres_.push_back(where);
  addBinding(value);
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::where(const std::string& column,
                                      const ParamValue& value) {
  return where(column, "=", value);
}

QueryBuilder::Ptr QueryBuilder::whereRaw(
    const std::string& rawWhere,
    const std::vector<ParamValue>& bindings) {
  WhereClause where;
  where.type = WhereClause::Type::RAW;
  where.column = rawWhere;
  where.isOr = false;
  wheres_.push_back(where);
  addBindings(bindings);
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::whereIn(const std::string& column,
                                        const std::vector<ParamValue>& values) {
  if (values.empty()) {
    return whereRaw("0 = 1");  // Always false when empty
  }

  WhereClause where;
  where.type = WhereClause::Type::IN;
  where.column = column;
  where.values = values;
  where.isOr = false;
  wheres_.push_back(where);
  addBindings(values);
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::whereIn(const std::string& column,
                                        const Ptr& subQuery) {
  WhereClause where;
  where.type = WhereClause::Type::SUB_QUERY;
  where.column = column;
  where.subQuery = subQuery;
  where.isOr = false;
  wheres_.push_back(where);
  addBindings(subQuery->getBindings());
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::whereNotIn(
    const std::string& column,
    const std::vector<ParamValue>& values) {
  if (values.empty()) {
    return whereRaw("1 = 1");  // Always true when empty
  }

  WhereClause where;
  where.type = WhereClause::Type::NOT_IN;
  where.column = column;
  where.values = values;
  where.isOr = false;
  wheres_.push_back(where);
  addBindings(values);
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::whereNotIn(const std::string& column,
                                           const Ptr& subQuery) {
  WhereClause where;
  where.type = WhereClause::Type::SUB_QUERY;
  where.column = column;
  where.op = "NOT IN";
  where.subQuery = subQuery;
  where.isOr = false;
  wheres_.push_back(where);
  addBindings(subQuery->getBindings());
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::whereNull(const std::string& column) {
  WhereClause where;
  where.type = WhereClause::Type::NULL_CHECK;
  where.column = column;
  where.isOr = false;
  wheres_.push_back(where);
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::whereNotNull(const std::string& column) {
  WhereClause where;
  where.type = WhereClause::Type::NOT_NULL;
  where.column = column;
  where.isOr = false;
  wheres_.push_back(where);
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::whereBetween(const std::string& column,
                                             const ParamValue& min,
                                             const ParamValue& max) {
  WhereClause where;
  where.type = WhereClause::Type::BETWEEN;
  where.column = column;
  where.values.push_back(min);
  where.values.push_back(max);
  where.isOr = false;
  wheres_.push_back(where);
  addBinding(min);
  addBinding(max);
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::whereNotBetween(const std::string& column,
                                                const ParamValue& min,
                                                const ParamValue& max) {
  WhereClause where;
  where.type = WhereClause::Type::NOT_BETWEEN;
  where.column = column;
  where.values.push_back(min);
  where.values.push_back(max);
  where.isOr = false;
  wheres_.push_back(where);
  addBinding(min);
  addBinding(max);
  return shared_from_this();
}

// 逻辑操作符
QueryBuilder::Ptr QueryBuilder::orWhere(const std::string& column,
                                        const std::string& op,
                                        const ParamValue& value) {
  WhereClause where;
  where.type = WhereClause::Type::BASIC;
  where.column = column;
  where.op = op;
  where.values.push_back(value);
  where.isOr = true;
  wheres_.push_back(where);
  addBinding(value);
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::orWhere(const std::string& column,
                                        const ParamValue& value) {
  return orWhere(column, "=", value);
}

QueryBuilder::Ptr QueryBuilder::orWhereRaw(
    const std::string& rawWhere,
    const std::vector<ParamValue>& bindings) {
  WhereClause where;
  where.type = WhereClause::Type::RAW;
  where.column = rawWhere;
  where.isOr = true;
  wheres_.push_back(where);
  addBindings(bindings);
  return shared_from_this();
}

// GROUP BY和HAVING子句
QueryBuilder::Ptr QueryBuilder::groupBy(
    const std::vector<std::string>& columns) {
  for (const auto& column : columns) {
    groups_.push_back(column);
  }
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::groupBy(const std::string& column) {
  groups_.push_back(column);
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::having(const std::string& column,
                                       const std::string& op,
                                       const ParamValue& value) {
  HavingClause having;
  having.type = HavingClause::Type::BASIC;
  having.column = column;
  having.op = op;
  having.value = value;
  havings_.push_back(having);
  addBinding(value);
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::having(const std::string& column,
                                       const ParamValue& value) {
  return having(column, "=", value);
}

QueryBuilder::Ptr QueryBuilder::havingRaw(
    const std::string& rawHaving,
    const std::vector<ParamValue>& bindings) {
  HavingClause having;
  having.type = HavingClause::Type::RAW;
  having.column = rawHaving;
  having.bindings = bindings;
  havings_.push_back(having);
  addBindings(bindings);
  return shared_from_this();
}

// ORDER BY子句
QueryBuilder::Ptr QueryBuilder::orderBy(const std::string& column,
                                        OrderDirection direction) {
  OrderClause order;
  order.column = column;
  order.direction = direction;
  order.isRaw = false;
  orders_.push_back(order);
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::orderByRaw(const std::string& raw) {
  OrderClause order;
  order.column = raw;
  order.isRaw = true;
  orders_.push_back(order);
  return shared_from_this();
}

// LIMIT和OFFSET子句
QueryBuilder::Ptr QueryBuilder::limit(int limit) {
  limit_ = limit;
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::offset(int offset) {
  offset_ = offset;
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::take(int limit) {
  return this->limit(limit);
}

QueryBuilder::Ptr QueryBuilder::skip(int offset) {
  return this->offset(offset);
}

// 分页辅助方法
QueryBuilder::Ptr QueryBuilder::forPage(int page, int perPage) {
  return skip((page - 1) * perPage)->take(perPage);
}

// 聚合函数
QueryBuilder::Ptr QueryBuilder::count(const std::string& column) {
  type_ = QueryType::SELECT;
  columns_.clear();
  columns_.push_back("COUNT(" + column + ")");
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::max(const std::string& column) {
  type_ = QueryType::SELECT;
  columns_.clear();
  columns_.push_back("MAX(" + column + ")");
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::min(const std::string& column) {
  type_ = QueryType::SELECT;
  columns_.clear();
  columns_.push_back("MIN(" + column + ")");
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::avg(const std::string& column) {
  type_ = QueryType::SELECT;
  columns_.clear();
  columns_.push_back("AVG(" + column + ")");
  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::sum(const std::string& column) {
  type_ = QueryType::SELECT;
  columns_.clear();
  columns_.push_back("SUM(" + column + ")");
  return shared_from_this();
}

// INSERT操作
QueryBuilder::Ptr QueryBuilder::insert(
    const std::unordered_map<std::string, ParamValue>& values) {
  type_ = QueryType::INSERT;
  insertData_.clear();
  insertData_.push_back(values);

  for (const auto& pair : values) {
    addBinding(pair.second);
  }

  return shared_from_this();
}

QueryBuilder::Ptr QueryBuilder::insert(
    const std::vector<std::unordered_map<std::string, ParamValue>>& rows) {
  if (rows.empty()) {
    return shared_from_this();
  }

  type_ = QueryType::INSERT;
  insertData_ = rows;

  for (const auto& row : rows) {
    for (const auto& pair : row) {
      addBinding(pair.second);
    }
  }

  return shared_from_this();
}

// UPDATE操作
QueryBuilder::Ptr QueryBuilder::update(
    const std::unordered_map<std::string, ParamValue>& values) {
  type_ = QueryType::UPDATE;
  updateData_ = values;

  for (const auto& pair : values) {
    addBinding(pair.second);
  }

  return shared_from_this();
}

// DELETE操作
QueryBuilder::Ptr QueryBuilder::del() {
  type_ = QueryType::DELETE;
  return shared_from_this();
}

// 执行查询
ResultSetPtr QueryBuilder::get(ConnectionPtr connection) {
  try {
    std::string sql = toSql();
    LOG_DEBUG << "Executing query: " << sql;

    auto statement = connection->prepareStatement(sql);
    int paramIndex = 1;

    for (const auto& param : bindings_) {
      bindParam(statement, paramIndex++, param);
    }

    SqlResultSetPtr sqlResultSet(statement->executeQuery());
    return std::make_unique<ResultSet>(std::move(sqlResultSet));
  } catch (const SqlException& ex) {
    LOG_ERROR << "Failed to execute query: " << ex.what();
    throw;
  }
}

bool QueryBuilder::execute(ConnectionPtr connection) {
  try {
    std::string sql = toSql();
    LOG_DEBUG << "Executing statement: " << sql;

    auto statement = connection->prepareStatement(sql);
    int paramIndex = 1;

    for (const auto& param : bindings_) {
      bindParam(statement, paramIndex++, param);
    }

    return statement->execute();
  } catch (const SqlException& ex) {
    LOG_ERROR << "Failed to execute statement: " << ex.what();
    throw;
  }
}

int QueryBuilder::executeWithRowCount(ConnectionPtr connection) {
  try {
    std::string sql = toSql();
    LOG_DEBUG << "Executing update: " << sql;

    auto statement = connection->prepareStatement(sql);
    int paramIndex = 1;

    for (const auto& param : bindings_) {
      bindParam(statement, paramIndex++, param);
    }

    return statement->executeUpdate();
  } catch (const SqlException& ex) {
    LOG_ERROR << "Failed to execute update: " << ex.what();
    throw;
  }
}

// 辅助方法
QueryBuilder::Ptr QueryBuilder::clone() const {
  auto builder = QueryBuilder::create();
  builder->type_ = type_;
  builder->table_ = table_;
  builder->columns_ = columns_;
  builder->distinct_ = distinct_;
  builder->fromSubQuery_ = fromSubQuery_;
  builder->fromAlias_ = fromAlias_;
  builder->joins_ = joins_;
  builder->wheres_ = wheres_;
  builder->groups_ = groups_;
  builder->havings_ = havings_;
  builder->orders_ = orders_;
  builder->limit_ = limit_;
  builder->offset_ = offset_;
  builder->insertData_ = insertData_;
  builder->updateData_ = updateData_;
  builder->bindings_ = bindings_;
  return builder;
}

bool QueryBuilder::exists(ConnectionPtr connection) {
  auto existsQuery = clone();
  existsQuery->select("1");
  existsQuery->limit(1);

  auto result = existsQuery->get(connection);
  return result && result->next();
}

bool QueryBuilder::doesntExist(ConnectionPtr connection) {
  return !exists(connection);
}

std::optional<QueryBuilder::ParamValue> QueryBuilder::first(
    ConnectionPtr connection,
    const std::string& column) {
  auto query = clone();
  query->select(column);
  query->limit(1);

  auto result = query->get(connection);
  if (result && result->next()) {
    return result->getValue(0);
  }

  return std::nullopt;
}

// 生成SQL语句和获取绑定参数
std::string QueryBuilder::toSql() const {
  switch (type_) {
    case QueryType::SELECT:
      return buildSelectSql();
    case QueryType::INSERT:
      return buildInsertSql();
    case QueryType::UPDATE:
      return buildUpdateSql();
    case QueryType::DELETE:
      return buildDeleteSql();
    default:
      throw std::runtime_error("Unsupported query type");
  }
}

std::vector<QueryBuilder::ParamValue> QueryBuilder::getBindings() const {
  return bindings_;
}

// 参数绑定
void QueryBuilder::bindParam(SqlPreparedStatementPtr& statement,
                             int index,
                             const ParamValue& value) {
  std::visit(
      [&statement, index](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, std::nullptr_t>) {
          statement->setNull(index, sql::Types::VARCHAR);
        } else if constexpr (std::is_same_v<T, int>) {
          statement->setInt(index, arg);
        } else if constexpr (std::is_same_v<T, int64_t>) {
          statement->setInt64(index, arg);
        } else if constexpr (std::is_same_v<T, double>) {
          statement->setDouble(index, arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
          statement->setString(index, arg.c_str());
        } else if constexpr (std::is_same_v<T, bool>) {
          statement->setBoolean(index, arg);
        } else {
          throw std::runtime_error("Unsupported parameter type");
        }
      },
      value);
}

// 私有方法实现
std::string QueryBuilder::escapeIdentifier(
    const std::string& identifier) const {
  // 简单的转义，实际可以更复杂
  return "`" + identifier + "`";
}

// 构建各类型SQL语句
std::string QueryBuilder::buildSelectSql() const {
  std::stringstream sql;

  sql << "SELECT ";

  if (distinct_) {
    sql << "DISTINCT ";
  }

  sql << buildColumns();
  sql << buildFrom();
  sql << buildJoins();
  sql << buildWheres();
  sql << buildGroups();
  sql << buildHavings();
  sql << buildOrders();
  sql << buildLimitOffset();

  return sql.str();
}

std::string QueryBuilder::buildInsertSql() const {
  if (insertData_.empty()) {
    throw std::runtime_error("No data provided for insert");
  }

  std::stringstream sql;
  sql << "INSERT INTO " << escapeIdentifier(table_) << " ";

  // 获取所有列名
  std::vector<std::string> columns;
  for (const auto& pair : insertData_[0]) {
    columns.push_back(pair.first);
  }

  // 构建列部分
  sql << "(";
  for (size_t i = 0; i < columns.size(); ++i) {
    if (i > 0)
      sql << ", ";
    sql << escapeIdentifier(columns[i]);
  }
  sql << ") VALUES ";

  // 构建值部分
  for (size_t i = 0; i < insertData_.size(); ++i) {
    if (i > 0)
      sql << ", ";

    sql << "(";
    for (size_t j = 0; j < columns.size(); ++j) {
      if (j > 0)
        sql << ", ";
      sql << "?";
    }
    sql << ")";
  }

  return sql.str();
}

std::string QueryBuilder::buildUpdateSql() const {
  if (updateData_.empty()) {
    throw std::runtime_error("No data provided for update");
  }

  std::stringstream sql;
  sql << "UPDATE " << escapeIdentifier(table_) << " SET ";

  size_t i = 0;
  for (const auto& pair : updateData_) {
    if (i > 0)
      sql << ", ";
    sql << escapeIdentifier(pair.first) << " = ?";
    ++i;
  }

  sql << buildWheres();

  return sql.str();
}

std::string QueryBuilder::buildDeleteSql() const {
  std::stringstream sql;
  sql << "DELETE FROM " << escapeIdentifier(table_);
  sql << buildWheres();

  return sql.str();
}

// 构建SQL各部分
std::string QueryBuilder::buildColumns() const {
  if (columns_.empty()) {
    return "*";
  }

  std::stringstream ss;
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (i > 0)
      ss << ", ";

    // 检查是否是原始表达式
    if (columns_[i].find('(') != std::string::npos ||
        columns_[i].find('*') != std::string::npos ||
        columns_[i].find(' ') != std::string::npos) {
      ss << columns_[i];
    } else {
      ss << escapeIdentifier(columns_[i]);
    }
  }

  return ss.str();
}

std::string QueryBuilder::buildFrom() const {
  std::stringstream ss;
  ss << " FROM ";

  if (fromSubQuery_) {
    ss << "(" << fromSubQuery_->toSql() << ") AS "
       << escapeIdentifier(fromAlias_);
  } else {
    ss << escapeIdentifier(table_);
  }

  return ss.str();
}

std::string QueryBuilder::buildJoins() const {
  if (joins_.empty()) {
    return "";
  }

  std::stringstream ss;

  for (const auto& join : joins_) {
    switch (join.type) {
      case JoinType::INNER:
        ss << " INNER JOIN ";
        break;
      case JoinType::LEFT:
        ss << " LEFT JOIN ";
        break;
      case JoinType::RIGHT:
        ss << " RIGHT JOIN ";
        break;
      case JoinType::FULL:
        ss << " FULL JOIN ";
        break;
    }

    ss << escapeIdentifier(join.table) << " ON ";
    ss << escapeIdentifier(join.first) << " " << join.op << " "
       << escapeIdentifier(join.second);
  }

  return ss.str();
}

std::string QueryBuilder::buildWheres() const {
  if (wheres_.empty()) {
    return "";
  }

  std::stringstream ss;
  ss << " WHERE ";

  for (size_t i = 0; i < wheres_.size(); ++i) {
    const auto& where = wheres_[i];

    if (i > 0) {
      ss << (where.isOr ? " OR " : " AND ");
    }

    switch (where.type) {
      case WhereClause::Type::BASIC:
        ss << escapeIdentifier(where.column) << " " << where.op << " ?";
        break;

      case WhereClause::Type::RAW:
        ss << "(" << where.column << ")";
        break;

      case WhereClause::Type::IN:
        ss << escapeIdentifier(where.column) << " IN (";
        for (size_t j = 0; j < where.values.size(); ++j) {
          if (j > 0)
            ss << ", ";
          ss << "?";
        }
        ss << ")";
        break;

      case WhereClause::Type::NOT_IN:
        ss << escapeIdentifier(where.column) << " NOT IN (";
        for (size_t j = 0; j < where.values.size(); ++j) {
          if (j > 0)
            ss << ", ";
          ss << "?";
        }
        ss << ")";
        break;

      case WhereClause::Type::NULL_CHECK:
        ss << escapeIdentifier(where.column) << " IS NULL";
        break;

      case WhereClause::Type::NOT_NULL:
        ss << escapeIdentifier(where.column) << " IS NOT NULL";
        break;

      case WhereClause::Type::BETWEEN:
        ss << escapeIdentifier(where.column) << " BETWEEN ? AND ?";
        break;

      case WhereClause::Type::NOT_BETWEEN:
        ss << escapeIdentifier(where.column) << " NOT BETWEEN ? AND ?";
        break;

      case WhereClause::Type::SUB_QUERY:
        ss << escapeIdentifier(where.column);
        if (where.op.empty()) {
          ss << " IN ";
        } else {
          ss << " " << where.op << " ";
        }
        ss << "(" << where.subQuery->toSql() << ")";
        break;
    }
  }

  return ss.str();
}

std::string QueryBuilder::buildGroups() const {
  if (groups_.empty()) {
    return "";
  }

  std::stringstream ss;
  ss << " GROUP BY ";

  for (size_t i = 0; i < groups_.size(); ++i) {
    if (i > 0)
      ss << ", ";
    ss << escapeIdentifier(groups_[i]);
  }

  return ss.str();
}

std::string QueryBuilder::buildHavings() const {
  if (havings_.empty()) {
    return "";
  }

  std::stringstream ss;
  ss << " HAVING ";

  for (size_t i = 0; i < havings_.size(); ++i) {
    const auto& having = havings_[i];

    if (i > 0)
      ss << " AND ";

    switch (having.type) {
      case HavingClause::Type::BASIC:
        ss << escapeIdentifier(having.column) << " " << having.op << " ?";
        break;

      case HavingClause::Type::RAW:
        ss << "(" << having.column << ")";
        break;
    }
  }

  return ss.str();
}

std::string QueryBuilder::buildOrders() const {
  if (orders_.empty()) {
    return "";
  }

  std::stringstream ss;
  ss << " ORDER BY ";

  for (size_t i = 0; i < orders_.size(); ++i) {
    const auto& order = orders_[i];

    if (i > 0)
      ss << ", ";

    if (order.isRaw) {
      ss << order.column;
    } else {
      ss << escapeIdentifier(order.column);
      ss << (order.direction == OrderDirection::ASC ? " ASC" : " DESC");
    }
  }

  return ss.str();
}

std::string QueryBuilder::buildLimitOffset() const {
  std::stringstream ss;

  if (limit_.has_value()) {
    ss << " LIMIT " << limit_.value();
  }

  if (offset_.has_value()) {
    ss << " OFFSET " << offset_.value();
  }

  return ss.str();
}

// 参数绑定
void QueryBuilder::addBinding(const ParamValue& value) {
  bindings_.push_back(value);
}

void QueryBuilder::addBindings(const std::vector<ParamValue>& values) {
  for (const auto& value : values) {
    bindings_.push_back(value);
  }
}
