#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Include starry network library
#include "eventloop.h"
#include "inet_address.h"
#include "logging.h"
#include "rpc_channel.h"
#include "tcp_client.h"

// Include generated protocol files
#include "chat.pb.h"
#include "message.pb.h"
#include "user.pb.h"

using namespace std;
using namespace starry;
using namespace starrychat;

// Helper function to print user info
void printUserInfo(const UserInfo& user) {
  cout << "User ID: " << user.id() << endl;
  cout << "Username: " << user.username() << endl;
  cout << "Nickname: " << user.nickname() << endl;
  cout << "Email: " << user.email() << endl;
  cout << "Status: " << user.status() << endl;
}

// Helper function to print chat room info
void printChatRoom(const ChatRoom& room) {
  cout << "Chat Room ID: " << room.id() << endl;
  cout << "Name: " << room.name() << endl;
  cout << "Description: " << room.description() << endl;
  cout << "Creator ID: " << room.creator_id() << endl;
  cout << "Member Count: " << room.member_count() << endl;
}

// Helper function to print message
void printMessage(const Message& msg) {
  cout << "Message ID: " << msg.id() << endl;
  cout << "Sender ID: " << msg.sender_id() << endl;
  cout << "Chat ID: " << msg.chat_id() << endl;
  cout << "Type: " << msg.type() << endl;
  cout << "Timestamp: " << msg.timestamp() << endl;

  if (msg.type() == MESSAGE_TYPE_TEXT) {
    cout << "Content: " << msg.text().text() << endl;
  } else if (msg.type() == MESSAGE_TYPE_SYSTEM) {
    cout << "System Message: " << msg.system().text() << endl;
    cout << "Code: " << msg.system().code() << endl;
  }
}

class StarryChatClient {
 public:
  StarryChatClient(const string& serverIp, uint16_t serverPort)
      : serverAddr_(serverIp, serverPort), connected_(false), loggedIn_(false) {
    // Initialize logging
    Logger::setLogLevel(LogLevel::INFO);

    // Initialize event loop
    loop_ = new EventLoop();

    // Initialize TCP client
    client_ = new TcpClient(loop_, serverAddr_, "StarryChatClient");

    // Initialize RPC channel
    channel_ = make_shared<RpcChannel>();

    // Initialize service stubs
    userService_ = make_unique<UserService::Stub>(channel_.get());
    chatService_ = make_unique<ChatService::Stub>(channel_.get());
    messageService_ = make_unique<MessageService::Stub>(channel_.get());

    // Set up connection callback
    client_->setConnectionCallback([this](const TcpConnectionPtr& conn) {
      if (conn->connected()) {
        LOG_INFO << "Connected to server at " << serverAddr_.toIpPort();
        channel_->setConnection(conn);
        connected_ = true;
      } else {
        LOG_INFO << "Disconnected from server";
        channel_->setConnection(TcpConnectionPtr());
        connected_ = false;
        loggedIn_ = false;
      }
    });

    // Set up message callback to forward to RPC channel
    client_->setMessageCallback(
        std::bind(&RpcChannel::onMessage, channel_.get(), _1, _2, _3));
  }

  ~StarryChatClient() {
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

  // Start the client and connect to server
  void start() {
    LOG_INFO << "Starting client and connecting to " << serverAddr_.toIpPort();
    client_->connect();
  }

  // Stop the client
  void stop() {
    if (connected_) {
      // 必须在EventLoop线程中执行断开连接操作
      loop_->runInLoop([this]() {
        client_->disconnect();
        connected_ = false;

        // 延迟一段时间再退出，确保断开连接完成
        loop_->runAfter(500, [this]() { loop_->quit(); });
      });
    } else {
      loop_->quit();
    }
  }

  // Run the event loop
  void loop() { loop_->loop(); }

  // Check if connected
  bool isConnected() const { return connected_; }

  // Run tests in a separate thread
  void runTests() {
    // Register a new user
    testUserRegisterAndLogin();
  }

  // Test user registration and login
  // 1. 生成更随机的用户名
  string generateUniqueUsername() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 0xFFFF);

    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    std::stringstream ss;
    ss << "test_" << std::hex << dis(gen) << "_" << ms;
    return ss.str();
  }

  // 2. 实现"先注册，后登录"的回退策略
  void testUserRegisterAndLogin() {
    // 使用更随机的用户名
    string username = "test_admin_fixed";
    string password = "test123456";

    cout << "Registering user: " << username << endl;
    RegisterUserRequest registerRequest;
    registerRequest.set_username(username);
    registerRequest.set_password(password);
    registerRequest.set_email(username + "@example.com");
    registerRequest.set_nickname("Test User");

    userService_->RegisterUser(
        registerRequest, [this, username, password](
                             const shared_ptr<RegisterUserResponse>& response) {
          if (response->success()) {
            cout << "User registration successful!" << endl;
            printUserInfo(response->user_info());
            // 继续登录
            this->login(username, password);
          } else {
            cout << "User registration failed: " << response->error_message()
                 << endl;

            // 无论什么原因失败，尝试使用管理员账号登录
            cout << "Trying to login with admin account..." << endl;
            this->login("admin_fixed", "testpassword");
          }
        });
  }

  // Login with username and password
  void login(const string& username, const string& password) {
    cout << "\nLogging in with username: " << username << endl;

    LoginRequest loginRequest;
    loginRequest.set_username(username);
    loginRequest.set_password(password);

    userService_->Login(loginRequest, [this](const shared_ptr<LoginResponse>&
                                                 response) {
      if (response->success()) {
        cout << "Login successful!" << endl;
        cout << "Session Token: " << response->session_token() << endl;
        printUserInfo(response->user_info());

        // Save user info and session token
        currentUser_ = response->user_info();
        sessionToken_ = response->session_token();
        loggedIn_ = true;

        this->testUserProfile();  // Continue with next test
      } else {
        cout << "Login failed: " << response->error_message() << endl;
        this->testUserProfile();  // Continue with next test even if login fails
      }
    });
  }

  // Logout current user
  void logout() {
    if (!loggedIn_) {
      cout << "Not logged in." << endl;
      return;
    }

    cout << "\nLogging out user: " << currentUser_.username() << endl;

    LogoutRequest logoutRequest;
    logoutRequest.set_user_id(currentUser_.id());
    logoutRequest.set_session_token(sessionToken_);

    userService_->Logout(
        logoutRequest, [this](const shared_ptr<LogoutResponse>& response) {
          if (response->success()) {
            cout << "Logout successful!" << endl;
            loggedIn_ = false;
            sessionToken_ = "";
          } else {
            cout << "Logout failed: " << response->error_message() << endl;
          }
        });
  }

  // Test user profile operations
  void testUserProfile() {
    cout << "\n========== Testing User Profile Operations ==========\n" << endl;

    if (!loggedIn_) {
      cout << "Not logged in. Skipping user profile tests." << endl;
      testChatRooms();  // Continue with next test
      return;
    }

    // Get user info
    GetUserRequest getUserRequest;
    getUserRequest.set_user_id(currentUser_.id());

    userService_->GetUser(
        getUserRequest, [this](const shared_ptr<GetUserResponse>& response) {
          if (response->success()) {
            cout << "Got user info:" << endl;
            printUserInfo(response->user_info());

            // Continue with update profile test
            this->updateProfile();
          } else {
            cout << "Failed to get user info: " << response->error_message()
                 << endl;
            this->updateProfile();  // Continue with next part anyway
          }
        });
  }

  // Update profile test
  void updateProfile() {
    UpdateProfileRequest updateRequest;
    updateRequest.set_user_id(currentUser_.id());
    updateRequest.set_nickname("Updated Nickname");
    updateRequest.set_email(currentUser_.email());  // Keep same email

    userService_->UpdateProfile(
        updateRequest,
        [this](const shared_ptr<UpdateProfileResponse>& response) {
          if (response->success()) {
            cout << "Profile updated successfully!" << endl;
            printUserInfo(response->user_info());
          } else {
            cout << "Failed to update profile: " << response->error_message()
                 << endl;
          }

          // Get friends list
          this->getFriends();
        });
  }

  // Get friends list
  void getFriends() {
    GetFriendsRequest friendsRequest;
    friendsRequest.set_user_id(currentUser_.id());

    userService_->GetFriends(
        friendsRequest, [this](const shared_ptr<GetFriendsResponse>& response) {
          if (response->success()) {
            cout << "User has " << response->friends_size()
                 << " friends:" << endl;
            for (int i = 0; i < response->friends_size(); i++) {
              const UserBrief& friend_ = response->friends(i);
              cout << "  Friend #" << i + 1 << ": " << friend_.nickname()
                   << " (ID: " << friend_.id()
                   << ", Status: " << friend_.status() << ")" << endl;
            }
          } else {
            cout << "Failed to get friends: " << response->error_message()
                 << endl;
          }

          // Continue to next test
          this->testChatRooms();
        });
  }

  // Test chat room operations
  void testChatRooms() {
    cout << "\n========== Testing Chat Room Operations ==========\n" << endl;

    if (!loggedIn_) {
      cout << "Not logged in. Skipping chat room tests." << endl;
      testPrivateChat();  // Continue with next test
      return;
    }

    // Create a chat room
    CreateChatRoomRequest createRequest;
    createRequest.set_creator_id(currentUser_.id());
    createRequest.set_name("Test Chat Room");
    createRequest.set_description(
        "A test chat room created by the test client");

    chatService_->CreateChatRoom(
        createRequest,
        [this](const shared_ptr<CreateChatRoomResponse>& response) {
          if (response->success()) {
            cout << "Chat room created successfully!" << endl;
            printChatRoom(response->chat_room());

            // Save chat room ID for further tests
            chatRoomId_ = response->chat_room().id();

            // Continue with get chat room test
            this->getChatRoom();
          } else {
            cout << "Failed to create chat room: " << response->error_message()
                 << endl;
            this->testPrivateChat();  // Skip to next test
          }
        });
  }

  // Get chat room info
  void getChatRoom() {
    if (chatRoomId_ == 0) {
      cout << "No chat room ID available. Skipping get chat room test." << endl;
      testPrivateChat();
      return;
    }

    GetChatRoomRequest getRoomRequest;
    getRoomRequest.set_chat_room_id(chatRoomId_);
    getRoomRequest.set_user_id(currentUser_.id());

    chatService_->GetChatRoom(
        getRoomRequest,
        [this](const shared_ptr<GetChatRoomResponse>& response) {
          if (response->success()) {
            cout << "\nGot chat room info:" << endl;
            printChatRoom(response->chat_room());

            cout << "\nChat room has " << response->members_size()
                 << " members:" << endl;
            for (int i = 0; i < response->members_size(); i++) {
              const ChatRoomMember& member = response->members(i);
              cout << "  Member #" << i + 1 << ": User ID " << member.user_id()
                   << " (Role: " << member.role()
                   << ", Display Name: " << member.display_name() << ")"
                   << endl;
            }

            // Continue with update chat room test
            this->updateChatRoom();
          } else {
            cout << "Failed to get chat room info: "
                 << response->error_message() << endl;
            this->updateChatRoom();  // Continue anyway
          }
        });
  }

  // Update chat room
  void updateChatRoom() {
    if (chatRoomId_ == 0) {
      cout << "No chat room ID available. Skipping update chat room test."
           << endl;
      getUserChats();
      return;
    }

    UpdateChatRoomRequest updateRoomRequest;
    updateRoomRequest.set_chat_room_id(chatRoomId_);
    updateRoomRequest.set_user_id(currentUser_.id());
    updateRoomRequest.set_name("Updated Test Room");
    updateRoomRequest.set_description("An updated description for testing");

    chatService_->UpdateChatRoom(
        updateRoomRequest,
        [this](const shared_ptr<UpdateChatRoomResponse>& response) {
          if (response->success()) {
            cout << "\nChat room updated successfully!" << endl;
            printChatRoom(response->chat_room());
          } else {
            cout << "Failed to update chat room: " << response->error_message()
                 << endl;
          }

          // Continue with get user chats test
          this->getUserChats();
        });
  }

  // Get user chats
  void getUserChats() {
    GetUserChatsRequest userChatsRequest;
    userChatsRequest.set_user_id(currentUser_.id());

    chatService_->GetUserChats(
        userChatsRequest,
        [this](const shared_ptr<GetUserChatsResponse>& response) {
          if (response->success()) {
            cout << "\nUser has " << response->chats_size()
                 << " chats:" << endl;
            for (int i = 0; i < response->chats_size(); i++) {
              const ChatSummary& chat = response->chats(i);
              cout << "  Chat #" << i + 1 << ": " << chat.name()
                   << " (ID: " << chat.id() << ", Type: "
                   << (chat.type() == CHAT_TYPE_GROUP ? "Group" : "Private")
                   << ", Unread: " << chat.unread_count() << ")" << endl;

              if (!chat.last_message_preview().empty()) {
                cout << "    Last message: " << chat.last_message_preview()
                     << endl;
              }
            }
          } else {
            cout << "Failed to get user chats: " << response->error_message()
                 << endl;
          }

          // Dissolve the chat room if we created one
          this->dissolveChatRoom();
        });
  }

  // Dissolve chat room
  void dissolveChatRoom() {
    if (chatRoomId_ == 0) {
      cout << "No chat room ID available. Skipping dissolve chat room test."
           << endl;
      testPrivateChat();
      return;
    }

    DissolveChatRoomRequest dissolveRequest;
    dissolveRequest.set_chat_room_id(chatRoomId_);
    dissolveRequest.set_user_id(currentUser_.id());

    chatService_->DissolveChatRoom(
        dissolveRequest,
        [this](const shared_ptr<DissolveChatRoomResponse>& response) {
          if (response->success()) {
            cout << "\nChat room dissolved successfully!" << endl;
          } else {
            cout << "Failed to dissolve chat room: "
                 << response->error_message() << endl;
          }

          // Reset chat room ID
          chatRoomId_ = 0;

          // Continue to next test
          this->testPrivateChat();
        });
  }

  // Test private chat
  void testPrivateChat() {
    cout << "\n========== Testing Private Chat ==========\n" << endl;

    if (!loggedIn_) {
      cout << "Not logged in. Skipping private chat tests." << endl;
      testMessaging();  // Continue to next test
      return;
    }

    // First, find another user to chat with
    GetFriendsRequest friendsRequest;
    friendsRequest.set_user_id(currentUser_.id());

    userService_->GetFriends(
        friendsRequest, [this](const shared_ptr<GetFriendsResponse>& response) {
          if (response->success() && response->friends_size() > 0) {
            const UserBrief& other = response->friends(0);
            otherUserId_ = other.id();
            cout << "Will create a private chat with user: " << other.nickname()
                 << " (ID: " << otherUserId_ << ")" << endl;

            // Continue with create private chat
            this->createPrivateChat();
          } else {
            cout << "No other users found to test private chat." << endl;
            // Continue to next test
            this->testMessaging();
          }
        });
  }

  // Create private chat
  void createPrivateChat() {
    if (otherUserId_ == 0) {
      cout << "No other user ID available. Skipping create private chat test."
           << endl;
      testMessaging();
      return;
    }

    CreatePrivateChatRequest createPrivateRequest;
    createPrivateRequest.set_initiator_id(currentUser_.id());
    createPrivateRequest.set_receiver_id(otherUserId_);

    chatService_->CreatePrivateChat(
        createPrivateRequest,
        [this](const shared_ptr<CreatePrivateChatResponse>& response) {
          if (response->success()) {
            cout << "Private chat created successfully!" << endl;
            const PrivateChat& chat = response->private_chat();
            privateChatId_ = chat.id();

            cout << "Private Chat ID: " << chat.id() << endl;
            cout << "Between users: " << chat.user1_id() << " and "
                 << chat.user2_id() << endl;
            cout << "Created at: " << chat.created_time() << endl;

            // Continue with get private chat test
            this->getPrivateChat();
          } else {
            cout << "Failed to create private chat: "
                 << response->error_message() << endl;
            // Continue to next test
            this->testMessaging();
          }
        });
  }

  // Get private chat
  void getPrivateChat() {
    if (privateChatId_ == 0) {
      cout << "No private chat ID available. Skipping get private chat test."
           << endl;
      testMessaging();
      return;
    }

    GetPrivateChatRequest getPrivateRequest;
    getPrivateRequest.set_private_chat_id(privateChatId_);
    getPrivateRequest.set_user_id(currentUser_.id());

    chatService_->GetPrivateChat(
        getPrivateRequest,
        [this](const shared_ptr<GetPrivateChatResponse>& response) {
          if (response->success()) {
            cout << "\nGot private chat info:" << endl;
            const PrivateChat& chat = response->private_chat();

            cout << "Private Chat ID: " << chat.id() << endl;
            cout << "Between users: " << chat.user1_id() << " and "
                 << chat.user2_id() << endl;
            cout << "Created at: " << chat.created_time() << endl;

            cout << "\nPartner info:" << endl;
            printUserInfo(response->partner_info());
          } else {
            cout << "Failed to get private chat info: "
                 << response->error_message() << endl;
          }

          // Continue to next test
          this->testMessaging();
        });
  }

  // Test messaging
  void testMessaging() {
    cout << "\n========== Testing Messaging ==========\n" << endl;

    if (!loggedIn_) {
      cout << "Not logged in. Skipping messaging tests." << endl;
      finishTests();
      return;
    }

    // Create a temporary chat room for messaging tests
    CreateChatRoomRequest createRequest;
    createRequest.set_creator_id(currentUser_.id());
    createRequest.set_name("Messaging Test Room");
    createRequest.set_description("A temporary room for testing messaging");

    chatService_->CreateChatRoom(
        createRequest,
        [this](const shared_ptr<CreateChatRoomResponse>& response) {
          if (response->success()) {
            cout << "Created temporary chat room for messaging tests." << endl;
            messagingChatRoomId_ = response->chat_room().id();

            // Continue with send message test
            this->sendMessage();
          } else {
            cout << "Failed to create chat room: " << response->error_message()
                 << endl;
            this->finishTests();
          }
        });
  }

  // Send message
  void sendMessage() {
    if (messagingChatRoomId_ == 0) {
      cout << "No messaging chat room ID available. Skipping send message test."
           << endl;
      finishTests();
      return;
    }

    SendMessageRequest sendRequest;
    sendRequest.set_sender_id(currentUser_.id());
    sendRequest.set_chat_type(CHAT_TYPE_GROUP);
    sendRequest.set_chat_id(messagingChatRoomId_);
    sendRequest.set_type(MESSAGE_TYPE_TEXT);
    sendRequest.mutable_text()->set_text(
        "Hello! This is a test message from the StarryChat test client.");

    messageService_->SendMessage(
        sendRequest, [this](const shared_ptr<SendMessageResponse>& response) {
          if (response->success()) {
            cout << "Message sent successfully!" << endl;
            printMessage(response->message());
            messageId_ = response->message().id();

            // Continue with get messages test
            this->getMessages();
          } else {
            cout << "Failed to send message: " << response->error_message()
                 << endl;
            // Continue with cleanup
            this->cleanupMessagingTest();
          }
        });
  }

  // Get messages
  void getMessages() {
    if (messagingChatRoomId_ == 0) {
      cout << "No messaging chat room ID available. Skipping get messages test."
           << endl;
      cleanupMessagingTest();
      return;
    }

    GetMessagesRequest getMessagesRequest;
    getMessagesRequest.set_user_id(currentUser_.id());
    getMessagesRequest.set_chat_type(CHAT_TYPE_GROUP);
    getMessagesRequest.set_chat_id(messagingChatRoomId_);
    getMessagesRequest.set_limit(10);

    messageService_->GetMessages(
        getMessagesRequest,
        [this](const shared_ptr<GetMessagesResponse>& response) {
          if (response->success()) {
            cout << "\nGot " << response->messages_size()
                 << " messages:" << endl;
            for (int i = 0; i < response->messages_size(); i++) {
              cout << "\nMessage #" << (i + 1) << ":" << endl;
              printMessage(response->messages(i));
            }

            // Continue with update message status test
            this->updateMessageStatus();
          } else {
            cout << "Failed to get messages: " << response->error_message()
                 << endl;
            // Continue with cleanup
            this->cleanupMessagingTest();
          }
        });
  }

  // Update message status
  void updateMessageStatus() {
    if (messageId_ == 0) {
      cout << "No message ID available. Skipping update message status test."
           << endl;
      cleanupMessagingTest();
      return;
    }

    UpdateMessageStatusRequest updateStatusRequest;
    updateStatusRequest.set_user_id(currentUser_.id());
    updateStatusRequest.set_message_id(messageId_);
    updateStatusRequest.set_status(MESSAGE_STATUS_READ);

    messageService_->UpdateMessageStatus(
        updateStatusRequest,
        [this](const shared_ptr<UpdateMessageStatusResponse>& response) {
          if (response->success()) {
            cout << "\nMessage status updated successfully to READ." << endl;

            // Continue with recall message test
            this->recallMessage();
          } else {
            cout << "Failed to update message status: "
                 << response->error_message() << endl;
            // Continue with cleanup
            this->cleanupMessagingTest();
          }
        });
  }

  // Recall message
  void recallMessage() {
    if (messageId_ == 0) {
      cout << "No message ID available. Skipping recall message test." << endl;
      cleanupMessagingTest();
      return;
    }

    RecallMessageRequest recallRequest;
    recallRequest.set_user_id(currentUser_.id());
    recallRequest.set_message_id(messageId_);

    messageService_->RecallMessage(
        recallRequest,
        [this](const shared_ptr<RecallMessageResponse>& response) {
          if (response->success()) {
            cout << "\nMessage recalled successfully." << endl;
          } else {
            cout << "Failed to recall message: " << response->error_message()
                 << endl;
          }

          // Continue with cleanup
          this->cleanupMessagingTest();
        });
  }

  // Clean up messaging test resources
  void cleanupMessagingTest() {
    if (messagingChatRoomId_ == 0) {
      // No chat room to clean up
      finishTests();
      return;
    }

    DissolveChatRoomRequest dissolveRequest;
    dissolveRequest.set_chat_room_id(messagingChatRoomId_);
    dissolveRequest.set_user_id(currentUser_.id());

    chatService_->DissolveChatRoom(
        dissolveRequest,
        [this](const shared_ptr<DissolveChatRoomResponse>& response) {
          if (response->success()) {
            cout << "\nTemporary chat room dissolved successfully." << endl;
          } else {
            cout << "Failed to dissolve chat room: "
                 << response->error_message() << endl;
          }

          // Reset messaging chat room ID
          messagingChatRoomId_ = 0;
          messageId_ = 0;

          // Finish tests
          this->finishTests();
        });
  }

  // Finish tests
  void finishTests() {
    cout << "\n========== All Tests Completed ==========\n" << endl;

    // Logout if logged in
    if (loggedIn_) {
      logout();
    }

    // Signal main thread that tests are complete
    testsCompleted_ = true;

    // Note: Disconnection and EventLoop quitting will be handled in main
  }

  // Check if tests are completed
  bool areTestsCompleted() const { return testsCompleted_; }

 private:
  EventLoop* loop_;
  TcpClient* client_;
  InetAddress serverAddr_;
  shared_ptr<RpcChannel> channel_;

  unique_ptr<UserService::Stub> userService_;
  unique_ptr<ChatService::Stub> chatService_;
  unique_ptr<MessageService::Stub> messageService_;

  bool connected_;
  bool loggedIn_;
  UserInfo currentUser_;
  string sessionToken_;

  // State variables for tests
  uint64_t chatRoomId_{0};
  uint64_t privateChatId_{0};
  uint64_t otherUserId_{0};
  uint64_t messagingChatRoomId_{0};
  uint64_t messageId_{0};

  // Test completion flag
  bool testsCompleted_{false};
};

int main(int argc, char* argv[]) {
  // Parse command line arguments
  string serverIp = "127.0.0.1";
  uint16_t serverPort = 8080;

  if (argc > 1) {
    serverIp = argv[1];
  }

  if (argc > 2) {
    serverPort = static_cast<uint16_t>(stoi(argv[2]));
  }

  // Seed random number generator for generating unique usernames
  srand(static_cast<unsigned int>(time(nullptr)));

  // Create and start the client
  cout << "Starting StarryChat Test Client" << endl;
  cout << "Connecting to server at " << serverIp << ":" << serverPort << endl;

  StarryChatClient client(serverIp, serverPort);

  // Connect to server
  client.start();

  // Create a thread to run tests after connection is established
  thread testThread([&client]() {
    // Wait for connection to establish
    while (!client.isConnected()) {
      this_thread::sleep_for(chrono::milliseconds(100));
    }

    // Wait a bit more to ensure everything is set up
    this_thread::sleep_for(chrono::seconds(1));

    // Run tests
    cout << "Starting tests..." << endl;
    client.runTests();

    // Wait for tests to complete
    while (!client.areTestsCompleted()) {
      this_thread::sleep_for(chrono::milliseconds(100));
    }

    // Allow time for cleanup operations to complete
    this_thread::sleep_for(chrono::seconds(1));

    // Stop client and event loop
    client.stop();
  });

  // Run the event loop in the main thread
  client.loop();

  // Wait for test thread to complete
  testThread.join();

  cout << "StarryChat Test Client exiting." << endl;
  return 0;
}
