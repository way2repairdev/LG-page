#include "ui/awsconfigdialog.h"
#include <QFormLayout>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QLabel>
#include <QSettings>
#include <QStandardPaths>
#include <QCoreApplication>

AwsConfigDialog::AwsConfigDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("AWS Configuration");
    setModal(true);
    auto *layout = new QFormLayout(this);

    // Show informational message about server-based configuration
    auto *infoLabel = new QLabel(
        "AWS access is now configured automatically through server authentication.\n\n"
        "Direct AWS credential configuration has been disabled for security.\n"
        "AWS S3 operations are handled securely through the server.", this);
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("QLabel { padding: 10px; background-color: #e6f3ff; border-radius: 5px; }");
    layout->addRow(infoLabel);

    // Only show bucket field for information (read-only)
    m_bucketEdit = new QLineEdit(this);
    m_bucketEdit->setReadOnly(true);
    m_bucketEdit->setPlaceholderText("Configured via server authentication");
    layout->addRow(new QLabel("S3 Bucket"), m_bucketEdit);

    // Create other fields as null to prevent crashes, but don't add them to layout
    m_accessKeyEdit = new QLineEdit(this);
    m_secretKeyEdit = new QLineEdit(this);
    m_regionEdit = new QLineEdit(this);
    m_endpointEdit = new QLineEdit(this);
    m_rememberCheckBox = new QCheckBox(this);
    
    // Hide the credential fields
    m_accessKeyEdit->hide();
    m_secretKeyEdit->hide();
    m_regionEdit->hide();
    m_endpointEdit->hide();
    m_rememberCheckBox->hide();

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    layout->addRow(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &AwsConfigDialog::accept);

    resize(420, 200);
}

QString AwsConfigDialog::accessKeyId() const    { return QString(); } // No longer used
QString AwsConfigDialog::secretAccessKey() const{ return QString(); } // No longer used  
QString AwsConfigDialog::region() const         { return QString(); } // No longer used
QString AwsConfigDialog::bucket() const         { return m_bucketEdit->text().trimmed(); }
QString AwsConfigDialog::endpoint() const       { return QString(); } // No longer used
bool AwsConfigDialog::rememberCredentials() const { return false; } // No longer used

void AwsConfigDialog::preloadFromEnv() {
    // Environment variable loading disabled - only server-proxied mode supported
    // This method is kept for compatibility but does nothing
}

void AwsConfigDialog::loadSavedCredentials() {
    // Clear any legacy AWS credentials from previous versions
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    
    if (settings.contains("aws/accessKey") || settings.contains("aws/secretKey")) {
        // Clean up old credential storage
        settings.remove("aws/accessKey");
        settings.remove("aws/secretKey");
        settings.remove("aws/region");
        settings.remove("aws/endpoint");
        settings.remove("aws/remember");
        settings.sync();
    }
    
    // Bucket information might still be shown for informational purposes
    const QString savedBucket = settings.value("aws/bucket", "").toString();
    if (!savedBucket.isEmpty()) {
        m_bucketEdit->setText(savedBucket);
    }
}

void AwsConfigDialog::saveCredentials() const {
    // Direct credential saving disabled - only server-proxied mode supported
    // This method is kept for compatibility but only clears old credentials
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    
    // Clear any remaining direct AWS credentials
    settings.remove("aws/accessKey");
    settings.remove("aws/secretKey");
    settings.remove("aws/region");
    settings.remove("aws/endpoint");
    settings.remove("aws/remember");
    settings.sync();
}
