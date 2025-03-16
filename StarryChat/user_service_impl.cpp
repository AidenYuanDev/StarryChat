#include "user_service_impl.h"

#include <chrono>
#include <mariadb/conncpp.hpp>
#include <random>
#include <sstream>
#include "db_manager.h"
#include "logging.h"
#include "redis_manager.h"
#include "user.h"

namespace StarryChat {

std::shared_ptr<sql::Connection> UserServiceImpl::getConnection() {
  return DBManager::getInstance().getConnection();
}

// 用户注册
void UserServiceImpl::RegisterUser(
    const starrychat::RegisterUserRequestPtr& request,
    const starrychat::RegisterUserResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  LOG_INFO << "RegisterUser called with username: [" << request->username()
           << "], length: " << request->username().length() << ", email: ["
           << request->email() << "]";

  // 确保用户名不为空
  if (request->username().empty()) {
    auto response = responsePrototype->New();
    response->set_success(false);
    response->set_error_message("Username cannot be empty");
    done(response);
    return;
  }

  auto response = responsePrototype->New();

  try {
    auto& redis = RedisManager::getInstance();

    // 快速检查用户名是否已存在 (Redis)
    auto existingId = redis.hget("username:to:id", request->username());
    if (existingId) {
      response->set_success(false);
      response->set_error_message("Username already exists");
      done(response);
      return;
    }

    auto conn = getConnection();
    if (!conn) {
      response->set_success(false);
      response->set_error_message("Database connection failed");
      done(response);
      return;
    }

    // 再次检查用户名是否存在 (数据库)
    std::unique_ptr<sql::PreparedStatement> checkStmt(
        conn->prepareStatement("SELECT 1 FROM users WHERE username = ?"));
    checkStmt->setString(1, request->username());

    std::unique_ptr<sql::ResultSet> checkRs(checkStmt->executeQuery());
    if (checkRs->next()) {
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
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement("INSERT INTO users (username, nickname, email, "
                               "status, created_time, password_hash, salt) "
                               "VALUES (?, ?, ?, ?, ?, ?, ?)",
                               sql::Statement::RETURN_GENERATED_KEYS));

    stmt->setString(1, user.getUsername());
    stmt->setString(2, user.getNickname());
    stmt->setString(3, user.getEmail());
    stmt->setInt(4, static_cast<int>(starrychat::USER_STATUS_OFFLINE));
    stmt->setUInt64(5, currentTime);
    stmt->setString(6, user.getPasswordHash());
    stmt->setString(7, user.getSalt());

    int result = stmt->executeUpdate();
    LOG_INFO << "SQL execution result: "
             << (result > 0 ? "success" : "failure");

    if (result > 0) {
      // 获取新用户ID
      std::unique_ptr<sql::ResultSet> rs(stmt->getGeneratedKeys());
      if (rs->next()) {
        uint64_t userId = rs->getUInt64(1);
        user.setId(userId);

        // 缓存用户信息
        cacheUserInfo(user);

        // 成功响应
        response->set_success(true);
        *response->mutable_user_info() = user.toProto();

        LOG_INFO << "User registered - ID: " << user.getId()
                 << ", Username: " << user.getUsername()
                 << ", Nickname: " << user.getNickname();
      } else {
        response->set_success(false);
        response->set_error_message("Failed to get new user ID");
      }
    } else {
      response->set_success(false);
      response->set_error_message("Failed to insert user");
    }
  } catch (sql::SQLException& e) {
    LOG_ERROR << "RegisterUser SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "RegisterUser error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 用户登录
void UserServiceImpl::Login(const starrychat::LoginRequestPtr& request,
                            const starrychat::LoginResponse* responsePrototype,
                            const starry::RpcDoneCallback& done) {
  LOG_INFO << "Login called with username: [" << request->username()
           << "], length: " << request->username().length();

  // 确保用户名不为空
  if (request->username().empty()) {
    auto response = responsePrototype->New();
    response->set_success(false);
    response->set_error_message("Username cannot be empty");
    done(response);
    return;
  }

  auto response = responsePrototype->New();

  try {
    auto& redis = RedisManager::getInstance();

    // 从Redis缓存尝试获取用户ID
    auto cachedUserId = redis.hget("username:to:id", request->username());
    uint64_t userId = 0;

    if (cachedUserId) {
      userId = std::stoull(*cachedUserId);
      LOG_INFO << "Found cached user ID mapping: " << request->username()
               << " -> " << userId;
    }

    // 从数据库查询用户（主要是为了验证密码）
    auto conn = getConnection();
    if (!conn) {
      response->set_success(false);
      response->set_error_message("Database connection failed");
      done(response);
      return;
    }

    // 查询语句，如果已知用户ID，按ID查询效率更高
    std::unique_ptr<sql::PreparedStatement> stmt;
    if (userId > 0) {
      stmt.reset(conn->prepareStatement("SELECT * FROM users WHERE id = ?"));
      stmt->setUInt64(1, userId);
    } else {
      stmt.reset(
          conn->prepareStatement("SELECT * FROM users WHERE username = ?"));
      stmt->setString(1, request->username());
    }

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    if (!rs->next()) {
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

    if (!rs->isNull("created_time")) {
      user.setId(rs->getUInt64("created_time"));
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
      std::unique_ptr<sql::PreparedStatement> updateStmt(conn->prepareStatement(
          "UPDATE users SET login_attempts = login_attempts + 1 WHERE id = ?"));
      updateStmt->setUInt64(1, userId);
      updateStmt->executeUpdate();

      done(response);
      return;
    }

    // 登录成功，更新用户状态和登录时间
    uint64_t currentTime = std::time(nullptr);
    std::unique_ptr<sql::PreparedStatement> updateStmt(
        conn->prepareStatement("UPDATE users SET status = ?, last_login_time = "
                               "?, login_attempts = 0 WHERE id = ?"));
    updateStmt->setInt(1, static_cast<int>(starrychat::USER_STATUS_ONLINE));
    updateStmt->setUInt64(2, currentTime);
    updateStmt->setUInt64(3, userId);
    updateStmt->executeUpdate();

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
  } catch (sql::SQLException& e) {
    LOG_ERROR << "Login SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "Login error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 获取用户信息
void UserServiceImpl::GetUser(
    const starrychat::GetUserRequestPtr& request,
    const starrychat::GetUserResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
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
    auto conn = getConnection();
    if (!conn) {
      response->set_success(false);
      response->set_error_message("Database connection failed");
      done(response);
      return;
    }

    // 查询用户
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement("SELECT * FROM users WHERE id = ?"));
    stmt->setUInt64(1, request->user_id());

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    if (rs->next()) {
      // 从数据库结果创建用户对象
      User user(rs->getUInt64("id"), std::string(rs->getString("username")));
      user.setNickname(std::string(rs->getString("nickname")));
      user.setEmail(std::string(rs->getString("email")));
      user.setStatus(static_cast<starrychat::UserStatus>(rs->getInt("status")));

      if (!rs->isNull("avatar_url")) {
        user.setAvatarUrl(std::string(rs->getString("avatar_url")));
      }

      // if (!rs->isNull("created_time")) {
      //   user.setCreatedTime(rs->getUInt64("created_time"));
      // }

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
  } catch (sql::SQLException& e) {
    LOG_ERROR << "GetUser SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "GetUser error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 更新用户资料
void UserServiceImpl::UpdateProfile(
    const starrychat::UpdateProfileRequestPtr& request,
    const starrychat::UpdateProfileResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
    // 更新数据库
    auto conn = getConnection();
    if (!conn) {
      response->set_success(false);
      response->set_error_message("Database connection failed");
      done(response);
      return;
    }

    // 更新用户资料
    std::string updateQuery = "UPDATE users SET ";
    bool hasUpdates = false;

    if (!request->nickname().empty()) {
      updateQuery += "nickname = ?";
      hasUpdates = true;
    }

    if (!request->email().empty()) {
      if (hasUpdates)
        updateQuery += ", ";
      updateQuery += "email = ?";
      hasUpdates = true;
    }

    if (!request->avatar_url().empty()) {
      if (hasUpdates)
        updateQuery += ", ";
      updateQuery += "avatar_url = ?";
      hasUpdates = true;
    }

    if (!hasUpdates) {
      response->set_success(false);
      response->set_error_message("No fields to update");
      done(response);
      return;
    }

    updateQuery += " WHERE id = ?";

    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(updateQuery));
    int paramIndex = 1;

    if (!request->nickname().empty()) {
      stmt->setString(paramIndex++, request->nickname());
    }

    if (!request->email().empty()) {
      stmt->setString(paramIndex++, request->email());
    }

    if (!request->avatar_url().empty()) {
      stmt->setString(paramIndex++, request->avatar_url());
    }

    stmt->setUInt64(paramIndex, request->user_id());

    if (stmt->executeUpdate() > 0) {
      // 查询更新后的用户信息
      std::unique_ptr<sql::PreparedStatement> selectStmt(
          conn->prepareStatement("SELECT * FROM users WHERE id = ?"));
      selectStmt->setUInt64(1, request->user_id());

      std::unique_ptr<sql::ResultSet> rs(selectStmt->executeQuery());
      if (rs->next()) {
        User user(rs->getUInt64("id"), std::string(rs->getString("username")));
        user.setNickname(std::string(rs->getString("nickname")));
        user.setEmail(std::string(rs->getString("email")));
        user.setStatus(
            static_cast<starrychat::UserStatus>(rs->getInt("status")));

        if (!rs->isNull("avatar_url")) {
          user.setAvatarUrl(std::string(rs->getString("avatar_url")));
        }

        // if (!rs->isNull("created_time")) {
        //   user.setCreatedTime(rs->getUInt64("created_time"));
        // }

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
    } else {
      response->set_success(false);
      response->set_error_message("Update failed");
    }
  } catch (sql::SQLException& e) {
    LOG_ERROR << "UpdateProfile SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "UpdateProfile error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 获取好友列表
void UserServiceImpl::GetFriends(
    const starrychat::GetFriendsRequestPtr& request,
    const starrychat::GetFriendsResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
    auto& redis = RedisManager::getInstance();

    // 从缓存尝试获取好友列表
    std::string friendsKey =
        "user:friends:" + std::to_string(request->user_id());
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
    auto conn = getConnection();
    if (!conn) {
      response->set_success(false);
      response->set_error_message("Database connection failed");
      done(response);
      return;
    }

    // 简单实现：返回所有其他用户作为"好友"
    // 实际应用中需要好友关系表
    std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
        "SELECT id, nickname, status FROM users WHERE id != ? LIMIT 100"));
    stmt->setUInt64(1, request->user_id());

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

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
  } catch (sql::SQLException& e) {
    LOG_ERROR << "GetFriends SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "GetFriends error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 用户注销
void UserServiceImpl::Logout(
    const starrychat::LogoutRequestPtr& request,
    const starrychat::LogoutResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
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
    auto conn = getConnection();
    if (conn) {
      std::unique_ptr<sql::PreparedStatement> stmt(
          conn->prepareStatement("UPDATE users SET status = ? WHERE id = ?"));
      stmt->setInt(1, static_cast<int>(starrychat::USER_STATUS_OFFLINE));
      stmt->setUInt64(2, userId);
      stmt->executeUpdate();
    }

    response->set_success(true);
    LOG_INFO << "User logged out: " << userId;
  } catch (sql::SQLException& e) {
    LOG_ERROR << "Logout SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "Logout error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 更新用户状态
void UserServiceImpl::UpdateStatus(
    const starrychat::UserStatusUpdatePtr& request,
    const starrychat::UserInfo* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
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

    // 更新数据库（保持数据一致性）
    auto conn = getConnection();
    if (conn) {
      std::unique_ptr<sql::PreparedStatement> stmt(
          conn->prepareStatement("UPDATE users SET status = ? WHERE id = ?"));
      stmt->setInt(1, static_cast<int>(newStatus));
      stmt->setUInt64(2, userId);
      stmt->executeUpdate();

      // 查询完整的用户信息
      std::unique_ptr<sql::PreparedStatement> selectStmt(
          conn->prepareStatement("SELECT * FROM users WHERE id = ?"));
      selectStmt->setUInt64(1, userId);

      std::unique_ptr<sql::ResultSet> rs(selectStmt->executeQuery());
      if (rs->next()) {
        User user(rs->getUInt64("id"), std::string(rs->getString("username")));
        user.setNickname(std::string(rs->getString("nickname")));
        user.setEmail(std::string(rs->getString("email")));
        user.setStatus(newStatus);

        if (!rs->isNull("avatar_url")) {
          user.setAvatarUrl(std::string(rs->getString("avatar_url")));
        }

        // if (!rs->isNull("created_time")) {
        //   user.setCreatedTime(rs->getUInt64("created_time"));
        // }

        if (!rs->isNull("last_login_time")) {
          user.setLastLoginTime(rs->getUInt64("last_login_time"));
        }

        // 更新Redis用户缓存的完整信息
        cacheUserInfo(user);

        *response = user.toProto();
        LOG_INFO << "User status updated successfully for user " << userId;
      }
    }
  } catch (sql::SQLException& e) {
    LOG_ERROR << "UpdateStatus SQL error: " << e.what();
  } catch (std::exception& e) {
    LOG_ERROR << "UpdateStatus error: " << e.what();
  }

  done(response);
}

// 心跳更新
void UserServiceImpl::UpdateHeartbeat(
    const starrychat::UserHeartbeatRequestPtr& request,
    const starrychat::HeartbeatResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
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
  } catch (std::exception& e) {
    LOG_ERROR << "UpdateHeartbeat error: " << e.what();
    response->set_success(false);
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

    // if (userData->find("created_time") != userData->end())
      // user.setCreatedTime(std::stoull((*userData)["created_time"]));

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
