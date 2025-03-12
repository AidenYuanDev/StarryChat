#pragma once

#include <memory>
#include <optional>
#include <string>
#include "model.h"

namespace StarryChat::orm {

// 前向声明，与 User 建立一对一关系的配置类
class UserConfig;

/**
 * @brief 用户模型
 *
 * 映射到数据库users表，提供用户相关操作。
 */
class User : public orm::Model {
  // 使用宏定义模型基本信息
  DEFINE_MODEL(User, "users")
  DEFINE_MODEL_FACTORY(User)

 public:
  User() = default;
  ~User() override = default;

  // 禁止拷贝
  User(const User&) = delete;
  User& operator=(const User&) = delete;

  // 允许移动
  User(User&&) = default;
  User& operator=(User&&) = default;

  // 便捷方法 - 获取字段
  int64_t getId() const { return get<int64_t>("id"); }
  std::string getUsername() const { return get<std::string>("username"); }
  std::string getEmail() const { return get<std::string>("email"); }
  int getStatus() const { return get<int>("status"); }
  int getLoginCount() const { return get<int>("login_count"); }

  std::optional<orm::TimePoint> getLastLoginAt() const {
    // 修正: 使用 hasAttribute 和 属性为空值的检查
    if (!hasAttribute("last_login_at") ||
        std::holds_alternative<std::nullptr_t>(getAttribute("last_login_at"))) {
      return std::nullopt;
    }
    return get<orm::TimePoint>("last_login_at");
  }

  orm::TimePoint getCreatedAt() const {
    return get<orm::TimePoint>("created_at");
  }
  orm::TimePoint getUpdatedAt() const {
    return get<orm::TimePoint>("updated_at");
  }

  // 便捷方法 - 设置字段
  void setUsername(const std::string& username) { set("username", username); }
  void setEmail(const std::string& email) { set("email", email); }
  void setStatus(int status) { set("status", status); }
  void setLoginCount(int count) { set("login_count", count); }
  void setLastLoginAt(const orm::TimePoint& time) {
    set("last_login_at", time);
  }

  // 业务方法
  bool isActive() const { return getStatus() == 1; }

  /**
   * 记录用户登录
   *
   * 更新登录次数和最后登录时间
   */
  void recordLogin() {
    setLoginCount(getLoginCount() + 1);
    setLastLoginAt(std::chrono::system_clock::now());
  }

  /**
   * 启用用户
   */
  void activate() { setStatus(1); }

  /**
   * 禁用用户
   */
  void deactivate() { setStatus(0); }

  /**
   * 获取用户配置
   *
   * 展示如何实现一对一关系
   * 注意：在实际使用前，需要先实现UserConfig类
   */
  orm::RowData getUserConfig(orm::ConnectionPtr conn = nullptr) {
    // 修正：返回RowData而不是未定义的UserConfig类型
    auto query = orm::QueryBuilder::create()
                     ->table("user_configs")
                     ->where("user_id", getId())
                     ->limit(1);

    auto result = query->get(conn ? conn : getConnection());

    if (result && result->next()) {
      // 返回行数据而不是对象
      return result->getRow();
    }

    return {};  // 返回空的RowData
  }

  // 验证逻辑
  bool validate() override {
    // 简单示例：用户名不能为空
    if (getUsername().empty()) {
      return false;
    }
    return true;
  }

 protected:
  // 添加钩子示例
  void beforeSave() override {
    // 在保存前的逻辑
  }

  void afterSave() override {
    // 在保存后的逻辑
  }
};

}  // namespace StarryChat::orm
