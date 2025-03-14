#include <openssl/evp.h>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include "user.h"

namespace StarryChat {

// 构造函数实现
User::User() : createdTime_(std::time(nullptr)) {}

User::User(uint64_t id, const std::string& username)
    : id_(id),
      username_(username),
      nickname_(username),  
      status_(starrychat::USER_STATUS_OFFLINE),
      createdTime_(std::time(nullptr)),
      loginAttempts_(0) {}

// 密码处理实现
std::string User::generateSalt() const {
  // 生成16字节随机盐值
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 255);

  std::stringstream ss;
  for (int i = 0; i < 16; ++i) {
    ss << std::hex << std::setw(2) << std::setfill('0') << dis(gen);
  }
  return ss.str();
}

std::string User::hashPassword(const std::string& password,
                               const std::string& salt) const {
  // 组合密码和盐值
  std::string combined = password + salt;

  // 使用EVP接口进行SHA-256哈希
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len;

  // 创建消息摘要上下文
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (ctx == nullptr) {
    return "";  // 处理错误
  }

  // 初始化摘要算法
  if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
    EVP_MD_CTX_free(ctx);
    return "";  // 处理错误
  }

  // 更新数据
  if (EVP_DigestUpdate(ctx, combined.c_str(), combined.size()) != 1) {
    EVP_MD_CTX_free(ctx);
    return "";  // 处理错误
  }

  // 完成哈希计算
  if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
    EVP_MD_CTX_free(ctx);
    return "";  // 处理错误
  }

  // 释放上下文
  EVP_MD_CTX_free(ctx);

  // 转换为十六进制字符串
  std::stringstream ss;
  for (unsigned int i = 0; i < hash_len; ++i) {
    ss << std::hex << std::setw(2) << std::setfill('0')
       << static_cast<int>(hash[i]);
  }
  return ss.str();
}

bool User::verifyPassword(const std::string& password) const {
  if (passwordHash_.empty() || salt_.empty()) {
    return false;  // 没有设置密码
  }

  std::string computedHash = hashPassword(password, salt_);
  return computedHash == passwordHash_;
}

void User::setPassword(const std::string& password) {
  salt_ = generateSalt();
  passwordHash_ = hashPassword(password, salt_);
}

// 登录相关实现
bool User::login(const std::string& password) {
  if (!verifyPassword(password)) {
    incrementLoginAttempts();
    return false;
  }

  // 登录成功
  setStatus(starrychat::USER_STATUS_ONLINE);
  setLastLoginTime(std::time(nullptr));
  resetLoginAttempts();
  return true;
}

void User::logout() {
  setStatus(starrychat::USER_STATUS_OFFLINE);
}

// Protobuf 转换实现
starrychat::UserInfo User::toProto() const {
  starrychat::UserInfo proto;
  proto.set_id(id_);
  proto.set_username(username_);
  proto.set_nickname(nickname_);
  proto.set_email(email_);
  proto.set_avatar_url(avatarUrl_);
  proto.set_status(status_);
  proto.set_created_time(createdTime_);
  proto.set_last_login_time(lastLoginTime_);
  // 注意：不包含密码哈希和盐值，这些是敏感信息
  return proto;
}

User User::fromProto(const starrychat::UserInfo& proto) {
  User user(proto.id(), proto.username());
  user.setNickname(proto.nickname());
  user.setEmail(proto.email());
  user.setAvatarUrl(proto.avatar_url());
  user.setStatus(proto.status());
  user.lastLoginTime_ = proto.last_login_time();
  user.createdTime_ = proto.created_time();
  // 注意：密码相关信息不从Proto恢复，需要单独处理
  return user;
}

// 调试辅助实现
std::string User::toString() const {
  std::stringstream ss;
  ss << "User[id=" << id_ << ", username=" << username_
     << ", nickname=" << nickname_ << ", email=" << email_
     << ", status=" << static_cast<int>(status_) << ", created=" << createdTime_
     << ", lastLogin=" << lastLoginTime_ << "]";
  return ss.str();
}

}  // namespace StarryChat
