add_executable(StarryChat)

target_sources(StarryChat PRIVATE
  ./main.cpp
  ./config.cpp
)

target_include_directories(StarryChat PRIVATE .)

target_link_libraries(StarryChat PRIVATE
  yaml-cpp::yaml-cpp
  net 
  log
)
