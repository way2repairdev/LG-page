#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QString>

struct UserInfo {
    QString username;
    QString fullName;
    QString email;
    bool isActive{true};
};

class DatabaseManager : public QObject {
    Q_OBJECT
public:
    explicit DatabaseManager(QObject* parent = nullptr) : QObject(parent) {}
    ~DatabaseManager() override {}

    // No-op DB connection; we'll authenticate via AuthService/Node.js
    bool connectToDatabase(const QString& hostname = QString(), const QString& database = QString(),
                           const QString& username = QString(), const QString& password = QString(), int port = 0)
    { Q_UNUSED(hostname) Q_UNUSED(database) Q_UNUSED(username) Q_UNUSED(password) Q_UNUSED(port) emit connectionStatusChanged(false); return false; }
    void disconnectFromDatabase() {}
    bool isConnected() const { return false; }

    // Legacy methods kept for interface compatibility (always return false/empty)
    bool authenticateUser(const QString& username, const QString& password) { Q_UNUSED(username) Q_UNUSED(password) return false; }
    UserInfo getUserInfo(const QString& username) { UserInfo u; u.username = username; return u; }

signals:
    void connectionStatusChanged(bool connected);
    void errorOccurred(const QString& message);
};

#endif // DATABASEMANAGER_H
