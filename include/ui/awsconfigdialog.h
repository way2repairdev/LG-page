#ifndef AWSCONFIGDIALOG_H
#define AWSCONFIGDIALOG_H

#include <QDialog>
#include <QString>

class QLineEdit;
class QDialogButtonBox;
class QCheckBox;

class AwsConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit AwsConfigDialog(QWidget* parent = nullptr);

    QString accessKeyId() const;
    QString secretAccessKey() const;
    QString region() const;
    QString bucket() const;
    QString endpoint() const; // optional
    bool rememberCredentials() const;

    // Convenience: prefill from env if available
    void preloadFromEnv();
    
    // Load/save credentials from settings
    void loadSavedCredentials();
    void saveCredentials() const;

private:
    QLineEdit* m_accessKeyEdit{nullptr};
    QLineEdit* m_secretKeyEdit{nullptr};
    QLineEdit* m_regionEdit{nullptr};
    QLineEdit* m_bucketEdit{nullptr};
    QLineEdit* m_endpointEdit{nullptr};
    QCheckBox* m_rememberCheckBox{nullptr};
    QDialogButtonBox* m_buttons{nullptr};
};

#endif // AWSCONFIGDIALOG_H
