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

    LOG_INFO << "Fetching messages for chat type: "
             << static_cast<int>(request->chat_type())
             << ", chat ID: " << request->chat_id();

    // 首先尝试从Redis缓存获取消息ID列表
    auto messageIds = getRecentMessageIds(
        request->chat_type(), request->chat_id(),
        request->limit() > 0 ? request->limit() : 20, request->before_msg_id());

    // 标记是否从缓存获取了消息
    bool useCache = !messageIds.empty();

    // 如果成功从缓存获取了消息ID列表
    if (useCache) {
      LOG_INFO << "Found " << messageIds.size() << " message IDs in cache";

      // 尝试从缓存获取消息数据
      for (uint64_t messageId : messageIds) {
        auto cachedMessage = getMessageFromCache(messageId);
        if (cachedMessage) {
          *response->add_messages() = *cachedMessage;
        } else {
          // 如果有任何一条消息未命中缓存，切换到数据库查询所有消息
          LOG_INFO << "Cache miss for message ID: " << messageId
                   << ", falling back to database";
          useCache = false;
          break;
        }
      }
    }

    // 如果缓存未命中或不完整，从数据库查询
    if (!useCache) {
      LOG_INFO << "Querying messages from database";
      auto conn = getConnection();
      if (!conn) {
        response->set_success(false);
        response->set_error_message("Database connection failed");
        done(response);
        return;
      }

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

      std::unique_ptr<sql::PreparedStatement> stmt(
          conn->prepareStatement(query));
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

      // 清空之前的结果，避免重复
      response->clear_messages();

      // 处理结果
      while (rs->next()) {
        Message message;
        message.setId(rs->getUInt64("id"));
        message.setSenderId(rs->getUInt64("sender_id"));
        message.setChatType(
            static_cast<starrychat::ChatType>(rs->getInt("chat_type")));
        message.setChatId(rs->getUInt64("chat_id"));
        message.setType(
            static_cast<starrychat::MessageType>(rs->getInt("type")));
        message.setTimestamp(rs->getUInt64("timestamp"));
        message.setStatus(
            static_cast<starrychat::MessageStatus>(rs->getInt("status")));

        // 根据消息类型设置内容
        if (message.isTextMessage()) {
          message.setText(std::string(rs->getString("content")));
        } else if (message.isSystemMessage()) {
          message.setSystemMessage(std::string(rs->getString("content")),
                                   std::string(rs->getString("system_code")),
                                   {});
        }

        // 处理回复和提及
        if (rs->getUInt64("reply_to_id") > 0) {
          message.setReplyToId(rs->getUInt64("reply_to_id"));
        }

        // 添加到响应
        *response->add_messages() = message.toProto();

        // 缓存消息
        cacheMessage(message.toProto());
      }

      // 确保消息按时间倒序排列
      std::sort(response->mutable_messages()->begin(),
                response->mutable_messages()->end(),
                [](const starrychat::Message& a, const starrychat::Message& b) {
                  return a.timestamp() > b.timestamp();
                });
    }

    // 标记成功和是否有更多消息
    response->set_success(true);
    response->set_has_more(response->messages_size() >= request->limit());

    // 获取消息时自动重置该用户的未读计数
    resetUnreadCount(request->user_id(), request->chat_type(),
                     request->chat_id());
    LOG_INFO << "Reset unread count for user " << request->user_id()
             << " in chat type " << static_cast<int>(request->chat_type())
             << ", chat ID " << request->chat_id();

    LOG_INFO << "Successfully retrieved " << response->messages_size()
             << " messages";

  } catch (sql::SQLException& e) {
    LOG_ERROR << "GetMessages SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error: " + std::string(e.what()));
  } catch (std::exception& e) {
    LOG_ERROR << "GetMessages error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error: " + std::string(e.what()));
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

    LOG_INFO << "Sending message from user " << request->sender_id()
             << " to chat type " << static_cast<int>(request->chat_type())
             << ", chat ID " << request->chat_id();

    // 创建消息对象
    Message message(request->sender_id(), request->chat_type(),
                    request->chat_id());

    // 设置时间戳
    uint64_t timestamp =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    message.setTimestamp(timestamp);
    message.setStatus(starrychat::MESSAGE_STATUS_SENT);

    // 设置消息内容
    switch (request->type()) {
      case starrychat::MESSAGE_TYPE_TEXT:
        message.setType(starrychat::MESSAGE_TYPE_TEXT);
        message.setText(request->text().text());
        LOG_INFO << "Text message content: " << request->text().text();
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

      // 缓存消息
      cacheMessage(message.toProto());

      // 更新消息时间线
      updateMessageTimeline(message.getChatType(), message.getChatId(),
                            messageId, message.getTimestamp());

      // 发布消息通知
      publishMessageNotification(message.toProto());

      // 更新最后一条消息信息
      updateLastMessage(message.getChatType(), message.getChatId(),
                        message.toProto());

      // 增加其他用户的未读消息计数
      auto members = getChatMembers(message.getChatType(), message.getChatId());
      for (uint64_t memberId : members) {
        if (memberId != message.getSenderId()) {
          incrementUnreadCount(memberId, message.getChatType(),
                               message.getChatId());
        }
      }

      // 设置响应
      response->set_success(true);
      *response->mutable_message() = message.toProto();

      LOG_INFO << "Message sent successfully. ID: " << messageId;
    } else {
      response->set_success(false);
      response->set_error_message("Failed to save message");
      LOG_ERROR << "Failed to save message to database";
    }
  } catch (sql::SQLException& e) {
    LOG_ERROR << "SendMessage SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error: " + std::string(e.what()));
  } catch (std::exception& e) {
    LOG_ERROR << "SendMessage error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error: " + std::string(e.what()));
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
    if (!conn) {
      response->set_success(false);
      response->set_error_message("Database connection failed");
      done(response);
      return;
    }

    // 验证消息存在并且用户有权更新
    std::unique_ptr<sql::PreparedStatement> checkStmt(conn->prepareStatement(
        "SELECT chat_type, chat_id, sender_id FROM messages WHERE id = ?"));
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
    uint64_t senderId = checkRs->getUInt64("sender_id");

    // 验证用户是否为聊天成员
    if (!isValidChatMember(request->user_id(), chatType, chatId)) {
      response->set_success(false);
      response->set_error_message("Not a member of this chat");
      done(response);
      return;
    }

    // 更新消息状态
    if (updateMessageStatusInDB(request->message_id(), request->status())) {
      // 更新缓存中的消息状态
      auto cachedMessage = getMessageFromCache(request->message_id());
      if (cachedMessage) {
        starrychat::Message updatedMessage = *cachedMessage;
        updatedMessage.set_status(request->status());
        cacheMessage(updatedMessage);
      }

      // 发布状态变更通知
      publishStatusChangeNotification(request->message_id(), request->status());

      // 如果是标记为已读，并且当前用户是接收方（非发送方），则减少未读计数
      if (request->status() == starrychat::MESSAGE_STATUS_READ &&
          request->user_id() != senderId) {
        // 此处可以选择减少单条消息的未读计数，但通常直接重置更简单
        resetUnreadCount(request->user_id(), chatType, chatId);
      }

      response->set_success(true);
      LOG_INFO << "Updated status for message " << request->message_id()
               << " to " << static_cast<int>(request->status());
    } else {
      response->set_success(false);
      response->set_error_message("Failed to update message status");
      LOG_ERROR << "Failed to update status for message "
                << request->message_id();
    }
  } catch (sql::SQLException& e) {
    LOG_ERROR << "UpdateMessageStatus SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error: " + std::string(e.what()));
  } catch (std::exception& e) {
    LOG_ERROR << "UpdateMessageStatus error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error: " + std::string(e.what()));
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
    if (!conn) {
      response->set_success(false);
      response->set_error_message("Database connection failed");
      done(response);
      return;
    }

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
    starrychat::ChatType chatType =
        static_cast<starrychat::ChatType>(checkRs->getInt("chat_type"));
    uint64_t chatId = checkRs->getUInt64("chat_id");

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
    if (updateMessageStatusInDB(request->message_id(),
                                starrychat::MESSAGE_STATUS_RECALLED)) {
      // 更新缓存中的消息状态
      auto cachedMessage = getMessageFromCache(request->message_id());
      if (cachedMessage) {
        starrychat::Message updatedMessage = *cachedMessage;
        updatedMessage.set_status(starrychat::MESSAGE_STATUS_RECALLED);
        cacheMessage(updatedMessage);
      }

      // 创建撤回通知消息
      Message recallNotice(request->user_id(), chatType, chatId);
      recallNotice.setType(starrychat::MESSAGE_TYPE_RECALL);
      recallNotice.setTimestamp(currentTime);
      recallNotice.setStatus(starrychat::MESSAGE_STATUS_SENT);

      // 设置撤回内容
      starrychat::RecallContent recallContent;
      recallContent.set_recalled_msg_id(request->message_id());

      // 保存撤回通知
      uint64_t noticeId = saveMessageToDatabase(recallNotice.toProto());
      if (noticeId > 0) {
        recallNotice.setId(noticeId);

        // 缓存通知消息
        cacheMessage(recallNotice.toProto());

        // 更新消息时间线
        updateMessageTimeline(recallNotice.getChatType(),
                              recallNotice.getChatId(), noticeId,
                              recallNotice.getTimestamp());

        // 发布通知
        publishMessageNotification(recallNotice.toProto());
      }

      // 发布撤回通知
      publishStatusChangeNotification(request->message_id(),
                                      starrychat::MESSAGE_STATUS_RECALLED);

      response->set_success(true);
      LOG_INFO << "Message " << request->message_id() << " recalled by user "
               << request->user_id();
    } else {
      response->set_success(false);
      response->set_error_message("Failed to recall message");
      LOG_ERROR << "Failed to recall message " << request->message_id();
    }
  } catch (sql::SQLException& e) {
    LOG_ERROR << "RecallMessage SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error: " + std::string(e.what()));
  } catch (std::exception& e) {
    LOG_ERROR << "RecallMessage error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error: " + std::string(e.what()));
  }

  done(response);
}

// 验证用户是否为聊天成员
bool MessageServiceImpl::isValidChatMember(uint64_t userId,
                                           starrychat::ChatType chatType,
                                           uint64_t chatId) {
  try {
    // 先尝试从Redis缓存验证
    auto& redis = RedisManager::getInstance();

    if (chatType == starrychat::CHAT_TYPE_PRIVATE) {
      // 私聊检查 - 检查用户是否是私聊的参与者
      std::string privateKey =
          "private_chat:" + std::to_string(chatId) + ":members";
      auto members = redis.smembers(privateKey);
      if (members && !members->empty()) {
        std::string userIdStr = std::to_string(userId);
        return std::find(members->begin(), members->end(), userIdStr) !=
               members->end();
      }
    } else if (chatType == starrychat::CHAT_TYPE_GROUP) {
      // 群聊检查 - 检查用户是否是群聊成员
      std::string groupKey = "chat_room:" + std::to_string(chatId) + ":members";
      auto members = redis.smembers(groupKey);
      if (members && !members->empty()) {
        std::string userIdStr = std::to_string(userId);
        return std::find(members->begin(), members->end(), userIdStr) !=
               members->end();
      }
    }

    // 缓存未命中，从数据库查询
    auto conn = getConnection();
    if (!conn) {
      return false;
    }

    if (chatType == starrychat::CHAT_TYPE_PRIVATE) {
      // 私聊检查
      std::unique_ptr<sql::PreparedStatement> stmt(
          conn->prepareStatement("SELECT 1 FROM private_chats WHERE id = ? AND "
                                 "(user1_id = ? OR user2_id = ?)"));
      stmt->setUInt64(1, chatId);
      stmt->setUInt64(2, userId);
      stmt->setUInt64(3, userId);

      std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
      bool result = rs->next();

      // 缓存结果
      if (result) {
        redis.sadd("private_chat:" + std::to_string(chatId) + ":members",
                   std::to_string(userId));
      }

      return result;
    } else if (chatType == starrychat::CHAT_TYPE_GROUP) {
      // 群聊检查
      std::unique_ptr<sql::PreparedStatement> stmt(
          conn->prepareStatement("SELECT 1 FROM chat_room_members WHERE "
                                 "chat_room_id = ? AND user_id = ?"));
      stmt->setUInt64(1, chatId);
      stmt->setUInt64(2, userId);

      std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
      bool result = rs->next();

      // 缓存结果
      if (result) {
        redis.sadd("chat_room:" + std::to_string(chatId) + ":members",
                   std::to_string(userId));
      }

      return result;
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
    if (!conn) {
      return 0;
    }

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

// 更新数据库中的消息状态
bool MessageServiceImpl::updateMessageStatusInDB(
    uint64_t messageId,
    starrychat::MessageStatus status) {
  try {
    auto conn = getConnection();
    if (!conn) {
      return false;
    }

    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement("UPDATE messages SET status = ? WHERE id = ?"));
    stmt->setInt(1, static_cast<int>(status));
    stmt->setUInt64(2, messageId);

    return stmt->executeUpdate() > 0;
  } catch (sql::SQLException& e) {
    LOG_ERROR << "updateMessageStatusInDB SQL error: " << e.what();
    return false;
  } catch (std::exception& e) {
    LOG_ERROR << "updateMessageStatusInDB error: " << e.what();
    return false;
  }
}

// 缓存消息
void MessageServiceImpl::cacheMessage(const starrychat::Message& message) {
  try {
    auto& redis = RedisManager::getInstance();

    // 消息键
    std::string messageKey = "message:" + std::to_string(message.id());

    // 将消息序列化为字符串
    std::string serialized;
    if (!message.SerializeToString(&serialized)) {
      LOG_ERROR << "Failed to serialize message " << message.id();
      return;
    }

    // 存储消息，使用较长的过期时间（7天）
    redis.set(messageKey, serialized, std::chrono::hours(24 * 7));

    LOG_INFO << "Cached message " << message.id();
  } catch (std::exception& e) {
    LOG_ERROR << "cacheMessage error: " << e.what();
  }
}

// 从缓存获取消息
std::optional<starrychat::Message> MessageServiceImpl::getMessageFromCache(
    uint64_t messageId) {
  try {
    auto& redis = RedisManager::getInstance();

    // 消息键
    std::string messageKey = "message:" + std::to_string(messageId);

    // 获取缓存消息
    auto serialized = redis.get(messageKey);
    if (!serialized) {
      return std::nullopt;
    }

    // 解析消息
    starrychat::Message message;
    if (!message.ParseFromString(*serialized)) {
      LOG_ERROR << "Failed to parse cached message " << messageId;
      return std::nullopt;
    }

    // 刷新缓存过期时间
    redis.expire(messageKey, std::chrono::hours(24 * 7));

    return message;
  } catch (std::exception& e) {
    LOG_ERROR << "getMessageFromCache error: " << e.what();
    return std::nullopt;
  }
}

// 使缓存的消息失效
void MessageServiceImpl::invalidateMessageCache(uint64_t messageId) {
  try {
    auto& redis = RedisManager::getInstance();

    // 消息键
    std::string messageKey = "message:" + std::to_string(messageId);

    // 删除缓存
    redis.del(messageKey);

    LOG_INFO << "Invalidated cache for message " << messageId;
  } catch (std::exception& e) {
    LOG_ERROR << "invalidateMessageCache error: " << e.what();
  }
}

// 更新消息时间线
void MessageServiceImpl::updateMessageTimeline(starrychat::ChatType chatType,
                                               uint64_t chatId,
                                               uint64_t messageId,
                                               uint64_t timestamp) {
  try {
    auto& redis = RedisManager::getInstance();

    // 时间线键
    std::string timelineKey =
        "timeline:" + std::to_string(static_cast<int>(chatType)) + ":" +
        std::to_string(chatId);

    // 添加消息ID到有序集合，以时间戳为分数
    redis.zadd(timelineKey, std::to_string(messageId),
               static_cast<double>(timestamp));

    // 限制时间线大小（保留最近的1000条消息）
    // 使用Redis命令直接执行
    auto redis_ptr = redis.getRedis();
    if (redis_ptr) {
      // 删除旧消息，保留最新的1000条
      redis_ptr->zremrangebyrank(timelineKey, 0, -1001);
    }

    // 设置较长的过期时间（30天）
    redis.expire(timelineKey, std::chrono::hours(24 * 30));

    LOG_INFO << "Updated message timeline for chat type "
             << static_cast<int>(chatType) << ", chat ID " << chatId;
  } catch (std::exception& e) {
    LOG_ERROR << "updateMessageTimeline error: " << e.what();
  }
}

// 获取最近消息ID列表
// 获取最近消息ID列表
std::vector<uint64_t> MessageServiceImpl::getRecentMessageIds(
    starrychat::ChatType chatType,
    uint64_t chatId,
    int limit,
    uint64_t beforeMsgId) {
  std::vector<uint64_t> result;

  try {
    auto& redis = RedisManager::getInstance();

    // 时间线键
    std::string timelineKey =
        "timeline:" + std::to_string(static_cast<int>(chatType)) + ":" +
        std::to_string(chatId);

    // 获取消息ID列表
    auto redis_ptr = redis.getRedis();
    if (!redis_ptr) {
      return result;
    }

    std::vector<std::string> ids;

    if (beforeMsgId > 0) {
      // 获取指定消息ID之前的消息
      // 替代方案：使用zrevrange获取所有消息，然后手动过滤

      // 1. 首先获取beforeMsgId的排名（rank）
      auto rank = redis_ptr->zrevrank(timelineKey, std::to_string(beforeMsgId));

      if (rank) {
        // 2. 获取从rank+1开始的limit条消息（这些消息的时间戳比beforeMsgId旧）
        redis_ptr->zrevrange(timelineKey,
                             *rank + 1,      // 从指定消息之后开始
                             *rank + limit,  // 获取limit条消息
                             std::back_inserter(ids));
      }
    } else {
      // 获取最新的消息ID列表
      redis_ptr->zrevrange(timelineKey, 0, limit - 1, std::back_inserter(ids));
    }

    // 转换为uint64_t
    for (const auto& id : ids) {
      result.push_back(std::stoull(id));
    }

    LOG_INFO << "Retrieved " << result.size() << " message IDs from cache";
  } catch (std::exception& e) {
    LOG_ERROR << "getRecentMessageIds error: " << e.what();
  }

  return result;
}

// 发布消息通知
void MessageServiceImpl::publishMessageNotification(
    const starrychat::Message& message) {
  try {
    auto& redis = RedisManager::getInstance();

    // 序列化消息
    std::string serialized;
    if (!message.SerializeToString(&serialized)) {
      LOG_ERROR << "Failed to serialize message for notification";
      return;
    }

    // 发布新消息通知
    std::string channel =
        "chat:message:" +
        std::to_string(static_cast<int>(message.chat_type())) + ":" +
        std::to_string(message.chat_id());
    redis.publish(channel, serialized);

    // 发送个人通知
    auto members = getChatMembers(message.chat_type(), message.chat_id());
    for (uint64_t memberId : members) {
      if (memberId != message.sender_id()) {
        std::string userChannel = "user:message:" + std::to_string(memberId);
        redis.publish(userChannel, serialized);
      }
    }

    LOG_INFO << "Published message notification for message " << message.id();
  } catch (std::exception& e) {
    LOG_ERROR << "publishMessageNotification error: " << e.what();
  }
}

// 发布状态变更通知
void MessageServiceImpl::publishStatusChangeNotification(
    uint64_t messageId,
    starrychat::MessageStatus status) {
  try {
    auto& redis = RedisManager::getInstance();

    // 获取消息信息
    auto cachedMessage = getMessageFromCache(messageId);
    if (!cachedMessage) {
      auto conn = getConnection();
      if (!conn) {
        return;
      }

      std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
          "SELECT chat_type, chat_id FROM messages WHERE id = ?"));
      stmt->setUInt64(1, messageId);

      std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
      if (!rs->next()) {
        return;
      }

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
    } else {
      // 发布状态变更通知
      std::string channel =
          "chat:message:status:" +
          std::to_string(static_cast<int>(cachedMessage->chat_type())) + ":" +
          std::to_string(cachedMessage->chat_id());
      std::string message = std::to_string(messageId) + ":" +
                            std::to_string(static_cast<int>(status));

      redis.publish(channel, message);
    }

    LOG_INFO << "Published status change notification for message " << messageId
             << " to status " << static_cast<int>(status);
  } catch (std::exception& e) {
    LOG_ERROR << "publishStatusChangeNotification error: " << e.what();
  }
}

// 增加未读消息计数
void MessageServiceImpl::incrementUnreadCount(uint64_t userId,
                                              starrychat::ChatType chatType,
                                              uint64_t chatId) {
  try {
    auto& redis = RedisManager::getInstance();

    // 未读计数键
    std::string unreadKey = "unread:" + std::to_string(userId) + ":" +
                            std::to_string(static_cast<int>(chatType)) + ":" +
                            std::to_string(chatId);

    // 增加未读计数
    redis.incr(unreadKey);

    LOG_INFO << "Incremented unread count for user " << userId
             << " in chat type " << static_cast<int>(chatType) << ", chat ID "
             << chatId;
  } catch (std::exception& e) {
    LOG_ERROR << "incrementUnreadCount error: " << e.what();
  }
}

// 重置未读消息计数
void MessageServiceImpl::resetUnreadCount(uint64_t userId,
                                          starrychat::ChatType chatType,
                                          uint64_t chatId) {
  try {
    auto& redis = RedisManager::getInstance();

    // 未读计数键
    std::string unreadKey = "unread:" + std::to_string(userId) + ":" +
                            std::to_string(static_cast<int>(chatType)) + ":" +
                            std::to_string(chatId);

    // 重置未读计数
    redis.set(unreadKey, "0");

    LOG_INFO << "Reset unread count for user " << userId << " in chat type "
             << static_cast<int>(chatType) << ", chat ID " << chatId;
  } catch (std::exception& e) {
    LOG_ERROR << "resetUnreadCount error: " << e.what();
  }
}

// 获取未读消息计数
uint64_t MessageServiceImpl::getUnreadCount(uint64_t userId,
                                            starrychat::ChatType chatType,
                                            uint64_t chatId) {
  try {
    auto& redis = RedisManager::getInstance();

    // 未读计数键
    std::string unreadKey = "unread:" + std::to_string(userId) + ":" +
                            std::to_string(static_cast<int>(chatType)) + ":" +
                            std::to_string(chatId);

    // 获取未读计数
    auto countStr = redis.get(unreadKey);
    if (countStr) {
      return std::stoull(*countStr);
    }
  } catch (std::exception& e) {
    LOG_ERROR << "getUnreadCount error: " << e.what();
  }

  return 0;
}

// 获取聊天成员
std::vector<uint64_t> MessageServiceImpl::getChatMembers(
    starrychat::ChatType chatType,
    uint64_t chatId) {
  std::vector<uint64_t> members;

  try {
    auto& redis = RedisManager::getInstance();

    // 尝试从缓存获取成员列表
    if (chatType == starrychat::CHAT_TYPE_PRIVATE) {
      std::string key = "private_chat:" + std::to_string(chatId) + ":members";
      auto memberStrings = redis.smembers(key);

      if (memberStrings && !memberStrings->empty()) {
        for (const auto& memberStr : *memberStrings) {
          members.push_back(std::stoull(memberStr));
        }
        return members;
      }
    } else if (chatType == starrychat::CHAT_TYPE_GROUP) {
      std::string key = "chat_room:" + std::to_string(chatId) + ":members";
      auto memberStrings = redis.smembers(key);

      if (memberStrings && !memberStrings->empty()) {
        for (const auto& memberStr : *memberStrings) {
          members.push_back(std::stoull(memberStr));
        }
        return members;
      }
    }

    // 缓存未命中，从数据库查询
    auto conn = getConnection();
    if (!conn) {
      return members;
    }

    if (chatType == starrychat::CHAT_TYPE_PRIVATE) {
      // 私聊成员
      std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
          "SELECT user1_id, user2_id FROM private_chats WHERE id = ?"));
      stmt->setUInt64(1, chatId);

      std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
      if (rs->next()) {
        uint64_t user1Id = rs->getUInt64("user1_id");
        uint64_t user2Id = rs->getUInt64("user2_id");

        members.push_back(user1Id);
        members.push_back(user2Id);

        // 缓存结果
        std::string key = "private_chat:" + std::to_string(chatId) + ":members";
        redis.sadd(key, std::to_string(user1Id));
        redis.sadd(key, std::to_string(user2Id));
        redis.expire(key, std::chrono::hours(24));
      }
    } else if (chatType == starrychat::CHAT_TYPE_GROUP) {
      // 群聊成员
      std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
          "SELECT user_id FROM chat_room_members WHERE chat_room_id = ?"));
      stmt->setUInt64(1, chatId);

      std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
      std::string key = "chat_room:" + std::to_string(chatId) + ":members";

      while (rs->next()) {
        uint64_t userId = rs->getUInt64("user_id");
        members.push_back(userId);

        // 缓存结果
        redis.sadd(key, std::to_string(userId));
      }

      redis.expire(key, std::chrono::hours(24));
    }
  } catch (sql::SQLException& e) {
    LOG_ERROR << "getChatMembers SQL error: " << e.what();
  } catch (std::exception& e) {
    LOG_ERROR << "getChatMembers error: " << e.what();
  }

  return members;
}

// 获取最后一条消息预览
std::string MessageServiceImpl::getLastMessagePreview(
    starrychat::ChatType chatType,
    uint64_t chatId) {
  try {
    auto& redis = RedisManager::getInstance();

    // 尝试从缓存获取最后一条消息
    std::string lastMessageKey =
        "chat:last_message:" + std::to_string(static_cast<int>(chatType)) +
        ":" + std::to_string(chatId);

    auto preview = redis.get(lastMessageKey);
    if (preview) {
      return *preview;
    }

    // 缓存未命中，从数据库查询
    auto conn = getConnection();
    if (!conn) {
      return "";
    }

    std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
        "SELECT type, content, system_code FROM messages "
        "WHERE chat_type = ? AND chat_id = ? "
        "ORDER BY timestamp DESC LIMIT 1"));
    stmt->setInt(1, static_cast<int>(chatType));
    stmt->setUInt64(2, chatId);

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    if (rs->next()) {
      starrychat::MessageType msgType =
          static_cast<starrychat::MessageType>(rs->getInt("type"));

      std::string previewText;

      if (msgType == starrychat::MESSAGE_TYPE_TEXT) {
        previewText = std::string(rs->getString("content"));
        // 限制预览长度
        if (previewText.length() > 30) {
          previewText = previewText.substr(0, 27) + "...";
        }
      } else if (msgType == starrychat::MESSAGE_TYPE_SYSTEM) {
        previewText =
            "[System: " + std::string(rs->getString("system_code")) + "]";
      } else if (msgType == starrychat::MESSAGE_TYPE_IMAGE) {
        previewText = "[Image]";
      } else if (msgType == starrychat::MESSAGE_TYPE_FILE) {
        previewText = "[File]";
      } else if (msgType == starrychat::MESSAGE_TYPE_AUDIO) {
        previewText = "[Audio]";
      } else if (msgType == starrychat::MESSAGE_TYPE_VIDEO) {
        previewText = "[Video]";
      } else if (msgType == starrychat::MESSAGE_TYPE_LOCATION) {
        previewText = "[Location]";
      } else if (msgType == starrychat::MESSAGE_TYPE_RECALL) {
        previewText = "[Message was recalled]";
      }

      // 缓存结果
      if (!previewText.empty()) {
        redis.set(lastMessageKey, previewText, std::chrono::hours(24));
      }

      return previewText;
    }
  } catch (sql::SQLException& e) {
    LOG_ERROR << "getLastMessagePreview SQL error: " << e.what();
  } catch (std::exception& e) {
    LOG_ERROR << "getLastMessagePreview error: " << e.what();
  }

  return "";
}

// 更新最后一条消息
void MessageServiceImpl::updateLastMessage(starrychat::ChatType chatType,
                                           uint64_t chatId,
                                           const starrychat::Message& message) {
  try {
    auto& redis = RedisManager::getInstance();

    // 生成预览文本
    std::string previewText;

    if (message.type() == starrychat::MESSAGE_TYPE_TEXT) {
      previewText = message.text().text();
      // 限制预览长度
      if (previewText.length() > 30) {
        previewText = previewText.substr(0, 27) + "...";
      }
    } else if (message.type() == starrychat::MESSAGE_TYPE_SYSTEM) {
      previewText = "[System: " + message.system().code() + "]";
    } else if (message.type() == starrychat::MESSAGE_TYPE_IMAGE) {
      previewText = "[Image]";
    } else if (message.type() == starrychat::MESSAGE_TYPE_FILE) {
      previewText = "[File]";
    } else if (message.type() == starrychat::MESSAGE_TYPE_AUDIO) {
      previewText = "[Audio]";
    } else if (message.type() == starrychat::MESSAGE_TYPE_VIDEO) {
      previewText = "[Video]";
    } else if (message.type() == starrychat::MESSAGE_TYPE_LOCATION) {
      previewText = "[Location]";
    } else if (message.type() == starrychat::MESSAGE_TYPE_RECALL) {
      previewText = "[Message was recalled]";
    }

    // 更新最后一条消息预览
    std::string lastMessageKey =
        "chat:last_message:" + std::to_string(static_cast<int>(chatType)) +
        ":" + std::to_string(chatId);
    redis.set(lastMessageKey, previewText, std::chrono::hours(24));

    // 更新最后一条消息时间
    std::string lastActiveKey =
        "chat:last_active:" + std::to_string(static_cast<int>(chatType)) + ":" +
        std::to_string(chatId);
    redis.set(lastActiveKey, std::to_string(message.timestamp()),
              std::chrono::hours(24));

    LOG_INFO << "Updated last message for chat type "
             << static_cast<int>(chatType) << ", chat ID " << chatId;
  } catch (std::exception& e) {
    LOG_ERROR << "updateLastMessage error: " << e.what();
  }
}

// 验证会话令牌
bool MessageServiceImpl::validateSession(const std::string& token,
                                         uint64_t userId) {
  auto& redis = RedisManager::getInstance();

  // 检查会话token是否存在
  auto sessionUserId = redis.get("session:" + token);
  if (!sessionUserId) {
    return false;
  }

  // 验证用户ID是否匹配
  return std::stoull(*sessionUserId) == userId;
}

}  // namespace StarryChat
