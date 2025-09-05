#include "core/memoryfilemanager.h"
#include <QDateTime>
#include <QCryptographicHash>
#include <QDebug>
#include <QMutexLocker>

MemoryFileManager* MemoryFileManager::s_instance = nullptr;

MemoryFileManager* MemoryFileManager::instance() {
    if (!s_instance) {
        s_instance = new MemoryFileManager();
    }
    return s_instance;
}

QString MemoryFileManager::storeFileData(const QString& originalKey, const QByteArray& data) {
    QMutexLocker locker(&m_mutex);
    
    QString memoryId = generateMemoryId();
    FileEntry entry;
    entry.originalKey = originalKey;
    entry.data = data;
    entry.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    m_files.insert(memoryId, entry);
    
    qDebug() << "MemoryFileManager: Stored file" << originalKey 
             << "with memory ID" << memoryId 
             << "(" << data.size() << "bytes)";
    
    return memoryId;
}

QByteArray MemoryFileManager::getFileData(const QString& memoryId) const {
    QMutexLocker locker(&m_mutex);
    
    auto it = m_files.find(memoryId);
    if (it != m_files.end()) {
        return it->data;
    }
    
    qWarning() << "MemoryFileManager: File not found for memory ID:" << memoryId;
    return QByteArray();
}

bool MemoryFileManager::hasFile(const QString& memoryId) const {
    QMutexLocker locker(&m_mutex);
    return m_files.contains(memoryId);
}

void MemoryFileManager::removeFile(const QString& memoryId) {
    QMutexLocker locker(&m_mutex);
    
    auto it = m_files.find(memoryId);
    if (it != m_files.end()) {
        qDebug() << "MemoryFileManager: Removed file" << it->originalKey 
                 << "with memory ID" << memoryId;
        m_files.erase(it);
    }
}

void MemoryFileManager::clearAll() {
    QMutexLocker locker(&m_mutex);
    
    qDebug() << "MemoryFileManager: Cleared all files (" << m_files.size() << "files)";
    m_files.clear();
}

QString MemoryFileManager::getOriginalKey(const QString& memoryId) const {
    QMutexLocker locker(&m_mutex);
    
    auto it = m_files.find(memoryId);
    if (it != m_files.end()) {
        return it->originalKey;
    }
    
    return QString();
}

MemoryFileManager::MemoryStats MemoryFileManager::getMemoryStats() const {
    QMutexLocker locker(&m_mutex);
    
    MemoryStats stats;
    stats.fileCount = m_files.size();
    stats.totalBytes = 0;
    stats.largestFileBytes = 0;
    
    for (auto it = m_files.begin(); it != m_files.end(); ++it) {
        qint64 size = it->data.size();
        stats.totalBytes += size;
        if (size > stats.largestFileBytes) {
            stats.largestFileBytes = size;
        }
    }
    
    return stats;
}

QString MemoryFileManager::generateMemoryId() const {
    // Generate a unique ID using current timestamp and a hash
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    QString baseString = QString("mem_%1_%2").arg(timestamp).arg(m_files.size());
    
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(baseString.toUtf8());
    
    return QString("memory://") + hash.result().toHex().left(16);
}
