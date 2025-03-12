#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "types.h"

namespace StarryChat::orm {

// 排序方向
enum class OrderDirection { ASC, DESC };

// 连接类型
enum class JoinType { INNER, LEFT, RIGHT, FULL };

// 查询类型
enum class QueryType { SELECT, INSERT, UPDATE, DELETE, RAW };

/**
 * @brief SQL查询构建器
 *
 * 提供流式API构建SQL查询语句，支持SELECT、INSERT、UPDATE、DELETE等操作，
 * 包含条件过滤、排序、分页等功能，并支持参数绑定防止SQL注入。
 */
class QueryBuilder : public std::enable_shared_from_this<QueryBuilder> {
 public:
  using Ptr = std::shared_ptr<QueryBuilder>;
  // using ParamValue =
  //     std::variant<std::nullptr_t, int, int64_t, double, std::string, bool>;
  // 修改ParamValue定义
  using ParamValue = SqlValue;

  // 创建新的查询构建器实例
  static Ptr create();

  // 指定查询的表名
  Ptr table(const std::string& tableName);

  // SELECT查询相关
  Ptr select(const std::vector<std::string>& columns);
  Ptr select(const std::string& column);
  template <typename... Args>
  Ptr select(Args&&... columns) {
    std::vector<std::string> cols = {std::forward<Args>(columns)...};
    return select(cols);
  }
  Ptr selectRaw(const std::string& expression);
  Ptr distinct();

  // FROM子句
  Ptr from(const std::string& table);
  Ptr from(const Ptr& subQuery, const std::string& alias);

  // JOIN子句
  Ptr join(const std::string& table,
           const std::string& first,
           const std::string& op,
           const std::string& second,
           JoinType type = JoinType::INNER);
  Ptr leftJoin(const std::string& table,
               const std::string& first,
               const std::string& op,
               const std::string& second);
  Ptr rightJoin(const std::string& table,
                const std::string& first,
                const std::string& op,
                const std::string& second);
  Ptr innerJoin(const std::string& table,
                const std::string& first,
                const std::string& op,
                const std::string& second);

  // WHERE子句
  Ptr where(const std::string& column,
            const std::string& op,
            const ParamValue& value);
  Ptr where(const std::string& column, const ParamValue& value);
  Ptr whereRaw(const std::string& rawWhere,
               const std::vector<ParamValue>& bindings = {});
  Ptr whereIn(const std::string& column, const std::vector<ParamValue>& values);
  Ptr whereIn(const std::string& column, const Ptr& subQuery);
  Ptr whereNotIn(const std::string& column,
                 const std::vector<ParamValue>& values);
  Ptr whereNotIn(const std::string& column, const Ptr& subQuery);
  Ptr whereNull(const std::string& column);
  Ptr whereNotNull(const std::string& column);
  Ptr whereBetween(const std::string& column,
                   const ParamValue& min,
                   const ParamValue& max);
  Ptr whereNotBetween(const std::string& column,
                      const ParamValue& min,
                      const ParamValue& max);

  // 逻辑操作符
  Ptr orWhere(const std::string& column,
              const std::string& op,
              const ParamValue& value);
  Ptr orWhere(const std::string& column, const ParamValue& value);
  Ptr orWhereRaw(const std::string& rawWhere,
                 const std::vector<ParamValue>& bindings = {});

  // GROUP BY和HAVING子句
  Ptr groupBy(const std::vector<std::string>& columns);
  Ptr groupBy(const std::string& column);
  template <typename... Args>
  Ptr groupBy(Args&&... columns) {
    std::vector<std::string> cols = {std::forward<Args>(columns)...};
    return groupBy(cols);
  }
  Ptr having(const std::string& column,
             const std::string& op,
             const ParamValue& value);
  Ptr having(const std::string& column, const ParamValue& value);
  Ptr havingRaw(const std::string& rawHaving,
                const std::vector<ParamValue>& bindings = {});

  // ORDER BY子句
  Ptr orderBy(const std::string& column,
              OrderDirection direction = OrderDirection::ASC);
  Ptr orderByRaw(const std::string& raw);

  // LIMIT和OFFSET子句
  Ptr limit(int limit);
  Ptr offset(int offset);
  Ptr take(int limit);
  Ptr skip(int offset);

  // 分页辅助方法
  Ptr forPage(int page, int perPage);

  // 聚合函数
  Ptr count(const std::string& column = "*");
  Ptr max(const std::string& column);
  Ptr min(const std::string& column);
  Ptr avg(const std::string& column);
  Ptr sum(const std::string& column);

  // INSERT操作
  Ptr insert(const std::unordered_map<std::string, ParamValue>& values);
  Ptr insert(
      const std::vector<std::unordered_map<std::string, ParamValue>>& rows);

  // UPDATE操作
  Ptr update(const std::unordered_map<std::string, ParamValue>& values);

  // DELETE操作
  Ptr del();

  // 执行查询
  ResultSetPtr get(ConnectionPtr connection);
  bool execute(ConnectionPtr connection);
  int executeWithRowCount(ConnectionPtr connection);

  // 辅助方法
  Ptr clone() const;
  bool exists(ConnectionPtr connection);
  bool doesntExist(ConnectionPtr connection);
  std::optional<ParamValue> first(ConnectionPtr connection,
                                  const std::string& column);

  // 生成SQL语句和获取绑定参数
  std::string toSql() const;
  std::vector<ParamValue> getBindings() const;

 private:
  // 私有构造函数，通过create()静态方法创建实例
  QueryBuilder();

  // 查询类型
  QueryType type_;

  // 查询组件
  std::string table_;
  std::vector<std::string> columns_;
  bool distinct_;

  // 子查询
  Ptr fromSubQuery_;
  std::string fromAlias_;

  // JOIN相关
  struct JoinClause {
    std::string table;
    std::string first;
    std::string op;
    std::string second;
    JoinType type;
  };
  std::vector<JoinClause> joins_;

  // WHERE相关
  struct WhereClause {
    enum class Type {
      BASIC,
      RAW,
      IN,
      NOT_IN,
      NULL_CHECK,
      NOT_NULL,
      BETWEEN,
      NOT_BETWEEN,
      SUB_QUERY
    };

    Type type;
    std::string column;
    std::string op;
    std::vector<ParamValue> values;
    Ptr subQuery;
    bool isOr;

    WhereClause() : type(Type::BASIC), isOr(false) {}
  };
  std::vector<WhereClause> wheres_;

  // GROUP BY和HAVING
  std::vector<std::string> groups_;

  struct HavingClause {
    enum class Type { BASIC, RAW };

    Type type;
    std::string column;
    std::string op;
    ParamValue value;
    std::vector<ParamValue> bindings;

    HavingClause() : type(Type::BASIC) {}
  };
  std::vector<HavingClause> havings_;

  // ORDER BY
  struct OrderClause {
    std::string column;
    OrderDirection direction;
    bool isRaw;

    OrderClause() : direction(OrderDirection::ASC), isRaw(false) {}
  };
  std::vector<OrderClause> orders_;

  // LIMIT和OFFSET
  std::optional<int> limit_;
  std::optional<int> offset_;

  // INSERT和UPDATE操作的数据
  std::vector<std::unordered_map<std::string, ParamValue>> insertData_;
  std::unordered_map<std::string, ParamValue> updateData_;

  // 绑定参数
  std::vector<ParamValue> bindings_;

  // 参数绑定
  void bindParam(SqlPreparedStatementPtr& statement,
                 int index,
                 const ParamValue& value);

  // 转义表名和列名
  std::string escapeIdentifier(const std::string& identifier) const;

  // 根据不同查询类型构建SQL
  std::string buildSelectSql() const;
  std::string buildInsertSql() const;
  std::string buildUpdateSql() const;
  std::string buildDeleteSql() const;

  // 构建SQL的各部分
  std::string buildColumns() const;
  std::string buildFrom() const;
  std::string buildJoins() const;
  std::string buildWheres() const;
  std::string buildGroups() const;
  std::string buildHavings() const;
  std::string buildOrders() const;
  std::string buildLimitOffset() const;

  // 添加参数绑定
  void addBinding(const ParamValue& value);
  void addBindings(const std::vector<ParamValue>& values);

  // 检查并添加WHERE逻辑
  void checkWhereBoolean();
};

}  // namespace StarryChat::orm
