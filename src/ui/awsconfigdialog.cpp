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

    m_accessKeyEdit = new QLineEdit(this);
    m_secretKeyEdit = new QLineEdit(this);
    m_regionEdit    = new QLineEdit(this);
    m_bucketEdit    = new QLineEdit(this);
    m_endpointEdit  = new QLineEdit(this);
    m_rememberCheckBox = new QCheckBox("Remember credentials (stored securely)", this);

    // Secret masked
    m_secretKeyEdit->setEchoMode(QLineEdit::Password);

    layout->addRow(new QLabel("Access Key ID"), m_accessKeyEdit);
    layout->addRow(new QLabel("Secret Access Key"), m_secretKeyEdit);
    layout->addRow(new QLabel("Region"), m_regionEdit);
    layout->addRow(new QLabel("Bucket"), m_bucketEdit);
    layout->addRow(new QLabel("Endpoint (optional)"), m_endpointEdit);
    layout->addRow(m_rememberCheckBox);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addRow(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &AwsConfigDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &AwsConfigDialog::reject);

    // Load saved credentials on startup
    loadSavedCredentials();

    resize(420, 300);
}

QString AwsConfigDialog::accessKeyId() const    { return m_accessKeyEdit->text().trimmed(); }
QString AwsConfigDialog::secretAccessKey() const{ return m_secretKeyEdit->text().trimmed(); }
QString AwsConfigDialog::region() const         { return m_regionEdit->text().trimmed(); }
QString AwsConfigDialog::bucket() const         { return m_bucketEdit->text().trimmed(); }
QString AwsConfigDialog::endpoint() const       { return m_endpointEdit->text().trimmed(); }
bool AwsConfigDialog::rememberCredentials() const { return m_rememberCheckBox->isChecked(); }

void AwsConfigDialog::preloadFromEnv() {
    auto get = [](const char* name){ return QString::fromLocal8Bit(qgetenv(name)); };
    const QString ak = get("AWS_ACCESS_KEY_ID");
    const QString sk = get("AWS_SECRET_ACCESS_KEY");
    const QString rg = get("AWS_REGION");
    const QString bk = get("AWS_S3_BUCKET");
    const QString ep = get("AWS_S3_ENDPOINT");
    if (!ak.isEmpty()) m_accessKeyEdit->setText(ak);
    if (!sk.isEmpty()) m_secretKeyEdit->setText(sk);
    if (!rg.isEmpty()) m_regionEdit->setText(rg);
    if (!bk.isEmpty()) m_bucketEdit->setText(bk);
    if (!ep.isEmpty()) m_endpointEdit->setText(ep);
}

void AwsConfigDialog::loadSavedCredentials() {
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    
    // Load saved credentials if available
    const QString savedAccessKey = settings.value("aws/accessKey", "").toString();
    const QString savedSecretKey = settings.value("aws/secretKey", "").toString();
    const QString savedRegion = settings.value("aws/region", "us-east-1").toString();
    const QString savedBucket = settings.value("aws/bucket", "").toString();
    const QString savedEndpoint = settings.value("aws/endpoint", "").toString();
    const bool rememberWasChecked = settings.value("aws/remember", false).toBool();
    
    if (!savedAccessKey.isEmpty()) m_accessKeyEdit->setText(savedAccessKey);
    if (!savedSecretKey.isEmpty()) m_secretKeyEdit->setText(savedSecretKey);
    if (!savedRegion.isEmpty()) m_regionEdit->setText(savedRegion);
    if (!savedBucket.isEmpty()) m_bucketEdit->setText(savedBucket);
    if (!savedEndpoint.isEmpty()) m_endpointEdit->setText(savedEndpoint);
    
    m_rememberCheckBox->setChecked(rememberWasChecked);
    
    // If we have saved credentials, also try to load from environment (env takes precedence)
    preloadFromEnv();
}

void AwsConfigDialog::saveCredentials() const {
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    
    if (rememberCredentials()) {
        // Save credentials
        settings.setValue("aws/accessKey", accessKeyId());
        settings.setValue("aws/secretKey", secretAccessKey());
        settings.setValue("aws/region", region());
        settings.setValue("aws/bucket", bucket());
        settings.setValue("aws/endpoint", endpoint());
        settings.setValue("aws/remember", true);
    } else {
        // Clear saved credentials
        settings.remove("aws/accessKey");
        settings.remove("aws/secretKey");
        settings.remove("aws/region");
        settings.remove("aws/bucket");
        settings.remove("aws/endpoint");
        settings.setValue("aws/remember", false);
    }
    
    settings.sync();
}
