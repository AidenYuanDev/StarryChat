# 设置输出目录
set(PROTO_OUT_DIR ${CMAKE_BINARY_DIR}/generated/StarryChat)
file(MAKE_DIRECTORY ${PROTO_OUT_DIR})

# 获取所有 proto 文件
file(GLOB PROTO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/protos/*.proto")

# 定义正确的插件路径 
set(PLUGIN_PATH ${CMAKE_BINARY_DIR}/bin/gen-cpp-plugin)

# 定义生成的文件
set(GENERATED_PB_FILES
    ${PROTO_OUT_DIR}/user.pb.cc 
    ${PROTO_OUT_DIR}/user.pb.h 
    ${PROTO_OUT_DIR}/message.pb.cc 
    ${PROTO_OUT_DIR}/message.pb.h 
    ${PROTO_OUT_DIR}/chat.pb.cc 
    ${PROTO_OUT_DIR}/chat.pb.h 
)

# 自定义命令生成 protobuf 文件
add_custom_command(
    OUTPUT ${GENERATED_PB_FILES}
    COMMAND protoc 
            --cpp_out=${PROTO_OUT_DIR} 
            --plugin=protoc-gen-cpp-plugin=${PLUGIN_PATH}
            --cpp-plugin_out=${PROTO_OUT_DIR} 
            -I${CMAKE_CURRENT_SOURCE_DIR}/protos/
            -I${PROTO_OUT_DIR}
            ${PROTO_FILES}
    DEPENDS ${PROTO_FILES} ${PLUGIN_PATH}
    COMMENT "Generating StarryChat protocol buffer files"
    VERBATIM
)

add_executable(StarryChat)

target_sources(StarryChat PRIVATE
  ${GENERATED_PB_FILES}  # 将生成的文件添加到库中
  ./main.cpp
  ./config.cpp
)

target_include_directories(StarryChat PRIVATE 
  .
  ${Protobuf_INCLUDE_DIRS}
  ${PROTO_OUT_DIR}
)

target_link_libraries(StarryChat PRIVATE
  ${Protobuf_LIBRARIES}
  OpenSSL::Crypto
  yaml-cpp::yaml-cpp
  rpc
)
