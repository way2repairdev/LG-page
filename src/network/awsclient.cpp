#include "network/awsclient.h"
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QDateTime>
#include <memory>

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

    Impl() {
        options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Fatal;
        Aws::InitAPI(options);
        initialized = true;
    }
    ~Impl() {
        if (initialized) Aws::ShutdownAPI(options);
    }
};

AwsClient::AwsClient() : d(new Impl) {}
AwsClient::~AwsClient() { delete d; }

bool AwsClient::loadFromEnv() {
    const QString ak = qEnvironmentVariable("AWS_ACCESS_KEY_ID");
    const QString sk = qEnvironmentVariable("AWS_SECRET_ACCESS_KEY");
    const QString rg = qEnvironmentVariable("AWS_REGION");
    const QString bk = qEnvironmentVariable("AWS_S3_BUCKET");
    const QString ep = qEnvironmentVariable("AWS_S3_ENDPOINT");
    // Set endpoint first so it is respected when building client
    if (!ep.isEmpty()) setEndpointOverride(ep);
    if (!ak.isEmpty() && !sk.isEmpty() && !rg.isEmpty()) {
        setCredentials(ak, sk, rg);
    }
    if (!bk.isEmpty()) setBucket(bk);
    return isReady();
}

void AwsClient::setCredentials(const QString& accessKey, const QString& secretKey, const QString& region, const QString& sessionToken) {
    d->region = region;
    d->lastError.clear();
    Aws::Client::ClientConfiguration cfg;
    cfg.region = region.toStdString();
    if (!d->endpointOverride.isEmpty()) cfg.endpointOverride = d->endpointOverride.toStdString();
    // TLS and sane defaults
    cfg.verifySSL = true;
    cfg.connectTimeoutMs = 8000;
    cfg.requestTimeoutMs = 20000;
    Aws::Auth::AWSCredentials creds(accessKey.toStdString(), secretKey.toStdString(), sessionToken.toStdString());
    try {
        d->s3 = std::make_shared<Aws::S3::S3Client>(creds, cfg, Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, false);
    } catch (const std::exception &e) {
        d->lastError = QString::fromLocal8Bit(e.what());
        d->s3.reset();
    }
}

void AwsClient::setBucket(const QString& bucket) { d->bucket = bucket; }
void AwsClient::setEndpointOverride(const QString& endpoint) {
    d->endpointOverride = endpoint;
    // Rebuild client with the same credentials if already set
    if (d->s3) {
        // Unfortunately we don't have direct access to creds here; require caller to call setCredentials again to apply endpoint.
        // Clear to force reconfiguration.
        d->s3.reset();
    }
}

bool AwsClient::isReady() const {
    return d->s3 != nullptr && !d->bucket.isEmpty();
}

std::optional<QVector<AwsListEntry>> AwsClient::list(const QString& prefix, int maxKeys) {
    if (!isReady()) { d->lastError = QStringLiteral("Client not configured"); return std::nullopt; }
    Aws::S3::Model::ListObjectsV2Request req;
    req.WithBucket(d->bucket.toStdString())
       .WithMaxKeys(maxKeys)
       .WithDelimiter("/");
    if (!prefix.isEmpty()) req.WithPrefix(prefix.toStdString());

    auto outcome = d->s3->ListObjectsV2(req);
    if (!outcome.IsSuccess()) {
        const auto &err = outcome.GetError();
        d->lastError = QString::fromStdString(err.GetExceptionName()) + ": " + QString::fromStdString(err.GetMessage());
        return std::nullopt;
    }

    QVector<AwsListEntry> out;
    const auto& res = outcome.GetResult();
    // Folders (CommonPrefixes)
    for (const auto& cp : res.GetCommonPrefixes()) {
        AwsListEntry e;
        e.isDir = true;
        QString p = QString::fromStdString(cp.GetPrefix());
        // derive folder name after last '/'
        QString name = p;
        if (name.endsWith('/')) name.chop(1);
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.mid(slash+1);
        e.name = name;
        e.key = p; // prefix
        out.push_back(e);
    }
    // Files (Contents)
    for (const auto& obj : res.GetContents()) {
        AwsListEntry e;
        e.isDir = false;
        QString k = QString::fromStdString(obj.GetKey());
        // skip pseudo-folder markers
        if (k.endsWith('/')) continue;
        int slash = k.lastIndexOf('/');
        e.name = (slash >= 0) ? k.mid(slash+1) : k;
        e.key = k;
        e.size = static_cast<qint64>(obj.GetSize());
        out.push_back(e);
    }
    return out;
}

std::optional<QString> AwsClient::downloadToFile(const QString& key, const QString& localPath) {
    if (!isReady()) { d->lastError = QStringLiteral("Client not configured"); return std::nullopt; }
    Aws::S3::Model::GetObjectRequest req;
    req.WithBucket(d->bucket.toStdString()).WithKey(key.toStdString());
    auto outcome = d->s3->GetObject(req);
    if (!outcome.IsSuccess()) {
        const auto &err = outcome.GetError();
        d->lastError = QString::fromStdString(err.GetExceptionName()) + ": " + QString::fromStdString(err.GetMessage());
        return std::nullopt;
    }

    // Ensure dir exists
    QFileInfo fi(localPath);
    QDir().mkpath(fi.absolutePath());
    QFile f(localPath);
    if (!f.open(QIODevice::WriteOnly)) return std::nullopt;
    auto& body = outcome.GetResultWithOwnership().GetBody();
    const size_t chunk = 64 * 1024;
    std::vector<char> buf(chunk);
    while (body.good()) {
        body.read(buf.data(), chunk);
        std::streamsize got = body.gcount();
        if (got > 0) f.write(buf.data(), static_cast<qint64>(got));
    }
    f.close();
    return localPath;
}

std::optional<QByteArray> AwsClient::downloadToMemory(const QString& key) {
    if (!isReady()) { d->lastError = QStringLiteral("Client not configured"); return std::nullopt; }
    Aws::S3::Model::GetObjectRequest req;
    req.WithBucket(d->bucket.toStdString()).WithKey(key.toStdString());
    auto outcome = d->s3->GetObject(req);
    if (!outcome.IsSuccess()) {
        const auto &err = outcome.GetError();
        d->lastError = QString::fromStdString(err.GetExceptionName()) + ": " + QString::fromStdString(err.GetMessage());
        return std::nullopt;
    }

    // Read entire stream into QByteArray
    auto& body = outcome.GetResultWithOwnership().GetBody();
    QByteArray data;
    const size_t chunk = 64 * 1024;
    std::vector<char> buf(chunk);
    while (body.good()) {
        body.read(buf.data(), chunk);
        std::streamsize got = body.gcount();
        if (got > 0) data.append(buf.data(), static_cast<int>(got));
    }
    return data;
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

#else

struct AwsClient::Impl {
    QString bucket;
    QString region;
    QString endpointOverride;
    QString lastError;
};

AwsClient::AwsClient() : d(new Impl) {}
AwsClient::~AwsClient() { delete d; }

bool AwsClient::loadFromEnv() {
    const QString bk = qEnvironmentVariable("AWS_S3_BUCKET");
    if (!bk.isEmpty()) d->bucket = bk;
    return false;
}

void AwsClient::setCredentials(const QString& accessKey, const QString& secretKey, const QString& region, const QString& sessionToken) {
    Q_UNUSED(accessKey)
    Q_UNUSED(secretKey)
    Q_UNUSED(sessionToken)
    d->region = region;
    d->lastError = "AWS SDK not available - application built without AWS support";
}
void AwsClient::setBucket(const QString& bucket) { d->bucket = bucket; }
void AwsClient::setEndpointOverride(const QString& endpoint) { d->endpointOverride = endpoint; }
bool AwsClient::isReady() const { return false; }
std::optional<QVector<AwsListEntry>> AwsClient::list(const QString& prefix, int maxKeys) {
    Q_UNUSED(prefix)
    Q_UNUSED(maxKeys)
    d->lastError = "AWS SDK not available - cannot list S3 objects";
    return std::nullopt;
}
std::optional<QString> AwsClient::downloadToFile(const QString& key, const QString& localPath) {
    Q_UNUSED(key)
    Q_UNUSED(localPath)
    return std::nullopt;
}

std::optional<QByteArray> AwsClient::downloadToMemory(const QString& key) {
    Q_UNUSED(key)
    d->lastError = "AWS SDK not available - cannot download S3 objects to memory";
    return std::nullopt;
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

#endif
