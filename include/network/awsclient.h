#ifndef AWSCLIENT_H
#define AWSCLIENT_H

#include <QString>
#include <QVector>
#include <optional>

struct AwsListEntry {
    bool isDir{false};
    QString name;   // display name (file or folder)
    QString key;    // full S3 key (for files) or prefix ending with '/'
    qint64 size{0}; // file size if any
};

class AwsClient {
public:
    AwsClient();
    ~AwsClient();

    // Setup from environment variables if present
    // AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY, AWS_REGION, AWS_S3_BUCKET
    bool loadFromEnv();
    void setCredentials(const QString& accessKey, const QString& secretKey, const QString& region);
    void setBucket(const QString& bucket);
    void setEndpointOverride(const QString& endpoint); // optional (e.g., S3-compatible)

    bool isReady() const;

    // List objects under prefix; returns "folders" (CommonPrefixes) and files
    // If delimiter is '/', S3 simulates folders
    std::optional<QVector<AwsListEntry>> list(const QString& prefix, int maxKeys = 1000);

    // Download object (key) to a local file path (creates/overwrites)
    // Returns local path on success
    std::optional<QString> downloadToFile(const QString& key, const QString& localPath);

    // Utility: derive cache path for a key (no IO)
    QString cachePathForKey(const QString& key) const;

    QString bucket() const;
    // Last error message from a failed AWS operation (if any)
    QString lastError() const;

private:
    struct Impl;
    Impl* d{nullptr};
};

#endif // AWSCLIENT_H
