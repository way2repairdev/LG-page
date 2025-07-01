# MySQL ODBC Driver Installation Guide

## Step 1: Download MySQL ODBC Driver

1. Go to https://dev.mysql.com/downloads/connector/odbc/
2. Select "Windows (x86, 64-bit), MSI Installer"
3. Download the latest version (e.g., mysql-connector-odbc-8.0.x-winx64.msi)
4. You may need to create a free Oracle account or click "No thanks, just start my download"

## Step 2: Install the Driver

1. Run the downloaded MSI installer as Administrator
2. Follow the installation wizard
3. Use default installation options
4. Complete the installation

## Step 3: Verify Installation

1. Press `Win + R` and type `odbcad32.exe` (or search for "ODBC Data Sources" in Start menu)
2. Go to the "Drivers" tab
3. Look for entries like:
   - "MySQL ODBC 8.0 ANSI Driver"
   - "MySQL ODBC 8.0 Unicode Driver"

## Step 4: Test the Connection

1. Make sure WAMP is running (green icon in system tray)
2. Ensure MySQL service is started in WAMP
3. Create the database using phpMyAdmin:
   - Go to http://localhost/phpmyadmin/
   - Import the `database_setup.sql` file
   - Or manually create the `login_system` database and tables

## Step 5: Run the Application

1. Double-click `run_w2r_login.bat`
2. Or build and run from Qt Creator

## Troubleshooting

### If you still get "Driver not loaded" error:

1. Check the exact driver name in ODBC Administrator
2. Update the driver name in `databasemanager.cpp` if needed
3. Common driver names:
   - "MySQL ODBC 8.0 Unicode Driver"
   - "MySQL ODBC 8.0 ANSI Driver"
   - "MySQL ODBC 8.4 Unicode Driver"

### If connection fails:

1. Verify WAMP is running (green icon)
2. Check MySQL service in WAMP manager
3. Test connection in phpMyAdmin first
4. Ensure the database `login_system` exists
5. Check username/password (default: root with no password)

### Test Database Connection:

```sql
-- In phpMyAdmin, run this query to verify the test users exist:
SELECT * FROM users;

-- You should see:
-- admin / password
-- user / 1234
```

## Default Test Credentials

- Username: `admin`, Password: `password`
- Username: `user`, Password: `1234`

After installation, the application should connect to MySQL automatically and show "Connected" in the title bar.
