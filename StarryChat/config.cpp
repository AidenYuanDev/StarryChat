#include <unistd.h>
#include <yaml-cpp/node/parse.h>
#include <cctype>
#include <cstdint>
#include <string>
#include "config.h"
#include "logging.h"

using namespace StarryChat;

std::string to_lower(const std::string& str) {
  std::string result = str;
  for (auto& ch : result) {
    if (ch >= 'A' && ch <= 'Z') {
      ch += 32;
    }
  }
  return result;
}

Config& Config::getInstance() {
  static Config instance;
  return instance;
}

bool Config::loadConfig(const std::string& configFilePath) {
  configFile_ = YAML::LoadFile(configFilePath);
  if (configFile_.IsNull()) {
    LOG_ERROR << "Failed to load config file: " << configFilePath;
    return false;
  }

  if (!configFile_["server"]["host"]) {
    LOG_ERROR << "config file not set server host";
    return false;
  }
  serverHost_ = configFile_["server"]["host"].as<std::string>();

  if (!configFile_["server"]["port"]) {
    LOG_ERROR << "config file not set server port";
    return false;
  }
  serverPort_ = configFile_["server"]["port"].as<int>();

  if (!configFile_["server"]["threads"]) {
    LOG_ERROR << "config file not set server threads";
    return false;
  }
  serverThreads_ = configFile_["server"]["threads"].as<int>();

  if (!configFile_["database"]["mariadb"]["host"]) {
    LOG_ERROR << "config file not set database mariadb host";
    return false;
  }
  mariaDBHost_ = configFile_["database"]["mariadb"]["host"].as<std::string>();

  if (!configFile_["database"]["mariadb"]["port"]) {
    LOG_ERROR << "config file not set database mariadb port";
    return false;
  }
  mariaDBPort_ = configFile_["database"]["mariadb"]["port"].as<int>();

  if (!configFile_["database"]["mariadb"]["username"]) {
    LOG_ERROR << "config file not set database mariadb username";
    return false;
  }
  mariaDBUsername_ =
      configFile_["database"]["mariadb"]["username"].as<std::string>();

  if (!configFile_["database"]["mariadb"]["password"]) {
    LOG_ERROR << "config file not set database mariadb password";
    return false;
  }
  mariaDBPassword_ =
      configFile_["database"]["mariadb"]["password"].as<std::string>();

  if (!configFile_["database"]["mariadb"]["database"]) {
    LOG_ERROR << "config file not set database mariadb database";
    return false;
  }
  mariaDBDatabase_ =
      configFile_["database"]["mariadb"]["database"].as<std::string>();

  if (!configFile_["database"]["mariadb"]["poolSize"]) {
    LOG_ERROR << "config file not set database mariadb poolSize";
    return false;
  }
  mariaDBPoolSize_ = configFile_["database"]["mariadb"]["poolSize"].as<int>();

  if (!configFile_["database"]["redis"]["host"]) {
    LOG_ERROR << "config file not set database redis host";
    return false;
  }
  redisHost_ = configFile_["database"]["redis"]["host"].as<std::string>();

  if (!configFile_["database"]["redis"]["port"]) {
    LOG_ERROR << "config file not set database redis port";
    return false;
  }
  redisPort_ = configFile_["database"]["redis"]["port"].as<int>();

  if (!configFile_["database"]["redis"]["password"]) {
    LOG_ERROR << "config file not set database redis password";
    return false;
  }
  redisPassword_ =
      configFile_["database"]["redis"]["password"].as<std::string>();

  if (!configFile_["database"]["redis"]["db"]) {
    LOG_ERROR << "config file not set database redis db";
    return false;
  }
  redisDB_ = configFile_["database"]["redis"]["db"].as<int>();

  if (!configFile_["database"]["redis"]["poolSize"]) {
    LOG_ERROR << "config file not set database redis poolSize";
    return false;
  }
  redisPoolSize_ = configFile_["database"]["redis"]["poolSize"].as<int>();

  if (!configFile_["logging"]["basename"]) {
    LOG_ERROR << "config file not set logging basename";
    return false;
  }

  loggingBaseName_ = configFile_["logging"]["basename"].as<std::string>();

  if (!configFile_["logging"]["rollSize"]) {
    LOG_ERROR << "config file not set logging rollSize";
    return false;
  }

  loggingRollSize_ = configFile_["logging"]["rollSize"].as<off_t>();

  if (!configFile_["logging"]["refreshInterval"]) {
    LOG_ERROR << "config file not set logging refreshInterval";
    return false;
  }

  loggingRefreshInterval_ =
      configFile_["logging"]["refreshInterval"].as<int64_t>();

  if (!configFile_["logging"]["level"]) {
    LOG_ERROR << "config file not set logging level";
    return false;
  }

  std::string logger_level =
      to_lower(configFile_["logging"]["level"].as<std::string>());

  std::map<std::string, starry::LogLevel> st_to_level = {
      {"trace", starry::LogLevel::TRACE},
      {"debug", starry::LogLevel::DEBUG},
      {"info", starry::LogLevel::INFO},
      {"warn", starry::LogLevel::WARN},
      {"fatal", starry::LogLevel::FATAL}};

  if (st_to_level.find(logger_level) == st_to_level.end()) {
    LOG_ERROR << "Invalid logging level: " << logger_level;
  } else {
    loggingLevel_ = st_to_level[logger_level];
  }

  return valiConfig();
}

bool Config::valiConfig() {
  // 验证端口号
  if (serverPort_ <= 0 || serverPort_ > 65535) {
    LOG_ERROR << "Invalid server port: " << serverPort_;
    return false;
  }

  // 验证线程数
  if (serverThreads_ <= 0) {
    LOG_ERROR << "Invalid server threads: " << serverThreads_;
    return false;
  }

  return true;
}

std::string Config::getServerHost() const {
  return serverHost_;
}

int Config::getServerPort() const {
  return serverPort_;
}

int Config::getServerThreads() const {
  return serverThreads_;
}

std::string Config::getMariaDBHost() const {
  return mariaDBHost_;
}

int Config::getMariaDBPort() const {
  return mariaDBPort_;
}

std::string Config::getMariaDBUsername() const {
  return mariaDBUsername_;
}

std::string Config::getMariaDBPassword() const {
  return mariaDBPassword_;
}

std::string Config::getMariaDBDatabase() const {
  return mariaDBDatabase_;
}

int Config::getMariaDBPoolSize() const {
  return mariaDBPoolSize_;
}

std::string Config::getRedisHost() const {
  return redisHost_;
}

int Config::getRedisPort() const {
  return redisPort_;
}

std::string Config::getRedisPassword() const {
  return redisPassword_;
}

int Config::getRedisDB() const {
  return redisDB_;
}

int Config::getRedisPoolSize() const {
  return redisPoolSize_;
}

std::string Config::getLoggingBaseName() const {
  return loggingBaseName_;
}

starry::LogLevel Config::getLoggingLevel() const {
  return loggingLevel_;
}

off_t Config::getLoggingRollSize() const {
  return loggingRollSize_;
}

int64_t Config::getLoggingRefreshInterval() const {
  return loggingRefreshInterval_;
}
