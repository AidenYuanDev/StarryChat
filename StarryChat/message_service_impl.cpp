#include "message_service_impl.h"

#include <chrono>
#include <mariadb/conncpp.hpp>
#include "db_manager.h"
#include "logging.h"
#include "message.h"
#include "redis_manager.h"

namespace StarryChat {

std::shared_ptr<sql::Connection> MessageServiceImpl::getConnection() {
  return DBManager::getInstance().getConnection();
}

// 获取消息历史
void MessageServiceImpl::GetMessages(
    const starrychat::GetMessagesRequestPtr& request,
    const starrychat::GetMessagesResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
    // 验证用户是否为聊天成员
    if (!isValidChatMember(request->user_id(), request->chat_type(),
                           request->chat_id())) {
      response->set_success(false);
      response->set_error_message("Not a member of this chat");
      done(response);
      return;
    }

    auto conn = getConnection();
    std::string query =
        "SELECT * FROM messages WHERE chat_type = ? AND chat_id = ?";
    std::vector<std::string> conditions;

    // 添加时间范围条件
    if (request->start_time() > 0) {
      conditions.push_back(" timestamp >= ?");
    }
    if (request->end_time() > 0) {
      conditions.push_back(" timestamp <= ?");
    }
    if (request->before_msg_id() > 0) {
      conditions.push_back(" id < ?");
    }

    // 组合条件
    for (const auto& condition : conditions) {
      query += " AND" + condition;
    }

    // 添加排序和限制
    query += " ORDER BY timestamp DESC LIMIT ?";

    std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(query));
    int paramIndex = 1;

    stmt->setInt(paramIndex++, static_cast<int>(request->chat_type()));
    stmt->setUInt64(paramIndex++, request->chat_id());

    if (request->start_time() > 0) {
      stmt->setUInt64(paramIndex++, request->start_time());
    }
    if (request->end_time() > 0) {
      stmt->setUInt64(paramIndex++, request->end_time());
    }
    if (request->before_msg_id() > 0) {
      stmt->setUInt64(paramIndex++, request->before_msg_id());
    }

    // 设置限制
    stmt->setInt(paramIndex, request->limit() > 0 ? request->limit() : 20);

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

    // 处理结果
    response->set_success(true);
    std::vector<starrychat::Message> messages;

    while (rs->next()) {
      Message message;
      message.setId(rs->getUInt64("id"));
      message.setSenderId(rs->getUInt64("sender_id"));
      message.setChatType(
          static_cast<starrychat::ChatType>(rs->getInt("chat_type")));
      message.setChatId(rs->getUInt64("chat_id"));
      message.setType(static_cast<starrychat::MessageType>(rs->getInt("type")));
      message.setTimestamp(rs->getUInt64("timestamp"));
      message.setStatus(
          static_cast<starrychat::MessageStatus>(rs->getInt("status")));

      // 根据消息类型设置内容
      if (message.isTextMessage()) {
        message.setText(std::string(rs->getString("content")));
      } else if (message.isSystemMessage()) {
        // 处理系统消息，这里需要根据实际格式解析
        // 简化处理，假设系统消息内容直接存储
        message.setSystemMessage(std::string(rs->getString("content")),
                                 std::string(rs->getString("system_code")), {});
      }

      // 处理回复和提及
      if (rs->getUInt64("reply_to_id") > 0) {
        message.setReplyToId(rs->getUInt64("reply_to_id"));
      }

      // 添加到响应
      *response->add_messages() = message.toProto();
    }

    // 检查是否有更多消息
    response->set_has_more(messages.size() >=
                           static_cast<size_t>(request->limit()));

  } catch (sql::SQLException& e) {
    LOG_ERROR << "GetMessages SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "GetMessages error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 发送消息
void MessageServiceImpl::SendMessage(
    const starrychat::SendMessageRequestPtr& request,
    const starrychat::SendMessageResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
    // 验证用户是否为聊天成员
    if (!isValidChatMember(request->sender_id(), request->chat_type(),
                           request->chat_id())) {
      response->set_success(false);
      response->set_error_message("Not a member of this chat");
      done(response);
      return;
    }

    // 创建消息对象
    Message message(request->sender_id(), request->chat_type(),
                    request->chat_id());
    message.setTimestamp(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    message.setStatus(starrychat::MESSAGE_STATUS_SENT);

    // 设置消息内容
    switch (request->type()) {
      case starrychat::MESSAGE_TYPE_TEXT:
        message.setType(starrychat::MESSAGE_TYPE_TEXT);
        message.setText(request->text().text());
        break;

        // 其他消息类型处理可以在这里添加

      default:
        response->set_success(false);
        response->set_error_message("Unsupported message type");
        done(response);
        return;
    }

    // 设置关联信息
    if (request->reply_to_id() > 0) {
      message.setReplyToId(request->reply_to_id());
    }

    for (int i = 0; i < request->mention_user_ids_size(); i++) {
      message.addMentionUserId(request->mention_user_ids(i));
    }

    // 保存消息到数据库
    uint64_t messageId = saveMessageToDatabase(message.toProto());

    if (messageId > 0) {
      message.setId(messageId);

      // 发布消息通知
      publishMessageNotification(message.toProto());

      // 缓存最近消息
      cacheRecentMessage(message.toProto());

      // 设置响应
      response->set_success(true);
      *response->mutable_message() = message.toProto();
    } else {
      response->set_success(false);
      response->set_error_message("Failed to save message");
    }
  } catch (sql::SQLException& e) {
    LOG_ERROR << "SendMessage SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "SendMessage error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 更新消息状态
void MessageServiceImpl::UpdateMessageStatus(
    const starrychat::UpdateMessageStatusRequestPtr& request,
    const starrychat::UpdateMessageStatusResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
    auto conn = getConnection();

    // 验证消息存在并且用户有权更新
    std::unique_ptr<sql::PreparedStatement> checkStmt(conn->prepareStatement(
        "SELECT chat_type, chat_id FROM messages WHERE id = ?"));
    checkStmt->setUInt64(1, request->message_id());

    std::unique_ptr<sql::ResultSet> checkRs(checkStmt->executeQuery());
    if (!checkRs->next()) {
      response->set_success(false);
      response->set_error_message("Message not found");
      done(response);
      return;
    }

    starrychat::ChatType chatType =
        static_cast<starrychat::ChatType>(checkRs->getInt("chat_type"));
    uint64_t chatId = checkRs->getUInt64("chat_id");

    // 验证用户是否为聊天成员
    if (!isValidChatMember(request->user_id(), chatType, chatId)) {
      response->set_success(false);
      response->set_error_message("Not a member of this chat");
      done(response);
      return;
    }

    // 更新消息状态
    updateMessageStatusInDB(request->message_id(), request->status());

    // 发布状态变更通知
    publishStatusChangeNotification(request->message_id(), request->status());

    response->set_success(true);
  } catch (sql::SQLException& e) {
    LOG_ERROR << "UpdateMessageStatus SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "UpdateMessageStatus error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 撤回消息
void MessageServiceImpl::RecallMessage(
    const starrychat::RecallMessageRequestPtr& request,
    const starrychat::RecallMessageResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
    auto conn = getConnection();

    // 验证消息存在并且用户有权撤回
    std::unique_ptr<sql::PreparedStatement> checkStmt(
        conn->prepareStatement("SELECT sender_id, chat_type, chat_id, "
                               "timestamp FROM messages WHERE id = ?"));
    checkStmt->setUInt64(1, request->message_id());

    std::unique_ptr<sql::ResultSet> checkRs(checkStmt->executeQuery());
    if (!checkRs->next()) {
      response->set_success(false);
      response->set_error_message("Message not found");
      done(response);
      return;
    }

    uint64_t senderId = checkRs->getUInt64("sender_id");
    uint64_t timestamp = checkRs->getUInt64("timestamp");
    uint64_t currentTime =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

    // 检查是否为消息发送者
    if (senderId != request->user_id()) {
      response->set_success(false);
      response->set_error_message("You can only recall your own messages");
      done(response);
      return;
    }

    // 检查是否在可撤回时间内（例如2分钟）
    if (currentTime - timestamp > 120) {
      response->set_success(false);
      response->set_error_message(
          "Messages can only be recalled within 2 minutes of sending");
      done(response);
      return;
    }

    // 更新消息状态为已撤回
    updateMessageStatusInDB(request->message_id(),
                            starrychat::MESSAGE_STATUS_RECALLED);

    // 创建撤回通知消息
    starrychat::ChatType chatType =
        static_cast<starrychat::ChatType>(checkRs->getInt("chat_type"));
    uint64_t chatId = checkRs->getUInt64("chat_id");

    Message recallNotice(request->user_id(), chatType, chatId);
    recallNotice.setType(starrychat::MESSAGE_TYPE_RECALL);
    recallNotice.setTimestamp(currentTime);
    recallNotice.setStatus(starrychat::MESSAGE_STATUS_SENT);

    // 设置撤回内容
    starrychat::RecallContent recallContent;
    recallContent.set_recalled_msg_id(request->message_id());

    // 保存撤回通知
    saveMessageToDatabase(recallNotice.toProto());

    // 发布撤回通知
    publishStatusChangeNotification(request->message_id(),
                                    starrychat::MESSAGE_STATUS_RECALLED);

    response->set_success(true);
  } catch (sql::SQLException& e) {
    LOG_ERROR << "RecallMessage SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "RecallMessage error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 验证用户是否为聊天成员
bool MessageServiceImpl::isValidChatMember(uint64_t userId,
                                           starrychat::ChatType chatType,
                                           uint64_t chatId) {
  try {
    auto conn = getConnection();

    if (chatType == starrychat::CHAT_TYPE_PRIVATE) {
      // 私聊检查
      std::unique_ptr<sql::PreparedStatement> stmt(
          conn->prepareStatement("SELECT 1 FROM private_chats WHERE id = ? AND "
                                 "(user1_id = ? OR user2_id = ?)"));
      stmt->setUInt64(1, chatId);
      stmt->setUInt64(2, userId);
      stmt->setUInt64(3, userId);

      std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
      return rs->next();
    } else if (chatType == starrychat::CHAT_TYPE_GROUP) {
      // 群聊检查
      std::unique_ptr<sql::PreparedStatement> stmt(
          conn->prepareStatement("SELECT 1 FROM chat_room_members WHERE "
                                 "chat_room_id = ? AND user_id = ?"));
      stmt->setUInt64(1, chatId);
      stmt->setUInt64(2, userId);

      std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
      return rs->next();
    }

    return false;
  } catch (std::exception& e) {
    LOG_ERROR << "isValidChatMember error: " << e.what();
    return false;
  }
}

// 保存消息到数据库
uint64_t MessageServiceImpl::saveMessageToDatabase(
    const starrychat::Message& message) {
  try {
    auto conn = getConnection();

    // 准备SQL
    std::string query =
        "INSERT INTO messages (sender_id, chat_type, chat_id, type, content, "
        "system_code, timestamp, status, reply_to_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";

    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(query, sql::Statement::RETURN_GENERATED_KEYS));

    stmt->setUInt64(1, message.sender_id());
    stmt->setInt(2, message.chat_type());
    stmt->setUInt64(3, message.chat_id());
    stmt->setInt(4, message.type());

    // 设置内容
    if (message.type() == starrychat::MESSAGE_TYPE_TEXT && message.has_text()) {
      stmt->setString(5, message.text().text());
      stmt->setNull(6, sql::DataType::VARCHAR);
    } else if (message.type() == starrychat::MESSAGE_TYPE_SYSTEM &&
               message.has_system()) {
      stmt->setString(5, message.system().text());
      stmt->setString(6, message.system().code());
    } else {
      stmt->setNull(5, sql::DataType::VARCHAR);
      stmt->setNull(6, sql::DataType::VARCHAR);
    }

    stmt->setUInt64(7, message.timestamp());
    stmt->setInt(8, message.status());

    if (message.reply_to_id() > 0) {
      stmt->setUInt64(9, message.reply_to_id());
    } else {
      stmt->setNull(9, sql::DataType::BIGINT);
    }

    if (stmt->executeUpdate() > 0) {
      std::unique_ptr<sql::ResultSet> rs(stmt->getGeneratedKeys());
      if (rs->next()) {
        uint64_t messageId = rs->getUInt64(1);

        // 处理提及用户
        if (message.mention_user_ids_size() > 0) {
          std::string mentionQuery =
              "INSERT INTO message_mentions (message_id, user_id) VALUES (?, "
              "?)";
          std::unique_ptr<sql::PreparedStatement> mentionStmt(
              conn->prepareStatement(mentionQuery));

          for (int i = 0; i < message.mention_user_ids_size(); i++) {
            mentionStmt->setUInt64(1, messageId);
            mentionStmt->setUInt64(2, message.mention_user_ids(i));
            mentionStmt->executeUpdate();
          }
        }

        return messageId;
      }
    }

    return 0;
  } catch (sql::SQLException& e) {
    LOG_ERROR << "saveMessageToDatabase SQL error: " << e.what();
    return 0;
  } catch (std::exception& e) {
    LOG_ERROR << "saveMessageToDatabase error: " << e.what();
    return 0;
  }
}

// 发布消息通知
void MessageServiceImpl::publishMessageNotification(
    const starrychat::Message& message) {
  try {
    auto& redis = RedisManager::getInstance();

    // 获取需要通知的用户
    std::vector<uint64_t> members =
        getChatMembers(message.chat_type(), message.chat_id());

    // 序列化消息
    std::string messageStr = message.SerializeAsString();

    // 发布新消息通知
    std::string channel =
        "chat:message:" +
        std::to_string(static_cast<int>(message.chat_type())) + ":" +
        std::to_string(message.chat_id());
    redis.publish(channel, messageStr);

    // 发送个人通知
    for (uint64_t memberId : members) {
      if (memberId != message.sender_id()) {
        // 增加未读计数
        std::string unreadKey =
            "unread:" + std::to_string(memberId) + ":" +
            std::to_string(static_cast<int>(message.chat_type())) + ":" +
            std::to_string(message.chat_id());
        redis.incr(unreadKey);

        // 个人通知
        std::string userChannel = "user:message:" + std::to_string(memberId);
        redis.publish(userChannel, messageStr);
      }
    }
  } catch (std::exception& e) {
    LOG_ERROR << "publishMessageNotification error: " << e.what();
  }
}

// 缓存最近消息
void MessageServiceImpl::cacheRecentMessage(
    const starrychat::Message& message) {
  try {
    auto& redis = RedisManager::getInstance();

    // 缓存聊天的最新消息
    std::string chatKey =
        "chat:recent:" + std::to_string(static_cast<int>(message.chat_type())) +
        ":" + std::to_string(message.chat_id());

    // 序列化消息
    std::string messageStr = message.SerializeAsString();

    // 添加到最近消息列表
    redis.lpush(chatKey, messageStr);

    // 保持列表长度限制（例如保留100条）
    // Redis的LTRIM是O(1)操作，不会随着列表长度增加而变慢
    // 这里可以使用RedisManager扩展功能或直接使用getRedis()
    auto redis_ptr = redis.getRedis();
    if (redis_ptr) {
      redis_ptr->command("LTRIM", chatKey, "0", "99");
    }

    // 更新聊天的最后活动时间
    std::string lastActiveKey =
        "chat:last_active:" +
        std::to_string(static_cast<int>(message.chat_type())) + ":" +
        std::to_string(message.chat_id());
    redis.set(lastActiveKey, std::to_string(message.timestamp()));
  } catch (std::exception& e) {
    LOG_ERROR << "cacheRecentMessage error: " << e.what();
  }
}

// 获取聊天成员
std::vector<uint64_t> MessageServiceImpl::getChatMembers(
    starrychat::ChatType chatType,
    uint64_t chatId) {
  std::vector<uint64_t> members;

  try {
    auto conn = getConnection();

    if (chatType == starrychat::CHAT_TYPE_PRIVATE) {
      // 私聊成员
      std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
          "SELECT user1_id, user2_id FROM private_chats WHERE id = ?"));
      stmt->setUInt64(1, chatId);

      std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
      if (rs->next()) {
        members.push_back(rs->getUInt64("user1_id"));
        members.push_back(rs->getUInt64("user2_id"));
      }
    } else if (chatType == starrychat::CHAT_TYPE_GROUP) {
      // 群聊成员
      std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
          "SELECT user_id FROM chat_room_members WHERE chat_room_id = ?"));
      stmt->setUInt64(1, chatId);

      std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
      while (rs->next()) {
        members.push_back(rs->getUInt64("user_id"));
      }
    }
  } catch (sql::SQLException& e) {
    LOG_ERROR << "getChatMembers SQL error: " << e.what();
  } catch (std::exception& e) {
    LOG_ERROR << "getChatMembers error: " << e.what();
  }

  return members;
}

// 更新消息状态
void MessageServiceImpl::updateMessageStatusInDB(
    uint64_t messageId,
    starrychat::MessageStatus status) {
  try {
    auto conn = getConnection();

    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement("UPDATE messages SET status = ? WHERE id = ?"));
    stmt->setInt(1, static_cast<int>(status));
    stmt->setUInt64(2, messageId);

    stmt->executeUpdate();
  } catch (sql::SQLException& e) {
    LOG_ERROR << "updateMessageStatusInDB SQL error: " << e.what();
  } catch (std::exception& e) {
    LOG_ERROR << "updateMessageStatusInDB error: " << e.what();
  }
}

// 发布状态变更通知
void MessageServiceImpl::publishStatusChangeNotification(
    uint64_t messageId,
    starrychat::MessageStatus status) {
  try {
    auto& redis = RedisManager::getInstance();

    // 获取消息信息
    auto conn = getConnection();
    std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
        "SELECT chat_type, chat_id FROM messages WHERE id = ?"));
    stmt->setUInt64(1, messageId);

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    if (rs->next()) {
      starrychat::ChatType chatType =
          static_cast<starrychat::ChatType>(rs->getInt("chat_type"));
      uint64_t chatId = rs->getUInt64("chat_id");

      // 发布状态变更通知
      std::string channel =
          "chat:message:status:" + std::to_string(static_cast<int>(chatType)) +
          ":" + std::to_string(chatId);
      std::string message = std::to_string(messageId) + ":" +
                            std::to_string(static_cast<int>(status));

      redis.publish(channel, message);
    }
  } catch (std::exception& e) {
    LOG_ERROR << "publishStatusChangeNotification error: " << e.what();
  }
}

// 验证会话令牌
bool MessageServiceImpl::validateSession(const std::string& token,
                                         uint64_t userId) {
  auto& redis = RedisManager::getInstance();
  auto sessionUserId = redis.get("session:" + token);

  if (!sessionUserId) {
    return false;
  }

  return std::stoull(*sessionUserId) == userId;
}

}  // namespace StarryChat
