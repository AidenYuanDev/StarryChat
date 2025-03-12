-- 创建测试数据库（如果不存在）
CREATE DATABASE IF NOT EXISTS starrychat_test;
USE starrychat_test;

-- 确保表不存在（如果存在则先删除）
DROP TABLE IF EXISTS messages;
DROP TABLE IF EXISTS categories;
DROP TABLE IF EXISTS users;

-- 创建用户表
CREATE TABLE users (
    id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(100) NOT NULL UNIQUE,
    password VARCHAR(255) NOT NULL,
    email VARCHAR(100) NOT NULL UNIQUE,
    status TINYINT NOT NULL DEFAULT 1 COMMENT '1: 活跃, 0: 禁用',
    last_login_at DATETIME NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    
    INDEX idx_username (username),
    INDEX idx_email (email),
    INDEX idx_status (status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 创建消息分类表
CREATE TABLE categories (
    id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(50) NOT NULL UNIQUE,
    description TEXT NULL,
    parent_id INT UNSIGNED NULL,
    display_order INT NOT NULL DEFAULT 0,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    
    INDEX idx_parent_id (parent_id),
    INDEX idx_display_order (display_order),
    
    CONSTRAINT fk_category_parent FOREIGN KEY (parent_id) 
        REFERENCES categories (id) ON DELETE SET NULL ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 创建消息表
CREATE TABLE messages (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id INT UNSIGNED NOT NULL,
    receiver_id INT UNSIGNED NULL COMMENT '如果为NULL则是公开消息',
    category_id INT UNSIGNED NOT NULL,
    content TEXT NOT NULL,
    attachment VARCHAR(255) NULL COMMENT '附件路径',
    is_read TINYINT(1) NOT NULL DEFAULT 0 COMMENT '0: 未读, 1: 已读',
    read_at DATETIME NULL COMMENT '已读时间',
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    
    INDEX idx_user_id (user_id),
    INDEX idx_receiver_id (receiver_id),
    INDEX idx_category_id (category_id),
    INDEX idx_is_read (is_read),
    INDEX idx_created_at (created_at),
    
    CONSTRAINT fk_message_user FOREIGN KEY (user_id) 
        REFERENCES users (id) ON DELETE CASCADE ON UPDATE CASCADE,
    CONSTRAINT fk_message_receiver FOREIGN KEY (receiver_id) 
        REFERENCES users (id) ON DELETE CASCADE ON UPDATE CASCADE,
    CONSTRAINT fk_message_category FOREIGN KEY (category_id) 
        REFERENCES categories (id) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 插入测试用户数据
INSERT INTO users (username, password, email, status) VALUES
('admin', '$2y$10$92IXUNpkjO0rOQ5byMi.Ye4oKoEa3Ro9llC/.og/at2.uheWG/igi', 'admin@example.com', 1),
('test_user', '$2y$10$92IXUNpkjO0rOQ5byMi.Ye4oKoEa3Ro9llC/.og/at2.uheWG/igi', 'user@example.com', 1),
('inactive_user', '$2y$10$92IXUNpkjO0rOQ5byMi.Ye4oKoEa3Ro9llC/.og/at2.uheWG/igi', 'inactive@example.com', 0);

-- 插入测试分类数据
INSERT INTO categories (name, description, parent_id, display_order) VALUES
('公告', '系统公告和重要信息', NULL, 1),
('聊天', '普通聊天消息', NULL, 2),
('通知', '系统通知', NULL, 3),
('技术讨论', '技术相关话题', 2, 1),
('闲聊', '非技术闲聊', 2, 2);

-- 插入测试消息数据
INSERT INTO messages (user_id, receiver_id, category_id, content, is_read) VALUES
(1, NULL, 1, '欢迎来到StarryChat聊天系统！', 1),
(1, 2, 3, '你有一条新的系统通知', 0),
(2, 1, 2, '管理员，我有一些问题需要咨询', 1),
(2, NULL, 4, '有人了解C++中的智能指针吗？', 0),
(1, 2, 5, '周末有什么计划？', 0),
(2, NULL, 4, '我在学习网络编程，有推荐的资料吗？', 0),
(3, NULL, 5, '大家好，我是新来的！', 1);

-- 创建存储过程，用于模拟批量数据
DELIMITER //
CREATE PROCEDURE generate_test_data(IN message_count INT)
BEGIN
    DECLARE i INT DEFAULT 0;
    
    WHILE i < message_count DO
        INSERT INTO messages (user_id, receiver_id, category_id, content, is_read)
        VALUES (
            IF(i % 3 = 0, 1, 2), -- 在用户1和2之间切换
            IF(i % 5 = 0, NULL, IF(i % 2 = 0, 1, 2)), -- 有些是公开消息，有些是私信
            (i % 5) + 1, -- 在5个分类之间循环
            CONCAT('这是测试消息 #', i), -- 消息内容
            i % 2 -- 一半已读，一半未读
        );
        
        SET i = i + 1;
    END WHILE;
END //
DELIMITER ;

-- 如果需要生成更多测试数据，可以调用：
-- CALL generate_test_data(100);
