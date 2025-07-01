# Way2Repair Login System with MySQL Database

A professional Qt C++ login application with MySQL database integration via ODBC.

## Features

- Modern, compact login dialog UI
- MySQL database authentication via ODBC
- Secure password hashing (SHA-256 with salt)
- Fallback offline mode
- User management system
- Last login tracking
- Professional styling matching modern standards

## Prerequisites

1. **WAMP Server** (Windows Apache MySQL PHP)
   - Download from: https://www.wampserver.com/
   - Install and ensure MySQL service is running

2. **MySQL ODBC Driver** (REQUIRED)
   - Download from: https://dev.mysql.com/downloads/connector/odbc/
   - Install the Windows x64 MSI version (version 9.3 recommended)
   - See `INSTALL_MYSQL_ODBC.md` for detailed instructions
   - Current system uses: MySQL ODBC 9.3 ANSI Driver
   
3. **Qt 6.x** with MinGW compiler
   - Qt Creator IDE
   - Qt SQL module (ODBC driver included)

## Quick Setup Guide

### Step 1: Install MySQL ODBC Driver
1. Download MySQL ODBC driver from the link above
2. Install the MSI package (run as Administrator)
3. Verify installation by running `check_odbc_drivers.bat`

### Step 2: Setup WAMP and Database
1. Install and start WAMP server
2. Access phpMyAdmin at http://localhost/phpmyadmin/
3. Import `database_setup.sql` to create the `login_system` database

### Step 3: Test Connection
1. Run `test_mysql_connection.bat` to verify connectivity
2. Note the working driver name for troubleshooting

### Step 4: Run Application
1. Double-click `run_w2r_login.bat`
2. Or build and run from Qt Creator

## Database Setup Instructions

### Automatic MySQL Setup

The application uses a MySQL database with the following connection:
- **Host**: localhost (127.0.0.1)
- **Port**: 3306 (default MySQL port)
- **Database**: login_system
- **Username**: root
- **Password**: (empty - WAMP default)

### Manual Database Setup

If automatic setup fails, manually create the database:

1. In phpMyAdmin, create database `login_system`
2. Run the SQL commands from `database_setup.sql`:

```sql
USE login_system;

CREATE TABLE users (
    id INT PRIMARY KEY AUTO_INCREMENT,
    username VARCHAR(50) UNIQUE NOT NULL,
    password_hash VARCHAR(64) NOT NULL,
    salt VARCHAR(32) NOT NULL,
    email VARCHAR(100),
    full_name VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    last_login TIMESTAMP NULL
);

-- Insert test users
INSERT INTO users (username, password_hash, salt, email, full_name) VALUES
('admin', SHA2(CONCAT('defaultsalt123', 'password'), 256), 'defaultsalt123', 'admin@way2repair.com', 'Administrator'),
('user', SHA2(CONCAT('defaultsalt456', '1234'), 256), 'defaultsalt456', 'user@way2repair.com', 'Test User');
```

### Database Configuration

- **Database Type**: MySQL via ODBC
- **Connection**: ODBC Driver with connection string
- **Host**: localhost (127.0.0.1)
- **Port**: 3306
- **Database**: login_system
- **Credentials**: root / (empty password)

## Default Test Accounts

After database setup, you can login with:

1. **Administrator Account**
   - Username: `admin`
   - Password: `password`

2. **Regular User Account**
   - Username: `user`
   - Password: `1234`

## Building and Running

### Using Qt Creator

1. Open `CMakeLists.txt` in Qt Creator
2. Configure the project with Qt 6.x MinGW kit
3. Build and run the project (F5)

### Using Command Line

```bash
cd LoginPage
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
LoginPage.exe
```

## Database Connection

The application will automatically:

1. Attempt to connect to the MySQL database on startup
2. Create necessary tables if they don't exist
3. Insert default user accounts if the database is empty
4. Fall back to offline mode if database connection fails

## Troubleshooting

### Database Connection Issues

1. **File permissions**
   - Ensure application has write access to its directory
   - Try running as administrator if needed

2. **SQLite driver not available**
   - This is very rare - SQLite is included with Qt by default
   - Check Qt installation completeness

### Build Issues

1. **Qt SQL module not found**
   - Ensure Qt installation includes SQL modules
   - Check CMakeLists.txt configuration

## File Structure

```
LoginPage/
├── main.cpp                      # Application entry point
├── mainwindow.h/.cpp/.ui        # Main login window
├── databasemanager.h/.cpp       # SQLite database handler
├── database_sqlite_schema.sql   # Database schema reference
├── w2r_login.db                 # SQLite database (created automatically)
├── CMakeLists.txt              # Build configuration
└── README.md                   # This file
```

## Security Features

- Passwords are hashed using SHA-256 with salt
- SQL injection prevention with prepared statements
- Input validation and sanitization
- Secure database connection handling

## Development Notes

- The application supports both online (database) and offline modes
- User sessions and login tracking are implemented  
- The UI is designed to be compact and professional
- SQLite database is portable and requires no server setup
- Error handling includes user-friendly messages

## Future Enhancements

- [ ] User registration functionality
- [ ] Password reset mechanism  
- [ ] Session management
- [ ] Role-based access control
- [ ] Login attempt limiting
- [ ] Email verification
- [ ] Two-factor authentication

## Support

For issues or questions, refer to the troubleshooting section or check the Qt and WAMP documentation.
