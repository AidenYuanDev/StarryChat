#pragma once

#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include "chat.pb.h"

namespace StarryChat {

// 使用proto中定义的枚举类型
using MemberRole = starrychat::MemberRole;

// 前向声明
class ChatRoomMember;

class ChatRoom {
 public:
  // 构造函数
  ChatRoom();
  ChatRoom(uint64_t id, const std::string& name, uint64_t creatorId);
  ~ChatRoom() = default;

  // 禁用拷贝，允许移动
  ChatRoom(const ChatRoom&) = delete;
  ChatRoom& operator=(const ChatRoom&) = delete;
  ChatRoom(ChatRoom&&) noexcept = default;
  ChatRoom& operator=(ChatRoom&&) noexcept = default;

  // 基本属性访问器
  uint64_t getId() const { return id_; }
  void setId(uint64_t id) { id_ = id; }

  const std::string& getName() const { return name_; }
  void setName(const std::string& name) { name_ = name; }

  const std::string& getDescription() const { return description_; }
  void setDescription(const std::string& description) {
    description_ = description;
  }

  uint64_t getCreatorId() const { return creatorId_; }

  uint64_t getCreatedTime() const { return createdTime_; }

  uint64_t getMemberCount() const { return memberCount_; }
  void setMemberCount(uint64_t count) { memberCount_ = count; }

  const std::string& getAvatarUrl() const { return avatarUrl_; }
  void setAvatarUrl(const std::string& url) { avatarUrl_ = url; }

  // 成员管理 - 这里只定义接口，实际实现会在ChatManager中
  // 因为成员可能需要数据库交互
  bool isMember(uint64_t userId) const;
  bool isAdmin(uint64_t userId) const;
  bool isOwner(uint64_t userId) const;

  // Protobuf序列化/反序列化
  starrychat::ChatRoom toProto() const;
  static ChatRoom fromProto(const starrychat::ChatRoom& proto);

  // 创建聊天室的便捷方法
  static ChatRoom createChatRoom(const std::string& name,
                                 uint64_t creatorId,
                                 const std::string& description = "",
                                 const std::string& avatarUrl = "");

  // 调试辅助
  std::string toString() const;

 private:
  uint64_t id_{0};
  std::string name_;
  std::string description_;
  uint64_t creatorId_{0};
  uint64_t createdTime_{0};
  uint64_t memberCount_{0};
  std::string avatarUrl_;
};

// 聊天室成员类
class ChatRoomMember {
 public:
  // 构造函数
  ChatRoomMember();
  ChatRoomMember(uint64_t chatRoomId, uint64_t userId, MemberRole role);
  ~ChatRoomMember() = default;

  // 禁用拷贝，允许移动
  ChatRoomMember(const ChatRoomMember&) = delete;
  ChatRoomMember& operator=(const ChatRoomMember&) = delete;
  ChatRoomMember(ChatRoomMember&&) noexcept = default;
  ChatRoomMember& operator=(ChatRoomMember&&) noexcept = default;

  // 基本属性访问器
  uint64_t getChatRoomId() const { return chatRoomId_; }
  uint64_t getUserId() const { return userId_; }

  MemberRole getRole() const { return role_; }
  void setRole(MemberRole role) { role_ = role; }

  uint64_t getJoinTime() const { return joinTime_; }

  const std::string& getDisplayName() const { return displayName_; }
  void setDisplayName(const std::string& name) { displayName_ = name; }

  // 角色检查
  bool isOwner() const { return role_ == MemberRole::MEMBER_ROLE_OWNER; }
  bool isAdmin() const {
    return role_ == MemberRole::MEMBER_ROLE_ADMIN || isOwner();
  }

  // Protobuf序列化/反序列化
  starrychat::ChatRoomMember toProto() const;
  static ChatRoomMember fromProto(const starrychat::ChatRoomMember& proto);

  // 调试辅助
  std::string toString() const;

 private:
  uint64_t chatRoomId_{0};
  uint64_t userId_{0};
  MemberRole role_{MemberRole::MEMBER_ROLE_MEMBER};
  uint64_t joinTime_{0};
  std::string displayName_;
};

// 使用智能指针表示聊天室和成员
using ChatRoomPtr = std::shared_ptr<ChatRoom>;
using ChatRoomMemberPtr = std::shared_ptr<ChatRoomMember>;

}  // namespace StarryChat
