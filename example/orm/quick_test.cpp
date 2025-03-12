#include <iostream>
#include <memory>
#include "models/user.h"
#include "test_utils.h"

using namespace StarryChat::orm;
using namespace StarryChat::Test;

// 测试数据库连接
void testConnection() {
  auto conn = getTestConnection();
  auto result = conn->executeQuery("SELECT 1 AS test");
  assertTrue(result->next(), "Should have one row");
  assertEquals(result->get<int>("test"), 1, "Should return 1");
}

// 测试查询构建器 - SELECT
void testQueryBuilderSelect() {
  auto conn = getTestConnection();
  auto query = QueryBuilder::create()
                   ->table("users")
                   ->select("id", "username", "email")
                   ->where("status", 1)
                   ->orderBy("id")
                   ->limit(2);

  auto result = query->get(conn);
  assertTrue(result->next(), "Should have at least one result");
  assertFalse(std::holds_alternative<std::nullptr_t>(result->getValue("id")),
              "ID should not be null");
  assertFalse(
      std::holds_alternative<std::nullptr_t>(result->getValue("username")),
      "Username should not be null");

  // 检查第一条记录是id=1的记录
  assertEquals(result->get<int>("id"), 1, "First record should have id=1");
  assertEquals(result->get<std::string>("username"), "test_user",
               "First record should be test_user");
}

// 测试查询构建器 - INSERT
void testQueryBuilderInsert() {
  auto conn = getTestConnection();

  // 插入新用户
  auto query = QueryBuilder::create()->table("users")->insert(
      {{"username", "new_user"},
       {"email", "new@example.com"},
       {"status", 1},
       {"login_count", 0}});

  bool success = query->execute(conn);
  assertTrue(success, "Insert should succeed");

  // 验证插入
  uint64_t lastId = conn->getLastInsertId();
  assertTrue(lastId > 0, "Should have valid last insert ID");

  auto verifyQuery = QueryBuilder::create()->table("users")->where(
      "id", static_cast<int64_t>(lastId));

  assertTrue(verifyQuery->exists(conn), "Inserted record should exist");
}

// 测试查询构建器 - UPDATE
void testQueryBuilderUpdate() {
  auto conn = getTestConnection();

  // 更新用户
  auto query =
      QueryBuilder::create()
          ->table("users")
          ->where("username", "test_user")
          ->update({{"login_count", 100}, {"email", "updated@example.com"}});

  bool success = query->execute(conn);
  assertTrue(success, "Update should succeed");

  // 验证更新
  auto verifyQuery =
      QueryBuilder::create()->table("users")->where("username", "test_user");

  auto result = verifyQuery->get(conn);
  assertTrue(result->next(), "Should find updated record");
  assertEquals(result->get<int>("login_count"), 100,
               "Login count should be updated");
  assertEquals(result->get<std::string>("email"), "updated@example.com",
               "Email should be updated");
}

// 测试查询构建器 - DELETE
void testQueryBuilderDelete() {
  auto conn = getTestConnection();

  // 先插入一条要删除的记录
  auto insertQuery = QueryBuilder::create()->table("users")->insert(
      {{"username", "to_delete"},
       {"email", "delete@example.com"},
       {"status", 1}});

  insertQuery->execute(conn);
  uint64_t idToDelete = conn->getLastInsertId();

  // 删除记录
  auto query = QueryBuilder::create()
                   ->table("users")
                   ->where("id", static_cast<int64_t>(idToDelete))
                   ->del();

  bool success = query->execute(conn);
  assertTrue(success, "Delete should succeed");

  // 验证删除
  auto verifyQuery = QueryBuilder::create()->table("users")->where(
      "id", static_cast<int64_t>(idToDelete));

  assertFalse(verifyQuery->exists(conn), "Deleted record should not exist");
}

// 测试模型 - 创建并保存
void testModelCreate() {
  // 修正：直接创建连接池并设置到Model
  auto config = std::make_shared<PoolConfig>();
  config->setHost(DbConfig::HOST)
      .setPort(DbConfig::PORT)
      .setDatabase(DbConfig::DATABASE)
      .setUsername(DbConfig::USERNAME)
      .setPassword(DbConfig::PASSWORD)
      .setCharset(DbConfig::CHARSET)
      .setMinPoolSize(DbConfig::MIN_CONNECTIONS)
      .setMaxPoolSize(DbConfig::MAX_CONNECTIONS);

  auto pool = std::make_shared<ConnectionPool>(config);
  pool->initialize();

  // 设置模型的连接池
  Model::setConnectionPool(pool);

  // 创建新用户
  auto user = std::make_shared<User>();
  user->setUsername("model_user");
  user->setEmail("model@example.com");
  user->setStatus(1);
  user->setLoginCount(0);

  // 保存用户
  bool success = user->save();
  assertTrue(success, "Model save should succeed");

  // 验证ID已分配
  int64_t userId = user->getId();
  assertTrue(userId > 0, "User should have valid ID after save");

  // 验证创建时间已设置
  auto createdAt = user->getCreatedAt();
  assertTrue(createdAt != TimePoint{}, "Created time should be set");
}

// 测试模型 - 查找和读取
void testModelFind() {
  // 查找用户
  auto user = User::find(1);
  assertNotNull(user.get(), "Should find user with ID 1");

  // 验证字段值
  assertEquals(user->getUsername(), "test_user", "Username should match");
  assertTrue(user->isActive(), "User should be active");

  // 测试不存在的用户
  auto nonExistent = User::find(9999);
  assertTrue(nonExistent == nullptr, "Non-existent user should return nullptr");
}

// 测试模型 - 更新
void testModelUpdate() {
  // 查找用户
  auto user = User::find(1);
  assertNotNull(user.get(), "Should find user with ID 1");

  // 修改用户
  user->setEmail("new_email@example.com");
  user->recordLogin();  // 测试业务方法

  // 保存更新
  bool success = user->save();
  assertTrue(success, "Model update should succeed");

  // 重新加载验证
  user = User::find(1);
  assertEquals(user->getEmail(), "new_email@example.com",
               "Email should be updated");
  assertTrue(user->getLoginCount() > 0, "Login count should be increased");
  assertTrue(user->getLastLoginAt().has_value(),
             "Last login time should be set");
}

// 测试模型 - 删除
void testModelDelete() {
  // 先创建一个用于删除的用户
  auto user = std::make_shared<User>();
  user->setUsername("delete_me");
  user->setEmail("delete_me@example.com");
  user->setStatus(1);
  user->save();

  int64_t userId = user->getId();

  // 执行删除
  bool success = user->remove();
  assertTrue(success, "Model delete should succeed");

  // 验证删除
  auto deletedUser = User::find(userId);
  assertTrue(deletedUser == nullptr, "Deleted user should not be found");
}

// 测试事务
void testTransaction() {
  auto conn = getTestConnection();

  // 开始事务
  Transaction transaction(conn);

  // 在事务中执行更新
  auto user = User::find(2, conn);
  user->setUsername("transaction_test");
  user->save(conn);

  // 提交事务
  transaction.commit();

  // 验证更新已提交
  user = User::find(2);
  assertEquals(user->getUsername(), "transaction_test",
               "Username should be updated after commit");

  // 测试事务回滚
  {
    Transaction rollbackTx(conn);

    user->setUsername("rollback_test");
    user->save(conn);

    // 不提交，让事务回滚
  }

  // 验证回滚生效
  user = User::find(2);
  assertEquals(user->getUsername(), "transaction_test",
               "Username should not change after rollback");
}

// 性能测试
void testPerformance() {
  // 测试大量插入的性能
  double timeMs = measureExecutionTime([]() {
    auto conn = getTestConnection();

    // 开始事务以提高性能
    Transaction transaction(conn);

    for (int i = 0; i < 100; i++) {
      auto query = QueryBuilder::create()->table("users")->insert(
          {{"username", "perf_user_" + std::to_string(i)},
           {"email", "perf" + std::to_string(i) + "@example.com"},
           {"status", 1}});

      query->execute(conn);
    }

    transaction.commit();
  });

  std::cout << ConsoleColor::YELLOW << "Inserted 100 records in " << timeMs
            << " ms (" << (timeMs / 100) << " ms per record)"
            << ConsoleColor::RESET << std::endl;
}

int main() {
  try {
    std::cout << ConsoleColor::BLUE << "Starting ORM quick tests...\n"
              << ConsoleColor::RESET;

    initTestEnvironment();

    // 基本连接测试
    runTest("Connection Test", testConnection);

    // 查询构建器测试
    runTest("QueryBuilder SELECT Test", testQueryBuilderSelect);
    runTest("QueryBuilder INSERT Test", testQueryBuilderInsert);
    runTest("QueryBuilder UPDATE Test", testQueryBuilderUpdate);
    runTest("QueryBuilder DELETE Test", testQueryBuilderDelete);

    // 模型测试
    runTest("Model Create Test", testModelCreate);
    runTest("Model Find Test", testModelFind);
    runTest("Model Update Test", testModelUpdate);
    runTest("Model Delete Test", testModelDelete);

    // 事务测试
    runTest("Transaction Test", testTransaction);

    // 性能测试
    runTest("Performance Test", testPerformance);

    // 打印摘要
    printTestSummary();

    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ConsoleColor::RED << "Test execution failed: " << ex.what()
              << ConsoleColor::RESET << std::endl;
    return 1;
  }
}
