#include <signal.h>
#include "chat_service_impl.h"
#include "config.h"
#include "db_manager.h"
#include "eventloop.h"
#include "inet_address.h"
#include "logging.h"
#include "message_service_impl.h"
#include "redis_manager.h"
#include "rpc_server.h"
#include "user_service_impl.h"

// 用户心跳检测线程
void startHeartbeatCheckerThread() {
  std::thread([] {
    LOG_INFO << "Starting user heartbeat checker thread";
    auto& redis = StarryChat::RedisManager::getInstance();

    while (true) {
      try {
        // 获取所有在线用户
        auto onlineUsers = redis.smembers("users:online");
        if (onlineUsers && !onlineUsers->empty()) {
          LOG_INFO << "Checking heartbeats for " << onlineUsers->size()
                   << " online users";

          for (const auto& userId : *onlineUsers) {
            // 检查心跳是否存在
            std::string heartbeatKey = "user:heartbeat:" + userId;
            if (!redis.exists(heartbeatKey)) {
              // 心跳过期，用户可能断线
              LOG_INFO << "User " << userId
                       << " heartbeat expired, marking as offline";

              // 更新状态为离线
              redis.hset("user:status", userId,
                         std::to_string(static_cast<int>(
                             starrychat::USER_STATUS_OFFLINE)));

              // 从在线用户集合中移除
              redis.srem("users:online", userId);

              // 发布状态变更通知
              std::string notification = userId + ":" +
                                         std::to_string(static_cast<int>(
                                             starrychat::USER_STATUS_OFFLINE));
              redis.publish("user:status:changed", notification);

              // 更新用户信息缓存
              std::string userKey = "user:" + userId;
              redis.hset(userKey, "status",
                         std::to_string(static_cast<int>(
                             starrychat::USER_STATUS_OFFLINE)));

              // 异步更新数据库
              auto& dbManager = StarryChat::DBManager::getInstance();
              if (auto conn = dbManager.getConnection()) {
                try {
                  std::unique_ptr<sql::PreparedStatement> stmt(
                      conn->prepareStatement(
                          "UPDATE users SET status = ? WHERE id = ?"));
                  stmt->setInt(
                      1, static_cast<int>(starrychat::USER_STATUS_OFFLINE));
                  stmt->setUInt64(2, std::stoull(userId));
                  stmt->executeUpdate();
                  LOG_INFO << "Updated database status to offline for user "
                           << userId;
                } catch (sql::SQLException& e) {
                  LOG_ERROR << "SQL error in heartbeat checker: " << e.what();
                }
              }
            }
          }
        }
      } catch (const std::exception& e) {
        LOG_ERROR << "Error in heartbeat checker: " << e.what();
      }

      // 每分钟检查一次
      std::this_thread::sleep_for(std::chrono::minutes(1));
    }
  }).detach();

  LOG_INFO << "Heartbeat checker thread started";
}

// 全局事件循环指针，用于信号处理
starry::EventLoop* g_loop = nullptr;

// 信号处理函数
void signalHandler(int sig) {
  LOG_INFO << "Received signal " << sig;
  if (g_loop) {
    g_loop->quit();
  }
}

int main() {
  // 在启动服务器后，启动心跳检测线程
  startHeartbeatCheckerThread();
  // 设置日志级别
  starry::Logger::setLogLevel(starry::LogLevel::INFO);
  LOG_INFO << "Starting StarryChat server...";

  // 加载配置
  auto& config = StarryChat::Config::getInstance();
  if (!config.loadConfig("config.yaml")) {
    LOG_ERROR << "Failed to load config file";
    return 1;
  }
  LOG_INFO << "Config loaded, server will listen on port "
           << config.getServerPort();

  // 初始化日志级别
  starry::Logger::setLogLevel(config.getLoggingLevel());

  // 初始化数据库连接
  auto& dbManager = StarryChat::DBManager::getInstance();
  if (!dbManager.initialize()) {
    LOG_ERROR << "Failed to initialize database connection";
    return 1;
  }
  LOG_INFO << "Database connection initialized";

  // 初始化Redis连接
  auto& redisManager = StarryChat::RedisManager::getInstance();
  if (!redisManager.initialize()) {
    LOG_ERROR << "Failed to initialize Redis connection";
    return 1;
  }
  LOG_INFO << "Redis connection initialized";

  // 创建事件循环
  starry::EventLoop loop;
  g_loop = &loop;

  // 设置信号处理
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  // 创建服务地址
  starry::InetAddress listenAddr(config.getServerPort());

  // 创建RPC服务器
  starry::RpcServer rpcServer(&loop, listenAddr);

  // 设置线程数
  rpcServer.setThreadNum(config.getServerThreads());

  // 创建并注册服务实现
  StarryChat::UserServiceImpl userService;
  StarryChat::ChatServiceImpl chatService;
  StarryChat::MessageServiceImpl messageService;

  // 注册服务
  rpcServer.registerService(&userService);
  rpcServer.registerService(&chatService);
  rpcServer.registerService(&messageService);

  // 启动服务器
  rpcServer.start();
  LOG_INFO << "StarryChat server started on port " << config.getServerPort();

  // 运行事件循环
  loop.loop();

  // 清理资源
  LOG_INFO << "Shutting down StarryChat server...";
  dbManager.shutdown();
  redisManager.shutdown();

  LOG_INFO << "StarryChat server stopped";
  return 0;
}
