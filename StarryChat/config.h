#pragma once

#include <unistd.h>
#include <yaml-cpp/node/node.h>
#include <yaml-cpp/yaml.h>
#include <cstdint>
#include <string>
#include "logging.h"

namespace StarryChat {

class Config {
 public:
  Config(const Config&) = delete;
  Config& operator=(const Config&) = delete;
  ~Config() = default;

  static Config& getInstance();
  bool loadConfig(const std::string& configFilePath);

  // Server
  std::string getServerHost() const;
  int getServerPort() const;
  int getServerThreads() const;

  // Database -- MariaDB
  std::string getMariaDBHost() const;
  int getMariaDBPort() const;
  std::string getMariaDBUsername() const;
  std::string getMariaDBPassword() const;
  std::string getMariaDBDatabase() const;
  int getMariaDBPoolSize() const;

  // Database - Redis
  std::string getRedisHost() const;
  int getRedisPort() const;
  std::string getRedisPassword() const;
  int getRedisDB() const;
  int getRedisPoolSize() const;

  // Logging
  std::string getLoggingBaseName() const;
  starry::LogLevel getLoggingLevel() const;
  int64_t getLoggingRollSize() const;
  int64_t getLoggingRefreshInterval() const;

 private:
  Config() = default;

  // 验证
  bool valiConfig();

  YAML::Node configFile_;

  // Server
  std::string serverHost_;
  int serverPort_;
  int serverThreads_;

  // Database -- MariaDB
  std::string mariaDBHost_;
  int mariaDBPort_;
  std::string mariaDBUsername_;
  std::string mariaDBPassword_;
  std::string mariaDBDatabase_;
  int mariaDBPoolSize_;

  // Database - Redis
  std::string redisHost_;
  int redisPort_;
  std::string redisPassword_;
  int redisDB_;
  int redisPoolSize_;

  // Logging
  std::string loggingBaseName_;
  starry::LogLevel loggingLevel_;
  off_t loggingRollSize_;
  int64_t loggingRefreshInterval_;
};

}  // namespace StarryChat
