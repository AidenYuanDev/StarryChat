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

