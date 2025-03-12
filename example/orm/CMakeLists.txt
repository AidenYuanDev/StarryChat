add_executable(rom_test
  ./quick_test.cpp)

target_include_directories(echo_client PRIVATE
  ${PROTO_GEN_DIR}
  ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(echo_client
  ZLIB::ZLIB
  echo_proto
  rpc
  net 
  log 
  orm
  ${Protobuf_LIBRARIES}
)
