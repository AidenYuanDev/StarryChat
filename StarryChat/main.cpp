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
