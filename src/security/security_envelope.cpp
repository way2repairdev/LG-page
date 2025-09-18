#include "security/security_envelope.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QByteArray>
#include <QVector>
#include <QStringList>

// JWT parsing: base64url decode of the second part (payload)
static QByteArray base64UrlToBase64(const QByteArray& url) {
    QByteArray b64 = url;
    b64.replace('-', '+').replace('_', '/');
    int pad = b64.size() % 4;
    if (pad) b64.append(QByteArray(4 - pad, '='));
    return b64;
}

QString SecurityEnvelope::extractJtiFromJwt(const QString& jwt) {
    const auto parts = jwt.split('.');
    if (parts.size() != 3) return {};
    const QByteArray payload = QByteArray::fromBase64(base64UrlToBase64(parts[1].toUtf8()));
    QJsonParseError pe{};
    const auto doc = QJsonDocument::fromJson(payload, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return doc.object().value("jti").toString();
}

#ifdef HAVE_AWS_KMS
#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/kms/KMSClient.h>
#include <aws/kms/model/DecryptRequest.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <bcrypt.h>
#ifdef _MSC_VER
#pragma comment(lib, "bcrypt.lib")
#endif
#endif

static bool aes256gcm_decrypt_win(const QByteArray& key,
                                  const QByteArray& iv,
                                  const QByteArray& aad,
                                  const QByteArray& tag,
                                  const QByteArray& ciphertext,
                                  QByteArray& outPlain,
                                  QString* err) {
#ifdef Q_OS_WIN
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (status < 0) { if (err) *err = "BCryptOpenAlgorithmProvider failed"; return false; }
    ULONG cbResult = 0;
    // BCRYPT_CHAIN_MODE_GCM is a wide string literal (L"...") – pass it directly
    status = BCryptSetProperty(hAlg,
                               BCRYPT_CHAINING_MODE,
                               reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                               (ULONG)sizeof(BCRYPT_CHAIN_MODE_GCM),
                               0);
    if (status < 0) { BCryptCloseAlgorithmProvider(hAlg,0); if (err) *err = "BCryptSetProperty GCM failed"; return false; }

    // Generate symmetric key object
    DWORD keyObjLen = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&keyObjLen, sizeof(keyObjLen), &cbResult, 0);
    if (status < 0) { BCryptCloseAlgorithmProvider(hAlg,0); if (err) *err = "Get BCRYPT_OBJECT_LENGTH failed"; return false; }
    QVector<UCHAR> keyObj(keyObjLen);

    status = BCryptGenerateSymmetricKey(hAlg, &hKey, keyObj.data(), keyObjLen, (PUCHAR)key.data(), (ULONG)key.size(), 0);
    if (status < 0) { BCryptCloseAlgorithmProvider(hAlg,0); if (err) *err = "BCryptGenerateSymmetricKey failed"; return false; }

    // Prepare auth info
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = (PUCHAR)iv.data();
    authInfo.cbNonce = (ULONG)iv.size();
    authInfo.pbAuthData = (PUCHAR)aad.data();
    authInfo.cbAuthData = (ULONG)aad.size();
    authInfo.pbTag = (PUCHAR)tag.data();
    authInfo.cbTag = (ULONG)tag.size();

    // Decrypt
    outPlain.resize(ciphertext.size());
    ULONG outLen = 0;
    status = BCryptDecrypt(hKey,
                           (PUCHAR)ciphertext.data(), (ULONG)ciphertext.size(),
                           &authInfo,
                           nullptr, 0,
                           (PUCHAR)outPlain.data(), (ULONG)outPlain.size(), &outLen,
                           0);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    if (status < 0) { if (err) *err = "BCryptDecrypt (GCM auth) failed"; return false; }
    outPlain.resize(outLen);
    return true;
#else
    Q_UNUSED(key); Q_UNUSED(iv); Q_UNUSED(aad); Q_UNUSED(tag); Q_UNUSED(ciphertext); Q_UNUSED(outPlain); Q_UNUSED(err);
    if (err) *err = "AES-GCM decrypt not implemented for this platform";
    return false;
#endif
}

std::optional<EnvelopeDecrypted> SecurityEnvelope::decrypt(const EnvelopeInputs& in, QString* errorOut) {
    auto setErr = [&](const QString& e){ if (errorOut) *errorOut = e; };
    if (in.algorithm != QLatin1String("AES-256-GCM")) { setErr("Unsupported algorithm"); return std::nullopt; }
    if (in.iv.size() != 12) { setErr("Invalid IV length"); return std::nullopt; }
    if (in.authTag.size() != 16) { setErr("Invalid authTag length"); return std::nullopt; }
    if (in.region.isEmpty()) { setErr("Missing AWS region for KMS decrypt"); return std::nullopt; }

    // 1) Decrypt the data key using AWS KMS
    QByteArray dataKey; // 32 bytes
#ifdef HAVE_AWS_KMS
    try {
        Aws::Client::ClientConfiguration cfg;
        cfg.region = in.region.toStdString().c_str();
        // Require explicit temporary credentials provided by the server for KMS Decrypt.
        // This prevents falling back to local/default credentials (e.g., a developer IAM user).
        if (in.accessKeyId.isEmpty() || in.secretAccessKey.isEmpty()) {
            setErr("Server did not provide AWS STS credentials for KMS Decrypt");
            return std::nullopt;
        }
        Aws::Auth::AWSCredentials explicitCreds(
            in.accessKeyId.toStdString().c_str(),
            in.secretAccessKey.toStdString().c_str(),
            in.sessionToken.toStdString().c_str());

        Aws::KMS::KMSClient kmsClient(explicitCreds, cfg);
        Aws::KMS::Model::DecryptRequest req;
        req.SetCiphertextBlob(Aws::Utils::ByteBuffer((unsigned char*)in.encryptedDataKey.data(), (unsigned)in.encryptedDataKey.size()));
        // Optionally: set EncryptionContext to harden if server used it
        auto outcome = kmsClient.Decrypt(req);
        if (!outcome.IsSuccess()) {
            setErr(QString::fromStdString(outcome.GetError().GetMessage()));
            return std::nullopt;
        }
        const auto& buf = outcome.GetResult().GetPlaintext();
        dataKey = QByteArray((const char*)buf.GetUnderlyingData(), (int)buf.GetLength());
        if (dataKey.size() != 32) { setErr("Unexpected data key length"); return std::nullopt; }
    } catch (...) {
        setErr("Exception during KMS Decrypt");
        return std::nullopt;
    }
#else
    Q_UNUSED(in);
    setErr("AWS KMS SDK not available (HAVE_AWS_KMS off)");
    return std::nullopt;
#endif

    // 2) AES-256-GCM decrypt using jti as AAD
    const QString jti = extractJtiFromJwt(in.jwtToken);
    if (jti.isEmpty()) { setErr("Failed to extract jti from JWT"); return std::nullopt; }
    QByteArray plaintext;
    if (!aes256gcm_decrypt_win(dataKey, in.iv, jti.toUtf8(), in.authTag, in.encryptedData, plaintext, errorOut)) {
        return std::nullopt;
    }

    EnvelopeDecrypted out{plaintext};
    return out;
}

    std::optional<QByteArray> SecurityEnvelope::decryptBuffer(const BufferInputs& in, QString* errorOut) {
        auto setErr = [&](const QString& e){ if (errorOut) *errorOut = e; };
        if (in.algorithm != QLatin1String("AES-256-GCM")) { setErr("Unsupported algorithm"); return std::nullopt; }
        if (in.iv.size() != 12) { setErr("Invalid IV length"); return std::nullopt; }
        if (in.authTag.size() != 16) { setErr("Invalid authTag length"); return std::nullopt; }
        if (in.region.isEmpty()) { setErr("Missing AWS region for KMS decrypt"); return std::nullopt; }

        QByteArray dataKey;
    #ifdef HAVE_AWS_KMS
        try {
            Aws::Client::ClientConfiguration cfg;
            cfg.region = in.region.toStdString().c_str();
            if (in.accessKeyId.isEmpty() || in.secretAccessKey.isEmpty()) {
                setErr("Server did not provide AWS STS credentials for KMS Decrypt");
                return std::nullopt;
            }
            Aws::Auth::AWSCredentials explicitCreds(
                in.accessKeyId.toStdString().c_str(),
                in.secretAccessKey.toStdString().c_str(),
                in.sessionToken.toStdString().c_str());

            Aws::KMS::KMSClient kmsClient(explicitCreds, cfg);
            Aws::KMS::Model::DecryptRequest req;
            req.SetCiphertextBlob(Aws::Utils::ByteBuffer((unsigned char*)in.encryptedDataKey.data(), (unsigned)in.encryptedDataKey.size()));
            auto outcome = kmsClient.Decrypt(req);
            if (!outcome.IsSuccess()) {
                setErr(QString::fromStdString(outcome.GetError().GetMessage()));
                return std::nullopt;
            }
            const auto& buf = outcome.GetResult().GetPlaintext();
            dataKey = QByteArray((const char*)buf.GetUnderlyingData(), (int)buf.GetLength());
            if (dataKey.size() != 32) { setErr("Unexpected data key length"); return std::nullopt; }
        } catch (...) {
            setErr("Exception during KMS Decrypt");
            return std::nullopt;
        }
    #else
        Q_UNUSED(in);
        setErr("AWS KMS SDK not available (HAVE_AWS_KMS off)");
        return std::nullopt;
    #endif

        QByteArray plaintext;
        if (!aes256gcm_decrypt_win(dataKey, in.iv, in.aad, in.authTag, in.encryptedData, plaintext, errorOut)) {
            return std::nullopt;
        }
        return plaintext;
    }
