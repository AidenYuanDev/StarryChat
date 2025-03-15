-- 创建数据库
CREATE DATABASE IF NOT EXISTS chatroom CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

-- 使用数据库
USE chatroom;

-- 用户表
CREATE TABLE users (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(64) NOT NULL UNIQUE,
    password_hash VARCHAR(128) NOT NULL,
    salt VARCHAR(32) NOT NULL,
    nickname VARCHAR(64) NOT NULL,
    email VARCHAR(128) NOT NULL,
    avatar_url VARCHAR(255) DEFAULT '',
    status TINYINT UNSIGNED DEFAULT 1,
    login_attempts INT UNSIGNED DEFAULT 0,
    created_time BIGINT UNSIGNED NOT NULL,
    last_login_time BIGINT UNSIGNED DEFAULT 0,
    INDEX idx_status (status),
    INDEX idx_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 聊天室表
CREATE TABLE chat_rooms (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(128) NOT NULL,
    description TEXT,
    creator_id BIGINT UNSIGNED NOT NULL,
    created_time BIGINT UNSIGNED NOT NULL,
    member_count INT UNSIGNED DEFAULT 0,
    avatar_url VARCHAR(255) DEFAULT '',
    last_message_time BIGINT UNSIGNED DEFAULT 0,
    FOREIGN KEY (creator_id) REFERENCES users(id),
    INDEX idx_creator (creator_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 私聊表
CREATE TABLE private_chats (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user1_id BIGINT UNSIGNED NOT NULL,
    user2_id BIGINT UNSIGNED NOT NULL,
    created_time BIGINT UNSIGNED NOT NULL,
    last_message_time BIGINT UNSIGNED DEFAULT 0,
    FOREIGN KEY (user1_id) REFERENCES users(id),
    FOREIGN KEY (user2_id) REFERENCES users(id),
    UNIQUE KEY uk_users (user1_id, user2_id),
    INDEX idx_user1 (user1_id),
    INDEX idx_user2 (user2_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 聊天室成员表
CREATE TABLE chat_room_members (
    chat_room_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    role TINYINT UNSIGNED NOT NULL DEFAULT 3,
    join_time BIGINT UNSIGNED NOT NULL,
    display_name VARCHAR(64) DEFAULT '',
    PRIMARY KEY (chat_room_id, user_id),
    FOREIGN KEY (chat_room_id) REFERENCES chat_rooms(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    INDEX idx_user_id (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 消息表
CREATE TABLE messages (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    sender_id BIGINT UNSIGNED NOT NULL,
    chat_type TINYINT UNSIGNED NOT NULL,
    chat_id BIGINT UNSIGNED NOT NULL,
    type TINYINT UNSIGNED NOT NULL,
    content TEXT,
    system_code VARCHAR(32) DEFAULT NULL,
    timestamp BIGINT UNSIGNED NOT NULL,
    status TINYINT UNSIGNED DEFAULT 1,
    reply_to_id BIGINT UNSIGNED DEFAULT NULL,
    FOREIGN KEY (sender_id) REFERENCES users(id),
    FOREIGN KEY (reply_to_id) REFERENCES messages(id) ON DELETE SET NULL,
    INDEX idx_chat (chat_type, chat_id, timestamp),
    INDEX idx_sender (sender_id),
    INDEX idx_timestamp (timestamp)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 消息@提及表
CREATE TABLE message_mentions (
    message_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY (message_id, user_id),
    FOREIGN KEY (message_id) REFERENCES messages(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 会话表
CREATE TABLE sessions (
    token VARCHAR(128) PRIMARY KEY,
    user_id BIGINT UNSIGNED NOT NULL,
    created_time BIGINT UNSIGNED NOT NULL,
    expiry_time BIGINT UNSIGNED NOT NULL,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    INDEX idx_user_id (user_id),
    INDEX idx_expiry (expiry_time)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 添加表注释
ALTER TABLE users COMMENT '用户表';
ALTER TABLE chat_rooms COMMENT '聊天室表';
ALTER TABLE chat_room_members COMMENT '聊天室成员表';
ALTER TABLE private_chats COMMENT '私聊表';
ALTER TABLE messages COMMENT '消息表';
ALTER TABLE message_mentions COMMENT '消息@提及表';
ALTER TABLE sessions COMMENT '会话表';

-- 创建管理员用户 (可选)
-- INSERT INTO users (username, password_hash, salt, nickname, email, status, created_time)
-- VALUES ('admin', '您的密码哈希', '您的盐值', '管理员', 'admin@example.com', 2, UNIX_TIMESTAMP());
