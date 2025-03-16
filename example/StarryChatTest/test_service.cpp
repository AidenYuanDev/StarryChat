#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// 网络库头文件
#include "eventloop.h"
#include "inet_address.h"
#include "logging.h"
#include "rpc_channel.h"
#include "tcp_client.h"

// Protocol Buffers头文件
#include "chat.pb.h"
#include "message.pb.h"
#include "user.pb.h"

using namespace std;
using namespace starry;
using namespace starrychat;

class ComprehensiveTestClient {
 public:
  ComprehensiveTestClient(const string& serverIp, uint16_t serverPort)
      : serverAddr_(serverIp, serverPort), connected_(false) {
    // 初始化日志
    Logger::setLogLevel(LogLevel::INFO);

    // 创建事件循环
    loop_ = new EventLoop();

    // 创建TCP客户端
    client_ = new TcpClient(loop_, serverAddr_, "ComprehensiveTestClient");

    // 创建RPC通道
    channel_ = make_shared<RpcChannel>();

    // 初始化所有服务存根
    userService_ = make_unique<UserService::Stub>(channel_.get());
    chatService_ = make_unique<ChatService::Stub>(channel_.get());
    messageService_ = make_unique<MessageService::Stub>(channel_.get());

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

  ~ComprehensiveTestClient() {
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

  // 启动客户端并运行测试
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

      // 开始综合测试流程
      startTestSequence();
    });

    // 运行事件循环
    loop_->loop();

    // 等待测试线程完成
    testThread.join();
  }

  // 测试流程排序
  void startTestSequence() {
    LOG_INFO << "Starting comprehensive test sequence";

    // 生成唯一用户名以避免冲突
    string timestamp = to_string(time(nullptr));
    testUser1_ = "test_user1_" + timestamp;
    testUser2_ = "test_user2_" + timestamp;

    // 按顺序测试
    testUserRegistration(testUser1_, "password123", "Test User 1",
                         testUser1_ + "@example.com");
  }

  // ============ 用户服务测试 ============

  // 1. 测试用户注册
  void testUserRegistration(const string& username,
                            const string& password,
                            const string& nickname,
                            const string& email) {
    cout << "\n========== 测试用户服务 - 用户注册 ==========" << endl;
    cout << "注册用户：" << username << endl;

    // 创建注册请求
    RegisterUserRequest registerRequest;
    registerRequest.set_username(username);
    registerRequest.set_password(password);
    registerRequest.set_nickname(nickname);
    registerRequest.set_email(email);

    // 使用共享指针保存请求对象
    auto sharedRequest = make_shared<RegisterUserRequest>(registerRequest);

    // 发送用户注册请求
    LOG_INFO << "Sending RegisterUser RPC request for " << username;
    userService_->RegisterUser(
        *sharedRequest,
        [this, sharedRequest, username, password, nickname,
         email](const shared_ptr<RegisterUserResponse>& response) {
          if (response) {
            cout << "收到注册响应: " << username << endl;
            if (response->success()) {
              cout << "注册成功! 用户ID: " << response->user_info().id()
                   << endl;
              // 保存第一个用户ID
              user1Id_ = response->user_info().id();

              // 继续注册第二个用户（如果是第一个用户的话）
              if (username == testUser1_) {
                testUserRegistration(testUser2_, "password456", "Test User 2",
                                     testUser2_ + "@example.com");
              } else {
                // 如果是第二个用户，继续登录测试
                testUserLogin(testUser1_, "password123");
              }
            } else {
              cout << "注册失败: " << response->error_message() << endl;
              // 尝试直接登录（可能用户已存在）
              if (username == testUser1_) {
                testUserLogin(testUser1_, "password123");
              } else if (username == testUser2_) {
                cout << "第二个用户注册失败，继续使用第一个用户测试" << endl;
                testUserLogin(testUser1_, "password123");
              } else {
                finishTests("用户注册失败");
              }
            }
          } else {
            cout << "错误：收到空响应" << endl;
            finishTests("注册响应为空");
          }
        });
  }

  // 2. 测试用户登录
  void testUserLogin(const string& username, const string& password) {
    cout << "\n========== 测试用户服务 - 用户登录 ==========" << endl;
    cout << "登录用户：" << username << endl;

    // 创建登录请求
    LoginRequest loginRequest;
    loginRequest.set_username(username);
    loginRequest.set_password(password);

    // 使用共享指针
    auto sharedRequest = make_shared<LoginRequest>(loginRequest);

    LOG_INFO << "Sending Login RPC request for " << username;
    userService_->Login(
        *sharedRequest, [this, sharedRequest,
                         username](const shared_ptr<LoginResponse>& response) {
          if (response) {
            cout << "收到登录响应: " << username << endl;
            if (response->success()) {
              cout << "登录成功! 会话令牌: " << response->session_token()
                   << endl;
              cout << "用户信息: ID=" << response->user_info().id()
                   << ", 用户名=" << response->user_info().username()
                   << ", 昵称=" << response->user_info().nickname() << endl;

              // 保存用户信息和会话
              if (username == testUser1_) {
                currentUserId_ = response->user_info().id();
                sessionToken_ = response->session_token();

                // 登录第二个用户
                testUserLogin(testUser2_, "password456");
              } else if (username == testUser2_) {
                user2Id_ = response->user_info().id();
                user2SessionToken_ = response->session_token();

                // 两个用户都登录成功，继续测试聊天服务
                testCreateChatRoom();
              }
            } else {
              cout << "登录失败: " << response->error_message() << endl;
              finishTests("用户登录失败");
            }
          } else {
            cout << "错误：收到空登录响应" << endl;
            finishTests("登录响应为空");
          }
        });
  }

  // ============ 聊天服务测试 ============

  // 3. 测试创建聊天室
  void testCreateChatRoom() {
    cout << "\n========== 测试聊天服务 - 创建聊天室 ==========" << endl;

    // 创建请求
    CreateChatRoomRequest request;
    request.set_name("测试聊天室-" + to_string(time(nullptr)));
    request.set_creator_id(currentUserId_);
    request.set_description("这是一个测试聊天室");
    request.set_avatar_url("https://example.com/avatar.png");

    // 添加初始成员（第二个用户）
    request.add_initial_member_ids(user2Id_);

    // 使用共享指针
    auto sharedRequest = make_shared<CreateChatRoomRequest>(request);

    LOG_INFO << "Sending CreateChatRoom RPC request";
    chatService_->CreateChatRoom(
        *sharedRequest,
        [this,
         sharedRequest](const shared_ptr<CreateChatRoomResponse>& response) {
          if (response) {
            cout << "收到创建聊天室响应" << endl;
            if (response->success()) {
              chatRoomId_ = response->chat_room().id();
              cout << "创建聊天室成功! ID: " << chatRoomId_
                   << ", 名称: " << response->chat_room().name() << endl;

              // 接下来测试私聊创建
              testCreatePrivateChat();
            } else {
              cout << "创建聊天室失败: " << response->error_message() << endl;
              finishTests("创建聊天室失败");
            }
          } else {
            cout << "错误：收到空响应" << endl;
            finishTests("创建聊天室响应为空");
          }
        });
  }

  // 4. 测试创建私聊
  void testCreatePrivateChat() {
    cout << "\n========== 测试聊天服务 - 创建私聊 ==========" << endl;

    // 创建请求
    CreatePrivateChatRequest request;
    request.set_initiator_id(currentUserId_);
    request.set_receiver_id(user2Id_);

    // 使用共享指针
    auto sharedRequest = make_shared<CreatePrivateChatRequest>(request);

    LOG_INFO << "Sending CreatePrivateChat RPC request";
    chatService_->CreatePrivateChat(
        *sharedRequest,
        [this,
         sharedRequest](const shared_ptr<CreatePrivateChatResponse>& response) {
          if (response) {
            cout << "收到创建私聊响应" << endl;
            if (response->success()) {
              privateChatId_ = response->private_chat().id();
              cout << "创建私聊成功! ID: " << privateChatId_ << endl;

              // 继续测试获取聊天室信息
              testGetChatRoom();
            } else {
              cout << "创建私聊失败: " << response->error_message() << endl;
              // 继续测试获取聊天室（即使私聊创建失败）
              testGetChatRoom();
            }
          } else {
            cout << "错误：收到空响应" << endl;
            finishTests("创建私聊响应为空");
          }
        });
  }

  // 5. 测试获取聊天室信息
  void testGetChatRoom() {
    cout << "\n========== 测试聊天服务 - 获取聊天室信息 ==========" << endl;

    // 创建请求
    GetChatRoomRequest request;
    request.set_chat_room_id(chatRoomId_);
    request.set_user_id(currentUserId_);

    // 使用共享指针
    auto sharedRequest = make_shared<GetChatRoomRequest>(request);

    LOG_INFO << "Sending GetChatRoom RPC request";
    chatService_->GetChatRoom(
        *sharedRequest,
        [this, sharedRequest](const shared_ptr<GetChatRoomResponse>& response) {
          if (response) {
            cout << "收到获取聊天室响应" << endl;
            if (response->success()) {
              cout << "获取聊天室信息成功!" << endl;
              cout << "聊天室名称: " << response->chat_room().name() << endl;
              cout << "聊天室描述: " << response->chat_room().description()
                   << endl;
              cout << "聊天室成员数量: " << response->chat_room().member_count()
                   << endl;

              cout << "成员列表:" << endl;
              for (int i = 0; i < response->members_size(); i++) {
                const auto& member = response->members(i);
                cout << "  用户ID: " << member.user_id()
                     << ", 角色: " << member.role()
                     << ", 显示名: " << member.display_name() << endl;
              }

              // 继续测试发送消息
              testSendMessage();
            } else {
              cout << "获取聊天室信息失败: " << response->error_message()
                   << endl;
              // 继续测试发送消息
              testSendMessage();
            }
          } else {
            cout << "错误：收到空响应" << endl;
            finishTests("获取聊天室响应为空");
          }
        });
  }

  // ============ 消息服务测试 ============

  // 6. 测试发送消息
  void testSendMessage() {
    cout << "\n========== 测试消息服务 - 发送消息 ==========" << endl;

    // 分别测试群聊消息和私聊消息
    testSendGroupMessage();
  }

  // 发送群聊消息
  void testSendGroupMessage() {
    // 创建请求
    SendMessageRequest request;
    request.set_sender_id(currentUserId_);
    request.set_chat_type(CHAT_TYPE_GROUP);
    request.set_chat_id(chatRoomId_);
    request.set_type(MESSAGE_TYPE_TEXT);

    // 设置文本内容
    auto* text = request.mutable_text();
    text->set_text("这是一条发送到群聊的测试消息 - " +
                   to_string(time(nullptr)));

    // 使用共享指针
    auto sharedRequest = make_shared<SendMessageRequest>(request);

    LOG_INFO << "Sending group message via SendMessage RPC";
    messageService_->SendMessage(
        *sharedRequest,
        [this, sharedRequest](const shared_ptr<SendMessageResponse>& response) {
          if (response) {
            cout << "收到发送群聊消息响应" << endl;
            if (response->success()) {
              groupMessageId_ = response->message().id();
              cout << "发送群聊消息成功! 消息ID: " << groupMessageId_ << endl;

              // 继续测试发送私聊消息
              testSendPrivateMessage();
            } else {
              cout << "发送群聊消息失败: " << response->error_message() << endl;
              // 尝试测试私聊消息
              testSendPrivateMessage();
            }
          } else {
            cout << "错误：收到空响应" << endl;
            finishTests("发送群聊消息响应为空");
          }
        });
  }

  // 发送私聊消息
  void testSendPrivateMessage() {
    if (privateChatId_ == 0) {
      cout << "私聊ID无效，跳过私聊消息测试" << endl;
      testGetMessages();
      return;
    }

    // 创建请求
    SendMessageRequest request;
    request.set_sender_id(currentUserId_);
    request.set_chat_type(CHAT_TYPE_PRIVATE);
    request.set_chat_id(privateChatId_);
    request.set_type(MESSAGE_TYPE_TEXT);

    // 设置文本内容
    auto* text = request.mutable_text();
    text->set_text("这是一条发送到私聊的测试消息 - " +
                   to_string(time(nullptr)));

    // 使用共享指针
    auto sharedRequest = make_shared<SendMessageRequest>(request);

    LOG_INFO << "Sending private message via SendMessage RPC";
    messageService_->SendMessage(
        *sharedRequest,
        [this, sharedRequest](const shared_ptr<SendMessageResponse>& response) {
          if (response) {
            cout << "收到发送私聊消息响应" << endl;
            if (response->success()) {
              privateMessageId_ = response->message().id();
              cout << "发送私聊消息成功! 消息ID: " << privateMessageId_ << endl;

              // 继续测试获取消息
              testGetMessages();
            } else {
              cout << "发送私聊消息失败: " << response->error_message() << endl;
              // 继续测试获取消息
              testGetMessages();
            }
          } else {
            cout << "错误：收到空响应" << endl;
            finishTests("发送私聊消息响应为空");
          }
        });
  }

  // 7. 测试获取消息
  void testGetMessages() {
    cout << "\n========== 测试消息服务 - 获取消息 ==========" << endl;

    // 创建请求
    GetMessagesRequest request;
    request.set_user_id(currentUserId_);
    request.set_chat_type(CHAT_TYPE_GROUP);
    request.set_chat_id(chatRoomId_);
    request.set_limit(10);  // 限制返回10条消息

    // 使用共享指针
    auto sharedRequest = make_shared<GetMessagesRequest>(request);

    LOG_INFO << "Sending GetMessages RPC request for group chat";
    messageService_->GetMessages(
        *sharedRequest,
        [this, sharedRequest](const shared_ptr<GetMessagesResponse>& response) {
          if (response) {
            cout << "收到获取消息响应" << endl;
            if (response->success()) {
              cout << "获取消息成功! 消息数量: " << response->messages_size()
                   << endl;

              for (int i = 0; i < response->messages_size(); i++) {
                const auto& msg = response->messages(i);
                cout << "消息ID: " << msg.id()
                     << ", 发送者: " << msg.sender_id()
                     << ", 类型: " << msg.type();

                if (msg.type() == MESSAGE_TYPE_TEXT && msg.has_text()) {
                  cout << ", 内容: " << msg.text().text();
                }
                cout << endl;
              }

              // 如果有群聊消息ID，测试更新消息状态
              if (groupMessageId_ > 0) {
                testUpdateMessageStatus();
              } else {
                testGetUserChats();
              }
            } else {
              cout << "获取消息失败: " << response->error_message() << endl;
              // 跳过更新消息状态测试
              testGetUserChats();
            }
          } else {
            cout << "错误：收到空响应" << endl;
            finishTests("获取消息响应为空");
          }
        });
  }

  // 8. 测试更新消息状态
  void testUpdateMessageStatus() {
    cout << "\n========== 测试消息服务 - 更新消息状态 ==========" << endl;

    // 创建请求
    UpdateMessageStatusRequest request;
    request.set_user_id(currentUserId_);
    request.set_message_id(groupMessageId_);
    request.set_status(MESSAGE_STATUS_READ);  // 标记为已读

    // 使用共享指针
    auto sharedRequest = make_shared<UpdateMessageStatusRequest>(request);

    LOG_INFO << "Sending UpdateMessageStatus RPC request";
    messageService_->UpdateMessageStatus(
        *sharedRequest,
        [this, sharedRequest](
            const shared_ptr<UpdateMessageStatusResponse>& response) {
          if (response) {
            cout << "收到更新消息状态响应" << endl;
            if (response->success()) {
              cout << "更新消息状态成功! 消息ID: " << groupMessageId_
                   << " 已标记为已读" << endl;

              // 继续测试获取用户聊天列表
              testGetUserChats();
            } else {
              cout << "更新消息状态失败: " << response->error_message() << endl;
              // 继续测试
              testGetUserChats();
            }
          } else {
            cout << "错误：收到空响应" << endl;
            finishTests("更新消息状态响应为空");
          }
        });
  }

  // 9. 测试获取用户聊天列表
  void testGetUserChats() {
    cout << "\n========== 测试聊天服务 - 获取用户聊天列表 ==========" << endl;

    // 创建请求
    GetUserChatsRequest request;
    request.set_user_id(currentUserId_);

    // 使用共享指针
    auto sharedRequest = make_shared<GetUserChatsRequest>(request);

    LOG_INFO << "Sending GetUserChats RPC request";
    chatService_->GetUserChats(
        *sharedRequest, [this, sharedRequest](
                            const shared_ptr<GetUserChatsResponse>& response) {
          if (response) {
            cout << "收到获取用户聊天列表响应" << endl;
            if (response->success()) {
              cout << "获取用户聊天列表成功! 聊天数量: "
                   << response->chats_size() << endl;

              for (int i = 0; i < response->chats_size(); i++) {
                const auto& chat = response->chats(i);
                cout << "聊天ID: " << chat.id() << ", 类型: "
                     << (chat.type() == CHAT_TYPE_PRIVATE ? "私聊" : "群聊")
                     << ", 名称: " << chat.name()
                     << ", 未读消息数: " << chat.unread_count() << endl;

                if (!chat.last_message_preview().empty()) {
                  cout << "  最后一条消息: " << chat.last_message_preview()
                       << endl;
                }
              }

              // 完成所有测试
              finishTests("所有测试完成", true);
            } else {
              cout << "获取用户聊天列表失败: " << response->error_message()
                   << endl;
              finishTests("获取用户聊天列表失败");
            }
          } else {
            cout << "错误：收到空响应" << endl;
            finishTests("获取用户聊天列表响应为空");
          }
        });
  }

  // 完成所有测试
  void finishTests(const string& message, bool success = false) {
    if (success) {
      cout << "\n\n========== 综合测试完成 ==========" << endl;
      cout << message << endl;
    } else {
      cout << "\n\n========== 测试中断 ==========" << endl;
      cout << "原因: " << message << endl;
    }

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

  // 服务存根
  unique_ptr<UserService::Stub> userService_;
  unique_ptr<ChatService::Stub> chatService_;
  unique_ptr<MessageService::Stub> messageService_;

  atomic<bool> connected_;

  // 测试用户信息
  string testUser1_;
  string testUser2_;

  // 当前登录用户信息
  uint64_t currentUserId_ = 0;
  uint64_t user1Id_ = 0;
  uint64_t user2Id_ = 0;
  string sessionToken_;
  string user2SessionToken_;

  // 测试聊天信息
  uint64_t chatRoomId_ = 0;
  uint64_t privateChatId_ = 0;

  // 测试消息信息
  uint64_t groupMessageId_ = 0;
  uint64_t privateMessageId_ = 0;
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

  cout << "启动综合测试客户端" << endl;
  cout << "连接服务器: " << serverIp << ":" << serverPort << endl;

  // 创建并运行客户端
  ComprehensiveTestClient client(serverIp, serverPort);
  client.start();

  cout << "测试客户端已完成." << endl;
  return 0;
}
