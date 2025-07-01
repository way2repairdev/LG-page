-- Database setup script for XinZhiZao Login System
-- Execute this script in phpMyAdmin or MySQL command line

-- Create database
CREATE DATABASE IF NOT EXISTS login_system 
CHARACTER SET utf8mb4 
COLLATE utf8mb4_unicode_ci;

-- Use the database
USE login_system;

-- Create users table
CREATE TABLE IF NOT EXISTS users (
    id INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(50) UNIQUE NOT NULL,
    password VARCHAR(255) NOT NULL,
    email VARCHAR(100) UNIQUE NOT NULL,
    full_name VARCHAR(100) NOT NULL,
    created_at DATETIME NOT NULL,
    last_login DATETIME NULL,
    is_active BOOLEAN DEFAULT TRUE,
    INDEX idx_username (username),
    INDEX idx_email (email)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Insert default users (passwords are hashed with SHA-256 + salt)
-- admin / password
-- user / 1234
INSERT INTO users (username, password, email, full_name, created_at, is_active) VALUES
('admin', '5994471abb01112afcc18159f6cc74b4f511b99806da59b3caf5a9c173cacfc5', 'admin@localhost.com', 'System Administrator', NOW(), TRUE),
('user', 'a665a45920422f9d417e4867efdc4fb8a04a1f3fff1fa07e998e86f7f7a27ae3', 'user@localhost.com', 'Regular User', NOW(), TRUE);

-- Create login_logs table for tracking login attempts
CREATE TABLE IF NOT EXISTS login_logs (
    id INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(50) NOT NULL,
    login_time DATETIME NOT NULL,
    ip_address VARCHAR(45) NULL,
    success BOOLEAN NOT NULL,
    INDEX idx_username_time (username, login_time)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Show tables
SHOW TABLES;

-- Show user data
SELECT id, username, email, full_name, created_at, last_login, is_active FROM users;
