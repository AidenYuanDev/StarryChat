#include <chrono>
#include <iomanip>
#include <sstream>
#include "logging.h"
#include "model.h"
#include "result_set.h"

using namespace StarryChat::orm;

// 静态成员初始化
ConnectionPoolPtr Model::connectionPool_ = nullptr;

// 构造函数
Model::Model() : newRecord_(true) {}

// 主键字段名（默认为id）
std::string Model::getPrimaryKey() const {
  return "id";
}

// 时间戳支持（默认启用）
bool Model::hasTimestamps() const {
  return true;
}

// 属性访问
bool Model::hasAttribute(const std::string& key) const {
  return attributes_.find(key) != attributes_.end();
}

SqlValue Model::getAttribute(const std::string& key) const {
  auto it = attributes_.find(key);
  if (it != attributes_.end()) {
    return it->second;
  }

  return nullptr;
}

void Model::setAttribute(const std::string& key, const SqlValue& value) {
  attributes_[key] = value;
}

// 获取所有属性
Model::AttributeMap Model::getAttributes() const {
  return attributes_;
}

// 原始属性和修改后的属性
Model::AttributeMap Model::getOriginal() const {
  return original_;
}

std::vector<std::string> Model::getDirty() const {
  std::vector<std::string> dirtyFields;

  for (const auto& [key, value] : attributes_) {
    auto it = original_.find(key);
    if (it == original_.end() || it->second != value) {
      dirtyFields.push_back(key);
    }
  }

  return dirtyFields;
}

bool Model::isDirty() const {
  return !getDirty().empty();
}

bool Model::isDirty(const std::string& key) const {
  auto it = original_.find(key);
  if (it == original_.end()) {
    return attributes_.find(key) != attributes_.end();
  }

  auto attrIt = attributes_.find(key);
  if (attrIt == attributes_.end()) {
    return true;
  }

  return it->second != attrIt->second;
}

void Model::syncOriginal() {
  original_ = attributes_;
}

// CRUD操作
bool Model::save(ConnectionPtr conn) {
  try {
    // 获取连接
    bool autoRelease = false;
    if (!conn) {
      conn = getConnection();
      autoRelease = true;
    }

    beforeSave();

    bool result;
    if (isNewRecord()) {
      result = insert(conn);
    } else {
      result = update(conn);
    }

    afterSave();

    return result;
  } catch (const std::exception& ex) {
    LOG_ERROR << "Failed to save model: " << ex.what();
    return false;
  }
}

bool Model::insert(ConnectionPtr conn) {
  try {
    // 获取连接
    bool autoRelease = false;
    if (!conn) {
      conn = getConnection();
      autoRelease = true;
    }

    // 验证模型
    if (!validate()) {
      LOG_ERROR << "Model validation failed";
      return false;
    }

    // 设置时间戳
    if (hasTimestamps()) {
      setCreatedAt();
      setUpdatedAt();
    }

    // 触发前置事件
    beforeInsert();

    // 构建插入查询
    auto query =
        QueryBuilder::create()->table(getTableName())->insert(attributes_);

    // 执行插入
    bool success = query->execute(conn);

    if (success) {
      // 获取自增ID
      auto primaryKey = getPrimaryKey();
      if (!hasAttribute(primaryKey) ||
          std::holds_alternative<std::nullptr_t>(getAttribute(primaryKey))) {
        auto lastId = conn->getLastInsertId();
        setAttribute(primaryKey, static_cast<int64_t>(lastId));
      }

      // 标记为非新记录
      newRecord_ = false;

      // 同步原始属性
      syncOriginal();

      // 触发后置事件
      afterInsert();
    }

    return success;
  } catch (const std::exception& ex) {
    LOG_ERROR << "Failed to insert model: " << ex.what();
    return false;
  }
}

bool Model::update(ConnectionPtr conn) {
  try {
    // 如果没有修改，直接返回成功
    if (!isDirty()) {
      return true;
    }

    // 获取连接
    bool autoRelease = false;
    if (!conn) {
      conn = getConnection();
      autoRelease = true;
    }

    // 验证模型
    if (!validate()) {
      LOG_ERROR << "Model validation failed";
      return false;
    }

    // 设置更新时间
    if (hasTimestamps()) {
      setUpdatedAt();
    }

    // 触发前置事件
    beforeUpdate();

    // 构建更新查询
    auto query = buildUpdateQuery();

    // 仅更新已修改的字段
    auto dirtyAttributes = getDirtyAttributes();

    // 执行更新
    bool success = false;
    if (!dirtyAttributes.empty()) {
      query->update(dirtyAttributes);
      success = query->execute(conn);
    } else {
      success = true;
    }

    if (success) {
      // 同步原始属性
      syncOriginal();

      // 触发后置事件
      afterUpdate();
    }

    return success;
  } catch (const std::exception& ex) {
    LOG_ERROR << "Failed to update model: " << ex.what();
    return false;
  }
}

bool Model::remove(ConnectionPtr conn) {
  try {
    // 获取连接
    bool autoRelease = false;
    if (!conn) {
      conn = getConnection();
      autoRelease = true;
    }

    // 触发前置事件
    beforeDelete();

    // 构建删除查询
    auto query = buildDeleteQuery();

    // 执行删除
    bool success = query->execute(conn);

    if (success) {
      // 触发后置事件
      afterDelete();
    }

    return success;
  } catch (const std::exception& ex) {
    LOG_ERROR << "Failed to delete model: " << ex.what();
    return false;
  }
}

// 重新加载模型
bool Model::refresh(ConnectionPtr conn) {
  try {
    // 检查主键
    auto primaryKey = getPrimaryKey();
    auto primaryKeyValue = getAttribute(primaryKey);

    if (std::holds_alternative<std::nullptr_t>(primaryKeyValue)) {
      LOG_ERROR << "Cannot refresh model without primary key";
      return false;
    }

    // 获取连接
    bool autoRelease = false;
    if (!conn) {
      conn = getConnection();
      autoRelease = true;
    }

    // 查询数据库
    auto query = QueryBuilder::create()
                     ->table(getTableName())
                     ->where(primaryKey, primaryKeyValue)
                     ->limit(1);

    auto resultSet = query->get(conn);

    if (resultSet && resultSet->next()) {
      // 更新属性
      attributes_ = resultSetToAttributes(resultSet);

      // 同步原始属性
      syncOriginal();

      // 标记为非新记录
      newRecord_ = false;

      return true;
    }

    return false;
  } catch (const std::exception& ex) {
    LOG_ERROR << "Failed to refresh model: " << ex.what();
    return false;
  }
}

// 查找记录
Model::Ptr Model::find(const SqlValue& id, ConnectionPtr conn) {
  try {
    // 获取连接
    bool autoRelease = false;
    if (!conn) {
      conn = getConnection();
      autoRelease = true;
    }

    // 创建模型实例
    Model::Ptr model = std::make_shared<Model>();

    // 查询数据库
    auto query = QueryBuilder::create()
                     ->table(model->getTableName())
                     ->where(model->getPrimaryKey(), id)
                     ->limit(1);

    auto resultSet = query->get(conn);

    if (resultSet && resultSet->next()) {
      // 填充模型
      model->fillFromResultSet(resultSet);

      // 标记为非新记录
      model->newRecord_ = false;

      // 同步原始属性
      model->syncOriginal();

      return model;
    }

    return nullptr;
  } catch (const std::exception& ex) {
    LOG_ERROR << "Failed to find model: " << ex.what();
    return nullptr;
  }
}

Model::Ptr Model::findOrFail(const SqlValue& id, ConnectionPtr conn) {
  auto model = find(id, conn);

  if (!model) {
    throw std::runtime_error("Model not found");
  }

  return model;
}

Model::ModelCollection Model::findMany(const std::vector<SqlValue>& ids,
                                       ConnectionPtr conn) {
  try {
    // 获取连接
    bool autoRelease = false;
    if (!conn) {
      conn = getConnection();
      autoRelease = true;
    }

    // 创建模型实例
    Model::Ptr model = std::make_shared<Model>();

    // 查询数据库
    auto query = QueryBuilder::create()
                     ->table(model->getTableName())
                     ->whereIn(model->getPrimaryKey(), ids);

    auto resultSet = query->get(conn);

    return hydrate(resultSet);
  } catch (const std::exception& ex) {
    LOG_ERROR << "Failed to find models: " << ex.what();
    return {};
  }
}

// 首条记录
Model::Ptr Model::first(ConnectionPtr conn) {
  try {
    // 获取连接
    bool autoRelease = false;
    if (!conn) {
      conn = getConnection();
      autoRelease = true;
    }

    // 创建模型实例
    Model::Ptr model = std::make_shared<Model>();

    // 查询数据库
    auto query = QueryBuilder::create()->table(model->getTableName())->limit(1);

    auto resultSet = query->get(conn);

    if (resultSet && resultSet->next()) {
      // 填充模型
      model->fillFromResultSet(resultSet);

      // 标记为非新记录
      model->newRecord_ = false;

      // 同步原始属性
      model->syncOriginal();

      return model;
    }

    return nullptr;
  } catch (const std::exception& ex) {
    LOG_ERROR << "Failed to get first model: " << ex.what();
    return nullptr;
  }
}

Model::Ptr Model::firstOrFail(ConnectionPtr conn) {
  auto model = first(conn);

  if (!model) {
    throw std::runtime_error("Model not found");
  }

  return model;
}

// 获取所有记录
Model::ModelCollection Model::all(ConnectionPtr conn) {
  try {
    // 获取连接
    bool autoRelease = false;
    if (!conn) {
      conn = getConnection();
      autoRelease = true;
    }

    // 创建模型实例
    Model::Ptr model = std::make_shared<Model>();

    // 查询数据库
    auto query = QueryBuilder::create()->table(model->getTableName());

    auto resultSet = query->get(conn);

    return hydrate(resultSet);
  } catch (const std::exception& ex) {
    LOG_ERROR << "Failed to get all models: " << ex.what();
    return {};
  }
}

// 条件查询
Model::ModelCollection Model::where(const std::string& column,
                                    const std::string& op,
                                    const SqlValue& value,
                                    ConnectionPtr conn) {
  try {
    // 获取连接
    bool autoRelease = false;
    if (!conn) {
      conn = getConnection();
      autoRelease = true;
    }

    // 创建模型实例
    Model::Ptr model = std::make_shared<Model>();

    // 查询数据库
    auto query = QueryBuilder::create()
                     ->table(model->getTableName())
                     ->where(column, op, value);

    auto resultSet = query->get(conn);

    return hydrate(resultSet);
  } catch (const std::exception& ex) {
    LOG_ERROR << "Failed to query models: " << ex.what();
    return {};
  }
}

Model::ModelCollection Model::where(const std::string& column,
                                    const SqlValue& value,
                                    ConnectionPtr conn) {
  return where(column, "=", value, conn);
}

// 基于QueryBuilder查询
QueryBuilder::Ptr Model::query() {
  // 创建模型实例
  Model::Ptr model = std::make_shared<Model>();

  // 创建查询构建器
  return QueryBuilder::create()->table(model->getTableName());
}

Model::ModelCollection Model::get(QueryBuilder::Ptr query, ConnectionPtr conn) {
  try {
    // 获取连接
    bool autoRelease = false;
    if (!conn) {
      conn = getConnection();
      autoRelease = true;
    }

    // 执行查询
    auto resultSet = query->get(conn);

    return hydrate(resultSet);
  } catch (const std::exception& ex) {
    LOG_ERROR << "Failed to execute query: " << ex.what();
    return {};
  }
}

// 记录是否存在
bool Model::exists(const SqlValue& id, ConnectionPtr conn) {
  try {
    // 获取连接
    bool autoRelease = false;
    if (!conn) {
      conn = getConnection();
      autoRelease = true;
    }

    // 创建模型实例
    Model::Ptr model = std::make_shared<Model>();

    // 查询数据库
    auto query = QueryBuilder::create()
                     ->table(model->getTableName())
                     ->where(model->getPrimaryKey(), id)
                     ->limit(1);

    return query->exists(conn);
  } catch (const std::exception& ex) {
    LOG_ERROR << "Failed to check if model exists: " << ex.what();
    return false;
  }
}

// 创建记录
Model::Ptr Model::create(const AttributeMap& attributes, ConnectionPtr conn) {
  try {
    // 创建模型实例
    Model::Ptr model = std::make_shared<Model>();

    // 填充属性
    model->fill(attributes);

    // 保存模型
    if (model->save(conn)) {
      return model;
    }

    return nullptr;
  } catch (const std::exception& ex) {
    LOG_ERROR << "Failed to create model: " << ex.what();
    return nullptr;
  }
}

// 批量更新
int Model::update(const AttributeMap& attributes,
                  QueryBuilder::Ptr query,
                  ConnectionPtr conn) {
  try {
    // 获取连接
    bool autoRelease = false;
    if (!conn) {
      conn = getConnection();
      autoRelease = true;
    }

    // 更新时间戳
    AttributeMap updatedAttributes = attributes;

    // 创建临时模型实例，用于获取配置
    Model::Ptr model = std::make_shared<Model>();
    if (model->hasTimestamps()) {
      updatedAttributes[model->getUpdatedAtColumn()] =
          model->getCurrentTimestamp();
    }

    // 执行更新
    query->update(updatedAttributes);
    return query->executeWithRowCount(conn);
  } catch (const std::exception& ex) {
    LOG_ERROR << "Failed to batch update models: " << ex.what();
    return 0;
  }
}

// 批量删除
int Model::remove(QueryBuilder::Ptr query, ConnectionPtr conn) {
  try {
    // 获取连接
    bool autoRelease = false;
    if (!conn) {
      conn = getConnection();
      autoRelease = true;
    }

    // 执行删除
    query->del();
    return query->executeWithRowCount(conn);
  } catch (const std::exception& ex) {
    LOG_ERROR << "Failed to batch delete models: " << ex.what();
    return 0;
  }
}

// 查询结果转换为模型集合
Model::ModelCollection Model::hydrate(const ResultSetPtr& resultSet) {
  ModelCollection models;

  if (!resultSet) {
    return models;
  }

  try {
    while (resultSet->next()) {
      // 创建模型实例
      Model::Ptr model = std::make_shared<Model>();

      // 填充属性
      model->fillFromResultSet(resultSet);

      // 标记为非新记录
      model->newRecord_ = false;

      // 同步原始属性
      model->syncOriginal();

      models.push_back(model);
    }
  } catch (const std::exception& ex) {
    LOG_ERROR << "Failed to hydrate models: " << ex.what();
  }

  return models;
}

// 连接管理
void Model::setConnectionPool(ConnectionPoolPtr pool) {
  connectionPool_ = pool;
}

ConnectionPoolPtr Model::getConnectionPool() {
  return connectionPool_;
}

ConnectionPtr Model::getConnection() {
  if (!connectionPool_) {
    throw std::runtime_error("Connection pool not set");
  }

  return connectionPool_->getConnection();
}

// 检查新记录
bool Model::isNewRecord() const {
  return newRecord_;
}

// 获取主键值
SqlValue Model::getPrimaryKeyValue() const {
  return getAttribute(getPrimaryKey());
}

// 填充模型属性
void Model::fill(const AttributeMap& attributes) {
  for (const auto& [key, value] : attributes) {
    setAttribute(key, value);
  }
}

void Model::fillFromResultSet(const ResultSetPtr& resultSet) {
  if (resultSet) {
    attributes_ = resultSetToAttributes(resultSet);
  }
}

// 日期时间字段
std::string Model::getCreatedAtColumn() const {
  return "created_at";
}

std::string Model::getUpdatedAtColumn() const {
  return "updated_at";
}

// 设置时间戳
void Model::setCreatedAt() {
  setAttribute(getCreatedAtColumn(), getCurrentTimestamp());
}

void Model::setUpdatedAt() {
  setAttribute(getUpdatedAtColumn(), getCurrentTimestamp());
}

// 获取当前时间戳
std::string Model::getCurrentTimestamp() const {
  auto now = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);
  std::stringstream ss;
  ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
  return ss.str();
}

// 将ResultSet转换为属性映射
Model::AttributeMap Model::resultSetToAttributes(
    const ResultSetPtr& resultSet) const {
  AttributeMap attributes;

  if (!resultSet) {
    return attributes;
  }

  auto columnNames = resultSet->getColumnNames();
  for (const auto& column : columnNames) {
    attributes[column] = resultSet->getValue(column);
  }

  return attributes;
}

// 构建保存操作的QueryBuilder
QueryBuilder::Ptr Model::buildSaveQuery() const {
  return QueryBuilder::create()->table(getTableName());
}

// 构建更新操作的QueryBuilder
QueryBuilder::Ptr Model::buildUpdateQuery() const {
  auto primaryKey = getPrimaryKey();
  auto primaryKeyValue = getAttribute(primaryKey);

  return QueryBuilder::create()
      ->table(getTableName())
      ->where(primaryKey, primaryKeyValue);
}

// 构建删除操作的QueryBuilder
QueryBuilder::Ptr Model::buildDeleteQuery() const {
  auto primaryKey = getPrimaryKey();
  auto primaryKeyValue = getAttribute(primaryKey);

  return QueryBuilder::create()
      ->table(getTableName())
      ->where(primaryKey, primaryKeyValue);
}

// 计算属性变更
std::unordered_map<std::string, SqlValue> Model::getDirtyAttributes() const {
  std::unordered_map<std::string, SqlValue> dirty;

  for (const auto& [key, value] : attributes_) {
    auto it = original_.find(key);
    if (it == original_.end() || it->second != value) {
      dirty[key] = value;
    }
  }

  return dirty;
}
