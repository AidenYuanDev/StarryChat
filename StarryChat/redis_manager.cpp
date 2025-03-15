#include "config.h"
#include "logging.h"
#include "redis_manager.h"

namespace StarryChat {

RedisManager& RedisManager::getInstance() {
  static RedisManager instance;
  return instance;
}

bool RedisManager::initialize() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (initialized_) {
    return true;  // 已经初始化过，直接返回
  }

  try {
    // 从配置中获取Redis设置
    auto& config = Config::getInstance();

    // 设置连接选项
    connectionOpts_.host = config.getRedisHost();
    connectionOpts_.port = config.getRedisPort();
    connectionOpts_.password = config.getRedisPassword();
    connectionOpts_.db = config.getRedisDB();
    connectionOpts_.connect_timeout =
        std::chrono::milliseconds(1000);  // 1秒超时
    connectionOpts_.socket_timeout =
        std::chrono::milliseconds(1000);  // 1秒超时

    // 设置连接池选项
    poolOpts_.size = config.getRedisPoolSize();                // 连接池大小
    poolOpts_.wait_timeout = std::chrono::milliseconds(100);   // 等待超时时间
    poolOpts_.connection_lifetime = std::chrono::minutes(10);  // 连接生存时间

    // 创建Redis连接池
    redis_ = std::make_unique<sw::redis::Redis>(connectionOpts_, poolOpts_);

    // 测试连接
    if (!exists("test_connection")) {
      set("test_connection", "1", std::chrono::seconds(1));
      del("test_connection");
    }

    initialized_ = true;
    LOG_INFO << "Redis connection initialized successfully";
    return true;

  } catch (const sw::redis::Error& e) {
    LOG_ERROR << "Redis initialization error: " << e.what();
    return false;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis initialization error: " << e.what();
    return false;
  }
}

void RedisManager::shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (initialized_) {
    redis_.reset();
    initialized_ = false;
    LOG_INFO << "Redis connection shut down";
  }
}

// 字符串操作
bool RedisManager::set(const std::string& key,
                       const std::string& value,
                       std::chrono::seconds ttl) {
  if (!initialized_)
    return false;

  try {
    if (ttl.count() > 0) {
      redis_->set(key, value, ttl);
    } else {
      redis_->set(key, value);
    }
    return true;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in set: " << e.what();
    return false;
  }
}

std::optional<std::string> RedisManager::get(const std::string& key) {
  if (!initialized_)
    return std::nullopt;

  try {
    auto val = redis_->get(key);
    return val;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in get: " << e.what();
    return std::nullopt;
  }
}

bool RedisManager::del(const std::string& key) {
  if (!initialized_)
    return false;

  try {
    redis_->del(key);
    return true;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in del: " << e.what();
    return false;
  }
}

// 哈希表操作
bool RedisManager::hset(const std::string& key,
                        const std::string& field,
                        const std::string& value) {
  if (!initialized_)
    return false;

  try {
    redis_->hset(key, field, value);
    return true;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in hset: " << e.what();
    return false;
  }
}

std::optional<std::string> RedisManager::hget(const std::string& key,
                                              const std::string& field) {
  if (!initialized_)
    return std::nullopt;

  try {
    auto val = redis_->hget(key, field);
    return val;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in hget: " << e.what();
    return std::nullopt;
  }
}

bool RedisManager::hdel(const std::string& key, const std::string& field) {
  if (!initialized_)
    return false;

  try {
    redis_->hdel(key, field);
    return true;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in hdel: " << e.what();
    return false;
  }
}

std::optional<std::unordered_map<std::string, std::string>>
RedisManager::hgetall(const std::string& key) {
  if (!initialized_)
    return std::nullopt;

  try {
    std::unordered_map<std::string, std::string> result;
    redis_->hgetall(key, std::inserter(result, result.begin()));
    return result;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in hgetall: " << e.what();
    return std::nullopt;
  }
}

// 列表操作
bool RedisManager::lpush(const std::string& key, const std::string& value) {
  if (!initialized_)
    return false;

  try {
    redis_->lpush(key, value);
    return true;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in lpush: " << e.what();
    return false;
  }
}

bool RedisManager::rpush(const std::string& key, const std::string& value) {
  if (!initialized_)
    return false;

  try {
    redis_->rpush(key, value);
    return true;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in rpush: " << e.what();
    return false;
  }
}

std::optional<std::string> RedisManager::lpop(const std::string& key) {
  if (!initialized_)
    return std::nullopt;

  try {
    auto val = redis_->lpop(key);
    return val;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in lpop: " << e.what();
    return std::nullopt;
  }
}

std::optional<std::string> RedisManager::rpop(const std::string& key) {
  if (!initialized_)
    return std::nullopt;

  try {
    auto val = redis_->rpop(key);
    return val;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in rpop: " << e.what();
    return std::nullopt;
  }
}

std::optional<std::vector<std::string>>
RedisManager::lrange(const std::string& key, long start, long stop) {
  if (!initialized_)
    return std::nullopt;

  try {
    std::vector<std::string> result;
    redis_->lrange(key, start, stop, std::back_inserter(result));
    return result;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in lrange: " << e.what();
    return std::nullopt;
  }
}

// 集合操作
bool RedisManager::sadd(const std::string& key, const std::string& member) {
  if (!initialized_)
    return false;

  try {
    redis_->sadd(key, member);
    return true;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in sadd: " << e.what();
    return false;
  }
}

bool RedisManager::srem(const std::string& key, const std::string& member) {
  if (!initialized_)
    return false;

  try {
    redis_->srem(key, member);
    return true;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in srem: " << e.what();
    return false;
  }
}

std::optional<std::unordered_set<std::string>> RedisManager::smembers(
    const std::string& key) {
  if (!initialized_)
    return std::nullopt;

  try {
    std::unordered_set<std::string> result;
    redis_->smembers(key, std::inserter(result, result.begin()));
    return result;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in smembers: " << e.what();
    return std::nullopt;
  }
}

// 有序集合操作
bool RedisManager::zadd(const std::string& key,
                        const std::string& member,
                        double score) {
  if (!initialized_)
    return false;

  try {
    redis_->zadd(key, member, score);
    return true;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in zadd: " << e.what();
    return false;
  }
}

bool RedisManager::zrem(const std::string& key, const std::string& member) {
  if (!initialized_)
    return false;

  try {
    redis_->zrem(key, member);
    return true;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in zrem: " << e.what();
    return false;
  }
}

std::optional<std::vector<std::string>>
RedisManager::zrange(const std::string& key, long start, long stop) {
  if (!initialized_)
    return std::nullopt;

  try {
    std::vector<std::string> result;
    redis_->zrange(key, start, stop, std::back_inserter(result));
    return result;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in zrange: " << e.what();
    return std::nullopt;
  }
}

std::optional<std::vector<std::pair<std::string, double>>>
RedisManager::zrangeWithScores(const std::string& key, long start, long stop) {
  if (!initialized_)
    return std::nullopt;

  try {
    // 1. 获取成员
    std::vector<std::string> members;
    redis_->zrange(key, start, stop, std::back_inserter(members));

    // 2. 获取每个成员的分数
    std::vector<std::pair<std::string, double>> result;
    for (const auto& member : members) {
      auto score = redis_->zscore(key, member);
      if (score) {
        result.emplace_back(member, *score);
      }
    }

    return result;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in zrangeWithScores: " << e.what();
    return std::nullopt;
  }
}

// 发布/订阅
bool RedisManager::publish(const std::string& channel,
                           const std::string& message) {
  if (!initialized_)
    return false;

  try {
    redis_->publish(channel, message);
    return true;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in publish: " << e.what();
    return false;
  }
}

// 其他操作
bool RedisManager::expire(const std::string& key, std::chrono::seconds ttl) {
  if (!initialized_)
    return false;

  try {
    return redis_->expire(key, ttl);
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in expire: " << e.what();
    return false;
  }
}

bool RedisManager::exists(const std::string& key) {
  if (!initialized_)
    return false;

  try {
    return redis_->exists(key) > 0;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in exists: " << e.what();
    return false;
  }
}

bool RedisManager::flushdb() {
  if (!initialized_)
    return false;

  try {
    redis_->flushdb();
    return true;
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in flushdb: " << e.what();
    return false;
  }
}

std::optional<long long> RedisManager::incr(const std::string& key) {
  if (!initialized_)
    return std::nullopt;

  try {
    return redis_->incr(key);
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in incr: " << e.what();
    return std::nullopt;
  }
}

std::optional<long long> RedisManager::decr(const std::string& key) {
  if (!initialized_)
    return std::nullopt;

  try {
    return redis_->decr(key);
  } catch (const std::exception& e) {
    LOG_ERROR << "Redis error in decr: " << e.what();
    return std::nullopt;
  }
}

sw::redis::Redis* RedisManager::getRedis() {
  return initialized_ ? redis_.get() : nullptr;
}

}  // namespace StarryChat
