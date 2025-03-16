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
      client_->disconnect();
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

    // Start event loop in a separate thread
    thread loopThread([this]() { loop_->loop(); });

    // Allow time for connection to establish
    this_thread::sleep_for(chrono::seconds(1));

    if (!connected_) {
      LOG_ERROR << "Failed to connect to server";
      loopThread.detach();  // Don't wait for thread
      return;
    }

    // Wait for loop thread to finish (when loop quits)
    loopThread.join();
  }

  // Stop the client
  void stop() {
    if (loggedIn_) {
      logout();
    }
    if (connected_) {
      client_->disconnect();
    }
    loop_->quit();
  }

  // Run specific tests
  void runTests() {
    if (!connected_) {
      LOG_ERROR << "Not connected to server. Cannot run tests.";
      return;
    }

    testUserRegisterAndLogin();
    testUserProfile();
    testChatRooms();
    testPrivateChat();
    testMessaging();

    // Stop after running tests
    stop();
  }

 private:
  // Test user registration and login
  void testUserRegisterAndLogin() {
    cout << "\n========== Testing User Registration and Login ==========\n"
         << endl;

    // Register new user
    string username = "testuser" + to_string(rand() % 10000);
    string password = "testpassword";
    string email = username + "@example.com";
    string nickname = "Test User";

    cout << "Registering user: " << username << endl;
    RegisterUserRequest registerRequest;
    registerRequest.set_username(username);
    registerRequest.set_password(password);
    registerRequest.set_email(email);
    registerRequest.set_nickname(nickname);

    userService_->RegisterUser(
        registerRequest, [this, username, password](
                             const shared_ptr<RegisterUserResponse>& response) {
          if (response->success()) {
            cout << "User registration successful!" << endl;
            printUserInfo(response->user_info());

            // Now try to login
            login(username, password);
          } else {
            cout << "User registration failed: " << response->error_message()
                 << endl;
          }
        });

    // Allow time for RPC to complete
    this_thread::sleep_for(chrono::seconds(2));
  }

  // Login with username and password
  void login(const string& username, const string& password) {
    cout << "\nLogging in with username: " << username << endl;

    LoginRequest loginRequest;
    loginRequest.set_username(username);
    loginRequest.set_password(password);

    userService_->Login(
        loginRequest, [this](const shared_ptr<LoginResponse>& response) {
          if (response->success()) {
            cout << "Login successful!" << endl;
            cout << "Session Token: " << response->session_token() << endl;
            printUserInfo(response->user_info());

            // Save user info and session token
            currentUser_ = response->user_info();
            sessionToken_ = response->session_token();
            loggedIn_ = true;
          } else {
            cout << "Login failed: " << response->error_message() << endl;
          }
        });

    // Allow time for RPC to complete
    this_thread::sleep_for(chrono::seconds(2));
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

    // Allow time for RPC to complete
    this_thread::sleep_for(chrono::seconds(1));
  }

  // Test user profile operations
  void testUserProfile() {
    if (!loggedIn_) {
      cout << "Must be logged in to test user profile. Skipping." << endl;
      return;
    }

    cout << "\n========== Testing User Profile Operations ==========\n" << endl;

    // Get user info
    GetUserRequest getUserRequest;
    getUserRequest.set_user_id(currentUser_.id());

    userService_->GetUser(
        getUserRequest, [](const shared_ptr<GetUserResponse>& response) {
          if (response->success()) {
            cout << "Got user info:" << endl;
            printUserInfo(response->user_info());
          } else {
            cout << "Failed to get user info: " << response->error_message()
                 << endl;
          }
        });

    // Allow time for RPC to complete
    this_thread::sleep_for(chrono::seconds(1));

    // Update user profile
    UpdateProfileRequest updateRequest;
    updateRequest.set_user_id(currentUser_.id());
    updateRequest.set_nickname("Updated Nickname");
    updateRequest.set_email(currentUser_.email());  // Keep same email

    userService_->UpdateProfile(
        updateRequest, [](const shared_ptr<UpdateProfileResponse>& response) {
          if (response->success()) {
            cout << "Profile updated successfully!" << endl;
            printUserInfo(response->user_info());
          } else {
            cout << "Failed to update profile: " << response->error_message()
                 << endl;
          }
        });

    // Allow time for RPC to complete
    this_thread::sleep_for(chrono::seconds(1));

    // Get user friends
    GetFriendsRequest friendsRequest;
    friendsRequest.set_user_id(currentUser_.id());

    userService_->GetFriends(
        friendsRequest, [](const shared_ptr<GetFriendsResponse>& response) {
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
        });

    // Allow time for RPC to complete
    this_thread::sleep_for(chrono::seconds(1));
  }

  // Test chat room operations
  void testChatRooms() {
    if (!loggedIn_) {
      cout << "Must be logged in to test chat rooms. Skipping." << endl;
      return;
    }

    cout << "\n========== Testing Chat Room Operations ==========\n" << endl;

    // Create a chat room
    CreateChatRoomRequest createRequest;
    createRequest.set_creator_id(currentUser_.id());
    createRequest.set_name("Test Chat Room");
    createRequest.set_description(
        "A test chat room created by the test client");

    uint64_t chatRoomId = 0;

    chatService_->CreateChatRoom(
        createRequest,
        [&chatRoomId](const shared_ptr<CreateChatRoomResponse>& response) {
          if (response->success()) {
            cout << "Chat room created successfully!" << endl;
            printChatRoom(response->chat_room());
            chatRoomId = response->chat_room().id();
          } else {
            cout << "Failed to create chat room: " << response->error_message()
                 << endl;
          }
        });

    // Allow time for RPC to complete
    this_thread::sleep_for(chrono::seconds(2));

    if (chatRoomId == 0) {
      cout << "Could not get chat room ID. Skipping chat room tests." << endl;
      return;
    }

    // Get chat room info
    GetChatRoomRequest getRoomRequest;
    getRoomRequest.set_chat_room_id(chatRoomId);
    getRoomRequest.set_user_id(currentUser_.id());

    chatService_->GetChatRoom(
        getRoomRequest, [](const shared_ptr<GetChatRoomResponse>& response) {
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
          } else {
            cout << "Failed to get chat room info: "
                 << response->error_message() << endl;
          }
        });

    // Allow time for RPC to complete
    this_thread::sleep_for(chrono::seconds(1));

    // Update chat room
    UpdateChatRoomRequest updateRoomRequest;
    updateRoomRequest.set_chat_room_id(chatRoomId);
    updateRoomRequest.set_user_id(currentUser_.id());
    updateRoomRequest.set_name("Updated Test Room");
    updateRoomRequest.set_description("An updated description for testing");

    chatService_->UpdateChatRoom(
        updateRoomRequest,
        [](const shared_ptr<UpdateChatRoomResponse>& response) {
          if (response->success()) {
            cout << "\nChat room updated successfully!" << endl;
            printChatRoom(response->chat_room());
          } else {
            cout << "Failed to update chat room: " << response->error_message()
                 << endl;
          }
        });

    // Allow time for RPC to complete
    this_thread::sleep_for(chrono::seconds(1));

    // Get user's chat list
    GetUserChatsRequest userChatsRequest;
    userChatsRequest.set_user_id(currentUser_.id());

    chatService_->GetUserChats(
        userChatsRequest, [](const shared_ptr<GetUserChatsResponse>& response) {
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
        });

    // Allow time for RPC to complete
    this_thread::sleep_for(chrono::seconds(1));

    // Dissolve the chat room when done testing
    DissolveChatRoomRequest dissolveRequest;
    dissolveRequest.set_chat_room_id(chatRoomId);
    dissolveRequest.set_user_id(currentUser_.id());

    chatService_->DissolveChatRoom(
        dissolveRequest,
        [](const shared_ptr<DissolveChatRoomResponse>& response) {
          if (response->success()) {
            cout << "\nChat room dissolved successfully!" << endl;
          } else {
            cout << "Failed to dissolve chat room: "
                 << response->error_message() << endl;
          }
        });

    // Allow time for RPC to complete
    this_thread::sleep_for(chrono::seconds(1));
  }

  // Test private chat
  void testPrivateChat() {
    if (!loggedIn_) {
      cout << "Must be logged in to test private chat. Skipping." << endl;
      return;
    }

    cout << "\n========== Testing Private Chat ==========\n" << endl;

    // First, find another user to chat with
    GetFriendsRequest friendsRequest;
    friendsRequest.set_user_id(currentUser_.id());

    uint64_t otherUserId = 0;

    userService_->GetFriends(
        friendsRequest,
        [&otherUserId](const shared_ptr<GetFriendsResponse>& response) {
          if (response->success() && response->friends_size() > 0) {
            const UserBrief& other = response->friends(0);
            otherUserId = other.id();
            cout << "Will create a private chat with user: " << other.nickname()
                 << " (ID: " << otherUserId << ")" << endl;
          } else {
            cout << "No other users found to test private chat." << endl;
          }
        });

    // Allow time for RPC to complete
    this_thread::sleep_for(chrono::seconds(1));

    if (otherUserId == 0) {
      cout << "No other user found for private chat. Skipping private chat "
              "tests."
           << endl;
      return;
    }

    // Create a private chat
    CreatePrivateChatRequest createPrivateRequest;
    createPrivateRequest.set_initiator_id(currentUser_.id());
    createPrivateRequest.set_receiver_id(otherUserId);

    uint64_t privateChatId = 0;

    chatService_->CreatePrivateChat(
        createPrivateRequest,
        [&privateChatId](
            const shared_ptr<CreatePrivateChatResponse>& response) {
          if (response->success()) {
            cout << "Private chat created successfully!" << endl;
            const PrivateChat& chat = response->private_chat();
            privateChatId = chat.id();

            cout << "Private Chat ID: " << chat.id() << endl;
            cout << "Between users: " << chat.user1_id() << " and "
                 << chat.user2_id() << endl;
            cout << "Created at: " << chat.created_time() << endl;
          } else {
            cout << "Failed to create private chat: "
                 << response->error_message() << endl;
          }
        });

    // Allow time for RPC to complete
    this_thread::sleep_for(chrono::seconds(2));

    if (privateChatId == 0) {
      cout << "Could not get private chat ID. Skipping private chat tests."
           << endl;
      return;
    }

    // Get private chat info
    GetPrivateChatRequest getPrivateRequest;
    getPrivateRequest.set_private_chat_id(privateChatId);
    getPrivateRequest.set_user_id(currentUser_.id());

    chatService_->GetPrivateChat(
        getPrivateRequest,
        [](const shared_ptr<GetPrivateChatResponse>& response) {
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
        });

    // Allow time for RPC to complete
    this_thread::sleep_for(chrono::seconds(1));
  }

  // Test messaging
  void testMessaging() {
    if (!loggedIn_) {
      cout << "Must be logged in to test messaging. Skipping." << endl;
      return;
    }

    cout << "\n========== Testing Messaging ==========\n" << endl;

    // Create a temporary chat room for messaging tests
    CreateChatRoomRequest createRequest;
    createRequest.set_creator_id(currentUser_.id());
    createRequest.set_name("Messaging Test Room");
    createRequest.set_description("A temporary room for testing messaging");

    uint64_t chatRoomId = 0;

    chatService_->CreateChatRoom(
        createRequest,
        [&chatRoomId](const shared_ptr<CreateChatRoomResponse>& response) {
          if (response->success()) {
            cout << "Created temporary chat room for messaging tests." << endl;
            chatRoomId = response->chat_room().id();
          } else {
            cout << "Failed to create chat room: " << response->error_message()
                 << endl;
          }
        });

    // Allow time for RPC to complete
    this_thread::sleep_for(chrono::seconds(2));

    if (chatRoomId == 0) {
      cout << "Could not create chat room for messaging tests. Skipping."
           << endl;
      return;
    }

    // Send a message
    SendMessageRequest sendRequest;
    sendRequest.set_sender_id(currentUser_.id());
    sendRequest.set_chat_type(CHAT_TYPE_GROUP);
    sendRequest.set_chat_id(chatRoomId);
    sendRequest.set_type(MESSAGE_TYPE_TEXT);
    sendRequest.mutable_text()->set_text(
        "Hello! This is a test message from the StarryChat test client.");

    uint64_t messageId = 0;

    messageService_->SendMessage(
        sendRequest,
        [&messageId](const shared_ptr<SendMessageResponse>& response) {
          if (response->success()) {
            cout << "Message sent successfully!" << endl;
            printMessage(response->message());
            messageId = response->message().id();
          } else {
            cout << "Failed to send message: " << response->error_message()
                 << endl;
          }
        });

    // Allow time for RPC to complete
    this_thread::sleep_for(chrono::seconds(2));

    // Get messages
    GetMessagesRequest getMessagesRequest;
    getMessagesRequest.set_user_id(currentUser_.id());
    getMessagesRequest.set_chat_type(CHAT_TYPE_GROUP);
    getMessagesRequest.set_chat_id(chatRoomId);
    getMessagesRequest.set_limit(10);

    messageService_->GetMessages(
        getMessagesRequest,
        [](const shared_ptr<GetMessagesResponse>& response) {
          if (response->success()) {
            cout << "\nGot " << response->messages_size()
                 << " messages:" << endl;
            for (int i = 0; i < response->messages_size(); i++) {
              cout << "\nMessage #" << (i + 1) << ":" << endl;
              printMessage(response->messages(i));
            }
          } else {
            cout << "Failed to get messages: " << response->error_message()
                 << endl;
          }
        });

    // Allow time for RPC to complete
    this_thread::sleep_for(chrono::seconds(2));

    if (messageId != 0) {
      // Update message status
      UpdateMessageStatusRequest updateStatusRequest;
      updateStatusRequest.set_user_id(currentUser_.id());
      updateStatusRequest.set_message_id(messageId);
      updateStatusRequest.set_status(MESSAGE_STATUS_READ);

      messageService_->UpdateMessageStatus(
          updateStatusRequest,
          [](const shared_ptr<UpdateMessageStatusResponse>& response) {
            if (response->success()) {
              cout << "\nMessage status updated successfully to READ." << endl;
            } else {
              cout << "Failed to update message status: "
                   << response->error_message() << endl;
            }
          });

      // Allow time for RPC to complete
      this_thread::sleep_for(chrono::seconds(1));

      // Recall message
      RecallMessageRequest recallRequest;
      recallRequest.set_user_id(currentUser_.id());
      recallRequest.set_message_id(messageId);

      messageService_->RecallMessage(
          recallRequest, [](const shared_ptr<RecallMessageResponse>& response) {
            if (response->success()) {
              cout << "\nMessage recalled successfully." << endl;
            } else {
              cout << "Failed to recall message: " << response->error_message()
                   << endl;
            }
          });

      // Allow time for RPC to complete
      this_thread::sleep_for(chrono::seconds(1));
    }

    // Clean up by dissolving the chat room
    DissolveChatRoomRequest dissolveRequest;
    dissolveRequest.set_chat_room_id(chatRoomId);
    dissolveRequest.set_user_id(currentUser_.id());

    chatService_->DissolveChatRoom(
        dissolveRequest,
        [](const shared_ptr<DissolveChatRoomResponse>& response) {
          if (response->success()) {
            cout << "\nTemporary chat room dissolved successfully." << endl;
          } else {
            cout << "Failed to dissolve chat room: "
                 << response->error_message() << endl;
          }
        });

    // Allow time for RPC to complete
    this_thread::sleep_for(chrono::seconds(1));
  }

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

  // Create a thread to run tests after connection is established
  thread testThread([&client]() {
    // Wait a bit to ensure connection is established
    this_thread::sleep_for(chrono::seconds(2));

    // Run the tests
    client.runTests();
    cout << "Tests completed." << endl;
  });

  // Start client (this will block on the event loop)
  client.start();

  // Wait for test thread to finish
  if (testThread.joinable()) {
    testThread.join();
  }

  cout << "StarryChat Test Client exiting." << endl;
  return 0;
}
