#include "chat_service_impl.h"

#include <algorithm>
#include <chrono>
#include <mariadb/conncpp.hpp>
#include <sstream>
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
          chatRoom.setName(rs->getString("name"));
          chatRoom.setDescription(rs->getString("description"));
          chatRoom.setCreatorId(rs->getUInt64("creator_id"));
          chatRoom.setCreatedTime(rs->getUInt64("created_time"));
          chatRoom.setMemberCount(rs->getUInt64("member_count"));
          chatRoom.setAvatarUrl(rs->getString("avatar_url"));

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

    auto conn = getConnection();

    // 获取聊天室信息
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement("SELECT * FROM chat_rooms WHERE id = ?"));
    stmt->setUInt64(1, request->chat_room_id());

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    if (rs->next()) {
      ChatRoom chatRoom;
      chatRoom.setId(rs->getUInt64("id"));
      chatRoom.setName(rs->getString("name"));
      chatRoom.setDescription(rs->getString("description"));
      chatRoom.setCreatorId(rs->getUInt64("creator_id"));
      chatRoom.setCreatedTime(rs->getUInt64("created_time"));
      chatRoom.setMemberCount(rs->getUInt64("member_count"));
      chatRoom.setAvatarUrl(rs->getString("avatar_url"));

      response->set_success(true);
      *response->mutable_chat_room() = chatRoom.toProto();

      // 获取成员列表
      std::unique_ptr<sql::PreparedStatement> memberStmt(conn->prepareStatement(
          "SELECT m.*, u.nickname FROM chat_room_members m "
          "JOIN users u ON m.user_id = u.id "
          "WHERE m.chat_room_id = ?"));
      memberStmt->setUInt64(1, request->chat_room_id());

      std::unique_ptr<sql::ResultSet> memberRs(memberStmt->executeQuery());
      while (memberRs->next()) {
        auto* member = response->add_members();
        member->set_chat_room_id(memberRs->getUInt64("chat_room_id"));
        member->set_user_id(memberRs->getUInt64("user_id"));
        member->set_role(
            static_cast<starrychat::MemberRole>(memberRs->getInt("role")));
        member->set_join_time(memberRs->getUInt64("join_time"));

        std::string displayName = memberRs->getString("display_name");
        if (displayName.empty()) {
          displayName = memberRs->getString("nickname");
        }
        member->set_display_name(displayName);
      }
    } else {
      response->set_success(false);
      response->set_error_message("Chat room not found");
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
        chatRoom.setName(rs->getString("name"));
        chatRoom.setDescription(rs->getString("description"));
        chatRoom.setCreatorId(rs->getUInt64("creator_id"));
        chatRoom.setCreatedTime(rs->getUInt64("created_time"));
        chatRoom.setMemberCount(rs->getUInt64("member_count"));
        chatRoom.setAvatarUrl(rs->getString("avatar_url"));

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
      std::unique_ptr<sql::PreparedStatement> memberStmt(conn->prepareStatement(
          "SELECT user_id FROM chat_room_members WHERE chat_room_id = ?"));
      memberStmt->setUInt64(1, request->chat_room_id());

      std::vector<uint64_t> memberIds;
      std::unique_ptr<sql::ResultSet> memberRs(memberStmt->executeQuery());
      while (memberRs->next()) {
        memberIds.push_back(memberRs->getUInt64("user_id"));
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

        // 通知所有成员
        for (uint64_t memberId : memberIds) {
          notifyMembershipChanged(request->chat_room_id(), memberId, false);
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
          // 添加到响应
          auto* member = response->add_members();
          member->set_chat_room_id(request->chat_room_id());
          member->set_user_id(userId);
          member->set_role(starrychat::MEMBER_ROLE_MEMBER);
          member->set_join_time(
              std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count());
          member->set_display_name(userRs->getString("nickname"));

          // 通知成员变更
          notifyMembershipChanged(request->chat_room_id(), userId, true);
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
        // 通知成员变更
        notifyMembershipChanged(request->chat_room_id(), userId, false);
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
        auto* member = response->mutable_member();
        member->set_chat_room_id(rs->getUInt64("chat_room_id"));
        member->set_user_id(rs->getUInt64("user_id"));
        member->set_role(
            static_cast<starrychat::MemberRole>(rs->getInt("role")));
        member->set_join_time(rs->getUInt64("join_time"));

        std::string displayName = rs->getString("display_name");
        if (displayName.empty()) {
          displayName = rs->getString("nickname");
        }
        member->set_display_name(displayName);

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
      response->set_success(true);

      // 通知成员变更
      notifyMembershipChanged(request->chat_room_id(), request->user_id(),
                              false);

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
        auto* privateChat = response->mutable_private_chat();
        privateChat->set_id(rs->getUInt64("id"));
        privateChat->set_user1_id(rs->getUInt64("user1_id"));
        privateChat->set_user2_id(rs->getUInt64("user2_id"));
        privateChat->set_created_time(rs->getUInt64("created_time"));

        if (!rs->isNull("last_message_time")) {
          privateChat->set_last_message_time(
              rs->getUInt64("last_message_time"));
        }

        response->set_success(true);

        // 通知私聊创建
        notifyPrivateChatCreated(privateChatId, privateChat->user1_id(),
                                 privateChat->user2_id());
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
    auto conn = getConnection();

    // 获取私聊信息
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement("SELECT * FROM private_chats WHERE id = ?"));
    stmt->setUInt64(1, request->private_chat_id());

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    if (rs->next()) {
      uint64_t user1Id = rs->getUInt64("user1_id");
      uint64_t user2Id = rs->getUInt64("user2_id");

      // 验证用户是否为私聊参与者
      if (request->user_id() != user1Id && request->user_id() != user2Id) {
        response->set_success(false);
        response->set_error_message("Not a participant of this private chat");
        done(response);
        return;
      }

      // 设置私聊信息
      auto* privateChat = response->mutable_private_chat();
      privateChat->set_id(rs->getUInt64("id"));
      privateChat->set_user1_id(user1Id);
      privateChat->set_user2_id(user2Id);
      privateChat->set_created_time(rs->getUInt64("created_time"));

      if (!rs->isNull("last_message_time")) {
        privateChat->set_last_message_time(rs->getUInt64("last_message_time"));
      }

      // 获取伙伴信息
      uint64_t partnerId = (request->user_id() == user1Id) ? user2Id : user1Id;

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

      response->set_success(true);
    } else {
      response->set_success(false);
      response->set_error_message("Private chat not found");
    }
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
    auto conn = getConnection();

    // 获取私聊列表
    std::unique_ptr<sql::PreparedStatement> privateStmt(conn->prepareStatement(
        "SELECT * FROM private_chats WHERE user1_id = ? OR user2_id = ? "
        "ORDER BY last_message_time DESC, created_time DESC"));
    privateStmt->setUInt64(1, request->user_id());
    privateStmt->setUInt64(2, request->user_id());

    std::unique_ptr<sql::ResultSet> privateRs(privateStmt->executeQuery());

    while (privateRs->next()) {
      uint64_t privateChatId = privateRs->getUInt64("id");
      *response->add_chats() = getChatSummary(
          starrychat::CHAT_TYPE_PRIVATE, privateChatId, request->user_id());
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
      *response->add_chats() = getChatSummary(starrychat::CHAT_TYPE_GROUP,
                                              chatRoomId, request->user_id());
    }

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
    auto conn = getConnection();

    std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
        "SELECT 1 FROM chat_room_members WHERE chat_room_id = ? AND user_id = "
        "? AND role = ?"));
    stmt->setUInt64(1, chatRoomId);
    stmt->setUInt64(2, userId);
    stmt->setInt(3, static_cast<int>(starrychat::MEMBER_ROLE_OWNER));

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    return rs->next();
  } catch (std::exception& e) {
    LOG_ERROR << "isChatRoomOwner error: " << e.what();
    return false;
  }
}

// 验证用户是否为聊天室管理员
bool ChatServiceImpl::isChatRoomAdmin(uint64_t userId, uint64_t chatRoomId) {
  try {
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
    auto conn = getConnection();

    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement("SELECT 1 FROM chat_room_members WHERE "
                               "chat_room_id = ? AND user_id = ?"));
    stmt->setUInt64(1, chatRoomId);
    stmt->setUInt64(2, userId);

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    return rs->next();
  } catch (std::exception& e) {
    LOG_ERROR << "isChatRoomMember error: " << e.what();
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

      // 更新成员数量
      std::unique_ptr<sql::PreparedStatement> updateStmt(conn->prepareStatement(
          "UPDATE chat_rooms SET member_count = ? WHERE id = ?"));
      updateStmt->setUInt64(1, memberCount);
      updateStmt->setUInt64(2, chatRoomId);

      return updateStmt->executeUpdate() > 0;
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
    auto conn = getConnection();

    // 确保 user1Id < user2Id 以保持一致性
    if (user1Id > user2Id) {
      std::swap(user1Id, user2Id);
    }

    // 查找现有私聊
    std::unique_ptr<sql::PreparedStatement> findStmt(conn->prepareStatement(
        "SELECT id FROM private_chats WHERE user1_id = ? AND user2_id = ?"));
    findStmt->setUInt64(1, user1Id);
    findStmt->setUInt64(2, user2Id);

    std::unique_ptr<sql::ResultSet> findRs(findStmt->executeQuery());
    if (findRs->next()) {
      return findRs->getUInt64("id");
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
        return rs->getUInt64(1);
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
    auto conn = getConnection();

    summary.set_id(chatId);
    summary.set_type(type);

    if (type == starrychat::CHAT_TYPE_PRIVATE) {
      // 私聊摘要
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
        uint64_t user2Id = rs->getUInt64("user2_id");

        // 使用对方的信息
        if (userId == user1Id) {
          summary.set_name(rs->getString("nick2"));
          summary.set_avatar_url(rs->getString("avatar2"));
        } else {
          summary.set_name(rs->getString("nick1"));
          summary.set_avatar_url(rs->getString("avatar1"));
        }

        if (!rs->isNull("last_message_time")) {
          summary.set_last_message_time(rs->getUInt64("last_message_time"));
        } else {
          summary.set_last_message_time(rs->getUInt64("created_time"));
        }
      }
    } else if (type == starrychat::CHAT_TYPE_GROUP) {
      // 群聊摘要
      std::unique_ptr<sql::PreparedStatement> stmt(
          conn->prepareStatement("SELECT * FROM chat_rooms WHERE id = ?"));
      stmt->setUInt64(1, chatId);

      std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
      if (rs->next()) {
        summary.set_name(rs->getString("name"));
        summary.set_avatar_url(rs->getString("avatar_url"));

        if (!rs->isNull("last_message_time")) {
          summary.set_last_message_time(rs->getUInt64("last_message_time"));
        } else {
          summary.set_last_message_time(rs->getUInt64("created_time"));
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
    auto conn = getConnection();

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

      if (msgType == starrychat::MESSAGE_TYPE_TEXT) {
        std::string content = rs->getString("content");
        // 限制预览长度
        if (content.length() > 30) {
          content = content.substr(0, 27) + "...";
        }
        return content;
      } else if (msgType == starrychat::MESSAGE_TYPE_SYSTEM) {
        return "[System: " + rs->getString("system_code") + "]";
      } else if (msgType == starrychat::MESSAGE_TYPE_IMAGE) {
        return "[Image]";
      } else if (msgType == starrychat::MESSAGE_TYPE_FILE) {
        return "[File]";
      } else if (msgType == starrychat::MESSAGE_TYPE_AUDIO) {
        return "[Audio]";
      } else if (msgType == starrychat::MESSAGE_TYPE_VIDEO) {
        return "[Video]";
      } else if (msgType == starrychat::MESSAGE_TYPE_LOCATION) {
        return "[Location]";
      } else if (msgType == starrychat::MESSAGE_TYPE_RECALL) {
        return "[Message was recalled]";
      }
    }

    return "";
  } catch (sql::SQLException& e) {
    LOG_ERROR << "getLastMessagePreview SQL error: " << e.what();
    return "";
  } catch (std::exception& e) {
    LOG_ERROR << "getLastMessagePreview error: " << e.what();
    return "";
  }
}

// 获取未读消息数
uint64_t ChatServiceImpl::getUnreadCount(uint64_t userId,
                                         starrychat::ChatType type,
                                         uint64_t chatId) {
  try {
    auto& redis = RedisManager::getInstance();

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
    }
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

}  // namespace StarryChat
