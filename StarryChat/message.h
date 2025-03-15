#pragma once

#include <cstdint>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "message.pb.h"

namespace StarryChat {

// 使用proto中定义的枚举类型
using MessageType = starrychat::MessageType;
using ChatType = starrychat::ChatType;
using MessageStatus = starrychat::MessageStatus;

class Message {
 public:
  // 构造函数
  Message();
  Message(uint64_t senderId, ChatType chatType, uint64_t chatId);
  ~Message() = default;

  // 禁用拷贝，允许移动
  Message(const Message&) = delete;
  Message& operator=(const Message&) = delete;
  Message(Message&&) noexcept = default;
  Message& operator=(Message&&) noexcept = default;

  // 基本属性访问器
  uint64_t getId() const { return id_; }
  void setId(uint64_t id) { id_ = id; }

  uint64_t getSenderId() const { return senderId_; }
  ChatType getChatType() const { return chatType_; }
  uint64_t getChatId() const { return chatId_; }
  MessageType getType() const { return type_; }
  void setType(MessageType type) { type_ = type; }

  uint64_t getTimestamp() const { return timestamp_; }
  void setTimestamp(uint64_t timestamp) { timestamp_ = timestamp; }

  MessageStatus getStatus() const { return status_; }
  void setStatus(MessageStatus status) { status_ = status; }

  // 文本消息相关
  bool isTextMessage() const { return type_ == MessageType::MESSAGE_TYPE_TEXT; }
  std::string getText() const;
  void setText(const std::string& text);

  // 系统消息相关
  bool isSystemMessage() const {
    return type_ == MessageType::MESSAGE_TYPE_SYSTEM;
  }
  std::string getSystemText() const;
  std::string getSystemCode() const;
  const std::map<std::string, std::string>& getSystemParams() const;
  void setSystemMessage(const std::string& text,
                        const std::string& code,
                        const std::map<std::string, std::string>& params);

  // 关联信息
  uint64_t getReplyToId() const { return replyToId_; }
  void setReplyToId(uint64_t messageId) { replyToId_ = messageId; }

  const std::vector<uint64_t>& getMentionUserIds() const {
    return mentionUserIds_;
  }
  void setMentionUserIds(const std::vector<uint64_t>& userIds) {
    mentionUserIds_ = userIds;
  }
  void addMentionUserId(uint64_t userId) { mentionUserIds_.push_back(userId); }

  // Protobuf序列化/反序列化
  starrychat::Message toProto() const;
  static Message fromProto(const starrychat::Message& proto);

  // 创建特定类型消息的便捷方法
  static Message createTextMessage(uint64_t senderId,
                                   ChatType chatType,
                                   uint64_t chatId,
                                   const std::string& text);

  static Message createSystemMessage(
      uint64_t senderId,
      ChatType chatType,
      uint64_t chatId,
      const std::string& text,
      const std::string& code,
      const std::map<std::string, std::string>& params);

  // 调试辅助
  std::string toString() const;

 private:
  uint64_t id_{0};
  uint64_t senderId_{0};
  ChatType chatType_{ChatType::CHAT_TYPE_UNKNOWN};
  uint64_t chatId_{0};
  MessageType type_{MessageType::MESSAGE_TYPE_UNKNOWN};
  uint64_t timestamp_{0};
  MessageStatus status_{MessageStatus::MESSAGE_STATUS_UNKNOWN};

  // 消息内容 - 支持文本和系统消息
  std::string textContent_;

  // 系统消息特有字段
  std::string systemCode_;
  std::map<std::string, std::string> systemParams_;

  // 关联信息
  uint64_t replyToId_{0};
  std::vector<uint64_t> mentionUserIds_;
};

// 使用智能指针表示消息
using MessagePtr = std::shared_ptr<Message>;
using MessageWeakPtr = std::weak_ptr<Message>;

}  // namespace StarryChat
