add_executable(test_service)

target_sources(test_service PRIVATE
  ${GENERATED_PB_FILES}  # 将生成的文件添加到库中
  ./test_service.cpp
)

target_include_directories(test_service PRIVATE 
  .
  ${Protobuf_INCLUDE_DIRS}
  ${CMAKE_BINARY_DIR}/generated/StarryChat
  ${HIREDIS_HEADER}
  ${REDIS_PLUS_PLUS_HEADER}
  ${MARIADB_CONNECTOR_INCLUDE_DIR}
)

target_link_libraries(test_service PRIVATE
  ${Protobuf_LIBRARIES}
  ${HIREDIS_LIB}
  ${REDIS_PLUS_PLUS_LIB}
  ${MARIADB_CONNECTOR_LIBRARY}
  OpenSSL::Crypto
  yaml-cpp::yaml-cpp
  rpc
)
