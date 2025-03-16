#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "message.pb.h"
#include "service.h"

namespace sql {
class Connection;
}

namespace StarryChat {

class MessageServiceImpl : public starrychat::MessageService {
 public:
  MessageServiceImpl() = default;
  ~MessageServiceImpl() = default;

  // RPC 服务方法实现
  void GetMessages(const starrychat::GetMessagesRequestPtr& request,
                   const starrychat::GetMessagesResponse* responsePrototype,
                   const starry::RpcDoneCallback& done) override;

  void SendMessage(const starrychat::SendMessageRequestPtr& request,
                   const starrychat::SendMessageResponse* responsePrototype,
                   const starry::RpcDoneCallback& done) override;

  void UpdateMessageStatus(
      const starrychat::UpdateMessageStatusRequestPtr& request,
      const starrychat::UpdateMessageStatusResponse* responsePrototype,
      const starry::RpcDoneCallback& done) override;

  void RecallMessage(const starrychat::RecallMessageRequestPtr& request,
                     const starrychat::RecallMessageResponse* responsePrototype,
                     const starry::RpcDoneCallback& done) override;

 private:
  // 获取数据库连接
  std::shared_ptr<sql::Connection> getConnection();

  // 会话验证
  bool validateSession(const std::string& token, uint64_t userId);

  // 消息处理辅助方法
  bool isValidChatMember(uint64_t userId,
                         starrychat::ChatType chatType,
                         uint64_t chatId);

  // 数据库操作方法
  uint64_t saveMessageToDatabase(const starrychat::Message& message);
  bool updateMessageStatusInDB(uint64_t messageId,
                               starrychat::MessageStatus status);

  // Redis缓存方法
  void cacheMessage(const starrychat::Message& message);
  std::optional<starrychat::Message> getMessageFromCache(uint64_t messageId);
  void invalidateMessageCache(uint64_t messageId);
  void updateMessageTimeline(starrychat::ChatType chatType,
                             uint64_t chatId,
                             uint64_t messageId,
                             uint64_t timestamp);
  std::vector<uint64_t> getRecentMessageIds(starrychat::ChatType chatType,
                                            uint64_t chatId,
                                            int limit,
                                            uint64_t beforeMsgId = 0);

  // 通知方法
  void publishMessageNotification(const starrychat::Message& message);
  void publishStatusChangeNotification(uint64_t messageId,
                                       starrychat::MessageStatus status);

  // 未读消息管理
  void incrementUnreadCount(uint64_t userId,
                            starrychat::ChatType chatType,
                            uint64_t chatId);
  void resetUnreadCount(uint64_t userId,
                        starrychat::ChatType chatType,
                        uint64_t chatId);
  uint64_t getUnreadCount(uint64_t userId,
                          starrychat::ChatType chatType,
                          uint64_t chatId);

  // 用户和成员管理
  std::vector<uint64_t> getChatMembers(starrychat::ChatType chatType,
                                       uint64_t chatId);
  std::string getLastMessagePreview(starrychat::ChatType chatType,
                                    uint64_t chatId);
  void updateLastMessage(starrychat::ChatType chatType,
                         uint64_t chatId,
                         const starrychat::Message& message);
};

}  // namespace StarryChat
