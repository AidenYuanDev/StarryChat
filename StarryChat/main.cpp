#include <iostream>
#include "config.h"
#include "logging.h"

using namespace StarryChat;

int main () {
  auto& config = Config::getInstance();
  if (!config.loadConfig("config.yaml")) {
    LOG_ERROR << "Failed to load config file: ";
    return 0;
  }

  std::cout << "Server Port" << config.getServerPort() << std::endl;
  
  return 0;
}
