#ifndef AWSCLIENT_H
#define AWSCLIENT_H

#include <QString>
#include <QVector>
#include <QByteArray>
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

    // Setup from environment variables - DISABLED (always returns false)
    // Direct AWS credential loading is no longer supported
    bool loadFromEnv();
    // Direct credentials - DISABLED (use server-proxied mode only)
    void setCredentials(const QString& accessKey, const QString& secretKey, const QString& region, const QString& sessionToken = QString());
    void setBucket(const QString& bucket);
    void setEndpointOverride(const QString& endpoint); // optional (used in server-proxied mode)
    
    // Server-proxied mode (REQUIRED - only supported method)
    void setServerMode(bool enabled, const QString& serverUrl = QString(), const QString& authToken = QString());
    bool isServerMode() const;

    bool isReady() const;

    // List objects under prefix; returns "folders" (CommonPrefixes) and files
    // If delimiter is '/', S3 simulates folders
    std::optional<QVector<AwsListEntry>> list(const QString& prefix, int maxKeys = 1000);

    // Download object (key) to a local file path (creates/overwrites)
    // Returns local path on success
    std::optional<QString> downloadToFile(const QString& key, const QString& localPath);

    // Download object (key) directly to memory buffer (for security)
    // Returns QByteArray with file contents on success
    std::optional<QByteArray> downloadToMemory(const QString& key);

    // Utility: derive cache path for a key (no IO)
    QString cachePathForKey(const QString& key) const;

    QString bucket() const;
    // Last error message from a failed AWS operation (if any)
    QString lastError() const;

private:
    struct Impl;
    Impl* d{nullptr};
    
    // Private helper for server-proxied operations
    std::optional<QVector<AwsListEntry>> listViaServer(const QString& prefix, int maxKeys);
    std::optional<QByteArray> downloadViaServer(const QString& key);
};

#endif // AWSCLIENT_H
