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
    ├── CMakeLists.txt                   # 构建配置
    ├── exceptions.h/cpp                 # 异常类定义
    ├── types.h                          # 基本类型定义
    ├── connection.h/cpp                 # 单个数据库连接封装
    ├── connection_pool.h/cpp            # 连接池实现
    ├── pool_config.h/cpp                # 连接池配置
    ├── query_builder.h/cpp              # SQL查询构建器
    ├── result_set.h/cpp                 # 查询结果封装
    ├── model.h/cpp                      # 基础模型类
    ├── relation.h/cpp                   # 关系处理
    ├── transaction.h/cpp                # 事务支持
    ├── field_mapper.h/cpp               # 字段映射工具
    ├── utils/                           # 工具类
    │   ├── string_utils.h/cpp           # 字符串处理
    │   └── type_converter.h/cpp         # 类型转换
    ├── test/                            # 测试目录
    └── examples/                        # 使用示例
```

