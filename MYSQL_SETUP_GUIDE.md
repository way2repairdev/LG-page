# MySQL Setup Guide for Way2Repair Login System

## Requirements for MySQL Connection

Your Qt installation doesn't have the native MySQL driver, but we can use ODBC to connect to MySQL. Here's what you need:

## Step 1: Install WAMP Server
1. Download WAMP from: https://www.wampserver.com/
2. Install and start WAMP server
3. Ensure the WAMP icon is GREEN in the system tray

## Step 2: Install MySQL ODBC Driver
1. Download MySQL Connector/ODBC from: https://dev.mysql.com/downloads/connector/odbc/
2. Choose "Windows (x86, 64-bit), MSI Installer" 
3. Install the driver (this enables Qt to connect via ODBC)

## Step 3: Create MySQL Database
1. Open phpMyAdmin (http://localhost/phpmyadmin)
2. Create a new database named `login_system`
3. Run the SQL script from `database_setup.sql`

## Step 4: Verify ODBC Connection
1. Open Windows "ODBC Data Sources" (search in Start menu)
2. Go to "Drivers" tab
3. Verify "MySQL ODBC 8.0 ANSI Driver" is listed

## Alternative: Use Different MySQL Driver Name
If you have a different MySQL ODBC driver version, update the connection string in `databasemanager.cpp`:

```cpp
// Look for your specific driver name in ODBC Data Sources
QString connectionString = QString("DRIVER={MySQL ODBC 8.0 Unicode Driver};"  // or other version
```

Common MySQL ODBC driver names:
- MySQL ODBC 8.0 ANSI Driver
- MySQL ODBC 8.0 Unicode Driver  
- MySQL ODBC 5.3 ANSI Driver
- MySQL ODBC 5.3 Unicode Driver

## Troubleshooting

### Error: "Data source name not found"
- Install MySQL ODBC Connector
- Check driver name in ODBC Data Sources

### Error: "Access denied for user 'root'"
- Check MySQL root password in WAMP
- Default WAMP has no password for root

### Error: "Unknown database 'login_system'"
- Create the database in phpMyAdmin
- Run the setup SQL script

### Error: "Can't connect to MySQL server"
- Start WAMP server
- Check MySQL service is running (green icon)

## Testing Your Setup
1. Start WAMP server (green icon)
2. Verify MySQL service is running
3. Create `login_system` database
4. Install MySQL ODBC driver
5. Run the Qt application

## Default Login Credentials
- Admin: admin / password
- User: user / 1234

The application will automatically create tables and insert default users on first connection.
