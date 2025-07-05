-- SQLite Database setup script for Way2Repair Login System
-- This file shows the structure that will be automatically created by the application

-- Create users table (SQLite syntax)
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    password TEXT NOT NULL,
    email TEXT UNIQUE NOT NULL,
    full_name TEXT NOT NULL,
    created_at TEXT NOT NULL,
    last_login TEXT NULL,
    is_active INTEGER DEFAULT 1
);

-- Create login_logs table for tracking login attempts
CREATE TABLE IF NOT EXISTS login_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL,
    login_time TEXT NOT NULL,
    ip_address TEXT NULL,
    success INTEGER NOT NULL
);

-- Sample data that will be automatically inserted by the application:
-- Username: admin, Password: password
-- Username: user, Password: 1234

-- Note: This SQLite database file (w2r_login.db) will be automatically 
-- created in the application directory when you first run the program.
-- No manual setup is required - just run the application!

-- To view the database contents, you can use:
-- 1. SQLite Browser (https://sqlitebrowser.org/)
-- 2. SQLite command line tool
-- 3. Qt Creator's SQL browser

-- Example queries to check the data:
-- SELECT * FROM users;
-- SELECT * FROM login_logs;
