#pragma once

#include <memory>
#include <string>
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
  uint64_t saveMessageToDatabase(const starrychat::Message& message);
  void publishMessageNotification(const starrychat::Message& message);
  void cacheRecentMessage(const starrychat::Message& message);
  std::vector<uint64_t> getChatMembers(starrychat::ChatType chatType,
                                       uint64_t chatId);

  // 消息状态相关
  void updateMessageStatusInDB(uint64_t messageId,
                               starrychat::MessageStatus status);
  void publishStatusChangeNotification(uint64_t messageId,
                                       starrychat::MessageStatus status);
};

}  // namespace StarryChat
