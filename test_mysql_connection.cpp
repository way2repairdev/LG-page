#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <iostream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    std::cout << "=== MySQL ODBC Connection Test ===" << std::endl;
    std::cout << std::endl;
    
    // Check available SQL drivers
    std::cout << "Available Qt SQL drivers:" << std::endl;
    QStringList drivers = QSqlDatabase::drivers();
    for (const QString &driver : drivers) {
        std::cout << "  - " << driver.toStdString() << std::endl;
    }
    std::cout << std::endl;
    
    if (!drivers.contains("QODBC")) {
        std::cout << "ERROR: QODBC driver not available!" << std::endl;
        return 1;
    }
    
    // Test ODBC connection to MySQL
    std::cout << "Testing MySQL ODBC connection..." << std::endl;
    
    QSqlDatabase db = QSqlDatabase::addDatabase("QODBC");
    
    // Try different common MySQL ODBC driver names
    QStringList driverNames = {
        "MySQL ODBC 9.3 Unicode Driver",
        "MySQL ODBC 9.3 ANSI Driver",
        "MySQL ODBC 8.0 Unicode Driver",
        "MySQL ODBC 8.0 ANSI Driver",
        "MySQL ODBC 8.4 Unicode Driver",
        "MySQL ODBC 5.3 Unicode Driver",
        "MySQL ODBC 5.3 ANSI Driver"
    };
    
    bool connected = false;
    QString workingDriver;
    
    for (const QString &driverName : driverNames) {
        std::cout << "Trying driver: " << driverName.toStdString() << std::endl;
        
        QString connectionString = QString(
            "DRIVER={%1};"
            "SERVER=localhost;"
            "PORT=3306;"
            "DATABASE=login_system;"
            "UID=root;"
            "PWD=;"
            "CHARSET=utf8;"
        ).arg(driverName);
        
        db.setDatabaseName(connectionString);
        
        if (db.open()) {
            std::cout << "SUCCESS: Connected with " << driverName.toStdString() << std::endl;
            workingDriver = driverName;
            connected = true;
            break;
        } else {
            std::cout << "Failed: " << db.lastError().text().toStdString() << std::endl;
        }
    }
    
    if (connected) {
        std::cout << std::endl << "=== Connection Test Successful ===" << std::endl;
        std::cout << "Working driver: " << workingDriver.toStdString() << std::endl;
        
        // Test a simple query
        QSqlQuery query(db);
        if (query.exec("SELECT COUNT(*) FROM users")) {
            if (query.next()) {
                int count = query.value(0).toInt();
                std::cout << "Users table contains " << count << " records" << std::endl;
            }
        } else {
            std::cout << "Query test failed: " << query.lastError().text().toStdString() << std::endl;
        }
        
        db.close();
        
        std::cout << std::endl << "Update databasemanager.cpp with this driver name:" << std::endl;
        std::cout << "\"" << workingDriver.toStdString() << "\"" << std::endl;
        
    } else {
        std::cout << std::endl << "=== Connection Test Failed ===" << std::endl;
        std::cout << "Please ensure:" << std::endl;
        std::cout << "1. MySQL ODBC driver is installed" << std::endl;
        std::cout << "2. WAMP server is running" << std::endl;
        std::cout << "3. MySQL service is started" << std::endl;
        std::cout << "4. Database 'login_system' exists" << std::endl;
    }
    
    return 0;
}
