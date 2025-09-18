#ifndef MEMORYFILEMANAGER_H
#define MEMORYFILEMANAGER_H

#include <QString>
#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <memory>

// Singleton class to manage files loaded into memory for security
class MemoryFileManager {
public:
    static MemoryFileManager* instance();
    
    // Store file data in memory with a unique ID
    QString storeFileData(const QString& originalKey, const QByteArray& data);
    
    // Get file data by memory ID
    QByteArray getFileData(const QString& memoryId) const;
    
    // Check if a memory ID exists
    bool hasFile(const QString& memoryId) const;
    
    // Remove file from memory
    void removeFile(const QString& memoryId);
    
    // Clear all stored files
    void clearAll();
    
    // Get original key for a memory ID
    QString getOriginalKey(const QString& memoryId) const;
    
    // Get memory usage statistics
    struct MemoryStats {
        int fileCount;
        qint64 totalBytes;
        qint64 largestFileBytes;
    };
    MemoryStats getMemoryStats() const;

private:
    MemoryFileManager() = default;
    ~MemoryFileManager() = default;
    MemoryFileManager(const MemoryFileManager&) = delete;
    MemoryFileManager& operator=(const MemoryFileManager&) = delete;
    
    struct FileEntry {
        QString originalKey;
        QByteArray data;
        qint64 timestamp;
    };
    static inline void secureZero(QByteArray &buf) {
#if defined(__STDC_LIB_EXT1__)
    if (!buf.isEmpty()) memset_s(buf.data(), buf.size(), 0, buf.size());
#else
    volatile char* p = buf.data();
    for (int i = 0; i < buf.size(); ++i) p[i] = 0;
#endif
    }
    
    mutable QMutex m_mutex;
    QHash<QString, FileEntry> m_files;
    static MemoryFileManager* s_instance;
    
    QString generateMemoryId() const;
};

#endif // MEMORYFILEMANAGER_H
