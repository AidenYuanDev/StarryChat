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
  LOG_INFO << "RegisterUser request: " << request->ShortDebugString();
  LOG_INFO << "Request binary size: " << request->ByteSizeLong();

  // 确保用户名不为空
  if (request->username().empty()) {
    auto response = responsePrototype->New();
    response->set_success(false);
    response->set_error_message("Username cannot be empty");
    done(response);
    return;
  }
  auto response = responsePrototype->New();

  // Modified section from user_service_impl.cpp
  // Inside RegisterUser method

  try {
    auto conn = getConnection();

    LOG_INFO << "Checking if username exists: " << request->username();

    // 检查用户名是否已存在
    std::unique_ptr<sql::PreparedStatement> checkStmt(
        conn->prepareStatement("SELECT 1 FROM users WHERE username = ?"));
    checkStmt->setString(1, request->username());

    std::unique_ptr<sql::ResultSet> checkRs(checkStmt->executeQuery());
    bool userExists =
        checkRs->next();  // Store the result to avoid calling next() twice
    if (userExists) {
      response->set_success(false);
      response->set_error_message("Username already exists");
      done(response);
      return;
    }
    LOG_INFO << "Username exists check result: "
             << (userExists ? "exists" : "new");

    // 创建用户对象处理密码
    User user(0, request->username());
    user.setPassword(request->password());
    user.setNickname(request->nickname());
    user.setEmail(request->email());
    user.setStatus(starrychat::USER_STATUS_OFFLINE);
    LOG_INFO << "Executing insert SQL for username: " << user.getUsername();

    // 插入新用户
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement("INSERT INTO users (username, nickname, email, "
                               "status, created_time, password_hash, salt) "
                               "VALUES (?, ?, ?, ?, ?, ?, ?)",
                               sql::Statement::RETURN_GENERATED_KEYS));

    // 先设置所有参数
    stmt->setString(1, user.getUsername());
    stmt->setString(2, user.getNickname());
    stmt->setString(3, user.getEmail());
    stmt->setInt(4, static_cast<int>(starrychat::USER_STATUS_OFFLINE));
    stmt->setUInt64(5, std::time(nullptr));
    stmt->setString(6, user.getPasswordHash());
    stmt->setString(7, user.getSalt());

    // 执行SQL（仅执行一次）
    int result = stmt->executeUpdate();
    LOG_INFO << "SQL execution result: "
             << (result > 0 ? "success" : "failure");

    if (result > 0) {  // 使用已存储的结果，而不是再次执行
      // 获取新用户ID
      std::unique_ptr<sql::ResultSet> rs(stmt->getGeneratedKeys());
      if (rs->next()) {
        uint64_t userId = rs->getUInt64(1);
        user.setId(userId);

        // 成功响应
        response->set_success(true);
        *response->mutable_user_info() = user.toProto();
      } else {
        response->set_success(false);
        response->set_error_message("Failed to get new user ID");
      }
    } else {
      response->set_success(false);
      response->set_error_message("Failed to insert user");
    }
    LOG_INFO << "User registered - ID: " << user.getId()
             << ", Username: " << user.getUsername()
             << ", Nickname: " << user.getNickname();
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
  // 添加更详细的日志
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
    auto conn = getConnection();

    // 查询用户
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement("SELECT * FROM users WHERE username = ?"));
    stmt->setString(1, request->username());

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    if (!rs->next()) {
      response->set_success(false);
      response->set_error_message("User not found");
      done(response);
      return;
    }

    // 创建用户对象并验证密码
    User user(rs->getUInt64("id"), std::string(rs->getString("username")));
    // ...设置其他字段
    user.setNickname(std::string(rs->getString("nickname")));
    user.setEmail(std::string(rs->getString("email")));
    user.setStatus(static_cast<starrychat::UserStatus>(rs->getInt("status")));

    // 设置密码相关字段 - 这是关键
    std::string passwordHash = std::string(rs->getString("password_hash"));
    std::string salt = std::string(rs->getString("salt"));
    // 需要在User类中添加一个方法来直接设置密码哈希和盐值
    user.setPasswordHashAndSalt(passwordHash, salt);

    if (!user.verifyPassword(request->password())) {
      response->set_success(false);
      response->set_error_message("Invalid password");
      done(response);
      return;
    }

    // 更新用户状态和登录时间
    std::unique_ptr<sql::PreparedStatement> updateStmt(conn->prepareStatement(
        "UPDATE users SET status = ?, last_login_time = ? WHERE id = ?"));
    updateStmt->setInt(1, static_cast<int>(starrychat::USER_STATUS_ONLINE));
    updateStmt->setUInt64(2, std::time(nullptr));
    updateStmt->setUInt64(3, user.getId());
    updateStmt->executeUpdate();

    // 生成会话令牌
    std::string sessionToken = generateSessionToken(user.getId());
    storeSession(sessionToken, user.getId());

    // 更新Redis用户状态
    updateUserOnlineStatus(user.getId(), starrychat::USER_STATUS_ONLINE);

    // 设置响应
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
    auto conn = getConnection();

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
      // ...设置其他字段
      LOG_INFO << "Loaded user from DB - ID: " << user.getId()
               << ", Username: " << user.getUsername()
               << ", Nickname: " << user.getNickname();

      // 设置响应
      response->set_success(true);
      *response->mutable_user_info() = user.toProto();
    } else {
      response->set_success(false);
      response->set_error_message("User not found");
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
    auto conn = getConnection();

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
        // ...设置其他字段

        response->set_success(true);
        *response->mutable_user_info() = user.toProto();
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
    auto conn = getConnection();

    // 简单实现：返回所有其他用户作为"好友"
    // 实际应用中可能需要好友关系表
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
    }
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

    auto conn = getConnection();

    // 更新用户状态
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement("UPDATE users SET status = ? WHERE id = ?"));
    stmt->setInt(1, static_cast<int>(starrychat::USER_STATUS_OFFLINE));
    stmt->setUInt64(2, request->user_id());

    stmt->executeUpdate();

    // 移除会话
    removeSession(request->session_token());

    // 更新Redis状态
    updateUserOnlineStatus(request->user_id(), starrychat::USER_STATUS_OFFLINE);

    response->set_success(true);
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
    auto conn = getConnection();

    // 更新用户状态
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement("UPDATE users SET status = ? WHERE id = ?"));
    stmt->setInt(1, static_cast<int>(request->status()));
    stmt->setUInt64(2, request->user_id());

    if (stmt->executeUpdate() > 0) {
      // 更新Redis状态
      updateUserOnlineStatus(request->user_id(), request->status());

      // 查询更新后的用户信息
      std::unique_ptr<sql::PreparedStatement> selectStmt(
          conn->prepareStatement("SELECT * FROM users WHERE id = ?"));
      selectStmt->setUInt64(1, request->user_id());

      std::unique_ptr<sql::ResultSet> rs(selectStmt->executeQuery());
      if (rs->next()) {
        User user(rs->getUInt64("id"), std::string(rs->getString("username")));
        // ...设置其他字段

        *response = user.toProto();
      }
    }
  } catch (sql::SQLException& e) {
    LOG_ERROR << "UpdateStatus SQL error: " << e.what();
    // 不设置错误信息，只返回空结果
  } catch (std::exception& e) {
    LOG_ERROR << "UpdateStatus error: " << e.what();
    // 不设置错误信息，只返回空结果
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
  auto sessionUserId = redis.get("session:" + token);

  if (!sessionUserId) {
    return false;
  }

  return std::stoull(*sessionUserId) == userId;
}

// 存储会话
void UserServiceImpl::storeSession(const std::string& token, uint64_t userId) {
  auto& redis = RedisManager::getInstance();

  // 存储会话令牌，24小时过期
  redis.set("session:" + token, std::to_string(userId), std::chrono::hours(24));

  // 记录用户当前会话
  redis.set("user:session:" + std::to_string(userId), token,
            std::chrono::hours(24));
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
}

// 更新用户在线状态
void UserServiceImpl::updateUserOnlineStatus(uint64_t userId,
                                             starrychat::UserStatus status) {
  auto& redis = RedisManager::getInstance();

  // 存储用户状态
  redis.hset("user:status", std::to_string(userId),
             std::to_string(static_cast<int>(status)));

  // 发布状态变更消息
  std::string message =
      std::to_string(userId) + ":" + std::to_string(static_cast<int>(status));
  redis.publish("user:status:changed", message);
}

}  // namespace StarryChat
