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
#include <QCoreApplication>
#include "security/security_envelope.h"

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

// Local helper: append a line to build/tab_debug.txt next to the exe
static inline void appendTabDebug(const QString &msg) {
    const QString logPath = QCoreApplication::applicationDirPath() + "/tab_debug.txt";
    QFile f(logPath);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        const QString line = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz ") + msg + QLatin1Char('\n');
        f.write(line.toUtf8());
        f.flush();
    }
}

// Private helper for server-proxied operations
std::optional<QVector<AwsListEntry>> AwsClient::listViaServer(const QString& prefix, int maxKeys) {
    if (!d->serverMode || d->serverUrl.isEmpty() || d->authToken.isEmpty()) {
        d->lastError = "Server mode not properly configured";
        return std::nullopt;
    }
    appendTabDebug(QString("AWS list: serverUrl=%1 prefix='%2' maxKeys=%3 bucket=%4")
                   .arg(d->serverUrl, prefix).arg(maxKeys).arg(d->bucket));
    
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
        appendTabDebug(QString("AWS list auth error: %1").arg(d->lastError));
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
        appendTabDebug(QString("AWS list parse error: %1").arg(d->lastError));
        return std::nullopt;
    }
    
    const QJsonObject authObj = authDoc.object();
    if (!authObj.value("success").toBool()) {
        d->lastError = authObj.value("message").toString("Server auth request failed");
        appendTabDebug(QString("AWS list server reported failure: %1").arg(d->lastError));
        return std::nullopt;
    }
    
    // Step 2: Get the pre-signed URL from server response
    const QString presignedUrl = authObj.value("presignedUrl").toString();
    if (presignedUrl.isEmpty()) {
        d->lastError = "Server did not return a list pre-signed URL";
        appendTabDebug("AWS list: missing presignedUrl in server response");
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
        appendTabDebug(QString("AWS list S3 request error: %1").arg(d->lastError));
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
        appendTabDebug(QString("AWS list XML parse error: %1").arg(d->lastError));
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
    appendTabDebug(QString("AWS download: key='%1' serverUrl=%2 bucket=%3")
                   .arg(key, d->serverUrl, d->bucket));
    
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

    // Always capture HTTP status and body for diagnostics
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    const QString errStr = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
    reply->deleteLater();
    d->currentAuthReply = nullptr;

    if (reply->error() != QNetworkReply::NoError || httpStatus >= 400) {
        // Try to parse JSON error payload
        QString detail;
        QJsonParseError pe{};
        const QJsonDocument edoc = QJsonDocument::fromJson(data, &pe);
        if (pe.error == QJsonParseError::NoError && edoc.isObject()) {
            const auto o = edoc.object();
            const QString msg = o.value("message").toString();
            const QString code = o.value("error").toString(o.value("code").toString());
            const QString stage = o.value("stage").toString();
            if (!msg.isEmpty() || !code.isEmpty() || !stage.isEmpty()) {
                detail = QString(" msg='%1' code=%2%3")
                             .arg(msg)
                             .arg(code.isEmpty()?QStringLiteral("none"):code)
                             .arg(stage.isEmpty()?QString():QString(" stage=%1").arg(stage));
            }
        }
        if (detail.isEmpty() && !data.isEmpty()) {
            const auto snippet = QString::fromUtf8(data.left(256)).trimmed();
            if (!snippet.isEmpty()) detail = QString(" body='%1'").arg(snippet);
        }
        d->lastError = QString("Server download request failed: HTTP %1%2%3")
                            .arg(httpStatus > 0 ? QString::number(httpStatus) : QStringLiteral("?"))
                            .arg(errStr.isEmpty()?QString():QString(" %1").arg(errStr))
                            .arg(detail);
        appendTabDebug(QString("AWS download server error: %1").arg(d->lastError));
        return std::nullopt;
    }
    
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError) {
        d->lastError = QString("Invalid JSON response: %1").arg(pe.errorString());
        appendTabDebug(QString("AWS download parse error: %1").arg(d->lastError));
        return std::nullopt;
    }
    
    const QJsonObject obj = doc.object();
    if (!obj.value("success").toBool()) {
        d->lastError = obj.value("message").toString("Server download request failed");
        appendTabDebug(QString("AWS download server failure: %1").arg(d->lastError));
        return std::nullopt;
    }
    
    // Envelope-aware path: prefer encrypted delivery if provided
    const QJsonObject envelope = obj.value("envelope").toObject().isEmpty()
        ? obj.value("encryptMeta").toObject() : obj.value("envelope").toObject();
    const bool hasEnvelope = !envelope.isEmpty();
    const bool kmsEnabled = obj.value("kmsEnabled").toBool();
    if (kmsEnabled && !hasEnvelope) {
        d->lastError = QStringLiteral("Server indicated encrypted delivery but did not include envelope metadata");
        appendTabDebug("AWS download error: kmsEnabled=true but envelope missing");
        return std::nullopt;
    }

    // Extract URL with multiple aliases for safety
    QString downloadUrl = obj.value("ciphertextUrl").toString();
    if (downloadUrl.isEmpty()) downloadUrl = obj.value("downloadUrl").toString();
    if (downloadUrl.isEmpty()) downloadUrl = obj.value("presignedUrl").toString();
    if (downloadUrl.isEmpty()) downloadUrl = obj.value("cipherTextUrl").toString();
    if (downloadUrl.isEmpty()) downloadUrl = obj.value("ciphertext_url").toString();
    if (downloadUrl.isEmpty()) downloadUrl = obj.value("url").toString();
    if (downloadUrl.isEmpty()) downloadUrl = obj.value("Location").toString();
    if (downloadUrl.isEmpty()) {
        QStringList keys; keys.reserve(obj.keys().size());
        for (const auto &k : obj.keys()) keys << k;
        d->lastError = QStringLiteral("Server did not return a download URL (keys: %1)").arg(keys.join(","));
        appendTabDebug(QString("AWS download error: missing URL; response keys=[%1]").arg(keys.join(",")));
        return std::nullopt;
    }

    // Download bytes (ciphertext or plaintext depending on server)
    const QUrl dlUrl(downloadUrl);
    appendTabDebug(QString("AWS download presigned URL target: %1%2").arg(dlUrl.host(), dlUrl.path()));
    QNetworkRequest downloadRequest{dlUrl};
    QNetworkReply* downloadReply = d->networkManager->get(downloadRequest);
    d->currentDownloadReply = downloadReply;

    QEventLoop downloadLoop;
    QObject::connect(downloadReply, &QNetworkReply::finished, &downloadLoop, &QEventLoop::quit);
    downloadLoop.exec();

    if (downloadReply->error() != QNetworkReply::NoError) {
        d->lastError = QString("File download failed: %1").arg(downloadReply->errorString());
        appendTabDebug(QString("AWS download HTTP error: %1").arg(d->lastError));
        downloadReply->deleteLater();
        d->currentDownloadReply = nullptr;
        return std::nullopt;
    }

    QByteArray downloaded = downloadReply->readAll();
    downloadReply->deleteLater();
    d->currentDownloadReply = nullptr;

    if (!hasEnvelope) {
        // Plaintext mode - log head for quick triage
        if (downloaded.size() >= 8) {
            appendTabDebug(QString("AWS download plaintext: size=%1 head=%2")
                           .arg(downloaded.size())
                           .arg(QString::fromLatin1(downloaded.left(8).toHex(' '))));
        }
        return downloaded;
    }

    // Decrypt using SecurityEnvelope::decryptBuffer
    SecurityEnvelope::BufferInputs bi;
    bi.algorithm = envelope.value("algorithm").toString();
    const auto b64 = [](const QJsonObject& o, const char* k){ return QByteArray::fromBase64(o.value(k).toString().toUtf8()); };
    bi.encryptedData = downloaded; // ciphertext body
    bi.encryptedDataKey = b64(envelope, "encryptedDataKey");
    bi.iv = b64(envelope, "iv");
    bi.authTag = b64(envelope, "authTag");

    // AAD bound to JWT jti when available
    const QString jti = SecurityEnvelope::extractJtiFromJwt(d->authToken);
    bi.aad = jti.isEmpty() ? QByteArray() : jti.toUtf8();

    // AWS credentials for KMS Decrypt (from server response)
    const QJsonObject aws = obj.value("aws").toObject();
    bi.accessKeyId = aws.value("accessKeyId").toString();
    bi.secretAccessKey = aws.value("secretAccessKey").toString();
    bi.sessionToken = aws.value("sessionToken").toString();
    bi.region = aws.value("region").toString();
    if (bi.region.isEmpty()) bi.region = obj.value("region").toString();

    appendTabDebug(QString("AWS envelope: edk=%1 iv=%2 tag=%3 aad=%4")
                   .arg(bi.encryptedDataKey.size())
                   .arg(bi.iv.size())
                   .arg(bi.authTag.size())
                   .arg(bi.aad.size()));

    QString decErr;
    auto plainOpt = SecurityEnvelope::decryptBuffer(bi, &decErr);
    if (!plainOpt.has_value()) {
        d->lastError = decErr.isEmpty() ? QStringLiteral("Failed to decrypt file content") : decErr;
        appendTabDebug(QString("AWS decrypt failed: %1").arg(d->lastError));
        return std::nullopt;
    }

    const QByteArray& plain = plainOpt.value();
    if (plain.size() >= 8) {
        appendTabDebug(QString("AWS decrypted: size=%1 head=%2")
                       .arg(plain.size())
                       .arg(QString::fromLatin1(plain.left(8).toHex(' '))));
    }
    return plain;
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

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    const QString errStr = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
    reply->deleteLater();
    d->currentAuthReply = nullptr;

    if (reply->error() != QNetworkReply::NoError || httpStatus >= 400) {
        QString detail;
        QJsonParseError pe{};
        const QJsonDocument edoc = QJsonDocument::fromJson(data, &pe);
        if (pe.error == QJsonParseError::NoError && edoc.isObject()) {
            const auto o = edoc.object();
            const QString msg = o.value("message").toString();
            const QString code = o.value("error").toString(o.value("code").toString());
            const QString stage = o.value("stage").toString();
            if (!msg.isEmpty() || !code.isEmpty() || !stage.isEmpty()) {
                detail = QString(" msg='%1' code=%2%3")
                             .arg(msg)
                             .arg(code.isEmpty()?QStringLiteral("none"):code)
                             .arg(stage.isEmpty()?QString():QString(" stage=%1").arg(stage));
            }
        }
        if (detail.isEmpty() && !data.isEmpty()) {
            const auto snippet = QString::fromUtf8(data.left(256)).trimmed();
            if (!snippet.isEmpty()) detail = QString(" body='%1'").arg(snippet);
        }
        d->lastError = QString("Server download request failed: HTTP %1%2%3")
                            .arg(httpStatus > 0 ? QString::number(httpStatus) : QStringLiteral("?"))
                            .arg(errStr.isEmpty()?QString():QString(" %1").arg(errStr))
                            .arg(detail);
        return std::nullopt;
    }
    
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

    // Envelope-aware path: if server returns encryption metadata, prefer encrypted download
    const QJsonObject envelope = obj.value("envelope").toObject().isEmpty()
        ? obj.value("encryptMeta").toObject() : obj.value("envelope").toObject();
    const bool hasEnvelope = !envelope.isEmpty();
    const bool kmsEnabled = obj.value("kmsEnabled").toBool();
    if (kmsEnabled && !hasEnvelope) {
        d->lastError = QStringLiteral("Server indicated encrypted delivery but did not include envelope metadata");
        appendTabDebug("AWS download error: kmsEnabled=true but envelope missing");
        return std::nullopt;
    }

    QString downloadUrl = obj.value("ciphertextUrl").toString();
    if (downloadUrl.isEmpty()) downloadUrl = obj.value("downloadUrl").toString();
    if (downloadUrl.isEmpty()) downloadUrl = obj.value("presignedUrl").toString();
    // Extra fallbacks for safety
    if (downloadUrl.isEmpty()) downloadUrl = obj.value("cipherTextUrl").toString();
    if (downloadUrl.isEmpty()) downloadUrl = obj.value("ciphertext_url").toString();
    if (downloadUrl.isEmpty()) downloadUrl = obj.value("url").toString();
    if (downloadUrl.isEmpty()) downloadUrl = obj.value("Location").toString();
    if (downloadUrl.isEmpty()) {
        // Emit diagnostic with available top-level keys
        QStringList keys; keys.reserve(obj.keys().size());
        for (const auto &k : obj.keys()) keys << k;
        d->lastError = QStringLiteral("Server did not return a download URL (keys: %1)").arg(keys.join(","));
        appendTabDebug(QString("AWS download error: missing URL; response keys=[%1]").arg(keys.join(",")));
        return std::nullopt;
    }

    // Download bytes (ciphertext or plaintext depending on server)
    // Avoid logging full presigned URL (contains signature). Log host+path only.
    const QUrl dlUrl(downloadUrl);
    appendTabDebug(QString("AWS download presigned URL target: %1%2")
                   .arg(dlUrl.host(), dlUrl.path()));
    QNetworkRequest downloadRequest{dlUrl};
    QNetworkReply* downloadReply = d->networkManager->get(downloadRequest);
    d->currentDownloadReply = downloadReply;

    QEventLoop downloadLoop;
    QObject::connect(downloadReply, &QNetworkReply::finished, &downloadLoop, &QEventLoop::quit);
    downloadLoop.exec();

    if (downloadReply->error() != QNetworkReply::NoError) {
        d->lastError = QString("File download failed: %1").arg(downloadReply->errorString());
        appendTabDebug(QString("AWS download HTTP error: %1").arg(d->lastError));
        downloadReply->deleteLater();
        d->currentDownloadReply = nullptr;
        return std::nullopt;
    }

    QByteArray downloaded = downloadReply->readAll();
    downloadReply->deleteLater();
    d->currentDownloadReply = nullptr;

    if (!hasEnvelope) {
        // Plaintext mode (current behavior) - log magic for quick triage
        if (downloaded.size() >= 5) {
            const QByteArray head = downloaded.left(8);
            qDebug() << "AWS download plaintext size=" << downloaded.size()
                     << " head(hex)=" << head.toHex(' ').constData();
            appendTabDebug(QString("AWS download plaintext: size=%1 head=%2")
                           .arg(downloaded.size())
                           .arg(QString::fromLatin1(head.toHex(' '))));
        }
        return downloaded;
    }

    // Decrypt using SecurityEnvelope::decryptBuffer
    SecurityEnvelope::BufferInputs bi;
    bi.algorithm = envelope.value("algorithm").toString();
    const auto b64 = [](const QJsonObject& o, const char* k){ return QByteArray::fromBase64(o.value(k).toString().toUtf8()); };
    bi.encryptedData = downloaded; // ciphertext body from S3/proxy
    bi.encryptedDataKey = b64(envelope, "encryptedDataKey");
    bi.iv = b64(envelope, "iv");
    bi.authTag = b64(envelope, "authTag");

    // AAD: bind to JWT jti from our bearer token when available
    const QString bearer = d->authToken;
    const QString jti = SecurityEnvelope::extractJtiFromJwt(bearer);
    bi.aad = jti.isEmpty() ? QByteArray() : jti.toUtf8();

    // AWS credentials for KMS Decrypt (from server response)
    const QJsonObject aws = obj.value("aws").toObject();
    bi.accessKeyId = aws.value("accessKeyId").toString();
    bi.secretAccessKey = aws.value("secretAccessKey").toString();
    bi.sessionToken = aws.value("sessionToken").toString();
    bi.region = aws.value("region").toString();
    if (bi.region.isEmpty()) bi.region = obj.value("region").toString(); // fallback

    // Quick envelope diagnostics
    qDebug() << "AWS envelope present. edkBytes=" << bi.encryptedDataKey.size()
             << " ivBytes=" << bi.iv.size() << " tagBytes=" << bi.authTag.size()
             << " aadBytes=" << bi.aad.size();
    appendTabDebug(QString("AWS envelope: edk=%1 iv=%2 tag=%3 aad=%4")
                   .arg(bi.encryptedDataKey.size())
                   .arg(bi.iv.size())
                   .arg(bi.authTag.size())
                   .arg(bi.aad.size()));

    QString decErr;
    auto plainOpt = SecurityEnvelope::decryptBuffer(bi, &decErr);
    if (!plainOpt.has_value()) {
        d->lastError = decErr.isEmpty() ? QStringLiteral("Failed to decrypt file content") : decErr;
        appendTabDebug(QString("AWS decrypt failed: %1").arg(d->lastError));
        return std::nullopt;
    }
    // Log magic of decrypted bytes for format verification
    const QByteArray& plain = plainOpt.value();
    if (plain.size() >= 8) {
        qDebug() << "AWS decrypted size=" << plain.size()
                 << " head(hex)=" << plain.left(8).toHex(' ').constData();
        appendTabDebug(QString("AWS decrypted: size=%1 head=%2")
                       .arg(plain.size())
                       .arg(QString::fromLatin1(plain.left(8).toHex(' '))));
        // Hint for PDF
        if (key.endsWith('.' + QStringLiteral("pdf"), Qt::CaseInsensitive)) {
            const bool looksPdf = plain.startsWith("%PDF-");
            qDebug() << "Looks like PDF?" << looksPdf;
            appendTabDebug(QString("Looks like PDF? %1").arg(looksPdf));
        }
    }
    return plain;
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
