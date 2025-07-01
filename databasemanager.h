#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QDebug>
#include <QCryptographicHash>

struct UserInfo {
    int id;
    QString username;
    QString email;
    QString fullName;
    bool isActive;
};

class DatabaseManager : public QObject
{
    Q_OBJECT

public:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager();

    // Database connection methods
    bool connectToDatabase(const QString &hostname = "localhost",
                          const QString &database = "w2r_login",
                          const QString &username = "root",
                          const QString &password = "",
                          int port = 3306);
    void disconnectFromDatabase();
    bool isConnected() const;

    // User authentication methods
    bool authenticateUser(const QString &username, const QString &password);
    UserInfo getUserInfo(const QString &username);
    
    // User management methods
    bool createUser(const QString &username, const QString &password, 
                   const QString &email, const QString &fullName);
    bool updateLastLogin(const QString &username);
    
    // Database setup methods
    bool createTables();
    bool tableExists(const QString &tableName);

private:
    QSqlDatabase m_database;
    QString m_connectionName;
    
    // Helper methods
    QString hashPassword(const QString &password) const;
    bool executeQuery(const QString &queryString, const QVariantList &bindings = QVariantList());

signals:
    void connectionStatusChanged(bool connected);
    void errorOccurred(const QString &error);
};

#endif // DATABASEMANAGER_H
