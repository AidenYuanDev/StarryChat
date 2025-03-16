#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

// 网络库头文件
#include "eventloop.h"
#include "inet_address.h"
#include "logging.h"
#include "rpc_channel.h"
#include "tcp_client.h"

// 用户服务相关的Protocol Buffers头文件
#include "user.pb.h"

using namespace std;
using namespace starry;
using namespace starrychat;

class SimpleUserClient {
 public:
  SimpleUserClient(const string& serverIp, uint16_t serverPort)
      : serverAddr_(serverIp, serverPort), connected_(false) {
    // 初始化日志
    Logger::setLogLevel(LogLevel::INFO);

    // 创建事件循环
    loop_ = new EventLoop();

    // 创建TCP客户端
    client_ = new TcpClient(loop_, serverAddr_, "SimpleUserClient");

    // 创建RPC通道
    channel_ = make_shared<RpcChannel>();

    // 初始化服务存根
    userService_ = make_unique<UserService::Stub>(channel_.get());

    // 设置连接回调
    client_->setConnectionCallback([this](const TcpConnectionPtr& conn) {
      LOG_INFO << "Connection callback triggered, connected: "
               << conn->connected();

      if (conn->connected()) {
        LOG_INFO << "Connected to server at " << serverAddr_.toIpPort();
        channel_->setConnection(conn);
        connected_ = true;
      } else {
        LOG_INFO << "Disconnected from server";
        channel_->setConnection(TcpConnectionPtr());
        connected_ = false;
      }
    });

    // 设置消息回调
    client_->setMessageCallback(
        bind(&RpcChannel::onMessage, channel_.get(), _1, _2, _3));
  }

  ~SimpleUserClient() {
    if (client_) {
      if (connected_) {
        client_->disconnect();
      }
      delete client_;
    }
    if (loop_) {
      delete loop_;
    }
  }

  // 简化连接逻辑，不使用条件变量
  void start() {
    LOG_INFO << "Starting client and connecting to " << serverAddr_.toIpPort();
    client_->connect();

    // 启动测试线程
    thread testThread([this]() {
      LOG_INFO << "Test thread started, waiting for connection...";

      // 轮询等待连接建立，有超时机制
      int retryCount = 0;
      while (!connected_ && retryCount < 30) {
        this_thread::sleep_for(chrono::milliseconds(100));
        retryCount++;
      }

      if (!connected_) {
        LOG_ERROR << "Failed to connect to server after 3 seconds";
        loop_->quit();
        return;
      }

      LOG_INFO << "Connection established, proceeding with tests";

      // 等待一段时间确保连接稳定
      this_thread::sleep_for(chrono::seconds(1));

      // 运行测试
      testUserService();
    });

    // 运行事件循环
    loop_->loop();

    // 等待测试线程完成
    testThread.join();
  }

  void testUserService() {
    cout << "========== 开始测试用户服务 ==========" << endl;

    // 生成唯一用户名
    string username = "test_user_" + to_string(time(nullptr));
    string password = "test_password_123";

    // 注册开始
    cout << "注册用户：" << username << endl;

    // 创建注册请求
    RegisterUserRequest registerRequest;
    registerRequest.set_username(username);
    registerRequest.set_password(password);
    registerRequest.set_email(username + "@example.com");
    registerRequest.set_nickname("测试用户");

    // 打印请求内容进行调试
    cout << "请求内容：" << endl;
    cout << "- Username: " << registerRequest.username() << endl;
    cout << "- Password: [hidden]" << endl;
    cout << "- Email: " << registerRequest.email() << endl;
    cout << "- Nickname: " << registerRequest.nickname() << endl;

    // 使用共享指针保存请求对象
    auto sharedRequest = make_shared<RegisterUserRequest>(registerRequest);

    // 用户注册，捕获sharedRequest延长生命周期
    LOG_INFO << "Sending RegisterUser RPC request";
    userService_->RegisterUser(
        *sharedRequest, [this, sharedRequest, username, password](
                            const shared_ptr<RegisterUserResponse>& response) {
          LOG_INFO << "Received RegisterUser response";

          if (response) {
            cout << "收到注册响应" << endl;
            if (response->success()) {
              cout << "注册成功! 用户ID: " << response->user_info().id()
                   << endl;
              testLogin(username, password);
            } else {
              cout << "注册失败: " << response->error_message() << endl;
              finishTests();
            }
          } else {
            cout << "错误：收到空响应" << endl;
            finishTests();
          }
        });
  }

  void testLogin(const string& username, const string& password) {
    cout << "\n测试登录：" << username << endl;

    // 创建登录请求
    LoginRequest loginRequest;
    loginRequest.set_username(username);
    loginRequest.set_password(password);

    // 使用共享指针
    auto sharedRequest = make_shared<LoginRequest>(loginRequest);

    LOG_INFO << "Sending Login RPC request";
    userService_->Login(
        *sharedRequest, [this, sharedRequest,
                         username](const shared_ptr<LoginResponse>& response) {
          LOG_INFO << "Received Login response";

          if (response) {
            cout << "收到登录响应" << endl;
            if (response->success()) {
              cout << "登录成功! Session: " << response->session_token()
                   << endl;
              cout << "用户信息: ID=" << response->user_info().id()
                   << ", 用户名=" << response->user_info().username()
                   << ", 昵称=" << response->user_info().nickname() << endl;

              // 保存用户信息
              currentUserId_ = response->user_info().id();
              sessionToken_ = response->session_token();

              // 测试获取用户信息
              testGetUser(currentUserId_);
            } else {
              cout << "登录失败: " << response->error_message() << endl;
              finishTests();
            }
          } else {
            cout << "错误：收到空登录响应" << endl;
            finishTests();
          }
        });
  }

  void testGetUser(uint64_t userId) {
    cout << "\n测试获取用户信息，用户ID: " << userId << endl;

    // 创建获取用户请求
    GetUserRequest getUserRequest;
    getUserRequest.set_user_id(userId);

    // 使用共享指针
    auto sharedRequest = make_shared<GetUserRequest>(getUserRequest);

    LOG_INFO << "Sending GetUser RPC request";
    userService_->GetUser(
        *sharedRequest,
        [this, sharedRequest](const shared_ptr<GetUserResponse>& response) {
          LOG_INFO << "Received GetUser response";

          if (response) {
            cout << "收到获取用户响应" << endl;
            if (response->success()) {
              cout << "获取用户信息成功!" << endl;
              cout << "用户名: " << response->user_info().username() << endl;
              cout << "邮箱: " << response->user_info().email() << endl;
              cout << "昵称: " << response->user_info().nickname() << endl;
            } else {
              cout << "获取用户信息失败: " << response->error_message() << endl;
            }
          } else {
            cout << "错误：收到空的获取用户响应" << endl;
          }

          // 测试完成
          cout << "========== 用户服务测试完成 ==========" << endl;
          finishTests();
        });
  }

  void finishTests() {
    LOG_INFO << "Testing completed, disconnecting...";

    // 在事件循环线程中安全地断开连接
    loop_->runInLoop([this]() {
      client_->disconnect();

      // 延迟退出，确保断开连接操作完成
      loop_->runAfter(500, [this]() { loop_->quit(); });
    });
  }

 private:
  EventLoop* loop_;
  TcpClient* client_;
  InetAddress serverAddr_;
  shared_ptr<RpcChannel> channel_;

  unique_ptr<UserService::Stub> userService_;

  atomic<bool> connected_;  // 使用原子变量，避免条件变量复杂性

  // 当前登录用户信息
  uint64_t currentUserId_ = 0;
  string sessionToken_;
};

int main(int argc, char* argv[]) {
  // 处理命令行参数
  string serverIp = "127.0.0.1";
  uint16_t serverPort = 8080;

  if (argc > 1) {
    serverIp = argv[1];
  }

  if (argc > 2) {
    serverPort = static_cast<uint16_t>(stoi(argv[2]));
  }

  cout << "启动简单测试客户端" << endl;
  cout << "连接服务器: " << serverIp << ":" << serverPort << endl;

  // 创建并运行客户端
  SimpleUserClient client(serverIp, serverPort);
  client.start();

  cout << "测试客户端已完成." << endl;
  return 0;
}
