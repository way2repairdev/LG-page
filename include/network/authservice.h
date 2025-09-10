#ifndef AUTHSERVICE_H
#define AUTHSERVICE_H

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDateTime>
#include <QTimer>
#include <optional>

struct AuthAwsCreds {
    QString accessKeyId;
    QString secretAccessKey;
    QString sessionToken; // optional
    QString region;
    QString bucket;
    QString endpoint; // optional (for S3-compatible)
};

struct AuthUserInfo {
    QString username;
    QString fullName;
    QString email;
    QString plan;
    bool isActivated;
    QString planExpiry;
};

struct AuthResult {
    QString token; // e.g. JWT from server
    AuthUserInfo user;
    AuthAwsCreds aws;
    QDateTime expiresAt; // Token expiration time
    bool isValid() const { return !token.isEmpty() && expiresAt > QDateTime::currentDateTime(); }
};

class AuthService : public QObject {
    Q_OBJECT
public:
    explicit AuthService(QObject* parent = nullptr);
    ~AuthService() override;

    void setBaseUrl(const QString& baseUrl); // e.g. http://localhost:3000
    QString baseUrl() const;

    // Authentication methods
    void login(const QString& username, const QString& password);
    void validateToken(const QString& token);
    void refreshToken(const QString& token);
    
    // Token management
    void setAuthToken(const QString& token);
    QString authToken() const;
    bool isTokenValid() const;
    
    // Secure storage
    void saveAuthResult(const AuthResult& result);
    AuthResult loadAuthResult();
    void clearAuthResult();

signals:
    void loginFinished(bool success, const AuthResult& result, const QString& errorMessage);
    void tokenValidated(bool valid, const AuthResult& result);
    void tokenRefreshed(bool success, const AuthResult& result);
    void tokenExpired();

private slots:
    void checkTokenExpiry();

private:
    QNetworkAccessManager m_net;
    QString m_baseUrl;
    QString m_currentToken;
    QDateTime m_tokenExpiry;
    QTimer* m_tokenTimer;
    
    void startTokenExpiryTimer();
    void stopTokenExpiryTimer();
    QNetworkRequest createSecureRequest(const QUrl& url, const QString& token = QString());
};

#endif // AUTHSERVICE_H
