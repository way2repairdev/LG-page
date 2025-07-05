// Sample C++ Configuration File
// This file contains configuration settings for the Way2Repair system

#ifndef CONFIG_H
#define CONFIG_H

#include <QString>
#include <QMap>

class AppConfig {
public:
    static AppConfig& getInstance();
    
    // Database configuration
    QString getDatabasePath() const;
    void setDatabasePath(const QString &path);
    
    // Network configuration
    int getServerPort() const;
    void setServerPort(int port);
    
    // UI configuration
    bool isDebugMode() const;
    void setDebugMode(bool enabled);
    
private:
    AppConfig() = default;
    
    QString m_databasePath = "data/w2r_login.db";
    int m_serverPort = 8080;
    bool m_debugMode = false;
    
    QMap<QString, QString> m_customSettings;
};

#endif // CONFIG_H
