#include <chrono>
#include <sstream>
#include "message.h"

namespace StarryChat {

// 构造函数实现
Message::Message()
    : timestamp_(std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count()),
      status_(MessageStatus::MESSAGE_STATUS_SENDING) {}

Message::Message(uint64_t senderId, ChatType chatType, uint64_t chatId)
    : senderId_(senderId),
      chatType_(chatType),
      chatId_(chatId),
      timestamp_(std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count()),
      status_(MessageStatus::MESSAGE_STATUS_SENDING) {}

// 文本消息相关方法
std::string Message::getText() const {
  if (isTextMessage()) {
    return textContent_;
  }
  return "";  // 非文本消息返回空字符串
}

void Message::setText(const std::string& text) {
  textContent_ = text;
  type_ = MessageType::MESSAGE_TYPE_TEXT;
}

// 系统消息相关方法
std::string Message::getSystemText() const {
  if (isSystemMessage()) {
    return textContent_;
  }
  return "";  // 非系统消息返回空字符串
}

std::string Message::getSystemCode() const {
  if (isSystemMessage()) {
    return systemCode_;
  }
  return "";  // 非系统消息返回空字符串
}

const std::map<std::string, std::string>& Message::getSystemParams() const {
  return systemParams_;  // 如果不是系统消息，将返回空map
}

void Message::setSystemMessage(
    const std::string& text,
    const std::string& code,
    const std::map<std::string, std::string>& params) {
  textContent_ = text;
  systemCode_ = code;
  systemParams_ = params;
  type_ = MessageType::MESSAGE_TYPE_SYSTEM;
}

// Protobuf 转换方法
starrychat::Message Message::toProto() const {
  starrychat::Message proto;

  // 基本字段
  proto.set_id(id_);
  proto.set_sender_id(senderId_);
  proto.set_chat_type(chatType_);
  proto.set_chat_id(chatId_);
  proto.set_type(type_);
  proto.set_timestamp(timestamp_);
  proto.set_status(status_);

  // 关联信息
  if (replyToId_ > 0) {
    proto.set_reply_to_id(replyToId_);
  }

  for (const auto& userId : mentionUserIds_) {
    proto.add_mention_user_ids(userId);
  }

  // 内容 - 根据消息类型
  if (isTextMessage()) {
    auto* text = proto.mutable_text();
    text->set_text(textContent_);
  } else if (isSystemMessage()) {
    auto* system = proto.mutable_system();
    system->set_text(textContent_);
    system->set_code(systemCode_);

    for (const auto& [key, value] : systemParams_) {
      (*system->mutable_params())[key] = value;
    }
  }

  return proto;
}

Message Message::fromProto(const starrychat::Message& proto) {
  Message message;

  // 基本字段
  message.id_ = proto.id();
  message.senderId_ = proto.sender_id();
  message.chatType_ = proto.chat_type();
  message.chatId_ = proto.chat_id();
  message.type_ = proto.type();
  message.timestamp_ = proto.timestamp();
  message.status_ = proto.status();

  // 关联信息
  if (proto.reply_to_id() > 0) {
    message.replyToId_ = proto.reply_to_id();
  }
  for (const auto& userId : proto.mention_user_ids()) {
    message.mentionUserIds_.push_back(userId);
  }

  // 内容 - 根据消息类型
  if (proto.type() == MessageType::MESSAGE_TYPE_TEXT && proto.has_text()) {
    message.textContent_ = proto.text().text();
  } else if (proto.type() == MessageType::MESSAGE_TYPE_SYSTEM &&
             proto.has_system()) {
    message.textContent_ = proto.system().text();
    message.systemCode_ = proto.system().code();

    for (const auto& [key, value] : proto.system().params()) {
      message.systemParams_[key] = value;
    }
  }

  return message;
}

// 便捷创建方法
Message Message::createTextMessage(uint64_t senderId,
                                   ChatType chatType,
                                   uint64_t chatId,
                                   const std::string& text) {
  Message message(senderId, chatType, chatId);
  message.setText(text);
  return message;
}

Message Message::createSystemMessage(
    uint64_t senderId,
    ChatType chatType,
    uint64_t chatId,
    const std::string& text,
    const std::string& code,
    const std::map<std::string, std::string>& params) {
  Message message(senderId, chatType, chatId);
  message.setSystemMessage(text, code, params);
  return message;
}

// 调试方法
std::string Message::toString() const {
  std::stringstream ss;
  ss << "Message[id=" << id_ << ", senderId=" << senderId_
     << ", chatType=" << static_cast<int>(chatType_) << ", chatId=" << chatId_
     << ", type=" << static_cast<int>(type_) << ", timestamp=" << timestamp_
     << ", status=" << static_cast<int>(status_) << ", content=";

  if (isTextMessage()) {
    ss << "\"" << textContent_ << "\"";
  } else if (isSystemMessage()) {
    ss << "[System: code=" << systemCode_ << ", text=\"" << textContent_
       << "\", params=";

    ss << "{";
    bool first = true;
    for (const auto& [key, value] : systemParams_) {
      if (!first)
        ss << ", ";
      ss << key << ":" << value;
      first = false;
    }
    ss << "}";
  }

  ss << "]";
  return ss.str();
}

}  // namespace StarryChat
