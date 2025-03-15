#pragma once

#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include "user.pb.h"  // 生成的protobuf头文件

namespace StarryChat {

// 使用proto中定义的用户状态枚举
using UserStatus = starrychat::UserStatus;

class User {
 public:
  // 构造函数
  User();
  User(uint64_t id, const std::string& username);
  ~User() = default;

  // 禁用拷贝，允许移动
  User(const User&) = delete;
  User& operator=(const User&) = delete;
  User(User&&) noexcept = default;
  User& operator=(User&&) noexcept = default;

  // 基本访问器
  uint64_t getId() const { return id_; }
  const std::string& getUsername() const { return username_; }
  const std::string& getNickname() const { return nickname_; }
  const std::string& getEmail() const { return email_; }
  const std::string& getAvatarUrl() const { return avatarUrl_; }
  UserStatus getStatus() const { return status_; }
  time_t getLastLoginTime() const { return lastLoginTime_; }
  time_t getCreatedTime() const { return createdTime_; }
  uint32_t getLoginAttempts() const { return loginAttempts_; }

  // 设置器
  void setNickname(const std::string& nickname) { nickname_ = nickname; }
  void setEmail(const std::string& email) { email_ = email; }
  void setAvatarUrl(const std::string& url) { avatarUrl_ = url; }
  void setStatus(UserStatus status) { status_ = status; }
  void setLastLoginTime(time_t time) { lastLoginTime_ = time; }
  void incrementLoginAttempts() { ++loginAttempts_; }
  void resetLoginAttempts() { loginAttempts_ = 0; }
  void setId(uint64_t id) { id_ = id; }

  // 密码相关
  bool verifyPassword(const std::string& password) const;
  void setPassword(const std::string& password);
  bool hasPassword() const { return !passwordHash_.empty() && !salt_.empty(); }

  // 登录相关
  bool login(const std::string& password);
  void logout();

  // Protobuf序列化/反序列化
  starrychat::UserInfo toProto() const;
  static User fromProto(const starrychat::UserInfo& proto);

  // 调试辅助
  std::string toString() const;

 private:
  uint64_t id_{0};
  std::string username_;
  std::string nickname_;
  std::string email_;
  std::string avatarUrl_;
  std::string passwordHash_;
  std::string salt_;
  UserStatus status_{starrychat::USER_STATUS_OFFLINE};
  time_t lastLoginTime_{0};
  time_t createdTime_{0};
  uint32_t loginAttempts_{0};

  // 密码处理辅助方法
  std::string generateSalt() const;
  std::string hashPassword(const std::string& password,
                           const std::string& salt) const;
};

// 使用智能指针表示用户
using UserPtr = std::shared_ptr<User>;
using UserWeakPtr = std::weak_ptr<User>;

}  // namespace StarryChat
