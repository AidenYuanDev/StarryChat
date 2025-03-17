#include "user_service_impl.h"

#include <chrono>
#include <random>
#include <sstream>
#include "db_manager.h"
#include "logging.h"
#include "redis_manager.h"
#include "user.h"

namespace StarryChat {

// 用户注册
void UserServiceImpl::RegisterUser(
    const starrychat::RegisterUserRequestPtr& request,
    const starrychat::RegisterUserResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  LOG_INFO << "RegisterUser called with username: [" << request->username()
           << "], length: " << request->username().length() << ", email: ["
           << request->email() << "]";

  auto response = responsePrototype->New();

  // 确保用户名不为空
  if (request->username().empty()) {
    response->set_success(false);
    response->set_error_message("Username cannot be empty");
    done(response);
    return;
  }

  auto& redis = RedisManager::getInstance();

  // 快速检查用户名是否已存在 (Redis)
  auto existingId = redis.hget("username:to:id", request->username());
  if (existingId) {
    response->set_success(false);
    response->set_error_message("Username already exists");
    done(response);
    return;
  }

  // 再次检查用户名是否存在 (数据库)
  std::unique_ptr<sql::ResultSet> rs;
  if (!DBManager::executeQuery("SELECT 1 FROM users WHERE username = ?", rs,
                               request->username())) {
    LOG_ERROR << "Failed to check if username exists: " << request->username();
    response->set_success(false);
    response->set_error_message("Database error checking username");
    done(response);
    return;
  }

  if (rs->next()) {
    response->set_success(false);
    response->set_error_message("Username already exists");
    done(response);
    return;
  }

  // 创建用户对象处理密码
  User user(0, request->username());
  user.setPassword(request->password());
  user.setNickname(request->nickname());
  user.setEmail(request->email());
  user.setStatus(starrychat::USER_STATUS_OFFLINE);

  // 当前时间戳
  uint64_t currentTime = std::time(nullptr);

  // 插入新用户
  uint64_t userId = 0;
  if (!DBManager::executeUpdateWithGeneratedKey(
          "INSERT INTO users (username, nickname, email, status, created_time, "
          "password_hash, salt) "
          "VALUES (?, ?, ?, ?, ?, ?, ?)",
          userId, user.getUsername(), user.getNickname(), user.getEmail(),
          static_cast<int>(starrychat::USER_STATUS_OFFLINE), currentTime,
          user.getPasswordHash(), user.getSalt())) {
    LOG_ERROR << "Failed to insert new user: " << user.getUsername();
    response->set_success(false);
    response->set_error_message("Failed to insert user");
    done(response);
    return;
  }

  // 设置用户ID并缓存信息
  user.setId(userId);
  cacheUserInfo(user);

  // 成功响应
  response->set_success(true);
  *response->mutable_user_info() = user.toProto();

  LOG_INFO << "User registered - ID: " << user.getId()
           << ", Username: " << user.getUsername()
           << ", Nickname: " << user.getNickname();

  done(response);
}

// 用户登录
void UserServiceImpl::Login(const starrychat::LoginRequestPtr& request,
                            const starrychat::LoginResponse* responsePrototype,
                            const starry::RpcDoneCallback& done) {
  LOG_INFO << "Login called with username: [" << request->username()
           << "], length: " << request->username().length();

  auto response = responsePrototype->New();

  // 确保用户名不为空
  if (request->username().empty()) {
    response->set_success(false);
    response->set_error_message("Username cannot be empty");
    done(response);
    return;
  }

  auto& redis = RedisManager::getInstance();

  // 尝试从Redis获取用户ID
  uint64_t userId = 0;
  auto cachedUserId = redis.hget("username:to:id", request->username());
  if (cachedUserId) {
    userId = std::stoull(*cachedUserId);
    LOG_INFO << "Found cached user ID mapping: " << request->username()
             << " -> " << userId;
  }

  // 查询用户信息
  std::unique_ptr<sql::ResultSet> rs;
  bool querySuccess = false;

  // 如果已知用户ID，按ID查询效率更高
  if (userId > 0) {
    querySuccess =
        DBManager::executeQuery("SELECT * FROM users WHERE id = ?", rs, userId);
    if (!querySuccess) {
      LOG_ERROR << "Failed to query user by ID: " << userId;
    }
  } else {
    querySuccess = DBManager::executeQuery(
        "SELECT * FROM users WHERE username = ?", rs, request->username());
    if (!querySuccess) {
      LOG_ERROR << "Failed to query user by username: " << request->username();
    }
  }

  if (!querySuccess || !rs->next()) {
    response->set_success(false);
    response->set_error_message("User not found");
    done(response);
    return;
  }

  // 获取用户信息
  userId = rs->getUInt64("id");
  User user(userId, std::string(rs->getString("username")));

  // 设置其他用户字段
  user.setNickname(std::string(rs->getString("nickname")));
  user.setEmail(std::string(rs->getString("email")));
  user.setStatus(static_cast<starrychat::UserStatus>(rs->getInt("status")));

  if (!rs->isNull("avatar_url")) {
    user.setAvatarUrl(std::string(rs->getString("avatar_url")));
  }

  if (!rs->isNull("last_login_time")) {
    user.setLastLoginTime(rs->getUInt64("last_login_time"));
  }

  // 获取密码验证信息
  std::string passwordHash = std::string(rs->getString("password_hash"));
  std::string salt = std::string(rs->getString("salt"));
  user.setPasswordHashAndSalt(passwordHash, salt);

  // 验证密码
  if (!user.verifyPassword(request->password())) {
    response->set_success(false);
    response->set_error_message("Invalid password");

    // 增加登录尝试次数
    DBManager::executeUpdate(
        "UPDATE users SET login_attempts = login_attempts + 1 WHERE id = ?",
        userId);

    done(response);
    return;
  }

  // 登录成功，更新用户状态和登录时间
  uint64_t currentTime = std::time(nullptr);

  if (!DBManager::executeUpdate(
          "UPDATE users SET status = ?, last_login_time = ?, login_attempts = "
          "0 WHERE id = ?",
          static_cast<int>(starrychat::USER_STATUS_ONLINE), currentTime,
          userId)) {
    LOG_ERROR << "Failed to update login status for user ID: " << userId;
    response->set_success(false);
    response->set_error_message("Failed to update user status");
    done(response);
    return;
  }

  // 更新用户对象
  user.setStatus(starrychat::USER_STATUS_ONLINE);
  user.setLastLoginTime(currentTime);

  // 生成会话令牌
  std::string sessionToken = generateSessionToken(userId);

  // 存储会话
  storeSession(sessionToken, userId);

  // 更新用户状态
  updateUserStatusInCache(userId, starrychat::USER_STATUS_ONLINE);

  // 设置心跳
  redis.set("user:heartbeat:" + std::to_string(userId), "1",
            std::chrono::minutes(5));

  // 缓存用户信息
  cacheUserInfo(user);

  // 发布用户上线通知
  std::string notification =
      std::to_string(userId) + ":" +
      std::to_string(static_cast<int>(starrychat::USER_STATUS_ONLINE));
  redis.publish("user:status:changed", notification);

  LOG_INFO << "User logged in successfully: " << user.getUsername()
           << " (ID: " << userId << ")";

  // 设置登录响应
  response->set_success(true);
  response->set_session_token(sessionToken);
  *response->mutable_user_info() = user.toProto();

  done(response);
}

// 获取用户信息
void UserServiceImpl::GetUser(
    const starrychat::GetUserRequestPtr& request,
    const starrychat::GetUserResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  // 尝试从缓存获取用户信息
  auto cachedUser = getUserFromCache(request->user_id());

  if (cachedUser) {
    LOG_INFO << "User cache hit for user ID: " << request->user_id();

    // 设置响应
    response->set_success(true);
    *response->mutable_user_info() = cachedUser->toProto();

    done(response);
    return;  // 直接返回，无需查询数据库
  }

  LOG_INFO << "User cache miss for user ID: " << request->user_id();

  // 缓存未命中，从数据库获取
  std::unique_ptr<sql::ResultSet> rs;
  if (!DBManager::executeQuery("SELECT * FROM users WHERE id = ?", rs,
                               request->user_id())) {
    LOG_ERROR << "Failed to get user information for ID: "
              << request->user_id();
    response->set_success(false);
    response->set_error_message("Database query failed");
    done(response);
    return;
  }

  if (rs->next()) {
    // 从数据库结果创建用户对象
    User user(rs->getUInt64("id"), std::string(rs->getString("username")));
    user.setNickname(std::string(rs->getString("nickname")));
    user.setEmail(std::string(rs->getString("email")));
    user.setStatus(static_cast<starrychat::UserStatus>(rs->getInt("status")));

    if (!rs->isNull("avatar_url")) {
      user.setAvatarUrl(std::string(rs->getString("avatar_url")));
    }

    if (!rs->isNull("last_login_time")) {
      user.setLastLoginTime(rs->getUInt64("last_login_time"));
    }

    LOG_INFO << "Loaded user from DB - ID: " << user.getId()
             << ", Username: " << user.getUsername()
             << ", Nickname: " << user.getNickname();

    // 设置响应
    response->set_success(true);
    *response->mutable_user_info() = user.toProto();

    // 缓存用户信息
    cacheUserInfo(user);
  } else {
    response->set_success(false);
    response->set_error_message("User not found");
    LOG_WARN << "User not found with ID: " << request->user_id();
  }

  done(response);
}

// 更新用户资料
void UserServiceImpl::UpdateProfile(
    const starrychat::UpdateProfileRequestPtr& request,
    const starrychat::UpdateProfileResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  // 检查是否有字段要更新
  bool hasUpdates = !request->nickname().empty() || !request->email().empty() ||
                    !request->avatar_url().empty();

  if (!hasUpdates) {
    response->set_success(false);
    response->set_error_message("No fields to update");
    done(response);
    return;
  }

  // 构建更新查询
  std::string updateQuery = "UPDATE users SET ";
  std::vector<std::string> updateFields;
  std::vector<std::any> updateValues;

  if (!request->nickname().empty()) {
    updateFields.push_back("nickname = ?");
    updateValues.push_back(request->nickname());
  }

  if (!request->email().empty()) {
    updateFields.push_back("email = ?");
    updateValues.push_back(request->email());
  }

  if (!request->avatar_url().empty()) {
    updateFields.push_back("avatar_url = ?");
    updateValues.push_back(request->avatar_url());
  }

  // 将字段连接起来
  for (size_t i = 0; i < updateFields.size(); ++i) {
    if (i > 0)
      updateQuery += ", ";
    updateQuery += updateFields[i];
  }

  updateQuery += " WHERE id = ?";

  // 执行更新 - 分别处理不同的字段组合
  bool updateSuccess = false;

  if (!request->nickname().empty() && !request->email().empty() &&
      !request->avatar_url().empty()) {
    updateSuccess = DBManager::executeUpdate(
        updateQuery, request->nickname(), request->email(),
        request->avatar_url(), request->user_id());
  } else if (!request->nickname().empty() && !request->email().empty()) {
    updateSuccess = DBManager::executeUpdate(
        updateQuery, request->nickname(), request->email(), request->user_id());
  } else if (!request->nickname().empty() && !request->avatar_url().empty()) {
    updateSuccess =
        DBManager::executeUpdate(updateQuery, request->nickname(),
                                 request->avatar_url(), request->user_id());
  } else if (!request->email().empty() && !request->avatar_url().empty()) {
    updateSuccess =
        DBManager::executeUpdate(updateQuery, request->email(),
                                 request->avatar_url(), request->user_id());
  } else if (!request->nickname().empty()) {
    updateSuccess = DBManager::executeUpdate(updateQuery, request->nickname(),
                                             request->user_id());
  } else if (!request->email().empty()) {
    updateSuccess = DBManager::executeUpdate(updateQuery, request->email(),
                                             request->user_id());
  } else if (!request->avatar_url().empty()) {
    updateSuccess = DBManager::executeUpdate(updateQuery, request->avatar_url(),
                                             request->user_id());
  }

  if (!updateSuccess) {
    LOG_ERROR << "Failed to update profile for user ID: " << request->user_id();
    response->set_success(false);
    response->set_error_message("Failed to update profile");
    done(response);
    return;
  }

  // 获取更新后的用户信息
  std::unique_ptr<sql::ResultSet> rs;
  if (!DBManager::executeQuery("SELECT * FROM users WHERE id = ?", rs,
                               request->user_id())) {
    LOG_ERROR
        << "Failed to retrieve updated user info after profile update for ID: "
        << request->user_id();
    response->set_success(false);
    response->set_error_message("Failed to retrieve updated user info");
    done(response);
    return;
  }

  if (rs->next()) {
    User user(rs->getUInt64("id"), std::string(rs->getString("username")));
    user.setNickname(std::string(rs->getString("nickname")));
    user.setEmail(std::string(rs->getString("email")));
    user.setStatus(static_cast<starrychat::UserStatus>(rs->getInt("status")));

    if (!rs->isNull("avatar_url")) {
      user.setAvatarUrl(std::string(rs->getString("avatar_url")));
    }

    if (!rs->isNull("last_login_time")) {
      user.setLastLoginTime(rs->getUInt64("last_login_time"));
    }

    // 更新缓存
    cacheUserInfo(user);

    // 发布更新通知
    auto& redis = RedisManager::getInstance();
    redis.publish("user:profile:updated", std::to_string(user.getId()));

    response->set_success(true);
    *response->mutable_user_info() = user.toProto();

    LOG_INFO << "Updated profile for user ID: " << user.getId();
  } else {
    response->set_success(false);
    response->set_error_message("User not found after update");
  }

  done(response);
}

// 获取好友列表
void UserServiceImpl::GetFriends(
    const starrychat::GetFriendsRequestPtr& request,
    const starrychat::GetFriendsResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();
  auto& redis = RedisManager::getInstance();

  // 从缓存尝试获取好友列表
  std::string friendsKey = "user:friends:" + std::to_string(request->user_id());
  auto cachedFriends = redis.get(friendsKey);

  if (cachedFriends) {
    // 尝试解析缓存的好友列表
    try {
      starrychat::GetFriendsResponse cachedResponse;
      if (cachedResponse.ParseFromString(*cachedFriends)) {
        *response = cachedResponse;
        done(response);
        return;
      }
    } catch (...) {
      // 解析错误，继续获取新数据
      LOG_WARN << "Failed to parse cached friends list for user "
               << request->user_id();
    }
  }

  // 从数据库获取好友列表
  std::unique_ptr<sql::ResultSet> rs;
  if (!DBManager::executeQuery(
          "SELECT id, nickname, status FROM users WHERE id != ? LIMIT 100", rs,
          request->user_id())) {
    LOG_ERROR << "Failed to query friends list for user ID: "
              << request->user_id();
    response->set_success(false);
    response->set_error_message("Failed to query friends");
    done(response);
    return;
  }

  response->set_success(true);
  while (rs->next()) {
    auto* friend_info = response->add_friends();
    friend_info->set_id(rs->getUInt64("id"));
    friend_info->set_nickname(rs->getString("nickname"));
    friend_info->set_status(
        static_cast<starrychat::UserStatus>(rs->getInt("status")));

    // 从Redis获取实时状态
    auto statusStr =
        redis.hget("user:status", std::to_string(friend_info->id()));
    if (statusStr) {
      friend_info->set_status(
          static_cast<starrychat::UserStatus>(std::stoi(*statusStr)));
    }
  }

  // 缓存好友列表（短期缓存）
  std::string serialized;
  if (response->SerializeToString(&serialized)) {
    redis.set(friendsKey, serialized, std::chrono::minutes(5));
  }

  LOG_INFO << "Retrieved friends list for user " << request->user_id()
           << ", count: " << response->friends_size();

  done(response);
}

// 用户注销
void UserServiceImpl::Logout(
    const starrychat::LogoutRequestPtr& request,
    const starrychat::LogoutResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  // 验证会话
  if (!validateSession(request->session_token(), request->user_id())) {
    response->set_success(false);
    response->set_error_message("Invalid session");
    done(response);
    return;
  }

  uint64_t userId = request->user_id();

  // 移除会话
  removeSession(request->session_token());

  // 更新用户状态为离线
  updateUserStatusInCache(userId, starrychat::USER_STATUS_OFFLINE);

  // 从在线用户集合中移除
  auto& redis = RedisManager::getInstance();
  redis.srem("users:online", std::to_string(userId));

  // 清除心跳检测
  redis.del("user:heartbeat:" + std::to_string(userId));

  // 更新数据库状态
  DBManager::executeUpdate("UPDATE users SET status = ? WHERE id = ?",
                           static_cast<int>(starrychat::USER_STATUS_OFFLINE),
                           userId);

  response->set_success(true);
  LOG_INFO << "User logged out: " << userId;

  done(response);
}

// 更新用户状态
void UserServiceImpl::UpdateStatus(
    const starrychat::UserStatusUpdatePtr& request,
    const starrychat::UserInfo* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  auto& redis = RedisManager::getInstance();
  uint64_t userId = request->user_id();
  starrychat::UserStatus newStatus = request->status();

  LOG_INFO << "Updating status for user " << userId << " to "
           << static_cast<int>(newStatus);

  // 更新Redis中的用户状态
  updateUserStatusInCache(userId, newStatus);

  // 管理在线用户集合
  if (newStatus == starrychat::USER_STATUS_ONLINE ||
      newStatus == starrychat::USER_STATUS_BUSY ||
      newStatus == starrychat::USER_STATUS_AWAY) {
    // 用户处于某种在线状态
    redis.sadd("users:online", std::to_string(userId));

    // 设置或更新心跳，5分钟过期
    redis.set("user:heartbeat:" + std::to_string(userId), "1",
              std::chrono::minutes(5));

    LOG_INFO << "User " << userId
             << " added to online users set with heartbeat";
  } else if (newStatus == starrychat::USER_STATUS_OFFLINE) {
    // 用户离线，从在线集合移除
    redis.srem("users:online", std::to_string(userId));

    // 移除心跳检测
    redis.del("user:heartbeat:" + std::to_string(userId));

    LOG_INFO << "User " << userId << " removed from online users set";
  }

  // 发布状态变更通知
  std::string notification = std::to_string(userId) + ":" +
                             std::to_string(static_cast<int>(newStatus));
  redis.publish("user:status:changed", notification);
  LOG_INFO << "Published status change notification: " << notification;

  // 更新数据库
  if (DBManager::executeUpdate("UPDATE users SET status = ? WHERE id = ?",
                               static_cast<int>(newStatus), userId)) {
    // 查询完整的用户信息
    std::unique_ptr<sql::ResultSet> rs;
    if (!DBManager::executeQuery("SELECT * FROM users WHERE id = ?", rs,
                                 userId)) {
      LOG_ERROR
          << "Failed to query complete user info after status update for ID: "
          << userId;
    } else if (rs->next()) {
      User user(rs->getUInt64("id"), std::string(rs->getString("username")));
      user.setNickname(std::string(rs->getString("nickname")));
      user.setEmail(std::string(rs->getString("email")));
      user.setStatus(newStatus);

      if (!rs->isNull("avatar_url")) {
        user.setAvatarUrl(std::string(rs->getString("avatar_url")));
      }

      if (!rs->isNull("last_login_time")) {
        user.setLastLoginTime(rs->getUInt64("last_login_time"));
      }

      // 更新Redis用户缓存的完整信息
      cacheUserInfo(user);

      *response = user.toProto();
      LOG_INFO << "User status updated successfully for user " << userId;
    }
  } else {
    LOG_ERROR << "Failed to update user status in database for ID: " << userId;
  }

  done(response);
}

// 心跳更新
void UserServiceImpl::UpdateHeartbeat(
    const starrychat::UserHeartbeatRequestPtr& request,
    const starrychat::HeartbeatResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  // 验证会话
  if (validateSession(request->session_token(), request->user_id())) {
    auto& redis = RedisManager::getInstance();
    uint64_t userId = request->user_id();

    // 更新心跳，设置5分钟过期
    redis.set("user:heartbeat:" + std::to_string(userId), "1",
              std::chrono::minutes(5));

    // 确保用户在在线集合中
    redis.sadd("users:online", std::to_string(userId));

    // 获取当前用户状态
    auto statusStr = redis.hget("user:status", std::to_string(userId));
    starrychat::UserStatus currentStatus = starrychat::USER_STATUS_OFFLINE;

    if (statusStr) {
      currentStatus =
          static_cast<starrychat::UserStatus>(std::stoi(*statusStr));
    }

    // 如果状态是离线，则更新为在线
    if (currentStatus == starrychat::USER_STATUS_OFFLINE) {
      // 更新为在线状态
      updateUserStatusInCache(userId, starrychat::USER_STATUS_ONLINE);
      LOG_INFO << "User " << userId
               << " status updated to ONLINE via heartbeat";
    }

    response->set_success(true);
    LOG_INFO << "Updated heartbeat for user " << userId;
  } else {
    response->set_success(false);
    LOG_WARN << "Invalid session in heartbeat update for user "
             << request->user_id();
  }

  done(response);
}

// 生成会话令牌
std::string UserServiceImpl::generateSessionToken(uint64_t userId) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 15);

  // 当前时间戳
  auto now = std::chrono::system_clock::now();
  auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
  auto epoch = now_ms.time_since_epoch();
  auto value =
      std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();

  // 生成随机令牌
  std::stringstream ss;
  ss << std::hex << userId << "-" << value << "-";
  for (int i = 0; i < 16; i++) {
    ss << std::hex << dis(gen);
  }

  return ss.str();
}

// 验证会话
bool UserServiceImpl::validateSession(const std::string& token,
                                      uint64_t userId) {
  auto& redis = RedisManager::getInstance();

  // 检查会话token是否存在
  auto sessionUserId = redis.get("session:" + token);
  if (!sessionUserId) {
    LOG_WARN << "Session token not found for user " << userId;
    return false;
  }

  // 验证用户ID是否匹配
  if (std::stoull(*sessionUserId) != userId) {
    LOG_WARN << "Session token user ID mismatch for user " << userId;
    return false;
  }

  // 检查这是否是用户的当前会话
  auto currentToken = redis.get("user:session:" + std::to_string(userId));
  if (!currentToken || *currentToken != token) {
    // 这是一个旧会话
    LOG_WARN << "Session token is old/invalid for user " << userId;
    return false;
  }

  // 刷新会话和心跳时间
  redis.expire("session:" + token, std::chrono::hours(24));
  redis.expire("user:session:" + std::to_string(userId),
               std::chrono::hours(24));

  // 更新心跳
  redis.set("user:heartbeat:" + std::to_string(userId), "1",
            std::chrono::minutes(5));

  // 确保用户在在线集合中
  redis.sadd("users:online", std::to_string(userId));

  return true;
}

// 存储会话
void UserServiceImpl::storeSession(const std::string& token, uint64_t userId) {
  auto& redis = RedisManager::getInstance();

  // 存储会话令牌，24小时过期
  redis.set("session:" + token, std::to_string(userId), std::chrono::hours(24));

  // 移除旧会话（如果存在）
  std::string userSessionKey = "user:session:" + std::to_string(userId);
  auto oldToken = redis.get(userSessionKey);
  if (oldToken) {
    // 删除旧会话
    redis.del("session:" + *oldToken);
  }

  // 记录用户当前会话
  redis.set(userSessionKey, token, std::chrono::hours(24));
}

// 移除会话
void UserServiceImpl::removeSession(const std::string& token) {
  auto& redis = RedisManager::getInstance();

  // 获取用户ID
  auto userIdStr = redis.get("session:" + token);
  if (userIdStr) {
    // 移除用户会话记录
    redis.del("user:session:" + *userIdStr);
  }

  // 移除会话令牌
  redis.del("session:" + token);

  LOG_INFO << "Removed session: " << token;
}

// 更新用户在线状态
void UserServiceImpl::updateUserOnlineStatus(uint64_t userId,
                                             starrychat::UserStatus status) {
  updateUserStatusInCache(userId, status);
}

// 缓存用户信息
void UserServiceImpl::cacheUserInfo(const User& user) {
  auto& redis = RedisManager::getInstance();
  std::string userKey = "user:" + std::to_string(user.getId());

  // 缓存基本信息
  redis.hset(userKey, "username", user.getUsername());
  redis.hset(userKey, "nickname", user.getNickname());
  redis.hset(userKey, "email", user.getEmail());

  // 可选字段
  if (!user.getAvatarUrl().empty()) {
    redis.hset(userKey, "avatar_url", user.getAvatarUrl());
  }

  // 状态信息
  redis.hset(userKey, "status",
             std::to_string(static_cast<int>(user.getStatus())));

  // 时间相关字段
  redis.hset(userKey, "created_time", std::to_string(user.getCreatedTime()));
  redis.hset(userKey, "last_login_time",
             std::to_string(user.getLastLoginTime()));

  // 用户名到ID映射
  redis.hset("username:to:id", user.getUsername(),
             std::to_string(user.getId()));

  // 设置缓存过期时间
  redis.expire(userKey, std::chrono::hours(24));

  LOG_INFO << "Cached user information for " << user.getUsername()
           << " (ID: " << user.getId() << ")";
}

// 从缓存获取用户信息
std::optional<User> UserServiceImpl::getUserFromCache(uint64_t userId) {
  auto& redis = RedisManager::getInstance();

  // 尝试从Redis缓存获取用户信息
  std::string userKey = "user:" + std::to_string(userId);
  auto userData = redis.hgetall(userKey);

  if (userData && !userData->empty() &&
      userData->find("username") != userData->end()) {
    // 从缓存构建用户对象
    User user(userId, (*userData)["username"]);

    if (userData->find("nickname") != userData->end())
      user.setNickname((*userData)["nickname"]);

    if (userData->find("email") != userData->end())
      user.setEmail((*userData)["email"]);

    if (userData->find("avatar_url") != userData->end())
      user.setAvatarUrl((*userData)["avatar_url"]);

    if (userData->find("status") != userData->end())
      user.setStatus(static_cast<starrychat::UserStatus>(
          std::stoi((*userData)["status"])));

    if (userData->find("last_login_time") != userData->end())
      user.setLastLoginTime(std::stoull((*userData)["last_login_time"]));

    // 刷新缓存过期时间
    redis.expire(userKey, std::chrono::hours(24));

    return user;
  }

  return std::nullopt;
}

// 使缓存中的用户信息失效
void UserServiceImpl::invalidateUserCache(uint64_t userId) {
  auto& redis = RedisManager::getInstance();

  // 获取用户名
  auto user = getUserFromCache(userId);
  if (user) {
    // 移除用户名到ID映射
    redis.hdel("username:to:id", user->getUsername());
  }

  // 删除用户缓存
  redis.del("user:" + std::to_string(userId));

  LOG_INFO << "Invalidated cache for user ID: " << userId;
}

// 更新缓存中的用户状态
void UserServiceImpl::updateUserStatusInCache(uint64_t userId,
                                              starrychat::UserStatus status) {
  auto& redis = RedisManager::getInstance();

  // 更新用户状态哈希表
  redis.hset("user:status", std::to_string(userId),
             std::to_string(static_cast<int>(status)));

  // 更新用户信息缓存中的状态
  std::string userKey = "user:" + std::to_string(userId);
  redis.hset(userKey, "status", std::to_string(static_cast<int>(status)));

  // 管理在线用户集合
  if (status == starrychat::USER_STATUS_ONLINE ||
      status == starrychat::USER_STATUS_BUSY ||
      status == starrychat::USER_STATUS_AWAY) {
    redis.sadd("users:online", std::to_string(userId));
  } else {
    redis.srem("users:online", std::to_string(userId));
  }

  LOG_INFO << "Updated status in cache for user " << userId << " to "
           << static_cast<int>(status);
}

}  // namespace StarryChat
