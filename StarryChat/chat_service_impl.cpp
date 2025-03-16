#include "chat_service_impl.h"

#include <algorithm>
#include <chrono>
#include <mariadb/conncpp.hpp>
#include "chat_room.h"
#include "db_manager.h"
#include "logging.h"
#include "redis_manager.h"

namespace StarryChat {

std::shared_ptr<sql::Connection> ChatServiceImpl::getConnection() {
  return DBManager::getInstance().getConnection();
}

// 创建聊天室
void ChatServiceImpl::CreateChatRoom(
    const starrychat::CreateChatRoomRequestPtr& request,
    const starrychat::CreateChatRoomResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
    // 创建聊天室记录
    uint64_t chatRoomId =
        createChatRoomInDB(request->name(), request->creator_id(),
                           request->description(), request->avatar_url());

    if (chatRoomId > 0) {
      // 添加创建者作为所有者
      if (addChatRoomMemberToDB(chatRoomId, request->creator_id(),
                                starrychat::MEMBER_ROLE_OWNER)) {
        // 添加其他初始成员
        for (int i = 0; i < request->initial_member_ids_size(); i++) {
          uint64_t memberId = request->initial_member_ids(i);
          if (memberId != request->creator_id()) {
            addChatRoomMemberToDB(chatRoomId, memberId,
                                  starrychat::MEMBER_ROLE_MEMBER);
          }
        }

        // 更新成员数量
        updateChatRoomMemberCount(chatRoomId);

        // 获取创建的聊天室信息
        auto conn = getConnection();
        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement("SELECT * FROM chat_rooms WHERE id = ?"));
        stmt->setUInt64(1, chatRoomId);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (rs->next()) {
          ChatRoom chatRoom;
          chatRoom.setId(rs->getUInt64("id"));
          chatRoom.setName(std::string(rs->getString("name")));
          chatRoom.setDescription(std::string(rs->getString("description")));
          chatRoom.setCreatorId(rs->getUInt64("creator_id"));
          chatRoom.setCreatedTime(rs->getUInt64("created_time"));
          chatRoom.setMemberCount(rs->getUInt64("member_count"));
          chatRoom.setAvatarUrl(std::string(rs->getString("avatar_url")));

          // 缓存聊天室信息
          cacheChatRoom(chatRoom);

          // 设置响应
          response->set_success(true);
          *response->mutable_chat_room() = chatRoom.toProto();
        } else {
          response->set_success(false);
          response->set_error_message("Failed to retrieve chat room info");
        }
      } else {
        response->set_success(false);
        response->set_error_message("Failed to add creator as owner");
      }
    } else {
      response->set_success(false);
      response->set_error_message("Failed to create chat room");
    }
  } catch (sql::SQLException& e) {
    LOG_ERROR << "CreateChatRoom SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "CreateChatRoom error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 获取聊天室
void ChatServiceImpl::GetChatRoom(
    const starrychat::GetChatRoomRequestPtr& request,
    const starrychat::GetChatRoomResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
    // 验证用户是否为聊天室成员
    if (!isChatRoomMember(request->user_id(), request->chat_room_id())) {
      response->set_success(false);
      response->set_error_message("Not a member of this chat room");
      done(response);
      return;
    }

    bool cacheMiss = true;
    ChatRoom chatRoom;
    std::vector<ChatRoomMember> members;

    // 尝试从缓存获取聊天室信息
    auto cachedChatRoom = getChatRoomFromCache(request->chat_room_id());
    if (cachedChatRoom) {
      // 由于 ChatRoom 禁用了复制赋值，所以手动复制所有属性
      chatRoom.setId(cachedChatRoom->getId());
      chatRoom.setName(cachedChatRoom->getName());
      chatRoom.setDescription(cachedChatRoom->getDescription());
      chatRoom.setCreatorId(cachedChatRoom->getCreatorId());
      chatRoom.setCreatedTime(cachedChatRoom->getCreatedTime());
      chatRoom.setMemberCount(cachedChatRoom->getMemberCount());
      chatRoom.setAvatarUrl(cachedChatRoom->getAvatarUrl());

      // 尝试从缓存获取成员列表
      members = getChatRoomMembersFromCache(request->chat_room_id());
      if (!members.empty()) {
        cacheMiss = false;
        LOG_INFO << "Chat room and members cache hit for ID: "
                 << request->chat_room_id();
      } else {
        LOG_INFO << "Chat room cache hit but members cache miss for ID: "
                 << request->chat_room_id();
      }
    } else {
      LOG_INFO << "Chat room cache miss for ID: " << request->chat_room_id();
    }

    if (cacheMiss) {
      // 缓存未命中，从数据库获取数据
      auto conn = getConnection();

      // 获取聊天室信息
      std::unique_ptr<sql::PreparedStatement> stmt(
          conn->prepareStatement("SELECT * FROM chat_rooms WHERE id = ?"));
      stmt->setUInt64(1, request->chat_room_id());

      std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
      if (rs->next()) {
        chatRoom.setId(rs->getUInt64("id"));
        chatRoom.setName(std::string(rs->getString("name")));
        chatRoom.setDescription(std::string(rs->getString("description")));
        chatRoom.setCreatorId(rs->getUInt64("creator_id"));
        chatRoom.setCreatedTime(rs->getUInt64("created_time"));
        chatRoom.setMemberCount(rs->getUInt64("member_count"));
        chatRoom.setAvatarUrl(std::string(rs->getString("avatar_url")));

        // 缓存聊天室信息
        cacheChatRoom(chatRoom);

        // 获取成员列表
        std::unique_ptr<sql::PreparedStatement> memberStmt(
            conn->prepareStatement(
                "SELECT m.*, u.nickname FROM chat_room_members m "
                "JOIN users u ON m.user_id = u.id "
                "WHERE m.chat_room_id = ?"));
        memberStmt->setUInt64(1, request->chat_room_id());

        std::unique_ptr<sql::ResultSet> memberRs(memberStmt->executeQuery());
        while (memberRs->next()) {
          // 获取所需数据
          ChatRoomMember member(
              memberRs->getUInt64("chat_room_id"),
              memberRs->getUInt64("user_id"),
              static_cast<MemberRole>(memberRs->getInt("role")));

          std::string displayName =
              std::string(memberRs->getString("display_name"));
          if (displayName.empty()) {
            displayName = std::string(memberRs->getString("nickname"));
          }
          member.setDisplayName(displayName);

          // 缓存成员信息
          cacheChatRoomMember(member);
        }
      } else {
        response->set_success(false);
        response->set_error_message("Chat room not found");
        done(response);
        return;
      }
    }

    // 设置响应
    response->set_success(true);
    *response->mutable_chat_room() = chatRoom.toProto();

    // 添加成员信息
    for (const auto& member : members) {
      *response->add_members() = member.toProto();
    }

  } catch (sql::SQLException& e) {
    LOG_ERROR << "GetChatRoom SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "GetChatRoom error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 更新聊天室
void ChatServiceImpl::UpdateChatRoom(
    const starrychat::UpdateChatRoomRequestPtr& request,
    const starrychat::UpdateChatRoomResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
    // 验证用户是否有权限更新聊天室
    if (!isChatRoomAdmin(request->user_id(), request->chat_room_id())) {
      response->set_success(false);
      response->set_error_message("No permission to update chat room");
      done(response);
      return;
    }

    auto conn = getConnection();

    // 构建更新查询
    std::string updateQuery = "UPDATE chat_rooms SET ";
    bool hasUpdates = false;

    if (!request->name().empty()) {
      updateQuery += "name = ?";
      hasUpdates = true;
    }

    if (!request->description().empty()) {
      if (hasUpdates)
        updateQuery += ", ";
      updateQuery += "description = ?";
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

    if (!request->name().empty()) {
      stmt->setString(paramIndex++, request->name());
    }

    if (!request->description().empty()) {
      stmt->setString(paramIndex++, request->description());
    }

    if (!request->avatar_url().empty()) {
      stmt->setString(paramIndex++, request->avatar_url());
    }

    stmt->setUInt64(paramIndex, request->chat_room_id());

    if (stmt->executeUpdate() > 0) {
      // 获取更新后的聊天室信息
      std::unique_ptr<sql::PreparedStatement> selectStmt(
          conn->prepareStatement("SELECT * FROM chat_rooms WHERE id = ?"));
      selectStmt->setUInt64(1, request->chat_room_id());

      std::unique_ptr<sql::ResultSet> rs(selectStmt->executeQuery());
      if (rs->next()) {
        ChatRoom chatRoom;
        chatRoom.setId(rs->getUInt64("id"));
        chatRoom.setName(std::string(rs->getString("name")));
        chatRoom.setDescription(std::string(rs->getString("description")));
        chatRoom.setCreatorId(rs->getUInt64("creator_id"));
        chatRoom.setCreatedTime(rs->getUInt64("created_time"));
        chatRoom.setMemberCount(rs->getUInt64("member_count"));
        chatRoom.setAvatarUrl(std::string(rs->getString("avatar_url")));

        // 更新缓存
        cacheChatRoom(chatRoom);

        // 设置响应
        response->set_success(true);
        *response->mutable_chat_room() = chatRoom.toProto();

        // 通知聊天室变更
        notifyChatRoomChanged(request->chat_room_id());
      } else {
        response->set_success(false);
        response->set_error_message("Chat room not found after update");
      }
    } else {
      response->set_success(false);
      response->set_error_message("No changes made");
    }
  } catch (sql::SQLException& e) {
    LOG_ERROR << "UpdateChatRoom SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "UpdateChatRoom error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 解散聊天室
void ChatServiceImpl::DissolveChatRoom(
    const starrychat::DissolveChatRoomRequestPtr& request,
    const starrychat::DissolveChatRoomResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
    // 验证用户是否为聊天室所有者
    if (!isChatRoomOwner(request->user_id(), request->chat_room_id())) {
      response->set_success(false);
      response->set_error_message("Only the owner can dissolve the chat room");
      done(response);
      return;
    }

    auto conn = getConnection();

    // 开始事务
    conn->setAutoCommit(false);

    try {
      // 获取所有成员ID用于通知
      std::vector<uint64_t> memberIds =
          getChatRoomMemberIdsFromCache(request->chat_room_id());

      if (memberIds.empty()) {
        // 缓存未命中，从数据库获取
        std::unique_ptr<sql::PreparedStatement> memberStmt(
            conn->prepareStatement("SELECT user_id FROM chat_room_members "
                                   "WHERE chat_room_id = ?"));
        memberStmt->setUInt64(1, request->chat_room_id());

        std::unique_ptr<sql::ResultSet> memberRs(memberStmt->executeQuery());
        while (memberRs->next()) {
          memberIds.push_back(memberRs->getUInt64("user_id"));
        }
      }

      // 删除所有成员
      std::unique_ptr<sql::PreparedStatement> deleteMembers(
          conn->prepareStatement(
              "DELETE FROM chat_room_members WHERE chat_room_id = ?"));
      deleteMembers->setUInt64(1, request->chat_room_id());
      deleteMembers->executeUpdate();

      // 删除聊天室
      std::unique_ptr<sql::PreparedStatement> deleteChatRoom(
          conn->prepareStatement("DELETE FROM chat_rooms WHERE id = ?"));
      deleteChatRoom->setUInt64(1, request->chat_room_id());

      if (deleteChatRoom->executeUpdate() > 0) {
        conn->commit();
        response->set_success(true);

        // 使缓存失效
        invalidateChatRoomCache(request->chat_room_id());

        // 通知所有成员
        for (uint64_t memberId : memberIds) {
          notifyMembershipChanged(request->chat_room_id(), memberId, false);
          // 使成员的聊天列表缓存失效
          invalidateUserChatsListCache(memberId);
        }
      } else {
        conn->rollback();
        response->set_success(false);
        response->set_error_message("Failed to dissolve chat room");
      }
    } catch (std::exception& e) {
      conn->rollback();
      throw;
    }
  } catch (sql::SQLException& e) {
    LOG_ERROR << "DissolveChatRoom SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "DissolveChatRoom error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 添加聊天室成员
void ChatServiceImpl::AddChatRoomMember(
    const starrychat::AddChatRoomMemberRequestPtr& request,
    const starrychat::AddChatRoomMemberResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
    // 验证操作者是否有权限添加成员
    if (!isChatRoomAdmin(request->operator_id(), request->chat_room_id())) {
      response->set_success(false);
      response->set_error_message("No permission to add members");
      done(response);
      return;
    }

    auto conn = getConnection();
    response->set_success(true);

    // 添加每个成员
    for (int i = 0; i < request->user_ids_size(); i++) {
      uint64_t userId = request->user_ids(i);

      // 检查用户是否已经是成员
      if (isChatRoomMember(userId, request->chat_room_id())) {
        continue;
      }

      if (addChatRoomMemberToDB(request->chat_room_id(), userId,
                                starrychat::MEMBER_ROLE_MEMBER)) {
        // 获取用户信息
        std::unique_ptr<sql::PreparedStatement> userStmt(
            conn->prepareStatement("SELECT nickname FROM users WHERE id = ?"));
        userStmt->setUInt64(1, userId);

        std::unique_ptr<sql::ResultSet> userRs(userStmt->executeQuery());
        if (userRs->next()) {
          // 创建成员对象
          ChatRoomMember member(request->chat_room_id(), userId,
                                starrychat::MEMBER_ROLE_MEMBER);

          // 创建成员对象
          // 注意:
          // 之前代码中已经创建了一个member变量，所以这里命名为memberObj避免重复声明
          ChatRoomMember memberObj(request->chat_room_id(), userId,
                                   starrychat::MEMBER_ROLE_MEMBER);

          // 转换 SQLString 到 std::string
          std::string nickname = std::string(userRs->getString("nickname"));
          memberObj.setDisplayName(nickname);

          // 缓存成员信息
          cacheChatRoomMember(memberObj);

          // 添加到响应
          *response->add_members() = memberObj.toProto();

          // 缓存成员信息
          cacheChatRoomMember(member);

          // 添加到缓存的成员列表
          addChatRoomMemberToCache(request->chat_room_id(), userId,
                                   starrychat::MEMBER_ROLE_MEMBER);

          // 添加到响应
          *response->add_members() = member.toProto();

          // 通知成员变更
          notifyMembershipChanged(request->chat_room_id(), userId, true);

          // 使用户的聊天列表缓存失效
          invalidateUserChatsListCache(userId);
        }
      }
    }

    // 更新成员数量
    updateChatRoomMemberCount(request->chat_room_id());

  } catch (sql::SQLException& e) {
    LOG_ERROR << "AddChatRoomMember SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "AddChatRoomMember error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 移除聊天室成员
void ChatServiceImpl::RemoveChatRoomMember(
    const starrychat::RemoveChatRoomMemberRequestPtr& request,
    const starrychat::RemoveChatRoomMemberResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
    // 验证操作者是否有权限移除成员
    if (!isChatRoomAdmin(request->operator_id(), request->chat_room_id())) {
      response->set_success(false);
      response->set_error_message("No permission to remove members");
      done(response);
      return;
    }

    response->set_success(true);

    // 移除每个成员
    for (int i = 0; i < request->user_ids_size(); i++) {
      uint64_t userId = request->user_ids(i);

      // 检查操作者不能移除自己
      if (userId == request->operator_id()) {
        continue;
      }

      // 检查不能移除聊天室所有者
      if (isChatRoomOwner(userId, request->chat_room_id())) {
        continue;
      }

      if (removeChatRoomMemberFromDB(request->chat_room_id(), userId)) {
        // 从缓存中移除成员
        removeChatRoomMemberFromCache(request->chat_room_id(), userId);

        // 通知成员变更
        notifyMembershipChanged(request->chat_room_id(), userId, false);

        // 使用户的聊天列表缓存失效
        invalidateUserChatsListCache(userId);
      }
    }

    // 更新成员数量
    updateChatRoomMemberCount(request->chat_room_id());

  } catch (sql::SQLException& e) {
    LOG_ERROR << "RemoveChatRoomMember SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "RemoveChatRoomMember error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 更新成员角色
void ChatServiceImpl::UpdateMemberRole(
    const starrychat::UpdateMemberRoleRequestPtr& request,
    const starrychat::UpdateMemberRoleResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
    // 验证操作者是否有权限
    if (!isChatRoomOwner(request->operator_id(), request->chat_room_id())) {
      response->set_success(false);
      response->set_error_message("Only the owner can update member roles");
      done(response);
      return;
    }

    // 检查不能修改自己的角色
    if (request->user_id() == request->operator_id()) {
      response->set_success(false);
      response->set_error_message("Cannot change your own role");
      done(response);
      return;
    }

    auto conn = getConnection();

    // 更新成员角色
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement("UPDATE chat_room_members SET role = ? WHERE "
                               "chat_room_id = ? AND user_id = ?"));
    stmt->setInt(1, static_cast<int>(request->new_role()));
    stmt->setUInt64(2, request->chat_room_id());
    stmt->setUInt64(3, request->user_id());

    if (stmt->executeUpdate() > 0) {
      // 获取更新后的成员信息
      std::unique_ptr<sql::PreparedStatement> selectStmt(conn->prepareStatement(
          "SELECT m.*, u.nickname FROM chat_room_members m "
          "JOIN users u ON m.user_id = u.id "
          "WHERE m.chat_room_id = ? AND m.user_id = ?"));
      selectStmt->setUInt64(1, request->chat_room_id());
      selectStmt->setUInt64(2, request->user_id());

      std::unique_ptr<sql::ResultSet> rs(selectStmt->executeQuery());
      if (rs->next()) {
        ChatRoomMember member(rs->getUInt64("chat_room_id"),
                              rs->getUInt64("user_id"),
                              static_cast<MemberRole>(rs->getInt("role")));

        // 这一段代码似乎重复了，删除这段代码
        // 上面已经创建了member变量并设置了属性

        // 更新成员缓存
        cacheChatRoomMember(member);

        // 更新响应
        auto* responseMember = response->mutable_member();
        *responseMember = member.toProto();

        response->set_success(true);

        // 通知角色变更
        notifyChatRoomChanged(request->chat_room_id());
      } else {
        response->set_success(false);
        response->set_error_message("Member not found after update");
      }
    } else {
      response->set_success(false);
      response->set_error_message("No changes made");
    }
  } catch (sql::SQLException& e) {
    LOG_ERROR << "UpdateMemberRole SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "UpdateMemberRole error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 离开聊天室
void ChatServiceImpl::LeaveChatRoom(
    const starrychat::LeaveChatRoomRequestPtr& request,
    const starrychat::LeaveChatRoomResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
    // 检查用户是否是聊天室成员
    if (!isChatRoomMember(request->user_id(), request->chat_room_id())) {
      response->set_success(false);
      response->set_error_message("Not a member of this chat room");
      done(response);
      return;
    }

    // 检查是否为所有者
    if (isChatRoomOwner(request->user_id(), request->chat_room_id())) {
      response->set_success(false);
      response->set_error_message(
          "Owner cannot leave chat room. Dissolve it instead");
      done(response);
      return;
    }

    // 移除成员
    if (removeChatRoomMemberFromDB(request->chat_room_id(),
                                   request->user_id())) {
      // 从缓存中移除成员
      removeChatRoomMemberFromCache(request->chat_room_id(),
                                    request->user_id());

      response->set_success(true);

      // 通知成员变更
      notifyMembershipChanged(request->chat_room_id(), request->user_id(),
                              false);

      // 使用户的聊天列表缓存失效
      invalidateUserChatsListCache(request->user_id());

      // 更新成员数量
      updateChatRoomMemberCount(request->chat_room_id());
    } else {
      response->set_success(false);
      response->set_error_message("Failed to leave chat room");
    }
  } catch (sql::SQLException& e) {
    LOG_ERROR << "LeaveChatRoom SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "LeaveChatRoom error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 创建私聊
void ChatServiceImpl::CreatePrivateChat(
    const starrychat::CreatePrivateChatRequestPtr& request,
    const starrychat::CreatePrivateChatResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
    // 检查用户是否存在
    auto conn = getConnection();
    std::unique_ptr<sql::PreparedStatement> userStmt(
        conn->prepareStatement("SELECT 1 FROM users WHERE id = ?"));
    userStmt->setUInt64(1, request->receiver_id());

    std::unique_ptr<sql::ResultSet> userRs(userStmt->executeQuery());
    if (!userRs->next()) {
      response->set_success(false);
      response->set_error_message("Receiver not found");
      done(response);
      return;
    }

    // 查找或创建私聊
    uint64_t privateChatId = findOrCreatePrivateChat(request->initiator_id(),
                                                     request->receiver_id());

    if (privateChatId > 0) {
      // 获取私聊信息
      std::unique_ptr<sql::PreparedStatement> stmt(
          conn->prepareStatement("SELECT * FROM private_chats WHERE id = ?"));
      stmt->setUInt64(1, privateChatId);

      std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
      if (rs->next()) {
        starrychat::PrivateChat privateChat;
        privateChat.set_id(rs->getUInt64("id"));
        privateChat.set_user1_id(rs->getUInt64("user1_id"));
        privateChat.set_user2_id(rs->getUInt64("user2_id"));
        privateChat.set_created_time(rs->getUInt64("created_time"));

        if (!rs->isNull("last_message_time")) {
          privateChat.set_last_message_time(rs->getUInt64("last_message_time"));
        }

        // 缓存私聊信息
        cachePrivateChat(privateChat);

        response->set_success(true);
        *response->mutable_private_chat() = privateChat;

        // 通知私聊创建
        notifyPrivateChatCreated(privateChatId, privateChat.user1_id(),
                                 privateChat.user2_id());

        // 使两个用户的聊天列表缓存失效
        invalidateUserChatsListCache(privateChat.user1_id());
        invalidateUserChatsListCache(privateChat.user2_id());
      } else {
        response->set_success(false);
        response->set_error_message("Failed to retrieve private chat info");
      }
    } else {
      response->set_success(false);
      response->set_error_message("Failed to create private chat");
    }
  } catch (sql::SQLException& e) {
    LOG_ERROR << "CreatePrivateChat SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "CreatePrivateChat error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 获取私聊
void ChatServiceImpl::GetPrivateChat(
    const starrychat::GetPrivateChatRequestPtr& request,
    const starrychat::GetPrivateChatResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
    // 尝试从缓存获取私聊信息
    bool cacheMiss = true;
    starrychat::PrivateChat privateChat;

    auto cachedPrivateChat =
        getPrivateChatFromCache(request->private_chat_id());
    if (cachedPrivateChat) {
      privateChat = *cachedPrivateChat;
      cacheMiss = false;
      LOG_INFO << "Private chat cache hit for ID: "
               << request->private_chat_id();
    } else {
      LOG_INFO << "Private chat cache miss for ID: "
               << request->private_chat_id();
    }

    if (cacheMiss) {
      // 缓存未命中，从数据库获取
      auto conn = getConnection();

      // 获取私聊信息
      std::unique_ptr<sql::PreparedStatement> stmt(
          conn->prepareStatement("SELECT * FROM private_chats WHERE id = ?"));
      stmt->setUInt64(1, request->private_chat_id());

      std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
      if (!rs->next()) {
        response->set_success(false);
        response->set_error_message("Private chat not found");
        done(response);
        return;
      }

      privateChat.set_id(rs->getUInt64("id"));
      privateChat.set_user1_id(rs->getUInt64("user1_id"));
      privateChat.set_user2_id(rs->getUInt64("user2_id"));
      privateChat.set_created_time(rs->getUInt64("created_time"));

      if (!rs->isNull("last_message_time")) {
        privateChat.set_last_message_time(rs->getUInt64("last_message_time"));
      }

      // 缓存私聊信息
      cachePrivateChat(privateChat);
    }

    uint64_t user1Id = privateChat.user1_id();
    uint64_t user2Id = privateChat.user2_id();

    // 验证用户是否为私聊参与者
    if (request->user_id() != user1Id && request->user_id() != user2Id) {
      response->set_success(false);
      response->set_error_message("Not a participant of this private chat");
      done(response);
      return;
    }

    // 获取伙伴信息
    uint64_t partnerId = (request->user_id() == user1Id) ? user2Id : user1Id;

    // 尝试从Redis缓存获取用户信息
    auto& redis = RedisManager::getInstance();
    std::string userKey = "user:" + std::to_string(partnerId);
    auto userData = redis.hgetall(userKey);

    if (userData && !userData->empty()) {
      // 从缓存构建用户信息
      auto* partnerInfo = response->mutable_partner_info();
      partnerInfo->set_id(partnerId);

      if (userData->find("username") != userData->end())
        partnerInfo->set_username((*userData)["username"]);

      if (userData->find("nickname") != userData->end())
        partnerInfo->set_nickname((*userData)["nickname"]);

      if (userData->find("email") != userData->end())
        partnerInfo->set_email((*userData)["email"]);

      if (userData->find("avatar_url") != userData->end())
        partnerInfo->set_avatar_url((*userData)["avatar_url"]);

      if (userData->find("status") != userData->end())
        partnerInfo->set_status(static_cast<starrychat::UserStatus>(
            std::stoi((*userData)["status"])));

      if (userData->find("created_time") != userData->end())
        partnerInfo->set_created_time(std::stoull((*userData)["created_time"]));

      if (userData->find("last_login_time") != userData->end())
        partnerInfo->set_last_login_time(
            std::stoull((*userData)["last_login_time"]));

      LOG_INFO << "Partner info cache hit for ID: " << partnerId;
    } else {
      // 缓存未命中，从数据库获取用户信息
      auto conn = getConnection();
      std::unique_ptr<sql::PreparedStatement> userStmt(
          conn->prepareStatement("SELECT * FROM users WHERE id = ?"));
      userStmt->setUInt64(1, partnerId);

      std::unique_ptr<sql::ResultSet> userRs(userStmt->executeQuery());
      if (userRs->next()) {
        auto* partnerInfo = response->mutable_partner_info();
        partnerInfo->set_id(userRs->getUInt64("id"));
        partnerInfo->set_username(userRs->getString("username"));
        partnerInfo->set_nickname(userRs->getString("nickname"));
        partnerInfo->set_email(userRs->getString("email"));
        partnerInfo->set_avatar_url(userRs->getString("avatar_url"));
        partnerInfo->set_status(
            static_cast<starrychat::UserStatus>(userRs->getInt("status")));

        if (!userRs->isNull("created_time")) {
          partnerInfo->set_created_time(userRs->getUInt64("created_time"));
        }

        if (!userRs->isNull("last_login_time")) {
          partnerInfo->set_last_login_time(
              userRs->getUInt64("last_login_time"));
        }
      }
    }

    // 设置响应
    response->set_success(true);
    *response->mutable_private_chat() = privateChat;
  } catch (sql::SQLException& e) {
    LOG_ERROR << "GetPrivateChat SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "GetPrivateChat error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 获取用户聊天列表
void ChatServiceImpl::GetUserChats(
    const starrychat::GetUserChatsRequestPtr& request,
    const starrychat::GetUserChatsResponse* responsePrototype,
    const starry::RpcDoneCallback& done) {
  auto response = responsePrototype->New();

  try {
    // 尝试从缓存获取聊天列表
    auto cachedChats = getUserChatsListFromCache(request->user_id());
    if (cachedChats) {
      LOG_INFO << "User chats list cache hit for user ID: "
               << request->user_id();

      response->set_success(true);
      for (const auto& chat : *cachedChats) {
        *response->add_chats() = chat;
      }

      done(response);
      return;
    }

    LOG_INFO << "User chats list cache miss for user ID: "
             << request->user_id();

    // 缓存未命中，从数据库获取
    auto conn = getConnection();
    if (!conn) {
      response->set_success(false);
      response->set_error_message("Database connection failed");
      done(response);
      return;
    }

    std::vector<starrychat::ChatSummary> chats;

    // 获取私聊列表
    std::unique_ptr<sql::PreparedStatement> privateStmt(conn->prepareStatement(
        "SELECT * FROM private_chats WHERE user1_id = ? OR user2_id = ? "
        "ORDER BY last_message_time DESC, created_time DESC"));
    privateStmt->setUInt64(1, request->user_id());
    privateStmt->setUInt64(2, request->user_id());

    std::unique_ptr<sql::ResultSet> privateRs(privateStmt->executeQuery());

    while (privateRs->next()) {
      uint64_t privateChatId = privateRs->getUInt64("id");
      starrychat::ChatSummary chatSummary = getChatSummary(
          starrychat::CHAT_TYPE_PRIVATE, privateChatId, request->user_id());

      *response->add_chats() = chatSummary;
      chats.push_back(chatSummary);
    }

    // 获取群聊列表
    std::unique_ptr<sql::PreparedStatement> groupStmt(conn->prepareStatement(
        "SELECT cr.* FROM chat_rooms cr "
        "JOIN chat_room_members crm ON cr.id = crm.chat_room_id "
        "WHERE crm.user_id = ? "
        "ORDER BY last_message_time DESC, created_time DESC"));
    groupStmt->setUInt64(1, request->user_id());

    std::unique_ptr<sql::ResultSet> groupRs(groupStmt->executeQuery());

    while (groupRs->next()) {
      uint64_t chatRoomId = groupRs->getUInt64("id");
      starrychat::ChatSummary chatSummary = getChatSummary(
          starrychat::CHAT_TYPE_GROUP, chatRoomId, request->user_id());

      *response->add_chats() = chatSummary;
      chats.push_back(chatSummary);
    }

    // 缓存聊天列表
    cacheUserChatsList(request->user_id(), chats);

    response->set_success(true);
  } catch (sql::SQLException& e) {
    LOG_ERROR << "GetUserChats SQL error: " << e.what();
    response->set_success(false);
    response->set_error_message("Database error");
  } catch (std::exception& e) {
    LOG_ERROR << "GetUserChats error: " << e.what();
    response->set_success(false);
    response->set_error_message("Internal error");
  }

  done(response);
}

// 验证用户是否为聊天室所有者
bool ChatServiceImpl::isChatRoomOwner(uint64_t userId, uint64_t chatRoomId) {
  try {
    auto& redis = RedisManager::getInstance();

    // 从Redis缓存检查
    std::string key = "chat_room:" + std::to_string(chatRoomId) +
                      ":member:" + std::to_string(userId);
    auto roleStr = redis.hget(key, "role");

    if (roleStr) {
      int role = std::stoi(*roleStr);
      return role == static_cast<int>(starrychat::MEMBER_ROLE_OWNER);
    }

    // 缓存未命中，从数据库查询
    auto conn = getConnection();

    std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
        "SELECT 1 FROM chat_room_members WHERE chat_room_id = ? AND user_id = "
        "? AND role = ?"));
    stmt->setUInt64(1, chatRoomId);
    stmt->setUInt64(2, userId);
    stmt->setInt(3, static_cast<int>(starrychat::MEMBER_ROLE_OWNER));

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    bool result = rs->next();

    // 如果是所有者，缓存结果
    if (result) {
      redis.hset(
          key, "role",
          std::to_string(static_cast<int>(starrychat::MEMBER_ROLE_OWNER)));
      redis.expire(key, std::chrono::hours(24));
    }

    return result;
  } catch (std::exception& e) {
    LOG_ERROR << "isChatRoomOwner error: " << e.what();
    return false;
  }
}

// 验证用户是否为聊天室管理员
bool ChatServiceImpl::isChatRoomAdmin(uint64_t userId, uint64_t chatRoomId) {
  try {
    auto& redis = RedisManager::getInstance();

    // 从Redis缓存检查
    std::string key = "chat_room:" + std::to_string(chatRoomId) +
                      ":member:" + std::to_string(userId);
    auto roleStr = redis.hget(key, "role");

    if (roleStr) {
      int role = std::stoi(*roleStr);
      return role == static_cast<int>(starrychat::MEMBER_ROLE_OWNER) ||
             role == static_cast<int>(starrychat::MEMBER_ROLE_ADMIN);
    }

    // 缓存未命中，从数据库查询
    auto conn = getConnection();

    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement("SELECT role FROM chat_room_members WHERE "
                               "chat_room_id = ? AND user_id = ?"));
    stmt->setUInt64(1, chatRoomId);
    stmt->setUInt64(2, userId);

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    if (rs->next()) {
      starrychat::MemberRole role =
          static_cast<starrychat::MemberRole>(rs->getInt("role"));

      // 缓存结果
      redis.hset(key, "role", std::to_string(static_cast<int>(role)));
      redis.expire(key, std::chrono::hours(24));

      return role == starrychat::MEMBER_ROLE_OWNER ||
             role == starrychat::MEMBER_ROLE_ADMIN;
    }
    return false;
  } catch (std::exception& e) {
    LOG_ERROR << "isChatRoomAdmin error: " << e.what();
    return false;
  }
}

// 验证用户是否为聊天室成员
bool ChatServiceImpl::isChatRoomMember(uint64_t userId, uint64_t chatRoomId) {
  try {
    auto& redis = RedisManager::getInstance();

    // 从Redis缓存检查
    std::string memberKey =
        "chat_room:" + std::to_string(chatRoomId) + ":members";
    auto members = redis.smembers(memberKey);
    if (members && !members->empty()) {
      std::string userIdStr = std::to_string(userId);
      return std::find(members->begin(), members->end(), userIdStr) !=
             members->end();
    }

    // 单独检查成员记录
    std::string key = "chat_room:" + std::to_string(chatRoomId) +
                      ":member:" + std::to_string(userId);
    if (redis.exists(key)) {
      return true;
    }

    // 缓存未命中，从数据库查询
    auto conn = getConnection();

    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement("SELECT 1 FROM chat_room_members WHERE "
                               "chat_room_id = ? AND user_id = ?"));
    stmt->setUInt64(1, chatRoomId);
    stmt->setUInt64(2, userId);

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    bool result = rs->next();

    // 如果是成员，缓存结果
    if (result) {
      redis.sadd(memberKey, std::to_string(userId));
      redis.expire(memberKey, std::chrono::hours(24));
    }

    return result;
  } catch (std::exception& e) {
    LOG_ERROR << "isChatRoomMember error: " << e.what();
    return false;
  }
}

// 验证用户是否为私聊成员
bool ChatServiceImpl::isPrivateChatMember(uint64_t userId,
                                          uint64_t privateChatId) {
  try {
    auto& redis = RedisManager::getInstance();

    // 从Redis缓存检查
    std::string key = "private_chat:" + std::to_string(privateChatId);
    auto cachedChat = redis.get(key);

    if (cachedChat) {
      starrychat::PrivateChat privateChat;
      if (privateChat.ParseFromString(*cachedChat)) {
        return privateChat.user1_id() == userId ||
               privateChat.user2_id() == userId;
      }
    }

    // 缓存未命中，从数据库查询
    auto conn = getConnection();

    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement("SELECT 1 FROM private_chats WHERE id = ? AND "
                               "(user1_id = ? OR user2_id = ?)"));
    stmt->setUInt64(1, privateChatId);
    stmt->setUInt64(2, userId);
    stmt->setUInt64(3, userId);

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    return rs->next();
  } catch (std::exception& e) {
    LOG_ERROR << "isPrivateChatMember error: " << e.what();
    return false;
  }
}

// 在数据库中创建聊天室
uint64_t ChatServiceImpl::createChatRoomInDB(const std::string& name,
                                             uint64_t creatorId,
                                             const std::string& description,
                                             const std::string& avatarUrl) {
  try {
    auto conn = getConnection();

    uint64_t currentTime =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

    std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
        "INSERT INTO chat_rooms (name, description, creator_id, created_time, "
        "member_count, avatar_url) "
        "VALUES (?, ?, ?, ?, 0, ?)",
        sql::Statement::RETURN_GENERATED_KEYS));

    stmt->setString(1, name);
    stmt->setString(2, description);
    stmt->setUInt64(3, creatorId);
    stmt->setUInt64(4, currentTime);
    stmt->setString(5, avatarUrl);

    if (stmt->executeUpdate() > 0) {
      std::unique_ptr<sql::ResultSet> rs(stmt->getGeneratedKeys());
      if (rs->next()) {
        return rs->getUInt64(1);
      }
    }

    return 0;
  } catch (sql::SQLException& e) {
    LOG_ERROR << "createChatRoomInDB SQL error: " << e.what();
    return 0;
  } catch (std::exception& e) {
    LOG_ERROR << "createChatRoomInDB error: " << e.what();
    return 0;
  }
}

// 添加聊天室成员到数据库
bool ChatServiceImpl::addChatRoomMemberToDB(uint64_t chatRoomId,
                                            uint64_t userId,
                                            starrychat::MemberRole role,
                                            const std::string& displayName) {
  try {
    auto conn = getConnection();

    uint64_t joinTime = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();

    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement("INSERT INTO chat_room_members (chat_room_id, "
                               "user_id, role, join_time, display_name) "
                               "VALUES (?, ?, ?, ?, ?) "
                               "ON DUPLICATE KEY UPDATE role = VALUES(role), "
                               "display_name = VALUES(display_name)"));

    stmt->setUInt64(1, chatRoomId);
    stmt->setUInt64(2, userId);
    stmt->setInt(3, static_cast<int>(role));
    stmt->setUInt64(4, joinTime);
    stmt->setString(5, displayName);

    return stmt->executeUpdate() > 0;
  } catch (sql::SQLException& e) {
    LOG_ERROR << "addChatRoomMemberToDB SQL error: " << e.what();
    return false;
  } catch (std::exception& e) {
    LOG_ERROR << "addChatRoomMemberToDB error: " << e.what();
    return false;
  }
}

// 从数据库中移除聊天室成员
bool ChatServiceImpl::removeChatRoomMemberFromDB(uint64_t chatRoomId,
                                                 uint64_t userId) {
  try {
    auto conn = getConnection();

    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement("DELETE FROM chat_room_members WHERE "
                               "chat_room_id = ? AND user_id = ?"));

    stmt->setUInt64(1, chatRoomId);
    stmt->setUInt64(2, userId);

    return stmt->executeUpdate() > 0;
  } catch (sql::SQLException& e) {
    LOG_ERROR << "removeChatRoomMemberFromDB SQL error: " << e.what();
    return false;
  } catch (std::exception& e) {
    LOG_ERROR << "removeChatRoomMemberFromDB error: " << e.what();
    return false;
  }
}

// 更新聊天室成员数量
bool ChatServiceImpl::updateChatRoomMemberCount(uint64_t chatRoomId) {
  try {
    auto conn = getConnection();

    // 查询成员数量
    std::unique_ptr<sql::PreparedStatement> countStmt(
        conn->prepareStatement("SELECT COUNT(*) AS count FROM "
                               "chat_room_members WHERE chat_room_id = ?"));
    countStmt->setUInt64(1, chatRoomId);

    std::unique_ptr<sql::ResultSet> countRs(countStmt->executeQuery());
    if (countRs->next()) {
      uint64_t memberCount = countRs->getUInt64("count");

      // 更新数据库中的成员数量
      std::unique_ptr<sql::PreparedStatement> updateStmt(conn->prepareStatement(
          "UPDATE chat_rooms SET member_count = ? WHERE id = ?"));
      updateStmt->setUInt64(1, memberCount);
      updateStmt->setUInt64(2, chatRoomId);

      bool result = updateStmt->executeUpdate() > 0;

      // 如果更新成功，同时更新缓存
      if (result) {
        auto cachedChatRoom = getChatRoomFromCache(chatRoomId);
        if (cachedChatRoom) {
          // 创建一个新的 ChatRoom 对象，因为不能直接修改 cachedChatRoom
          ChatRoom updatedChatRoom;
          updatedChatRoom.setId(cachedChatRoom->getId());
          updatedChatRoom.setName(cachedChatRoom->getName());
          updatedChatRoom.setDescription(cachedChatRoom->getDescription());
          updatedChatRoom.setCreatorId(cachedChatRoom->getCreatorId());
          updatedChatRoom.setCreatedTime(cachedChatRoom->getCreatedTime());
          updatedChatRoom.setAvatarUrl(cachedChatRoom->getAvatarUrl());

          // 设置新的成员数量
          updatedChatRoom.setMemberCount(memberCount);

          // 更新缓存
          cacheChatRoom(updatedChatRoom);
        }
      }

      return result;
    }

    return false;
  } catch (sql::SQLException& e) {
    LOG_ERROR << "updateChatRoomMemberCount SQL error: " << e.what();
    return false;
  } catch (std::exception& e) {
    LOG_ERROR << "updateChatRoomMemberCount error: " << e.what();
    return false;
  }
}

// 查找或创建私聊
uint64_t ChatServiceImpl::findOrCreatePrivateChat(uint64_t user1Id,
                                                  uint64_t user2Id) {
  try {
    auto& redis = RedisManager::getInstance();

    // 确保 user1Id < user2Id 以保持一致性
    if (user1Id > user2Id) {
      std::swap(user1Id, user2Id);
    }

    // 尝试从缓存获取
    std::string cacheKey = "private_chat:users:" + std::to_string(user1Id) +
                           ":" + std::to_string(user2Id);
    auto cachedId = redis.get(cacheKey);

    if (cachedId) {
      return std::stoull(*cachedId);
    }

    auto conn = getConnection();
    if (!conn) {
      return 0;
    }

    // 查找现有私聊
    std::unique_ptr<sql::PreparedStatement> findStmt(conn->prepareStatement(
        "SELECT id FROM private_chats WHERE user1_id = ? AND user2_id = ?"));
    findStmt->setUInt64(1, user1Id);
    findStmt->setUInt64(2, user2Id);

    std::unique_ptr<sql::ResultSet> findRs(findStmt->executeQuery());
    if (findRs->next()) {
      uint64_t privateChatId = findRs->getUInt64("id");

      // 缓存映射
      redis.set(cacheKey, std::to_string(privateChatId),
                std::chrono::hours(24));

      return privateChatId;
    }

    // 创建新私聊
    uint64_t createdTime =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

    std::unique_ptr<sql::PreparedStatement> createStmt(
        conn->prepareStatement("INSERT INTO private_chats (user1_id, user2_id, "
                               "created_time) VALUES (?, ?, ?)",
                               sql::Statement::RETURN_GENERATED_KEYS));

    createStmt->setUInt64(1, user1Id);
    createStmt->setUInt64(2, user2Id);
    createStmt->setUInt64(3, createdTime);

    if (createStmt->executeUpdate() > 0) {
      std::unique_ptr<sql::ResultSet> rs(createStmt->getGeneratedKeys());
      if (rs->next()) {
        uint64_t privateChatId = rs->getUInt64(1);

        // 缓存映射
        redis.set(cacheKey, std::to_string(privateChatId),
                  std::chrono::hours(24));

        return privateChatId;
      }
    }

    return 0;
  } catch (sql::SQLException& e) {
    LOG_ERROR << "findOrCreatePrivateChat SQL error: " << e.what();
    return 0;
  } catch (std::exception& e) {
    LOG_ERROR << "findOrCreatePrivateChat error: " << e.what();
    return 0;
  }
}

// 获取聊天摘要
starrychat::ChatSummary ChatServiceImpl::getChatSummary(
    starrychat::ChatType type,
    uint64_t chatId,
    uint64_t userId) {
  starrychat::ChatSummary summary;

  try {
    auto& redis = RedisManager::getInstance();
    auto conn = getConnection();

    summary.set_id(chatId);
    summary.set_type(type);

    if (type == starrychat::CHAT_TYPE_PRIVATE) {
      // 尝试从缓存获取私聊信息
      auto cachedPrivateChat = getPrivateChatFromCache(chatId);

      if (cachedPrivateChat) {
        uint64_t user1Id = cachedPrivateChat->user1_id();
        uint64_t user2Id = cachedPrivateChat->user2_id();
        uint64_t partnerId = (userId == user1Id) ? user2Id : user1Id;

        // 尝试从缓存获取伙伴信息
        std::string userKey = "user:" + std::to_string(partnerId);
        auto userData = redis.hgetall(userKey);

        if (userData && !userData->empty()) {
          if (userData->find("nickname") != userData->end())
            summary.set_name((*userData)["nickname"]);

          if (userData->find("avatar_url") != userData->end())
            summary.set_avatar_url((*userData)["avatar_url"]);
        } else {
          // 从数据库获取伙伴信息
          std::unique_ptr<sql::PreparedStatement> userStmt(
              conn->prepareStatement(
                  "SELECT nickname, avatar_url FROM users WHERE id = ?"));
          userStmt->setUInt64(1, partnerId);

          std::unique_ptr<sql::ResultSet> userRs(userStmt->executeQuery());
          if (userRs->next()) {
            summary.set_name(std::string(userRs->getString("nickname")));
            summary.set_avatar_url(
                std::string(userRs->getString("avatar_url")));
          }
        }

        // 检查是否有最后消息时间（通过检查是否大于0来代替has_last_message_time）
        if (cachedPrivateChat->last_message_time() > 0) {
          summary.set_last_message_time(cachedPrivateChat->last_message_time());
        } else {
          summary.set_last_message_time(cachedPrivateChat->created_time());
        }
      } else {
        // 从数据库获取私聊信息
        std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
            "SELECT pc.*, u1.nickname as nick1, u1.avatar_url as avatar1, "
            "u2.nickname as nick2, u2.avatar_url as avatar2 "
            "FROM private_chats pc "
            "JOIN users u1 ON pc.user1_id = u1.id "
            "JOIN users u2 ON pc.user2_id = u2.id "
            "WHERE pc.id = ?"));
        stmt->setUInt64(1, chatId);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (rs->next()) {
          uint64_t user1Id = rs->getUInt64("user1_id");

          // 使用对方的信息
          if (userId == user1Id) {
            summary.set_name(std::string(rs->getString("nick2")));
            summary.set_avatar_url(std::string(rs->getString("avatar2")));
          } else {
            summary.set_name(std::string(rs->getString("nick1")));
            summary.set_avatar_url(std::string(rs->getString("avatar1")));
          }

          if (!rs->isNull("last_message_time")) {
            summary.set_last_message_time(rs->getUInt64("last_message_time"));
          } else {
            summary.set_last_message_time(rs->getUInt64("created_time"));
          }
        }
      }
    } else if (type == starrychat::CHAT_TYPE_GROUP) {
      // 尝试从缓存获取聊天室信息
      auto cachedChatRoom = getChatRoomFromCache(chatId);

      if (cachedChatRoom) {
        summary.set_name(cachedChatRoom->getName());
        summary.set_avatar_url(cachedChatRoom->getAvatarUrl());

        // 检查是否有最后消息时间
        std::string lastActiveKey =
            "chat:last_active:" + std::to_string(static_cast<int>(type)) + ":" +
            std::to_string(chatId);
        auto lastActiveTime = redis.get(lastActiveKey);

        if (lastActiveTime) {
          summary.set_last_message_time(std::stoull(*lastActiveTime));
        } else {
          summary.set_last_message_time(cachedChatRoom->getCreatedTime());
        }
      } else {
        // 从数据库获取群聊信息
        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement("SELECT * FROM chat_rooms WHERE id = ?"));
        stmt->setUInt64(1, chatId);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (rs->next()) {
          summary.set_name(std::string(rs->getString("name")));
          summary.set_avatar_url(std::string(rs->getString("avatar_url")));

          if (!rs->isNull("last_message_time")) {
            summary.set_last_message_time(rs->getUInt64("last_message_time"));
          } else {
            summary.set_last_message_time(rs->getUInt64("created_time"));
          }
        }
      }
    }

    // 获取最后一条消息预览
    summary.set_last_message_preview(getLastMessagePreview(type, chatId));

    // 获取未读消息数
    summary.set_unread_count(getUnreadCount(userId, type, chatId));

  } catch (sql::SQLException& e) {
    LOG_ERROR << "getChatSummary SQL error: " << e.what();
  } catch (std::exception& e) {
    LOG_ERROR << "getChatSummary error: " << e.what();
  }

  return summary;
}

// 获取最后一条消息预览
std::string ChatServiceImpl::getLastMessagePreview(starrychat::ChatType type,
                                                   uint64_t chatId) {
  try {
    auto& redis = RedisManager::getInstance();

    // 尝试从缓存获取最后一条消息
    std::string lastMessageKey =
        "chat:last_message:" + std::to_string(static_cast<int>(type)) + ":" +
        std::to_string(chatId);

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
    stmt->setInt(1, static_cast<int>(type));
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

// 获取未读消息数
uint64_t ChatServiceImpl::getUnreadCount(uint64_t userId,
                                         starrychat::ChatType type,
                                         uint64_t chatId) {
  try {
    auto& redis = RedisManager::getInstance();

    // 未读计数键
    std::string unreadKey = "unread:" + std::to_string(userId) + ":" +
                            std::to_string(static_cast<int>(type)) + ":" +
                            std::to_string(chatId);

    auto countStr = redis.get(unreadKey);
    if (countStr) {
      return std::stoull(*countStr);
    }

    return 0;
  } catch (std::exception& e) {
    LOG_ERROR << "getUnreadCount error: " << e.what();
    return 0;
  }
}

// 通知聊天室变更
void ChatServiceImpl::notifyChatRoomChanged(uint64_t chatRoomId) {
  try {
    auto& redis = RedisManager::getInstance();

    // 发布聊天室变更通知
    std::string channel = "chat_room:changed:" + std::to_string(chatRoomId);
    redis.publish(channel, std::to_string(chatRoomId));

    // 使聊天室缓存失效，以便下次获取最新数据
    invalidateChatRoomCache(chatRoomId);

    LOG_INFO << "Published chat room change notification for room ID: "
             << chatRoomId;
  } catch (std::exception& e) {
    LOG_ERROR << "notifyChatRoomChanged error: " << e.what();
  }
}

// 通知成员资格变更
void ChatServiceImpl::notifyMembershipChanged(uint64_t chatRoomId,
                                              uint64_t userId,
                                              bool added) {
  try {
    auto& redis = RedisManager::getInstance();

    // 发布成员资格变更通知
    std::string channel = "chat_room:membership:" + std::to_string(chatRoomId);
    std::string message = std::to_string(userId) + ":" + (added ? "1" : "0");
    redis.publish(channel, message);

    // 个人通知
    std::string userChannel = "user:chat_room:" + std::to_string(userId);
    std::string userMessage =
        std::to_string(chatRoomId) + ":" + (added ? "1" : "0");
    redis.publish(userChannel, userMessage);

    LOG_INFO << "Published membership change notification: User " << userId
             << (added ? " added to " : " removed from ") << "chat room "
             << chatRoomId;
  } catch (std::exception& e) {
    LOG_ERROR << "notifyMembershipChanged error: " << e.what();
  }
}

// 通知私聊创建
void ChatServiceImpl::notifyPrivateChatCreated(uint64_t privateChatId,
                                               uint64_t user1Id,
                                               uint64_t user2Id) {
  try {
    auto& redis = RedisManager::getInstance();

    // 通知两个用户
    for (uint64_t userId : {user1Id, user2Id}) {
      std::string channel = "user:private_chat:" + std::to_string(userId);
      redis.publish(channel, std::to_string(privateChatId));

      // 使用户的聊天列表缓存失效
      invalidateUserChatsListCache(userId);
    }

    LOG_INFO << "Published private chat creation notification: Chat "
             << privateChatId << " between users " << user1Id << " and "
             << user2Id;
  } catch (std::exception& e) {
    LOG_ERROR << "notifyPrivateChatCreated error: " << e.what();
  }
}

// 验证会话令牌
bool ChatServiceImpl::validateSession(const std::string& token,
                                      uint64_t userId) {
  auto& redis = RedisManager::getInstance();
  auto sessionUserId = redis.get("session:" + token);

  if (!sessionUserId) {
    return false;
  }

  return std::stoull(*sessionUserId) == userId;
}

// ========== Redis 缓存相关方法实现 ==========

// 缓存聊天室
void ChatServiceImpl::cacheChatRoom(const ChatRoom& chatRoom) {
  try {
    auto& redis = RedisManager::getInstance();
    std::string key = "chat_room:" + std::to_string(chatRoom.getId());

    // 序列化聊天室信息
    std::string data = serializeChatRoom(chatRoom);

    // 存储聊天室信息
    redis.set(key, data, std::chrono::hours(24));

    LOG_INFO << "Cached chat room: " << chatRoom.getId();
  } catch (std::exception& e) {
    LOG_ERROR << "cacheChatRoom error: " << e.what();
  }
}

// 获取缓存的聊天室
std::optional<ChatRoom> ChatServiceImpl::getChatRoomFromCache(
    uint64_t chatRoomId) {
  try {
    auto& redis = RedisManager::getInstance();
    std::string key = "chat_room:" + std::to_string(chatRoomId);

    auto data = redis.get(key);
    if (!data) {
      return std::nullopt;
    }

    // 反序列化聊天室信息
    ChatRoom chatRoom = deserializeChatRoom(*data);

    // 刷新缓存过期时间
    redis.expire(key, std::chrono::hours(24));

    return chatRoom;
  } catch (std::exception& e) {
    LOG_ERROR << "getChatRoomFromCache error: " << e.what();
    return std::nullopt;
  }
}

// 使聊天室缓存失效
void ChatServiceImpl::invalidateChatRoomCache(uint64_t chatRoomId) {
  try {
    auto& redis = RedisManager::getInstance();

    // 删除聊天室缓存
    redis.del("chat_room:" + std::to_string(chatRoomId));

    // 删除成员列表缓存
    redis.del("chat_room:" + std::to_string(chatRoomId) + ":members");

    LOG_INFO << "Invalidated cache for chat room: " << chatRoomId;
  } catch (std::exception& e) {
    LOG_ERROR << "invalidateChatRoomCache error: " << e.what();
  }
}

// 缓存聊天室成员
void ChatServiceImpl::cacheChatRoomMember(const ChatRoomMember& member) {
  try {
    auto& redis = RedisManager::getInstance();

    uint64_t chatRoomId = member.getChatRoomId();
    uint64_t userId = member.getUserId();

    // 缓存成员详细信息
    std::string memberKey = "chat_room:" + std::to_string(chatRoomId) +
                            ":member:" + std::to_string(userId);
    std::string data = serializeChatRoomMember(member);
    redis.set(memberKey, data, std::chrono::hours(24));

    // 添加到成员列表
    std::string membersKey =
        "chat_room:" + std::to_string(chatRoomId) + ":members";
    redis.sadd(membersKey, std::to_string(userId));
    redis.expire(membersKey, std::chrono::hours(24));

    // 缓存成员角色
    redis.hset(memberKey, "role",
               std::to_string(static_cast<int>(member.getRole())));

    LOG_INFO << "Cached chat room member: Room " << chatRoomId << ", User "
             << userId;
  } catch (std::exception& e) {
    LOG_ERROR << "cacheChatRoomMember error: " << e.what();
  }
}

// 获取聊天室成员
std::vector<ChatRoomMember> ChatServiceImpl::getChatRoomMembersFromCache(
    uint64_t chatRoomId) {
  std::vector<ChatRoomMember> members;

  try {
    auto& redis = RedisManager::getInstance();

    // 获取成员ID列表
    std::string membersKey =
        "chat_room:" + std::to_string(chatRoomId) + ":members";
    auto memberIds = redis.smembers(membersKey);

    if (!memberIds || memberIds->empty()) {
      return members;
    }

    // 获取每个成员的详细信息
    for (const auto& idStr : *memberIds) {
      // 获取成员数据
      std::string memberKey =
          "chat_room:" + std::to_string(chatRoomId) + ":member:" + idStr;

      auto data = redis.get(memberKey);
      if (data) {
        starrychat::ChatRoomMember proto;
        if (proto.ParseFromString(*data)) {
          // 创建新的成员，避免复制
          members.push_back(ChatRoomMember::fromProto(proto));
        }
      }
    }

    // 刷新缓存过期时间
    redis.expire(membersKey, std::chrono::hours(24));

    LOG_INFO << "Retrieved " << members.size()
             << " members from cache for chat room " << chatRoomId;
  } catch (std::exception& e) {
    LOG_ERROR << "getChatRoomMembersFromCache error: " << e.what();
  }

  return members;
}

// 添加聊天室成员到缓存
void ChatServiceImpl::addChatRoomMemberToCache(uint64_t chatRoomId,
                                               uint64_t userId,
                                               starrychat::MemberRole role) {
  try {
    auto& redis = RedisManager::getInstance();

    // 添加到成员列表
    std::string membersKey =
        "chat_room:" + std::to_string(chatRoomId) + ":members";
    redis.sadd(membersKey, std::to_string(userId));
    redis.expire(membersKey, std::chrono::hours(24));

    // 缓存成员角色
    std::string memberKey = "chat_room:" + std::to_string(chatRoomId) +
                            ":member:" + std::to_string(userId);
    redis.hset(memberKey, "role", std::to_string(static_cast<int>(role)));
    redis.expire(memberKey, std::chrono::hours(24));

    LOG_INFO << "Added member to cache: Room " << chatRoomId << ", User "
             << userId;
  } catch (std::exception& e) {
    LOG_ERROR << "addChatRoomMemberToCache error: " << e.what();
  }
}

// 从缓存中移除聊天室成员
void ChatServiceImpl::removeChatRoomMemberFromCache(uint64_t chatRoomId,
                                                    uint64_t userId) {
  try {
    auto& redis = RedisManager::getInstance();

    // 从成员列表中移除
    std::string membersKey =
        "chat_room:" + std::to_string(chatRoomId) + ":members";
    redis.srem(membersKey, std::to_string(userId));

    // 删除成员详细信息
    std::string memberKey = "chat_room:" + std::to_string(chatRoomId) +
                            ":member:" + std::to_string(userId);
    redis.del(memberKey);

    LOG_INFO << "Removed member from cache: Room " << chatRoomId << ", User "
             << userId;
  } catch (std::exception& e) {
    LOG_ERROR << "removeChatRoomMemberFromCache error: " << e.what();
  }
}

// 更新缓存中的聊天室成员
void ChatServiceImpl::updateChatRoomMembersInCache(uint64_t chatRoomId) {
  try {
    auto conn = getConnection();
    auto& redis = RedisManager::getInstance();

    // 获取所有成员
    std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
        "SELECT m.*, u.nickname FROM chat_room_members m "
        "JOIN users u ON m.user_id = u.id "
        "WHERE m.chat_room_id = ?"));
    stmt->setUInt64(1, chatRoomId);

    // 清除现有成员缓存
    std::string membersKey =
        "chat_room:" + std::to_string(chatRoomId) + ":members";
    redis.del(membersKey);

    // 获取并缓存成员
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    while (rs->next()) {
      uint64_t userId = rs->getUInt64("user_id");
      MemberRole role = static_cast<MemberRole>(rs->getInt("role"));

      ChatRoomMember member(chatRoomId, userId, role);
      std::string displayName = std::string(rs->getString("display_name"));
      if (displayName.empty()) {
        displayName = std::string(rs->getString("nickname"));
      }

      // 创建新成员对象
      ChatRoomMember newMember(chatRoomId, userId, role);
      newMember.setDisplayName(displayName);

      // 将新成员添加到成员列表并缓存
      cacheChatRoomMember(newMember);

      // 缓存成员
      cacheChatRoomMember(member);
    }

    LOG_INFO << "Updated members cache for chat room " << chatRoomId;
  } catch (std::exception& e) {
    LOG_ERROR << "updateChatRoomMembersInCache error: " << e.what();
  }
}

// 获取聊天室成员ID列表
std::vector<uint64_t> ChatServiceImpl::getChatRoomMemberIdsFromCache(
    uint64_t chatRoomId) {
  std::vector<uint64_t> memberIds;

  try {
    auto& redis = RedisManager::getInstance();

    // 获取成员ID列表
    std::string membersKey =
        "chat_room:" + std::to_string(chatRoomId) + ":members";
    auto members = redis.smembers(membersKey);

    if (members && !members->empty()) {
      for (const auto& idStr : *members) {
        memberIds.push_back(std::stoull(idStr));
      }

      // 刷新缓存过期时间
      redis.expire(membersKey, std::chrono::hours(24));
    }

    LOG_INFO << "Retrieved " << memberIds.size()
             << " member IDs from cache for chat room " << chatRoomId;
  } catch (std::exception& e) {
    LOG_ERROR << "getChatRoomMemberIdsFromCache error: " << e.what();
  }

  return memberIds;
}

// 缓存私聊
void ChatServiceImpl::cachePrivateChat(
    const starrychat::PrivateChat& privateChat) {
  try {
    auto& redis = RedisManager::getInstance();

    // 序列化私聊信息
    std::string data = serializePrivateChat(privateChat);

    // 存储私聊信息
    std::string key = "private_chat:" + std::to_string(privateChat.id());
    redis.set(key, data, std::chrono::hours(24));

    // 缓存用户ID映射
    std::string userMapKey =
        "private_chat:users:" + std::to_string(privateChat.user1_id()) + ":" +
        std::to_string(privateChat.user2_id());
    redis.set(userMapKey, std::to_string(privateChat.id()),
              std::chrono::hours(24));

    // 缓存成员关系
    std::string membersKey =
        "private_chat:" + std::to_string(privateChat.id()) + ":members";
    redis.sadd(membersKey, std::to_string(privateChat.user1_id()));
    redis.sadd(membersKey, std::to_string(privateChat.user2_id()));
    redis.expire(membersKey, std::chrono::hours(24));

    LOG_INFO << "Cached private chat: " << privateChat.id();
  } catch (std::exception& e) {
    LOG_ERROR << "cachePrivateChat error: " << e.what();
  }
}

// 从缓存获取私聊
std::optional<starrychat::PrivateChat> ChatServiceImpl::getPrivateChatFromCache(
    uint64_t privateChatId) {
  try {
    auto& redis = RedisManager::getInstance();
    std::string key = "private_chat:" + std::to_string(privateChatId);

    auto data = redis.get(key);
    if (!data) {
      return std::nullopt;
    }

    // 反序列化私聊信息
    starrychat::PrivateChat privateChat = deserializePrivateChat(*data);

    // 刷新缓存过期时间
    redis.expire(key, std::chrono::hours(24));

    return privateChat;
  } catch (std::exception& e) {
    LOG_ERROR << "getPrivateChatFromCache error: " << e.what();
    return std::nullopt;
  }
}

// 使私聊缓存失效
void ChatServiceImpl::invalidatePrivateChatCache(uint64_t privateChatId) {
  try {
    auto& redis = RedisManager::getInstance();

    // 获取私聊信息以便删除用户映射
    auto cachedChat = getPrivateChatFromCache(privateChatId);
    if (cachedChat) {
      std::string userMapKey =
          "private_chat:users:" + std::to_string(cachedChat->user1_id()) + ":" +
          std::to_string(cachedChat->user2_id());
      redis.del(userMapKey);
    }

    // 删除私聊缓存
    redis.del("private_chat:" + std::to_string(privateChatId));

    // 删除成员列表缓存
    redis.del("private_chat:" + std::to_string(privateChatId) + ":members");

    LOG_INFO << "Invalidated cache for private chat: " << privateChatId;
  } catch (std::exception& e) {
    LOG_ERROR << "invalidatePrivateChatCache error: " << e.what();
  }
}

// 缓存用户聊天列表
void ChatServiceImpl::cacheUserChatsList(
    uint64_t userId,
    const std::vector<starrychat::ChatSummary>& chats) {
  try {
    auto& redis = RedisManager::getInstance();
    std::string key = "user:chats:" + std::to_string(userId);

    // 序列化聊天列表
    std::string data;
    starrychat::GetUserChatsResponse response;
    response.set_success(true);

    for (const auto& chat : chats) {
      *response.add_chats() = chat;
    }

    if (response.SerializeToString(&data)) {
      // 存储聊天列表
      redis.set(key, data,
                std::chrono::minutes(
                    30));  // 使用较短的过期时间，因为聊天列表会频繁变化
      LOG_INFO << "Cached user chats list for user " << userId << " with "
               << chats.size() << " chats";
    }
  } catch (std::exception& e) {
    LOG_ERROR << "cacheUserChatsList error: " << e.what();
  }
}

// 从缓存获取用户聊天列表
std::optional<std::vector<starrychat::ChatSummary>>
ChatServiceImpl::getUserChatsListFromCache(uint64_t userId) {
  try {
    auto& redis = RedisManager::getInstance();
    std::string key = "user:chats:" + std::to_string(userId);

    auto data = redis.get(key);
    if (!data) {
      return std::nullopt;
    }

    // 反序列化聊天列表
    starrychat::GetUserChatsResponse response;
    if (!response.ParseFromString(*data)) {
      return std::nullopt;
    }

    // 刷新缓存过期时间
    redis.expire(key, std::chrono::minutes(30));

    // 转换为 vector
    std::vector<starrychat::ChatSummary> result;
    for (int i = 0; i < response.chats_size(); i++) {
      result.push_back(response.chats(i));
    }

    return result;
  } catch (std::exception& e) {
    LOG_ERROR << "getUserChatsListFromCache error: " << e.what();
    return std::nullopt;
  }
}

// 使用户聊天列表缓存失效
void ChatServiceImpl::invalidateUserChatsListCache(uint64_t userId) {
  try {
    auto& redis = RedisManager::getInstance();
    std::string key = "user:chats:" + std::to_string(userId);
    redis.del(key);
    LOG_INFO << "Invalidated chats list cache for user " << userId;
  } catch (std::exception& e) {
    LOG_ERROR << "invalidateUserChatsListCache error: " << e.what();
  }
}

// 序列化/反序列化辅助方法
std::string ChatServiceImpl::serializeChatRoom(const ChatRoom& chatRoom) {
  starrychat::ChatRoom proto = chatRoom.toProto();
  std::string result;
  proto.SerializeToString(&result);
  return result;
}

ChatRoom ChatServiceImpl::deserializeChatRoom(const std::string& data) {
  starrychat::ChatRoom proto;
  proto.ParseFromString(data);
  return ChatRoom::fromProto(proto);
}

std::string ChatServiceImpl::serializeChatRoomMember(
    const ChatRoomMember& member) {
  starrychat::ChatRoomMember proto = member.toProto();
  std::string result;
  proto.SerializeToString(&result);
  return result;
}

ChatRoomMember ChatServiceImpl::deserializeChatRoomMember(
    const std::string& data) {
  starrychat::ChatRoomMember proto;
  proto.ParseFromString(data);
  return ChatRoomMember::fromProto(proto);
}

std::string ChatServiceImpl::serializePrivateChat(
    const starrychat::PrivateChat& privateChat) {
  std::string result;
  privateChat.SerializeToString(&result);
  return result;
}

starrychat::PrivateChat ChatServiceImpl::deserializePrivateChat(
    const std::string& data) {
  starrychat::PrivateChat privateChat;
  privateChat.ParseFromString(data);
  return privateChat;
}

}  // namespace StarryChat
