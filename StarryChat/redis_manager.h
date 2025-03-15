#pragma once

#include <sw/redis++/redis++.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace StarryChat {

/**
 * Redis 管理类 - 负责管理 Redis 连接和提供基本操作
 */
class RedisManager {
 public:
  static RedisManager& getInstance();

  RedisManager(const RedisManager&) = delete;
  RedisManager& operator=(const RedisManager&) = delete;
  RedisManager(RedisManager&&) = delete;
  RedisManager& operator=(RedisManager&&) = delete;

  bool initialize();
  void shutdown();

  // 字符串操作
  bool set(const std::string& key,
           const std::string& value,
           std::chrono::seconds ttl = std::chrono::seconds(0));
  std::optional<std::string> get(const std::string& key);
  bool del(const std::string& key);

  // 哈希表操作
  bool hset(const std::string& key,
            const std::string& field,
            const std::string& value);
  std::optional<std::string> hget(const std::string& key,
                                  const std::string& field);
  bool hdel(const std::string& key, const std::string& field);
  std::optional<std::unordered_map<std::string, std::string>> hgetall(
      const std::string& key);

  // 列表操作
  bool lpush(const std::string& key, const std::string& value);
  bool rpush(const std::string& key, const std::string& value);
  std::optional<std::string> lpop(const std::string& key);
  std::optional<std::string> rpop(const std::string& key);
  std::optional<std::vector<std::string>> lrange(const std::string& key,
                                                 long start,
                                                 long stop);

  // 集合操作
  bool sadd(const std::string& key, const std::string& member);
  bool srem(const std::string& key, const std::string& member);
  std::optional<std::unordered_set<std::string>> smembers(
      const std::string& key);

  // 有序集合操作
  bool zadd(const std::string& key, const std::string& member, double score);
  bool zrem(const std::string& key, const std::string& member);
  std::optional<std::vector<std::string>> zrange(const std::string& key,
                                                 long start,
                                                 long stop);
  std::optional<std::vector<std::pair<std::string, double>>>
  zrangeWithScores(const std::string& key, long start, long stop);

  // 发布/订阅
  bool publish(const std::string& channel, const std::string& message);

  // 其他操作
  bool expire(const std::string& key, std::chrono::seconds ttl);
  bool exists(const std::string& key);
  bool flushdb();
  std::optional<long long> incr(const std::string& key);
  std::optional<long long> decr(const std::string& key);

  // 获取原始 Redis 连接对象，用于高级操作
  sw::redis::Redis* getRedis();

 private:
  RedisManager() = default;
  ~RedisManager() = default;

  // Redis++ 连接对象
  std::unique_ptr<sw::redis::Redis> redis_;

  // 连接池配置
  sw::redis::ConnectionOptions connectionOpts_;
  sw::redis::ConnectionPoolOptions poolOpts_;

  // 连接池状态
  bool initialized_{false};
  std::mutex mutex_;
};

}  // namespace StarryChat
