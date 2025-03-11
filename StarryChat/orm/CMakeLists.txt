add_library(orm)

target_include_directories(orm PUBLIC .
  ${MARIADB_CONNECTOR_INCLUDE_DIR}
)

target_sources(orm PRIVATE
  ./result_set.cpp)

target_link_libraries(orm PRIVATE
  ${MARIADB_CONNECTOR_LIBRARY}
  log)

# 测试
# if(BUILD_TESTING)
#   add_subdirectory(test)
# endif()
