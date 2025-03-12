#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "connection.h"
#include "connection_pool.h"
#include "query_builder.h"
#include "result_set.h"
#include "transaction.h"
#include "types.h"

namespace StarryChat::orm {

/**
 * @brief ORM模型基类
 *
 * 提供对象关系映射的基本功能，将数据库记录映射到C++对象，
 * 支持CRUD操作、关系定义、数据验证等功能。
 */
class Model : public std::enable_shared_from_this<Model> {
 public:
  using Ptr = std::shared_ptr<Model>;
  using AttributeMap = std::unordered_map<std::string, SqlValue>;
  using ModelCollection = std::vector<Ptr>;

  // 构造函数和析构函数
  Model();
  virtual ~Model() = default;

  // 禁止拷贝，允许移动
  Model(const Model&) = delete;
  Model& operator=(const Model&) = delete;
  Model(Model&&) noexcept = default;
  Model& operator=(Model&&) noexcept = default;

  // 表结构配置（子类必须实现）
  virtual std::string getTableName() const = 0;
  virtual std::string getPrimaryKey() const;
  virtual bool hasTimestamps() const;

  // 属性访问
  bool hasAttribute(const std::string& key) const;
  SqlValue getAttribute(const std::string& key) const;
  void setAttribute(const std::string& key, const SqlValue& value);

  // 属性访问模板方法（带类型转换）
  template <typename T>
  T get(const std::string& key) const {
    auto value = getAttribute(key);
    if (std::holds_alternative<std::nullptr_t>(value)) {
      return T{};
    }

    return std::visit(
        [](auto&& arg) -> T {
          using U = std::decay_t<decltype(arg)>;
          if constexpr (std::is_convertible_v<U, T>) {
            return static_cast<T>(arg);
          } else {
            throw std::bad_variant_access();
          }
        },
        value);
  }

  template <typename T>
  void set(const std::string& key, const T& value) {
    setAttribute(key, value);
  }

  // 获取所有属性
  AttributeMap getAttributes() const;

  // 原始属性和修改后的属性
  AttributeMap getOriginal() const;
  std::vector<std::string> getDirty() const;
  bool isDirty() const;
  bool isDirty(const std::string& key) const;
  void syncOriginal();

  // CRUD操作
  bool save(ConnectionPtr conn = nullptr);
  bool insert(ConnectionPtr conn = nullptr);
  bool update(ConnectionPtr conn = nullptr);
  bool remove(ConnectionPtr conn = nullptr);

  // 重新加载模型
  bool refresh(ConnectionPtr conn = nullptr);

  // 查找记录
  static Ptr find(const SqlValue& id, ConnectionPtr conn = nullptr);
  static Ptr findOrFail(const SqlValue& id, ConnectionPtr conn = nullptr);
  static ModelCollection findMany(const std::vector<SqlValue>& ids,
                                  ConnectionPtr conn = nullptr);

  // 首条记录
  static Ptr first(ConnectionPtr conn = nullptr);
  static Ptr firstOrFail(ConnectionPtr conn = nullptr);

  // 获取所有记录
  static ModelCollection all(ConnectionPtr conn = nullptr);

  // 条件查询（结果转换为模型）
  static ModelCollection where(const std::string& column,
                               const std::string& op,
                               const SqlValue& value,
                               ConnectionPtr conn = nullptr);
  static ModelCollection where(const std::string& column,
                               const SqlValue& value,
                               ConnectionPtr conn = nullptr);

  // 基于QueryBuilder查询
  static QueryBuilder::Ptr query();
  static ModelCollection get(QueryBuilder::Ptr query,
                             ConnectionPtr conn = nullptr);

  // 记录是否存在
  static bool exists(const SqlValue& id, ConnectionPtr conn = nullptr);

  // 创建记录
  static Ptr create(const AttributeMap& attributes,
                    ConnectionPtr conn = nullptr);

  // 批量更新
  static int update(const AttributeMap& attributes,
                    QueryBuilder::Ptr query,
                    ConnectionPtr conn = nullptr);

  // 批量删除
  static int remove(QueryBuilder::Ptr query, ConnectionPtr conn = nullptr);

  // 查询结果转换为模型集合
  static ModelCollection hydrate(const ResultSetPtr& resultSet);

  // 事件钩子（子类可覆盖）
  virtual void beforeSave() {}
  virtual void afterSave() {}
  virtual void beforeInsert() {}
  virtual void afterInsert() {}
  virtual void beforeUpdate() {}
  virtual void afterUpdate() {}
  virtual void beforeDelete() {}
  virtual void afterDelete() {}

  // 数据验证
  virtual bool validate() { return true; }

  // 连接管理
  static void setConnectionPool(ConnectionPoolPtr pool);
  static ConnectionPoolPtr getConnectionPool();
  static ConnectionPtr getConnection();

  // 检查新记录
  bool isNewRecord() const;

  // 获取主键值
  SqlValue getPrimaryKeyValue() const;

 protected:
  // 属性存储
  AttributeMap attributes_;
  AttributeMap original_;

  // 创建实例的工厂方法
  virtual Ptr createInstance() const = 0;

  // 填充模型属性
  void fill(const AttributeMap& attributes);
  void fillFromResultSet(const ResultSetPtr& resultSet);

  // 日期时间字段
  virtual std::string getCreatedAtColumn() const;
  virtual std::string getUpdatedAtColumn() const;

  // 设置时间戳
  void setCreatedAt();
  void setUpdatedAt();

 private:
  // 标记是否为新记录
  bool newRecord_;

  // 错误信息
  std::unordered_map<std::string, std::vector<std::string>> errors_;

  // 数据库连接池（静态共享）
  static ConnectionPoolPtr connectionPool_;

  // 将ResultSet转换为属性映射
  AttributeMap resultSetToAttributes(const ResultSetPtr& resultSet) const;

  // 构建保存操作的QueryBuilder
  QueryBuilder::Ptr buildSaveQuery() const;

  // 构建更新操作的QueryBuilder
  QueryBuilder::Ptr buildUpdateQuery() const;

  // 构建删除操作的QueryBuilder
  QueryBuilder::Ptr buildDeleteQuery() const;

  // 计算属性变更
  std::unordered_map<std::string, SqlValue> getDirtyAttributes() const;

  std::string getCurrentTimestamp() const;
};

// 宏：简化模型定义
#define DEFINE_MODEL(ClassName, TableName)                   \
 public:                                                     \
  static std::shared_ptr<ClassName> cast(Model::Ptr model) { \
    return std::dynamic_pointer_cast<ClassName>(model);      \
  }                                                          \
  std::string getTableName() const override {                \
    return TableName;                                        \
  }                                                          \
                                                             \
 protected:                                                  \
  Model::Ptr createInstance() const override {               \
    return std::make_shared<ClassName>();                    \
  }

// 宏：定义模型工厂方法
#define DEFINE_MODEL_FACTORY(ClassName)                                        \
 public:                                                                       \
  static std::shared_ptr<ClassName> make() {                                   \
    return std::make_shared<ClassName>();                                      \
  }                                                                            \
  static std::shared_ptr<ClassName> find(const SqlValue& id,                   \
                                         ConnectionPtr conn = nullptr) {       \
    return cast(Model::find(id, conn));                                        \
  }                                                                            \
  static std::shared_ptr<ClassName> findOrFail(const SqlValue& id,             \
                                               ConnectionPtr conn = nullptr) { \
    return cast(Model::findOrFail(id, conn));                                  \
  }                                                                            \
  static std::vector<std::shared_ptr<ClassName>> all(ConnectionPtr conn =      \
                                                         nullptr) {            \
    auto models = Model::all(conn);                                            \
    std::vector<std::shared_ptr<ClassName>> result;                            \
    for (auto& model : models) {                                               \
      result.push_back(cast(model));                                           \
    }                                                                          \
    return result;                                                             \
  }                                                                            \
  static std::shared_ptr<ClassName> create(const AttributeMap& attributes,     \
                                           ConnectionPtr conn = nullptr) {     \
    return cast(Model::create(attributes, conn));                              \
  }

}  // namespace StarryChat::orm
