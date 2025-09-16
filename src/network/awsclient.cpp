#include "network/awsclient.h"
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QXmlStreamReader>
#include <QSet>
#include <memory>
#include <QPointer>

#ifdef HAVE_AWS_SDK
// AWS SDK
#include <aws/core/Aws.h>
#include <aws/core/utils/memory/stl/AWSString.h>
#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/s3/model/GetObjectRequest.h>

struct AwsClient::Impl {
    Aws::SDKOptions options;
    std::shared_ptr<Aws::S3::S3Client> s3;
    QString bucket;
    QString region;
    QString endpointOverride;
    bool initialized{false};
    QString lastError;
    
    // Server-proxied mode
    bool serverMode{false};
    QString serverUrl;
    QString authToken;
    std::unique_ptr<QNetworkAccessManager> networkManager;
    // Track in-flight requests for cancellation
    QPointer<QNetworkReply> currentAuthReply;
    QPointer<QNetworkReply> currentListReply;
    QPointer<QNetworkReply> currentDownloadReply;

    Impl() {
        options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Fatal;
        Aws::InitAPI(options);
        initialized = true;
        networkManager = std::make_unique<QNetworkAccessManager>();
    }
    
    ~Impl() {
        if (initialized) Aws::ShutdownAPI(options);
    }
};

AwsClient::AwsClient() : d(new Impl) {}
AwsClient::~AwsClient() { delete d; }

bool AwsClient::loadFromEnv() {
    // Direct AWS credential loading from environment disabled
    // Only server-proxied mode is supported
    return false;
}

void AwsClient::setCredentials(const QString& accessKey, const QString& secretKey, const QString& region, const QString& sessionToken) {
    Q_UNUSED(accessKey)
    Q_UNUSED(secretKey)
    Q_UNUSED(sessionToken)
    d->region = region;
    d->lastError = "Direct AWS credentials not supported - use server-proxied mode only";
    // Clear any existing S3 client
    d->s3.reset();
}

void AwsClient::setBucket(const QString& bucket) { d->bucket = bucket; }
void AwsClient::setEndpointOverride(const QString& endpoint) { 
    d->endpointOverride = endpoint;
    // Note: endpoint override only used in server-proxied mode now
}

void AwsClient::setServerMode(bool enabled, const QString& serverUrl, const QString& authToken) {
    d->serverMode = enabled;
    d->serverUrl = serverUrl;
    d->authToken = authToken;
}

bool AwsClient::isServerMode() const {
    return d->serverMode;
}

bool AwsClient::isReady() const {
    // Only server-proxied mode is supported
    return d->serverMode && !d->serverUrl.isEmpty() && !d->authToken.isEmpty() && !d->bucket.isEmpty();
}

std::optional<QVector<AwsListEntry>> AwsClient::list(const QString& prefix, int maxKeys) {
    if (!isReady()) { d->lastError = QStringLiteral("Client not configured for server mode"); return std::nullopt; }
    
    // Only server-proxied mode is supported
    return listViaServer(prefix, maxKeys);
}

std::optional<QString> AwsClient::downloadToFile(const QString& key, const QString& localPath) {
    if (!isReady()) { 
        d->lastError = QStringLiteral("Client not configured for server mode"); 
        return std::nullopt; 
    }
    
    // Use server-proxied download
    auto data = downloadViaServer(key);
    if (!data.has_value()) {
        return std::nullopt;
    }
    
    // Ensure directory exists
    QFileInfo fi(localPath);
    QDir().mkpath(fi.absolutePath());
    
    // Write data to file
    QFile file(localPath);
    if (!file.open(QIODevice::WriteOnly)) {
        d->lastError = QString("Cannot write to file: %1").arg(localPath);
        return std::nullopt;
    }
    
    file.write(data.value());
    file.close();
    return localPath;
}

std::optional<QByteArray> AwsClient::downloadToMemory(const QString& key) {
    if (!isReady()) { 
        d->lastError = QStringLiteral("Client not configured for server mode"); 
        return std::nullopt; 
    }
    
    // Use server-proxied download
    return downloadViaServer(key);
}

QString AwsClient::cachePathForKey(const QString& key) const {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QString safe = key;
    // Remove any traversal
    safe.replace("..", "");
    // Normalize slashes
    safe.replace('\\', '/');
    return QDir(base).absoluteFilePath(QString("aws/%1").arg(safe));
}

QString AwsClient::bucket() const { return d->bucket; }
QString AwsClient::lastError() const { return d->lastError; }

// Private helper for server-proxied operations
std::optional<QVector<AwsListEntry>> AwsClient::listViaServer(const QString& prefix, int maxKeys) {
    if (!d->serverMode || d->serverUrl.isEmpty() || d->authToken.isEmpty()) {
        d->lastError = "Server mode not properly configured";
        return std::nullopt;
    }
    
    // Step 1: Get pre-signed URL from server
    const QUrl authUrl(d->serverUrl + "/auth/s3/list");
    QNetworkRequest authRequest(authUrl);
    authRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    authRequest.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(d->authToken).toUtf8());
    authRequest.setRawHeader("Cache-Control", "no-cache");
    
    QJsonObject authBody;
    authBody["prefix"] = prefix;
    authBody["maxKeys"] = maxKeys;
    authBody["delimiter"] = "/";
    const QByteArray authPayload = QJsonDocument(authBody).toJson(QJsonDocument::Compact);
    
    QNetworkReply* authReply = d->networkManager->post(authRequest, authPayload);
    d->currentAuthReply = authReply;
    
    // Synchronous wait for auth response
    QEventLoop authLoop;
    QObject::connect(authReply, &QNetworkReply::finished, &authLoop, &QEventLoop::quit);
    authLoop.exec();
    
    if (authReply->error() != QNetworkReply::NoError) {
        d->lastError = QString("Server auth request failed: %1").arg(authReply->errorString());
        authReply->deleteLater();
        d->currentAuthReply = nullptr;
        return std::nullopt;
    }
    
    const QByteArray authData = authReply->readAll();
    authReply->deleteLater();
    
    QJsonParseError pe;
    const QJsonDocument authDoc = QJsonDocument::fromJson(authData, &pe);
    if (pe.error != QJsonParseError::NoError) {
        d->lastError = QString("Invalid JSON response from auth: %1").arg(pe.errorString());
        return std::nullopt;
    }
    
    const QJsonObject authObj = authDoc.object();
    if (!authObj.value("success").toBool()) {
        d->lastError = authObj.value("message").toString("Server auth request failed");
        return std::nullopt;
    }
    
    // Step 2: Get the pre-signed URL from server response
    const QString presignedUrl = authObj.value("presignedUrl").toString();
    if (presignedUrl.isEmpty()) {
        d->lastError = "Server did not return a list pre-signed URL";
        return std::nullopt;
    }
    
    // Step 3: Use the pre-signed URL to list S3 objects
    QNetworkRequest listRequest{QUrl(presignedUrl)};
    QNetworkReply* listReply = d->networkManager->get(listRequest);
    d->currentListReply = listReply;
    
    // Synchronous wait for list response
    QEventLoop listLoop;
    QObject::connect(listReply, &QNetworkReply::finished, &listLoop, &QEventLoop::quit);
    listLoop.exec();
    
    if (listReply->error() != QNetworkReply::NoError) {
        d->lastError = QString("S3 list request failed: %1").arg(listReply->errorString());
        listReply->deleteLater();
        d->currentListReply = nullptr;
        return std::nullopt;
    }
    
    const QByteArray listData = listReply->readAll();
    listReply->deleteLater();
    d->currentListReply = nullptr;
    
    // Step 4: Parse S3 XML response robustly using QXmlStreamReader
    QVector<AwsListEntry> out;
    QXmlStreamReader xml(listData);
    QString currentText;
    AwsListEntry currentFile;
    QSet<QString> seenDirs;

    auto emitDir = [&](const QString &dirKey){
        if (dirKey.isEmpty()) return;
        if (seenDirs.contains(dirKey)) return;
        if (dirKey == prefix) return; // skip echo of current prefix
        seenDirs.insert(dirKey);
        AwsListEntry dir;
        dir.isDir = true;
        dir.key = dirKey;
        dir.name = dirKey.endsWith('/') ? QString(dirKey).chopped(1).split('/').last() : dirKey.split('/').last();
        if (dir.name.isEmpty()) dir.name = dirKey;
        dir.size = 0;
        out.push_back(dir);
    };

    while (!xml.atEnd()) {
        switch (xml.tokenType()) {
            case QXmlStreamReader::StartElement: {
                if (xml.name() == QLatin1String("Contents")) {
                    currentFile = AwsListEntry{};
                    currentFile.isDir = false;
                    currentFile.size = 0;
                }
                currentText.clear();
                break;
            }
            case QXmlStreamReader::Characters:
                if (!xml.isWhitespace()) currentText += xml.text().toString();
                break;
            case QXmlStreamReader::EndElement: {
                if (xml.name() == QLatin1String("Key")) {
                    currentFile.key = currentText;
                } else if (xml.name() == QLatin1String("Size")) {
                    currentFile.size = currentText.toLongLong();
                } else if (xml.name() == QLatin1String("CommonPrefixes")) {
                    // handled by Prefix inside it
                } else if (xml.name() == QLatin1String("Prefix")) {
                    // Directory prefix (if present in response)
                    emitDir(currentText);
                } else if (xml.name() == QLatin1String("Contents")) {
                    if (!currentFile.key.isEmpty()) {
                        // Derive directory from key if any
                        int slash = currentFile.key.indexOf('/');
                        if (slash != -1) {
                            const QString dirKey = currentFile.key.left(currentFile.key.lastIndexOf('/') + 1);
                            emitDir(dirKey);
                        }
                        currentFile.name = currentFile.key.split('/').last();
                        if (!currentFile.name.isEmpty()) {
                            out.push_back(currentFile);
                        }
                    }
                }
                currentText.clear();
                break;
            }
            default:
                break;
        }
        xml.readNext();
    }

    if (xml.hasError()) {
        d->lastError = QStringLiteral("Failed to parse S3 XML: %1").arg(xml.errorString());
        return std::nullopt;
    }

    return out;
}

// Private helper for server-proxied download operations
std::optional<QByteArray> AwsClient::downloadViaServer(const QString& key) {
    if (!d->serverMode || d->serverUrl.isEmpty() || d->authToken.isEmpty()) {
        d->lastError = "Server mode not properly configured";
        return std::nullopt;
    }
    
    const QUrl url(d->serverUrl + "/auth/s3/download");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(d->authToken).toUtf8());
    request.setRawHeader("Cache-Control", "no-cache");
    
    QJsonObject body;
    body["key"] = key;
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    
    QNetworkReply* reply = d->networkManager->post(request, payload);
    d->currentAuthReply = reply;
    
    // Synchronous wait for response
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    if (reply->error() != QNetworkReply::NoError) {
        d->lastError = QString("Server download request failed: %1").arg(reply->errorString());
        reply->deleteLater();
        d->currentAuthReply = nullptr;
        return std::nullopt;
    }
    d->currentAuthReply = nullptr;
    
    const QByteArray data = reply->readAll();
    reply->deleteLater();
    
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError) {
        d->lastError = QString("Invalid JSON response: %1").arg(pe.errorString());
        return std::nullopt;
    }
    
    const QJsonObject obj = doc.object();
    if (!obj.value("success").toBool()) {
        d->lastError = obj.value("message").toString("Server download request failed");
        return std::nullopt;
    }
    
    // The server should return a pre-signed URL for download
    const QString presignedUrl = obj.value("presignedUrl").toString();
    if (presignedUrl.isEmpty()) {
        d->lastError = "Server did not return a download URL";
        return std::nullopt;
    }
    
    // Download the file using the pre-signed URL
    QNetworkRequest downloadRequest{QUrl(presignedUrl)};
    QNetworkReply* downloadReply = d->networkManager->get(downloadRequest);
    d->currentDownloadReply = downloadReply;
    
    // Synchronous wait for download
    QEventLoop downloadLoop;
    QObject::connect(downloadReply, &QNetworkReply::finished, &downloadLoop, &QEventLoop::quit);
    downloadLoop.exec();
    
    if (downloadReply->error() != QNetworkReply::NoError) {
        d->lastError = QString("File download failed: %1").arg(downloadReply->errorString());
        downloadReply->deleteLater();
        d->currentDownloadReply = nullptr;
        return std::nullopt;
    }
    d->currentDownloadReply = nullptr;
    
    const QByteArray fileData = downloadReply->readAll();
    downloadReply->deleteLater();
    
    return fileData;
}

#else

struct AwsClient::Impl {
    QString bucket;
    QString region;
    QString endpointOverride;
    QString lastError;
    
    // Server-proxied mode
    bool serverMode{false};
    QString serverUrl;
    QString authToken;
    std::unique_ptr<QNetworkAccessManager> networkManager;
    // Track in-flight requests for cancellation
    QPointer<QNetworkReply> currentAuthReply;
    QPointer<QNetworkReply> currentListReply;
    QPointer<QNetworkReply> currentDownloadReply;
    
    Impl() {
        networkManager = std::make_unique<QNetworkAccessManager>();
    }
};

AwsClient::AwsClient() : d(new Impl) {}
AwsClient::~AwsClient() { delete d; }

bool AwsClient::loadFromEnv() {
    // Direct AWS credential loading from environment disabled
    // Only server-proxied mode is supported
    return false;
}

void AwsClient::setCredentials(const QString& accessKey, const QString& secretKey, const QString& region, const QString& sessionToken) {
    Q_UNUSED(accessKey)
    Q_UNUSED(secretKey)
    Q_UNUSED(sessionToken)
    d->region = region;
    d->lastError = "Direct AWS credentials not supported - use server-proxied mode only";
}
void AwsClient::setBucket(const QString& bucket) { d->bucket = bucket; }
void AwsClient::setEndpointOverride(const QString& endpoint) { 
    d->endpointOverride = endpoint;
    // Note: endpoint override only used in server-proxied mode now
}

void AwsClient::setServerMode(bool enabled, const QString& serverUrl, const QString& authToken) {
    d->serverMode = enabled;
    d->serverUrl = serverUrl;
    d->authToken = authToken;
}

bool AwsClient::isServerMode() const {
    return d->serverMode;
}

bool AwsClient::isReady() const { 
    // Only server-proxied mode is supported
    return d->serverMode && !d->serverUrl.isEmpty() && !d->authToken.isEmpty() && !d->bucket.isEmpty();
}

std::optional<QVector<AwsListEntry>> AwsClient::list(const QString& prefix, int maxKeys) {
    if (!isReady()) { 
        d->lastError = QStringLiteral("Client not configured for server mode"); 
        return std::nullopt; 
    }
    
    // Only server-proxied mode is supported
    return listViaServer(prefix, maxKeys);
}

std::optional<QString> AwsClient::downloadToFile(const QString& key, const QString& localPath) {
    if (!isReady()) { 
        d->lastError = QStringLiteral("Client not configured for server mode"); 
        return std::nullopt; 
    }
    
    // Use server-proxied download
    auto data = downloadViaServer(key);
    if (!data.has_value()) {
        return std::nullopt;
    }
    
    // Ensure directory exists
    QFileInfo fi(localPath);
    QDir().mkpath(fi.absolutePath());
    
    // Write data to file
    QFile file(localPath);
    if (!file.open(QIODevice::WriteOnly)) {
        d->lastError = QString("Cannot write to file: %1").arg(localPath);
        return std::nullopt;
    }
    
    file.write(data.value());
    file.close();
    return localPath;
}

std::optional<QByteArray> AwsClient::downloadToMemory(const QString& key) {
    if (!isReady()) { 
        d->lastError = QStringLiteral("Client not configured for server mode"); 
        return std::nullopt; 
    }
    
    // Use server-proxied download
    return downloadViaServer(key);
}
QString AwsClient::cachePathForKey(const QString& key) const {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QString safe = key;
    safe.replace("..", "");
    safe.replace('\\', '/');
    return QDir(base).absoluteFilePath(QString("aws/%1").arg(safe));
}

QString AwsClient::bucket() const { return d->bucket; }
QString AwsClient::lastError() const { return d->lastError; }

// Private helper for server-proxied operations (non-AWS version)
std::optional<QVector<AwsListEntry>> AwsClient::listViaServer(const QString& prefix, int maxKeys) {
    if (!d->serverMode || d->serverUrl.isEmpty() || d->authToken.isEmpty()) {
        d->lastError = "Server mode not properly configured";
        return std::nullopt;
    }
    
    // Step 1: Get pre-signed URL from server
    const QUrl authUrl(d->serverUrl + "/auth/s3/list");
    QNetworkRequest authRequest(authUrl);
    authRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    authRequest.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(d->authToken).toUtf8());
    authRequest.setRawHeader("Cache-Control", "no-cache");
    
    QJsonObject authBody;
    authBody["prefix"] = prefix;
    authBody["maxKeys"] = maxKeys;
    authBody["delimiter"] = "/";
    const QByteArray authPayload = QJsonDocument(authBody).toJson(QJsonDocument::Compact);
    
    QNetworkReply* authReply = d->networkManager->post(authRequest, authPayload);
    d->currentAuthReply = authReply;
    
    // Synchronous wait for auth response
    QEventLoop authLoop;
    QObject::connect(authReply, &QNetworkReply::finished, &authLoop, &QEventLoop::quit);
    authLoop.exec();
    
    if (authReply->error() != QNetworkReply::NoError) {
        d->lastError = QString("Server auth request failed: %1").arg(authReply->errorString());
        authReply->deleteLater();
        d->currentAuthReply = nullptr;
        return std::nullopt;
    }
    
    const QByteArray authData = authReply->readAll();
    authReply->deleteLater();
    d->currentAuthReply = nullptr;
    
    QJsonParseError pe;
    const QJsonDocument authDoc = QJsonDocument::fromJson(authData, &pe);
    if (pe.error != QJsonParseError::NoError) {
        d->lastError = QString("Invalid JSON response from auth: %1").arg(pe.errorString());
        return std::nullopt;
    }
    
    const QJsonObject authObj = authDoc.object();
    if (!authObj.value("success").toBool()) {
        d->lastError = authObj.value("message").toString("Server auth request failed");
        return std::nullopt;
    }
    
    // Step 2: Get the pre-signed URL from server response
    const QString presignedUrl = authObj.value("presignedUrl").toString();
    if (presignedUrl.isEmpty()) {
        d->lastError = "Server did not return a list pre-signed URL";
        return std::nullopt;
    }
    
    // Step 3: Use the pre-signed URL to list S3 objects
    QNetworkRequest listRequest{QUrl(presignedUrl)};
    QNetworkReply* listReply = d->networkManager->get(listRequest);
    d->currentListReply = listReply;
    
    // Synchronous wait for list response
    QEventLoop listLoop;
    QObject::connect(listReply, &QNetworkReply::finished, &listLoop, &QEventLoop::quit);
    listLoop.exec();
    
    if (listReply->error() != QNetworkReply::NoError) {
        d->lastError = QString("S3 list request failed: %1").arg(listReply->errorString());
        listReply->deleteLater();
        d->currentListReply = nullptr;
        return std::nullopt;
    }
    
    const QByteArray listData = listReply->readAll();
    listReply->deleteLater();
    d->currentListReply = nullptr;
    
    // Step 4: Parse S3 XML response robustly using QXmlStreamReader
    QVector<AwsListEntry> out;
    QXmlStreamReader xml(listData);
    QString currentText;
    AwsListEntry currentFile;
    QSet<QString> seenDirs;

    auto emitDir = [&](const QString &dirKey){
        if (dirKey.isEmpty()) return;
        if (seenDirs.contains(dirKey)) return;
        if (dirKey == prefix) return; // skip echo of current prefix
        seenDirs.insert(dirKey);
        AwsListEntry dir;
        dir.isDir = true;
        dir.key = dirKey;
        dir.name = dirKey.endsWith('/') ? QString(dirKey).chopped(1).split('/').last() : dirKey.split('/').last();
        if (dir.name.isEmpty()) dir.name = dirKey;
        dir.size = 0;
        out.push_back(dir);
    };

    while (!xml.atEnd()) {
        switch (xml.tokenType()) {
            case QXmlStreamReader::StartElement: {
                if (xml.name() == QLatin1String("Contents")) {
                    currentFile = AwsListEntry{};
                    currentFile.isDir = false;
                    currentFile.size = 0;
                }
                currentText.clear();
                break;
            }
            case QXmlStreamReader::Characters:
                if (!xml.isWhitespace()) currentText += xml.text().toString();
                break;
            case QXmlStreamReader::EndElement: {
                if (xml.name() == QLatin1String("Key")) {
                    currentFile.key = currentText;
                } else if (xml.name() == QLatin1String("Size")) {
                    currentFile.size = currentText.toLongLong();
                } else if (xml.name() == QLatin1String("CommonPrefixes")) {
                    // handled by Prefix inside it
                } else if (xml.name() == QLatin1String("Prefix")) {
                    // Directory prefix (if present in response)
                    emitDir(currentText);
                } else if (xml.name() == QLatin1String("Contents")) {
                    if (!currentFile.key.isEmpty()) {
                        // Derive directory from key if any
                        int slash = currentFile.key.indexOf('/');
                        if (slash != -1) {
                            const QString dirKey = currentFile.key.left(currentFile.key.lastIndexOf('/') + 1);
                            emitDir(dirKey);
                        }
                        currentFile.name = currentFile.key.split('/').last();
                        if (!currentFile.name.isEmpty()) {
                            out.push_back(currentFile);
                        }
                    }
                }
                currentText.clear();
                break;
            }
            default:
                break;
        }
        xml.readNext();
    }

    if (xml.hasError()) {
        d->lastError = QStringLiteral("Failed to parse S3 XML: %1").arg(xml.errorString());
        return std::nullopt;
    }

    return out;
}

// Private helper for server-proxied download operations
std::optional<QByteArray> AwsClient::downloadViaServer(const QString& key) {
    if (!d->serverMode || d->serverUrl.isEmpty() || d->authToken.isEmpty()) {
        d->lastError = "Server mode not properly configured";
        return std::nullopt;
    }
    
    const QUrl url(d->serverUrl + "/auth/s3/download");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(d->authToken).toUtf8());
    request.setRawHeader("Cache-Control", "no-cache");
    
    QJsonObject body;
    body["key"] = key;
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    
    QNetworkReply* reply = d->networkManager->post(request, payload);
    d->currentAuthReply = reply;
    
    // Synchronous wait for response
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    if (reply->error() != QNetworkReply::NoError) {
        d->lastError = QString("Server download request failed: %1").arg(reply->errorString());
        reply->deleteLater();
        d->currentAuthReply = nullptr;
        return std::nullopt;
    }
    
    const QByteArray data = reply->readAll();
    reply->deleteLater();
    d->currentAuthReply = nullptr;
    
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError) {
        d->lastError = QString("Invalid JSON response: %1").arg(pe.errorString());
        return std::nullopt;
    }
    
    const QJsonObject obj = doc.object();
    if (!obj.value("success").toBool()) {
        d->lastError = obj.value("message").toString("Server download request failed");
        return std::nullopt;
    }
    
    // The server should return a pre-signed URL for download
    const QString presignedUrl = obj.value("presignedUrl").toString();
    if (presignedUrl.isEmpty()) {
        d->lastError = "Server did not return a download URL";
        return std::nullopt;
    }
    
    // Download the file using the pre-signed URL
    QNetworkRequest downloadRequest{QUrl(presignedUrl)};
    QNetworkReply* downloadReply = d->networkManager->get(downloadRequest);
    d->currentDownloadReply = downloadReply;
    
    // Synchronous wait for download
    QEventLoop downloadLoop;
    QObject::connect(downloadReply, &QNetworkReply::finished, &downloadLoop, &QEventLoop::quit);
    downloadLoop.exec();
    
    if (downloadReply->error() != QNetworkReply::NoError) {
        d->lastError = QString("File download failed: %1").arg(downloadReply->errorString());
        downloadReply->deleteLater();
        d->currentDownloadReply = nullptr;
        return std::nullopt;
    }
    
    const QByteArray fileData = downloadReply->readAll();
    downloadReply->deleteLater();
    d->currentDownloadReply = nullptr;
    
    return fileData;
}

#endif

// Unified cancellation method (available in both build configurations)
void AwsClient::cancelCurrentOperation() {
    // Abort any in-flight replies tracked in Impl (only meaningful in server-proxied mode)
    if (d && d->networkManager) {
    // These QPointers are only present when compiled with our headers; guard by dynamic checks
    // Use lambda to safely abort if non-null
    auto abortReply = [](QPointer<QNetworkReply>& r){ if (r) r->abort(); };
#ifdef HAVE_AWS_SDK
    abortReply(d->currentAuthReply);
    abortReply(d->currentListReply);
    abortReply(d->currentDownloadReply);
#else
    abortReply(d->currentAuthReply);
    abortReply(d->currentListReply);
    abortReply(d->currentDownloadReply);
#endif
    }
}
