add_executable(rom_test
  ./quick_test.cpp)

target_include_directories(echo_client PRIVATE .)

target_link_libraries(echo_client
  echo_proto
  rpc
  net 
  log 
  orm
)
