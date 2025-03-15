#include <chrono>
#include <sstream>
#include "chat_room.h"

namespace StarryChat {

// ChatRoom 实现

ChatRoom::ChatRoom()
    : createdTime_(std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count()),
      memberCount_(0) {}

ChatRoom::ChatRoom(uint64_t id, const std::string& name, uint64_t creatorId)
    : id_(id),
      name_(name),
      creatorId_(creatorId),
      createdTime_(std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count()),
      memberCount_(1) {  // 创建者默认为成员
}

// 这些方法需要访问成员列表，在实际实现中会由ChatManager提供
bool ChatRoom::isMember(uint64_t userId) const {
  // 在实际实现中，这里会检查数据库或内存中的成员列表
  // 暂时返回假设值
  return userId == creatorId_;  // 假设只有创建者是成员
}

bool ChatRoom::isAdmin(uint64_t userId) const {
  // 在实际实现中，这里会检查数据库或内存中的成员列表
  // 暂时返回假设值
  return userId == creatorId_;  // 假设只有创建者是管理员
}

bool ChatRoom::isOwner(uint64_t userId) const {
  return userId == creatorId_;
}

starrychat::ChatRoom ChatRoom::toProto() const {
  starrychat::ChatRoom proto;

  proto.set_id(id_);
  proto.set_name(name_);
  proto.set_description(description_);
  proto.set_creator_id(creatorId_);
  proto.set_created_time(createdTime_);
  proto.set_member_count(memberCount_);
  proto.set_avatar_url(avatarUrl_);

  return proto;
}

ChatRoom ChatRoom::fromProto(const starrychat::ChatRoom& proto) {
  ChatRoom chatRoom;

  chatRoom.id_ = proto.id();
  chatRoom.name_ = proto.name();
  chatRoom.description_ = proto.description();
  chatRoom.creatorId_ = proto.creator_id();
  chatRoom.createdTime_ = proto.created_time();
  chatRoom.memberCount_ = proto.member_count();
  chatRoom.avatarUrl_ = proto.avatar_url();

  return chatRoom;
}

ChatRoom ChatRoom::createChatRoom(const std::string& name,
                                  uint64_t creatorId,
                                  const std::string& description,
                                  const std::string& avatarUrl) {
  ChatRoom chatRoom(0, name, creatorId);  // ID 0表示新创建的，稍后会分配真实ID
  chatRoom.setDescription(description);
  chatRoom.setAvatarUrl(avatarUrl);
  return chatRoom;
}

std::string ChatRoom::toString() const {
  std::stringstream ss;
  ss << "ChatRoom[id=" << id_ << ", name=" << name_
     << ", description=" << description_ << ", creatorId=" << creatorId_
     << ", createdTime=" << createdTime_ << ", memberCount=" << memberCount_
     << ", avatarUrl=" << avatarUrl_ << "]";
  return ss.str();
}

// ChatRoomMember 实现

ChatRoomMember::ChatRoomMember()
    : joinTime_(std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count()) {}

ChatRoomMember::ChatRoomMember(uint64_t chatRoomId,
                               uint64_t userId,
                               MemberRole role)
    : chatRoomId_(chatRoomId),
      userId_(userId),
      role_(role),
      joinTime_(std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count()) {}

starrychat::ChatRoomMember ChatRoomMember::toProto() const {
  starrychat::ChatRoomMember proto;

  proto.set_chat_room_id(chatRoomId_);
  proto.set_user_id(userId_);
  proto.set_role(role_);
  proto.set_join_time(joinTime_);
  proto.set_display_name(displayName_);

  return proto;
}

ChatRoomMember ChatRoomMember::fromProto(
    const starrychat::ChatRoomMember& proto) {
  ChatRoomMember member;

  member.chatRoomId_ = proto.chat_room_id();
  member.userId_ = proto.user_id();
  member.role_ = proto.role();
  member.joinTime_ = proto.join_time();
  member.displayName_ = proto.display_name();

  return member;
}

std::string ChatRoomMember::toString() const {
  std::stringstream ss;
  ss << "ChatRoomMember[chatRoomId=" << chatRoomId_ << ", userId=" << userId_
     << ", role=" << static_cast<int>(role_) << ", joinTime=" << joinTime_
     << ", displayName=" << displayName_ << "]";
  return ss.str();
}

}  // namespace StarryChat
