#ifndef SECURITY_ENVELOPE_H
#define SECURITY_ENVELOPE_H

#include <QString>
#include <QByteArray>
#include <optional>

// Minimal interface to decrypt server-provided envelope payloads.
// It expects a JWT (to extract jti as AAD), the encrypted payload fields,
// and AWS credentials (either temp or default chain) to call KMS::Decrypt.

struct EnvelopeInputs {
    QString jwtToken;             // JWT returned by server (we use jti as AAD)
    QString algorithm;            // "AES-256-GCM"
    QByteArray encryptedData;     // raw bytes (decoded from base64)
    QByteArray encryptedDataKey;  // raw bytes (decoded from base64)
    QByteArray iv;                // 12 bytes
    QByteArray authTag;           // 16 bytes
    // Optional AWS fields (preferred). If empty, default credentials chain may be used.
    QString accessKeyId;
    QString secretAccessKey;
    QString sessionToken;
    QString region;               // required for KMS calls
};

// Result after decrypting envelope
struct EnvelopeDecrypted {
    QByteArray plaintext; // JSON bytes
};

class SecurityEnvelope {
public:
    // Decrypts the envelope using AWS KMS Decrypt to recover data key then AES-256-GCM.
    // Returns std::nullopt on failure.
    static std::optional<EnvelopeDecrypted> decrypt(const EnvelopeInputs& in, QString* errorOut = nullptr);

    // Extract JWT jti value for use as AAD; returns empty string if parsing fails.
    static QString extractJtiFromJwt(const QString& jwt);

    // Generic buffer decrypt using envelope fields; caller supplies AAD explicitly (e.g., jti, key, or object ETag).
    // Useful for file/content decryption where JWT may not be available or a different AAD is desired.
    struct BufferInputs {
        QString algorithm;            // "AES-256-GCM"
        QByteArray encryptedData;     // raw bytes (decoded from base64)
        QByteArray encryptedDataKey;  // raw bytes (decoded from base64)
        QByteArray iv;                // 12 bytes
        QByteArray authTag;           // 16 bytes
        QByteArray aad;               // Associated data to bind context (may be empty)
        // Required AWS fields for KMS::Decrypt
        QString accessKeyId;
        QString secretAccessKey;
        QString sessionToken;         // optional
        QString region;               // required for KMS calls
    };

    // Decrypt arbitrary ciphertext buffer using KMS-unwrapped key and AES-256-GCM with supplied AAD.
    static std::optional<QByteArray> decryptBuffer(const BufferInputs& in, QString* errorOut = nullptr);
};

#endif // SECURITY_ENVELOPE_H
