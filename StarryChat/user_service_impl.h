#pragma once

#include <memory>
#include <string>
#include "service.h"
#include "user.pb.h"
#include "user.h"

namespace sql {
class Connection;
}

namespace StarryChat {

class UserServiceImpl : public starrychat::UserService {
 public:
  UserServiceImpl() = default;
  ~UserServiceImpl() = default;

  // RPC 服务方法实现
  void RegisterUser(const starrychat::RegisterUserRequestPtr& request,
                    const starrychat::RegisterUserResponse* responsePrototype,
                    const starry::RpcDoneCallback& done) override;

  void Login(const starrychat::LoginRequestPtr& request,
             const starrychat::LoginResponse* responsePrototype,
             const starry::RpcDoneCallback& done) override;

  void GetUser(const starrychat::GetUserRequestPtr& request,
               const starrychat::GetUserResponse* responsePrototype,
               const starry::RpcDoneCallback& done) override;

  void UpdateProfile(const starrychat::UpdateProfileRequestPtr& request,
                     const starrychat::UpdateProfileResponse* responsePrototype,
                     const starry::RpcDoneCallback& done) override;

  void GetFriends(const starrychat::GetFriendsRequestPtr& request,
                  const starrychat::GetFriendsResponse* responsePrototype,
                  const starry::RpcDoneCallback& done) override;

  void Logout(const starrychat::LogoutRequestPtr& request,
              const starrychat::LogoutResponse* responsePrototype,
              const starry::RpcDoneCallback& done) override;

  void UpdateStatus(const starrychat::UserStatusUpdatePtr& request,
                    const starrychat::UserInfo* responsePrototype,
                    const starry::RpcDoneCallback& done) override;

  // 心跳方法
  void UpdateHeartbeat(const starrychat::UserHeartbeatRequestPtr& request,
                       const starrychat::HeartbeatResponse* responsePrototype,
                       const starry::RpcDoneCallback& done) override;

 private:
  // 获取数据库连接
  std::shared_ptr<sql::Connection> getConnection();

  // 会话管理助手方法
  std::string generateSessionToken(uint64_t userId);
  bool validateSession(const std::string& token, uint64_t userId);
  void storeSession(const std::string& token, uint64_t userId);
  void removeSession(const std::string& token);

  // 用户状态管理
  void updateUserOnlineStatus(uint64_t userId, starrychat::UserStatus status);

  // Redis缓存相关方法
  void cacheUserInfo(const User& user);
  std::optional<User> getUserFromCache(uint64_t userId);
  void invalidateUserCache(uint64_t userId);
  void updateUserStatusInCache(uint64_t userId, starrychat::UserStatus status);
};

}  // namespace StarryChat
