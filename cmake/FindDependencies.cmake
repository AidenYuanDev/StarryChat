include(FetchContent)

# 添加 Google Test
# FetchContent_Declare(
#   googletest
#   GIT_REPOSITORY https://github.com/google/googletest.git
#   GIT_TAG       main 
# )
# # For Windows: Prevent overriding the parent project's compiler/linker settings
# set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
# # gmock
# set(BUILD_GMOCK OFF CACHE BOOL "" FORCE) 
# # gtest
# set(BUILD_GTEST ON CACHE BOOL "" FORCE)
# FetchContent_MakeAvailable(googletest)

# 添加 protobuf 
find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})

# mariadb-connector-cpp
find_path(MARIADB_CONNECTOR_INCLUDE_DIR mariadb/conncpp.hpp
          PATHS /usr/include)
find_library(MARIADB_CONNECTOR_LIBRARY mariadbcpp
             PATHS /usr/lib)

if(MARIADB_CONNECTOR_INCLUDE_DIR AND MARIADB_CONNECTOR_LIBRARY)
    set(MARIADB_CONNECTOR_FOUND TRUE)
else()
    message(FATAL_ERROR "Could not find MariaDB Connector/C++")
endif()

# yaml-cpp
find_package(yaml-cpp REQUIRED)

# include(FetchContent)
#
# FetchContent_Declare(
#   yaml-cpp
#   GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
#   GIT_TAG <tag_name> # Can be a tag (yaml-cpp-x.x.x), a commit hash, or a branch name (master)
# )
# FetchContent_MakeAvailable(yaml-cpp)
#
# target_link_libraries(YOUR_LIBRARY PUBLIC yaml-cpp::yaml-cpp) # The library or executable that require yaml-cpp library
