# Way2Repair - Equipment Maintenance System

## 🏗️ Project Overview
Way2Repair v4.75 is a professional Qt-based C++ desktop application designed for intelligent terminal equipment maintenance management. It features a comprehensive login system, file browser, and database integration for managing equipment maintenance documentation.

## 📁 New Project Structure

```
Way2RepairLoginSystem/
├── 📁 src/                         # Source code files
│   ├── 📁 core/                    # Core application files
│   │   └── main.cpp                # Application entry point
│   ├── 📁 ui/                      # User interface implementation
│   │   ├── mainwindow.cpp          # Login window implementation
│   │   └── mainapplication.cpp     # Main application window
│   ├── 📁 database/                # Database layer
│   │   └── databasemanager.cpp     # Database operations
│   └── 📁 network/                 # Network operations (future)
│
├── 📁 include/                     # Header files
│   ├── 📁 core/                    # Core headers
│   ├── 📁 ui/                      # UI headers
│   │   ├── mainwindow.h            # Login window interface
│   │   └── mainapplication.h       # Main application interface
│   ├── 📁 database/                # Database headers
│   │   └── databasemanager.h       # Database manager interface
│   └── 📁 network/                 # Network headers (future)
│
├── 📁 resources/                   # Application resources
│   ├── 📁 ui/                      # UI form files
│   │   └── mainwindow.ui           # Login window UI design
│   └── 📁 images/                  # Icons and images
│
├── 📁 server/                      # Backend server files
│   └── 📁 api/                     # PHP API endpoints
│       ├── files.php               # File listing API
│       └── file_content.php        # File content API
│
├── 📁 database/                    # Database files and schemas
│   ├── 📁 schemas/                 # Database schema files
│   │   ├── database_setup.sql      # MySQL schema
│   │   └── database_sqlite_schema.sql # SQLite schema
│   └── w2r_login.db               # SQLite database file
│
├── 📁 docs/                        # Documentation
│   ├── README.md                   # This file
│   ├── FILE_BROWSER_GUIDE.md       # File browser usage guide
│   ├── MYSQL_SETUP_GUIDE.md        # MySQL setup instructions
│   └── INSTALL_MYSQL_ODBC.md       # ODBC driver installation
│
├── 📁 scripts/                     # Build and utility scripts
│   ├── run_app.bat                 # Windows run script
│   ├── run_app.ps1                 # PowerShell run script
│   ├── test_mysql_connection.bat   # MySQL test script
│   └── connection_test.bat         # Connection test script
│
├── 📁 tests/                       # Test files
│   ├── simple_test.cpp             # Simple test cases
│   ├── check_drivers.cpp           # ODBC driver test
│   ├── test_http.cpp               # HTTP functionality test
│   ├── test_mysql_connection.cpp   # MySQL connection test
│   ├── LoginPage.pro               # Qt project file
│   └── test_http.pro               # HTTP test project
│
├── 📁 build/                       # Build output directory
│   └── [Generated build files]
│
└── 📁 CMakeFiles/                  # CMake generated files
    └── [CMake build system files]
```

## 🛠️ Technology Stack

### Frontend
- **Framework**: Qt 6.x C++ (Qt Widgets, Qt SQL, Qt Network)
- **Build System**: CMake 3.16+
- **Compiler**: MSVC 2022 / MinGW-64
- **UI Design**: Qt Designer (.ui files)

### Backend
- **Server**: PHP (WAMP Stack)
- **Database**: MySQL with ODBC connectivity
- **API**: RESTful PHP endpoints
- **Fallback**: SQLite for local storage

### Development Tools
- **IDE**: Qt Creator / Visual Studio Code
- **Version Control**: Git
- **Testing**: Qt Test Framework

## 🚀 Key Features

### 1. **Secure Authentication System**
- MySQL-based user management
- SHA-256 password hashing with salt
- Session management and user roles
- Login attempt tracking

### 2. **Professional File Management**
- Tree-view file browser with HTTP backend
- Real-time file content loading
- Support for configuration files, logs, and documents
- Offline fallback mode

### 3. **Modern UI/UX**
- Gradient-based login interface
- Responsive layout with splitter controls
- Professional toolbar and menu system
- Status bar with user session info

### 4. **Database Integration**
- MySQL connection via ODBC
- SQLite fallback for offline use
- Automatic table creation and migration
- Connection pooling and error handling

## 📋 Requirements

### Development Environment
- Qt 6.x SDK (Widgets, SQL, Network modules)
- CMake 3.16 or higher
- C++17 compatible compiler
- MySQL ODBC Driver 8.0

### Runtime Environment
- Windows 10/11 (x64)
- WAMP Server (for file management features)
- MySQL 8.0+ or SQLite 3.x
- Qt 6.x Runtime Libraries

## 🔧 Build Instructions

### 1. Configure Environment
```bash
# Set Qt environment
export QT_DIR=/path/to/qt6
export PATH=$QT_DIR/bin:$PATH

# Or on Windows
set QT_DIR=C:\Qt\6.x.x\msvc2022_64
set PATH=%QT_DIR%\bin;%PATH%
```

### 2. Build with CMake
```bash
# Create build directory
mkdir build && cd build

# Configure
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build . --config Release

# Install (optional)
cmake --install .
```

### 3. Run Application
```bash
# From build directory
./Way2RepairLoginSystem

# Or use scripts
cd scripts
./run_app.bat
```

## 📊 Database Setup

### MySQL Setup
1. Install WAMP Server
2. Configure MySQL database
3. Run schema: `database/schemas/database_setup.sql`
4. Install MySQL ODBC Driver

### Default Users
- **Admin**: `admin` / `password`
- **User**: `user` / `1234`

## 🔍 Development Guide

### Adding New Features
1. **UI Components**: Add to `src/ui/` and `include/ui/`
2. **Database Operations**: Extend `databasemanager.h/cpp`
3. **Network Features**: Create in `src/network/` and `include/network/`
4. **Tests**: Add to `tests/` directory

### Code Organization
- **Headers**: All `.h` files in `include/` with subdirectories
- **Source**: All `.cpp` files in `src/` with matching structure
- **Resources**: UI files, images, and assets in `resources/`
- **Documentation**: Markdown files in `docs/`

## 🧪 Testing

### Unit Tests
```bash
# Run all tests
cd tests
cmake --build . --target test
```

### Manual Testing
1. Database connection test
2. File browser functionality
3. User authentication flow
4. HTTP API endpoints

## 🚀 Deployment

### Windows Deployment
```bash
# Using Qt's deployment tool
windeployqt Way2RepairLoginSystem.exe

# Or use install target
cmake --install . --prefix ./dist
```

### Server Deployment
1. Copy `server/api/` to WAMP `htdocs/api/`
2. Configure PHP settings for file access
3. Set up MySQL database and users

## 📝 Contributing

### Development Workflow
1. Create feature branch
2. Make changes following code style
3. Add tests for new functionality
4. Update documentation
5. Submit pull request

### Code Style
- Use Qt coding conventions
- Follow C++17 best practices
- Comment public interfaces
- Use meaningful variable names

## 🔐 Security Considerations

- Password hashing with salt
- SQL injection prevention
- File path traversal protection
- Session management
- Input validation

## 📄 License

Copyright © 2025 Way2Repair Systems. All rights reserved.

## 🤝 Support

For technical support or questions:
- Check documentation in `docs/`
- Review test files for examples
- Contact development team

---

**Note**: This restructured project provides a professional, maintainable codebase with clear separation of concerns and follows modern C++ and Qt development practices.
