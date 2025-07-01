#include "databasemanager.h"
#include <QtSql/QSqlDriver>
#include <QDateTime>
#include <QUuid>

DatabaseManager::DatabaseManager(QObject *parent)
    : QObject(parent)
    , m_connectionName(QUuid::createUuid().toString())
{
    // Initialize database connection
    m_database = QSqlDatabase();
}

DatabaseManager::~DatabaseManager()
{
    disconnectFromDatabase();
}

bool DatabaseManager::connectToDatabase(const QString &hostname,
                                       const QString &database,
                                       const QString &username,
                                       const QString &password,
                                       int port)
{
    // Remove existing connection if any
    if (m_database.isOpen()) {
        disconnectFromDatabase();
    }

    // Try MySQL via ODBC first (more compatible)
    m_database = QSqlDatabase::addDatabase("QODBC", m_connectionName);
    
    // Create ODBC connection string for MySQL
    QString connectionString = QString("DRIVER={MySQL ODBC 9.3 ANSI Driver};"
                                      "SERVER=%1;"
                                      "PORT=%2;"
                                      "DATABASE=%3;"
                                      "UID=%4;"
                                      "PWD=%5;"
                                      "CHARSET=utf8;")
                                      .arg(hostname)
                                      .arg(port)
                                      .arg(database)
                                      .arg(username)
                                      .arg(password);
    
    m_database.setDatabaseName(connectionString);

    // Attempt to open the connection
    if (!m_database.open()) {
        // If ODBC fails, try direct MySQL driver (if available)
        QSqlDatabase::removeDatabase(m_connectionName);
        
        if (QSqlDatabase::drivers().contains("QMYSQL")) {
            m_database = QSqlDatabase::addDatabase("QMYSQL", m_connectionName);
            m_database.setHostName(hostname);
            m_database.setDatabaseName(database);
            m_database.setUserName(username);
            m_database.setPassword(password);
            m_database.setPort(port);
            
            if (!m_database.open()) {
                QString error = QString("Failed to connect to MySQL database: %1")
                               .arg(m_database.lastError().text());
                emit errorOccurred(error);
                qDebug() << error;
                return false;
            }
        } else {
            QString error = QString("Failed to connect via ODBC: %1\n\nPlease install MySQL ODBC Driver from: https://dev.mysql.com/downloads/connector/odbc/")
                           .arg(m_database.lastError().text());
            emit errorOccurred(error);
            qDebug() << error;
            return false;
        }
    }

    qDebug() << "Successfully connected to MySQL database:" << database;
    emit connectionStatusChanged(true);

    // Create tables if they don't exist
    if (!createTables()) {
        qDebug() << "Warning: Could not create or verify database tables";
    }

    return true;
}

void DatabaseManager::disconnectFromDatabase()
{
    if (m_database.isOpen()) {
        m_database.close();
        emit connectionStatusChanged(false);
        qDebug() << "Disconnected from database";
    }
    
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool DatabaseManager::isConnected() const
{
    return m_database.isOpen();
}

bool DatabaseManager::authenticateUser(const QString &username, const QString &password)
{
    if (!isConnected()) {
        emit errorOccurred("Not connected to database");
        return false;
    }

    QString hashedPassword = hashPassword(password);
    
    QSqlQuery query(m_database);
    query.prepare("SELECT id FROM users WHERE username = :username AND password = :password AND is_active = 1");
    query.bindValue(":username", username.trimmed().toLower());
    query.bindValue(":password", hashedPassword);

    if (!query.exec()) {
        QString error = QString("Authentication query failed: %1").arg(query.lastError().text());
        emit errorOccurred(error);
        qDebug() << error;
        return false;
    }

    bool authenticated = query.next();
    
    if (authenticated) {
        // Update last login time
        updateLastLogin(username);
        qDebug() << "User authenticated successfully:" << username;
    } else {
        qDebug() << "Authentication failed for user:" << username;
    }

    return authenticated;
}

UserInfo DatabaseManager::getUserInfo(const QString &username)
{
    UserInfo userInfo = {0, "", "", "", false};
    
    if (!isConnected()) {
        emit errorOccurred("Not connected to database");
        return userInfo;
    }

    QSqlQuery query(m_database);
    query.prepare("SELECT id, username, email, full_name, is_active FROM users WHERE username = :username");
    query.bindValue(":username", username.trimmed().toLower());

    if (!query.exec()) {
        QString error = QString("Get user info query failed: %1").arg(query.lastError().text());
        emit errorOccurred(error);
        return userInfo;
    }

    if (query.next()) {
        userInfo.id = query.value("id").toInt();
        userInfo.username = query.value("username").toString();
        userInfo.email = query.value("email").toString();
        userInfo.fullName = query.value("full_name").toString();
        userInfo.isActive = query.value("is_active").toBool();
    }

    return userInfo;
}

bool DatabaseManager::createUser(const QString &username, const QString &password,
                                const QString &email, const QString &fullName)
{
    if (!isConnected()) {
        emit errorOccurred("Not connected to database");
        return false;
    }

    QString hashedPassword = hashPassword(password);
    
    QSqlQuery query(m_database);
    query.prepare("INSERT INTO users (username, password, email, full_name, created_at, is_active) "
                 "VALUES (:username, :password, :email, :fullName, :createdAt, 1)");
    query.bindValue(":username", username.trimmed().toLower());
    query.bindValue(":password", hashedPassword);
    query.bindValue(":email", email.trimmed().toLower());
    query.bindValue(":fullName", fullName.trimmed());
    query.bindValue(":createdAt", QDateTime::currentDateTime());

    if (!query.exec()) {
        QString error = QString("Create user query failed: %1").arg(query.lastError().text());
        emit errorOccurred(error);
        return false;
    }

    qDebug() << "User created successfully:" << username;
    return true;
}

bool DatabaseManager::updateLastLogin(const QString &username)
{
    if (!isConnected()) {
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare("UPDATE users SET last_login = :lastLogin WHERE username = :username");
    query.bindValue(":lastLogin", QDateTime::currentDateTime());
    query.bindValue(":username", username.trimmed().toLower());

    if (!query.exec()) {
        qDebug() << "Failed to update last login:" << query.lastError().text();
        return false;
    }

    return true;
}

bool DatabaseManager::createTables()
{
    if (!isConnected()) {
        return false;
    }

    // Check if users table exists
    if (tableExists("users")) {
        return true; // Table already exists
    }

    QSqlQuery query(m_database);
    
    // Create users table with simpler syntax for ODBC compatibility
    QString createUsersTable = 
        "CREATE TABLE users ("
        "id INT AUTO_INCREMENT PRIMARY KEY, "
        "username VARCHAR(50) UNIQUE NOT NULL, "
        "password VARCHAR(255) NOT NULL, "
        "email VARCHAR(100) UNIQUE NOT NULL, "
        "full_name VARCHAR(100) NOT NULL, "
        "created_at DATETIME NOT NULL, "
        "last_login DATETIME NULL, "
        "is_active BOOLEAN DEFAULT TRUE"
        ")";

    if (!query.exec(createUsersTable)) {
        QString error = QString("Failed to create users table: %1").arg(query.lastError().text());
        emit errorOccurred(error);
        qDebug() << error;
        return false;
    }

    // Create indexes separately to avoid ODBC issues
    query.exec("CREATE INDEX idx_username ON users(username)");
    query.exec("CREATE INDEX idx_email ON users(email)");

    // Insert default admin user if table is empty
    query.prepare("SELECT COUNT(*) FROM users");
    if (query.exec() && query.next() && query.value(0).toInt() == 0) {
        // Create default admin user
        createUser("admin", "password", "admin@localhost.com", "System Administrator");
        createUser("user", "1234", "user@localhost.com", "Regular User");
        qDebug() << "Default users created: admin/password and user/1234";
    }

    qDebug() << "Database tables created successfully";
    return true;
}

bool DatabaseManager::tableExists(const QString &tableName)
{
    if (!isConnected()) {
        return false;
    }

    QSqlQuery query(m_database);
    
    // Use a more ODBC-compatible way to check table existence
    query.prepare("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = ? AND table_name = ?");
    query.addBindValue("w2r_login");
    query.addBindValue(tableName);

    if (!query.exec()) {
        // Fallback: try to select from the table
        QSqlQuery fallbackQuery(m_database);
        QString testQuery = QString("SELECT 1 FROM %1 LIMIT 1").arg(tableName);
        return fallbackQuery.exec(testQuery);
    }

    return query.next() && query.value(0).toInt() > 0;
}

QString DatabaseManager::hashPassword(const QString &password) const
{
    // Use SHA-256 hashing with salt
    QByteArray salt = "LoginPage_Salt_2025"; // In production, use random salt per user
    QByteArray data = password.toUtf8() + salt;
    QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    return hash.toHex();
}

bool DatabaseManager::executeQuery(const QString &queryString, const QVariantList &bindings)
{
    if (!isConnected()) {
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(queryString);

    for (int i = 0; i < bindings.size(); ++i) {
        query.bindValue(i, bindings.at(i));
    }

    if (!query.exec()) {
        QString error = QString("Query execution failed: %1").arg(query.lastError().text());
        emit errorOccurred(error);
        return false;
    }

    return true;
}
