#include "network/authservice.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrlQuery>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QCryptographicHash>
#include <QSslConfiguration>
#include <QTimer>

AuthService::AuthService(QObject* parent) 
    : QObject(parent)
    , m_tokenTimer(new QTimer(this))
{
    connect(m_tokenTimer, &QTimer::timeout, this, &AuthService::checkTokenExpiry);
    
    // Load any existing auth result
    const auto result = loadAuthResult();
    if (result.isValid()) {
        m_currentToken = result.token;
        m_tokenExpiry = result.expiresAt;
        startTokenExpiryTimer();
    }
}

AuthService::~AuthService() = default;

void AuthService::setBaseUrl(const QString& baseUrl) { 
    m_baseUrl = baseUrl.trimmed(); 
}

QString AuthService::baseUrl() const { 
    return m_baseUrl; 
}

void AuthService::login(const QString& username, const QString& password)
{
    // Defensive: ensure base URL provided
    const QString root = m_baseUrl.isEmpty() ? QStringLiteral("http://localhost:3000") : m_baseUrl;
    const QUrl url(root + "/auth/login");
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject body;
    body["username"] = username;
    body["password"] = password;
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = m_net.post(req, payload);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        AuthResult result; QString err;
        bool ok = false;
        
        // Always read the response body, even for HTTP error status codes
        const QByteArray data = reply->readAll();
        const int httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        
        // Check for network errors (connection issues, etc.)
        if (reply->error() != QNetworkReply::NoError && httpStatusCode == 0) {
            // Network/connection error - no valid HTTP response
            err = QString("Network error: %1").arg(reply->errorString());
        } else {
            // We have an HTTP response (success or error status) - try to parse JSON
            QJsonParseError pe{};
            const auto doc = QJsonDocument::fromJson(data, &pe);
            if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
                err = pe.errorString().isEmpty() ? QStringLiteral("Invalid JSON response") : pe.errorString();
            } else {
                const QJsonObject obj = doc.object();
                const bool success = obj.value("success").toBool(false);
                if (!success) {
                    const QString message = obj.value("message").toString(QStringLiteral("Login failed"));
                    const QString code = obj.value("code").toString();
                    
                    // Handle specific plan validation errors
                    if (code == "FREE_PLAN_RESTRICTION") {
                        err = "Free Plan Access Restricted\n\n" + message;
                    } else if (code == "ACCOUNT_NOT_ACTIVATED") {
                        err = "Account Not Activated\n\n" + message;
                    } else if (code == "PLAN_EXPIRED") {
                        err = "Premium Plan Expired\n\n" + message;
                    } else if (code == "INVALID_PLAN") {
                        err = "Invalid Plan Type\n\n" + message + "\n\nPlease contact support for assistance.";
                    } else {
                        err = message;
                    }
                } else {
                    ok = true;
                    result.token = obj.value("token").toString();
                    
                    // Parse token expiration
                    const QString expiresAtStr = obj.value("expiresAt").toString();
                    if (!expiresAtStr.isEmpty()) {
                        result.expiresAt = QDateTime::fromString(expiresAtStr, Qt::ISODate);
                    } else {
                        // Fallback: assume 2 hour expiry if not provided
                        result.expiresAt = QDateTime::currentDateTime().addSecs(2 * 60 * 60);
                    }
                    
                    // user
                    if (obj.contains("user") && obj.value("user").isObject()) {
                        const auto uo = obj.value("user").toObject();
                        result.user.username = uo.value("username").toString();
                        result.user.fullName = uo.value("fullName").toString();
                        result.user.email = uo.value("email").toString();
                        result.user.plan = uo.value("plan").toString();
                        result.user.isActivated = uo.value("isActivated").toBool();
                        result.user.planExpiry = uo.value("planExpiry").toString();
                    }
                    // aws credentials
                    if (obj.contains("aws") && obj.value("aws").isObject()) {
                        const auto ao = obj.value("aws").toObject();
                        result.aws.accessKeyId = ao.value("accessKeyId").toString();
                        result.aws.secretAccessKey = ao.value("secretAccessKey").toString();
                        result.aws.sessionToken = ao.value("sessionToken").toString();
                        result.aws.region = ao.value("region").toString();
                        result.aws.bucket = ao.value("bucket").toString();
                        result.aws.endpoint = ao.value("endpoint").toString();
                    }
                    
                    // Update current token and save securely
                    if (ok) {
                        m_currentToken = result.token;
                        m_tokenExpiry = result.expiresAt;
                        saveAuthResult(result);
                        startTokenExpiryTimer();
                    }
                }
            }
        }
        reply->deleteLater();
        emit loginFinished(ok, result, err);
    });
}

QNetworkRequest AuthService::createSecureRequest(const QUrl& url, const QString& token) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setHeader(QNetworkRequest::UserAgentHeader, "W2R-Client/1.0");
    req.setRawHeader("Cache-Control", "no-cache");
    
    if (!token.isEmpty()) {
        req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(token).toUtf8());
    }
    
    // Enable SSL/TLS security
    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
    req.setSslConfiguration(sslConfig);
    
    return req;
}

void AuthService::setAuthToken(const QString& token) {
    m_currentToken = token;
}

QString AuthService::authToken() const {
    return m_currentToken;
}

bool AuthService::isTokenValid() const {
    return !m_currentToken.isEmpty() && m_tokenExpiry > QDateTime::currentDateTime();
}

void AuthService::saveAuthResult(const AuthResult& result) {
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/secure_auth.ini", QSettings::IniFormat);
    
    // Encrypt sensitive data before storage (simple XOR for demo - use proper encryption in production)
    const QString key = "W2R_AUTH_KEY"; // In production, use a proper key derivation
    auto encrypt = [&key](const QString& data) -> QString {
        if (data.isEmpty()) return QString();
        QByteArray bytes = data.toUtf8();
        for (int i = 0; i < bytes.size(); ++i) {
            bytes[i] = bytes[i] ^ key[i % key.length()].toLatin1();
        }
        return bytes.toBase64();
    };
    
    settings.beginGroup("auth");
    settings.setValue("token", encrypt(result.token));
    settings.setValue("expires", result.expiresAt.toString(Qt::ISODate));
    settings.setValue("username", result.user.username); // Username is not sensitive
    settings.setValue("fullName", result.user.fullName);
    settings.setValue("email", result.user.email);
    
    // AWS credentials (encrypted)
    settings.setValue("aws_access_key", encrypt(result.aws.accessKeyId));
    settings.setValue("aws_secret_key", encrypt(result.aws.secretAccessKey));
    settings.setValue("aws_session_token", encrypt(result.aws.sessionToken));
    settings.setValue("aws_region", result.aws.region);
    settings.setValue("aws_bucket", result.aws.bucket);
    settings.setValue("aws_endpoint", result.aws.endpoint);
    settings.endGroup();
    
    settings.sync();
}

AuthResult AuthService::loadAuthResult() {
    AuthResult result;
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/secure_auth.ini", QSettings::IniFormat);
    
    if (!settings.contains("auth/token")) {
        return result; // Empty result
    }
    
    const QString key = "W2R_AUTH_KEY"; // Same key as used for encryption
    auto decrypt = [&key](const QString& data) -> QString {
        if (data.isEmpty()) return QString();
        QByteArray bytes = QByteArray::fromBase64(data.toUtf8());
        for (int i = 0; i < bytes.size(); ++i) {
            bytes[i] = bytes[i] ^ key[i % key.length()].toLatin1();
        }
        return QString::fromUtf8(bytes);
    };
    
    settings.beginGroup("auth");
    result.token = decrypt(settings.value("token").toString());
    result.expiresAt = QDateTime::fromString(settings.value("expires").toString(), Qt::ISODate);
    result.user.username = settings.value("username").toString();
    result.user.fullName = settings.value("fullName").toString();
    result.user.email = settings.value("email").toString();
    
    // AWS credentials (decrypt)
    result.aws.accessKeyId = decrypt(settings.value("aws_access_key").toString());
    result.aws.secretAccessKey = decrypt(settings.value("aws_secret_key").toString());
    result.aws.sessionToken = decrypt(settings.value("aws_session_token").toString());
    result.aws.region = settings.value("aws_region").toString();
    result.aws.bucket = settings.value("aws_bucket").toString();
    result.aws.endpoint = settings.value("aws_endpoint").toString();
    settings.endGroup();
    
    return result;
}

void AuthService::clearAuthResult() {
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/secure_auth.ini", QSettings::IniFormat);
    settings.remove("auth");
    settings.sync();
    
    m_currentToken.clear();
    m_tokenExpiry = QDateTime();
    stopTokenExpiryTimer();
}

void AuthService::startTokenExpiryTimer() {
    if (!m_tokenExpiry.isValid()) return;
    
    const qint64 msUntilExpiry = QDateTime::currentDateTime().msecsTo(m_tokenExpiry);
    if (msUntilExpiry > 0) {
        // Check every minute or when 90% of token lifetime has passed
        const qint64 checkInterval = std::min(60000LL, msUntilExpiry / 10);
        m_tokenTimer->start(static_cast<int>(checkInterval));
    }
}

void AuthService::stopTokenExpiryTimer() {
    m_tokenTimer->stop();
}

void AuthService::checkTokenExpiry() {
    if (m_tokenExpiry.isValid() && QDateTime::currentDateTime() >= m_tokenExpiry) {
        emit tokenExpired();
        clearAuthResult();
    } else if (m_tokenExpiry.isValid()) {
        // Check if we're within 30 minutes of expiry - trigger refresh
        const qint64 msUntilExpiry = QDateTime::currentDateTime().msecsTo(m_tokenExpiry);
        if (msUntilExpiry < 30 * 60 * 1000) { // 30 minutes
            refreshToken(m_currentToken);
        }
    }
}

void AuthService::validateToken(const QString& token) {
    const QString root = m_baseUrl.isEmpty() ? QStringLiteral("http://localhost:3000") : m_baseUrl;
    const QUrl url(root + "/auth/validate");
    QNetworkRequest req = createSecureRequest(url, token);
    
    QNetworkReply* reply = m_net.post(req, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        AuthResult result;
        bool valid = false;
        
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray data = reply->readAll();
            const auto doc = QJsonDocument::fromJson(data);
            if (doc.isObject()) {
                const QJsonObject obj = doc.object();
                valid = obj.value("success").toBool(false);
                if (valid) {
                    // Update expiry time if provided
                    const QString expiresAtStr = obj.value("expiresAt").toString();
                    if (!expiresAtStr.isEmpty()) {
                        result.expiresAt = QDateTime::fromString(expiresAtStr, Qt::ISODate);
                    }
                }
            }
        }
        
        reply->deleteLater();
        emit tokenValidated(valid, result);
    });
}

void AuthService::refreshToken(const QString& token) {
    const QString root = m_baseUrl.isEmpty() ? QStringLiteral("http://localhost:3000") : m_baseUrl;
    const QUrl url(root + "/auth/refresh");
    QNetworkRequest req = createSecureRequest(url, token);
    
    QNetworkReply* reply = m_net.post(req, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        AuthResult result;
        bool success = false;
        
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray data = reply->readAll();
            const auto doc = QJsonDocument::fromJson(data);
            if (doc.isObject()) {
                const QJsonObject obj = doc.object();
                success = obj.value("success").toBool(false);
                if (success) {
                    result.token = obj.value("token").toString();
                    const QString expiresAtStr = obj.value("expiresAt").toString();
                    if (!expiresAtStr.isEmpty()) {
                        result.expiresAt = QDateTime::fromString(expiresAtStr, Qt::ISODate);
                    }
                    
                    // Update current token
                    m_currentToken = result.token;
                    m_tokenExpiry = result.expiresAt;
                    
                    // Load existing auth result and update token
                    AuthResult fullResult = loadAuthResult();
                    fullResult.token = result.token;
                    fullResult.expiresAt = result.expiresAt;
                    saveAuthResult(fullResult);
                    
                    startTokenExpiryTimer();
                }
            }
        }
        
        reply->deleteLater();
        emit tokenRefreshed(success, result);
    });
}
