# StarryChat
项目是依赖于`starry`网络库，结合`Mysql`、`Redis`所做的聊天室`Cpp`后端实现。
## 依赖
```
# mariadb-connector-cpp
yay -Sy mariadb-connector-cpp-git
``````

# 项目结构
## StarryChat/orm 结构
```
StarryChat/
└── orm/
    ├── CMakeLists.txt
    ├── connection.cpp
    ├── connection.h
    ├── connection_pool.cpp
    ├── connection_pool.h
    ├── model.cpp
    ├── model.h
    ├── pool_config.cpp
    ├── pool_config.h
    ├── query_builder.cpp
    ├── query_builder.h
    ├── result_set.cpp
    ├── result_set.h
    ├── transaction.cpp
    ├── transaction.h
    └── types.h
``````
## StarryChat/example/orm 结构
```
example/orm/
├── CMakeLists.txt             # 构建配置
├── models/                    # 测试用模型定义
│   ├── user.h                 # 用户模型
│   ├── message.h              # 消息模型
│   └── category.h             # 分类模型（用于测试关系）
├── test_utils.h               # 测试辅助函数
├── connection_test.cpp        # 基础连接测试
├── pool_test.cpp              # 连接池测试
├── query_builder_test.cpp     # 查询构建器测试
├── model_test.cpp             # 模型基本CRUD测试
├── relationship_test.cpp      # 模型关系测试
├── transaction_test.cpp       # 事务测试
├── performance_test.cpp       # 性能和并发测试
├── main.cpp                   # 主程序入口
└── schema/
    └── test_schema.sql        # 测试数据库结构定义脚本
```
