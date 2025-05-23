# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置默认构建类型
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE DEBUG)
endif()

# 启用测试
option(BUILD_TESTING "Build the testing tree." OFF)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 添加编译选项
add_compile_options(-Wall -Wextra -Wpedantic)

# 复制配置文件到构建目录
configure_file(${CMAKE_SOURCE_DIR}/config/config.yaml 
               ${CMAKE_BINARY_DIR}/bin/config.yaml 
               COPYONLY)

#生成ycm可识别的json文件，让源文件找到头文件
SET(CMAKE_EXPORT_COMPILE_COMMANDS ON )
