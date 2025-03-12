-- 创建测试数据库（如果不存在）
CREATE DATABASE IF NOT EXISTS starrychat_test;
USE starrychat_test;

-- 删除已存在的表（如果有）
DROP TABLE IF EXISTS user_configs;
DROP TABLE IF EXISTS users;

-- 创建用户表 - 基础表用于测试大部分ORM功能
CREATE TABLE users (
    id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(100) NOT NULL,
    email VARCHAR(100) NULL,
    status TINYINT NOT NULL DEFAULT 1,  -- 1=活跃，0=禁用
    login_count INT UNSIGNED DEFAULT 0,
    last_login_at DATETIME NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    
    UNIQUE INDEX idx_username (username),
    INDEX idx_status (status)
);

-- 创建用户配置表 - 用于测试简单的一对一关系
CREATE TABLE user_configs (
    id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id INT UNSIGNED NOT NULL,
    theme VARCHAR(50) DEFAULT 'default',
    notification_enabled TINYINT(1) DEFAULT 1,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    
    UNIQUE INDEX idx_user_id (user_id),
    CONSTRAINT fk_config_user FOREIGN KEY (user_id) 
        REFERENCES users (id) ON DELETE CASCADE ON UPDATE CASCADE
);

-- 插入一些基本测试数据
INSERT INTO users (username, email, status, login_count) VALUES
('test_user', 'test@example.com', 1, 5),
('admin', 'admin@example.com', 1, 10),
('inactive', 'inactive@example.com', 0, 2);

INSERT INTO user_configs (user_id, theme, notification_enabled) VALUES
(1, 'light', 1),
(2, 'dark', 1),
(3, 'default', 0);
