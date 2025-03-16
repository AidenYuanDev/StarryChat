#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "chat.pb.h"
#include "chat_room.h"
#include "service.h"

namespace sql {
class Connection;
}

namespace StarryChat {

class ChatServiceImpl : public starrychat::ChatService {
 public:
  ChatServiceImpl() = default;
  ~ChatServiceImpl() = default;

  // 聊天室操作
  void CreateChatRoom(
      const starrychat::CreateChatRoomRequestPtr& request,
      const starrychat::CreateChatRoomResponse* responsePrototype,
      const starry::RpcDoneCallback& done) override;

  void GetChatRoom(const starrychat::GetChatRoomRequestPtr& request,
                   const starrychat::GetChatRoomResponse* responsePrototype,
                   const starry::RpcDoneCallback& done) override;

  void UpdateChatRoom(
      const starrychat::UpdateChatRoomRequestPtr& request,
      const starrychat::UpdateChatRoomResponse* responsePrototype,
      const starry::RpcDoneCallback& done) override;

  void DissolveChatRoom(
      const starrychat::DissolveChatRoomRequestPtr& request,
      const starrychat::DissolveChatRoomResponse* responsePrototype,
      const starry::RpcDoneCallback& done) override;

  // 聊天室成员管理
  void AddChatRoomMember(
      const starrychat::AddChatRoomMemberRequestPtr& request,
      const starrychat::AddChatRoomMemberResponse* responsePrototype,
      const starry::RpcDoneCallback& done) override;

  void RemoveChatRoomMember(
      const starrychat::RemoveChatRoomMemberRequestPtr& request,
      const starrychat::RemoveChatRoomMemberResponse* responsePrototype,
      const starry::RpcDoneCallback& done) override;

  void UpdateMemberRole(
      const starrychat::UpdateMemberRoleRequestPtr& request,
      const starrychat::UpdateMemberRoleResponse* responsePrototype,
      const starry::RpcDoneCallback& done) override;

  void LeaveChatRoom(const starrychat::LeaveChatRoomRequestPtr& request,
                     const starrychat::LeaveChatRoomResponse* responsePrototype,
                     const starry::RpcDoneCallback& done) override;

  // 私聊操作
  void CreatePrivateChat(
      const starrychat::CreatePrivateChatRequestPtr& request,
      const starrychat::CreatePrivateChatResponse* responsePrototype,
      const starry::RpcDoneCallback& done) override;

  void GetPrivateChat(
      const starrychat::GetPrivateChatRequestPtr& request,
      const starrychat::GetPrivateChatResponse* responsePrototype,
      const starry::RpcDoneCallback& done) override;

  // 聊天列表
  void GetUserChats(const starrychat::GetUserChatsRequestPtr& request,
                    const starrychat::GetUserChatsResponse* responsePrototype,
                    const starry::RpcDoneCallback& done) override;

 private:
  // 获取数据库连接
  std::shared_ptr<sql::Connection> getConnection();

  // 会话验证
  bool validateSession(const std::string& token, uint64_t userId);

  // 聊天室权限检查
  bool isChatRoomOwner(uint64_t userId, uint64_t chatRoomId);
  bool isChatRoomAdmin(uint64_t userId, uint64_t chatRoomId);
  bool isChatRoomMember(uint64_t userId, uint64_t chatRoomId);

  // 私聊成员检查
  bool isPrivateChatMember(uint64_t userId, uint64_t privateChatId);

  // 通知助手方法
  void notifyChatRoomChanged(uint64_t chatRoomId);
  void notifyMembershipChanged(uint64_t chatRoomId,
                               uint64_t userId,
                               bool added);
  void notifyPrivateChatCreated(uint64_t privateChatId,
                                uint64_t user1Id,
                                uint64_t user2Id);

  // 聊天室辅助方法
  uint64_t createChatRoomInDB(const std::string& name,
                              uint64_t creatorId,
                              const std::string& description,
                              const std::string& avatarUrl);

  bool addChatRoomMemberToDB(uint64_t chatRoomId,
                             uint64_t userId,
                             starrychat::MemberRole role,
                             const std::string& displayName = "");

  bool removeChatRoomMemberFromDB(uint64_t chatRoomId, uint64_t userId);
  bool updateChatRoomMemberCount(uint64_t chatRoomId);

  // 私聊辅助方法
  uint64_t findOrCreatePrivateChat(uint64_t user1Id, uint64_t user2Id);

  // 聊天摘要
  starrychat::ChatSummary getChatSummary(starrychat::ChatType type,
                                         uint64_t chatId,
                                         uint64_t userId);
  std::string getLastMessagePreview(starrychat::ChatType type, uint64_t chatId);
  uint64_t getUnreadCount(uint64_t userId,
                          starrychat::ChatType type,
                          uint64_t chatId);

  // Redis 缓存相关方法
  // 聊天室缓存
  void cacheChatRoom(const ChatRoom& chatRoom);
  std::optional<ChatRoom> getChatRoomFromCache(uint64_t chatRoomId);
  void invalidateChatRoomCache(uint64_t chatRoomId);

  // 聊天室成员缓存
  void cacheChatRoomMember(const ChatRoomMember& member);
  std::vector<ChatRoomMember> getChatRoomMembersFromCache(uint64_t chatRoomId);
  void addChatRoomMemberToCache(uint64_t chatRoomId,
                                uint64_t userId,
                                starrychat::MemberRole role);
  void removeChatRoomMemberFromCache(uint64_t chatRoomId, uint64_t userId);
  void updateChatRoomMembersInCache(uint64_t chatRoomId);
  std::vector<uint64_t> getChatRoomMemberIdsFromCache(uint64_t chatRoomId);

  // 私聊缓存
  void cachePrivateChat(const starrychat::PrivateChat& privateChat);
  std::optional<starrychat::PrivateChat> getPrivateChatFromCache(
      uint64_t privateChatId);
  void invalidatePrivateChatCache(uint64_t privateChatId);

  // 用户聊天列表缓存
  void cacheUserChatsList(uint64_t userId,
                          const std::vector<starrychat::ChatSummary>& chats);
  std::optional<std::vector<starrychat::ChatSummary>> getUserChatsListFromCache(
      uint64_t userId);
  void invalidateUserChatsListCache(uint64_t userId);

  // 序列化/反序列化辅助方法
  std::string serializeChatRoom(const ChatRoom& chatRoom);
  ChatRoom deserializeChatRoom(const std::string& data);
  std::string serializeChatRoomMember(const ChatRoomMember& member);
  ChatRoomMember deserializeChatRoomMember(const std::string& data);
  std::string serializePrivateChat(const starrychat::PrivateChat& privateChat);
  starrychat::PrivateChat deserializePrivateChat(const std::string& data);
};

}  // namespace StarryChat
