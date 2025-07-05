#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMessageBox>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QProgressBar>
#include <QTimer>
#include <QDateTime>
#include "database/databasemanager.h"

// Forward declaration
class MainApplication;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onLoginButtonClicked();
    void onUsernameChanged();
    void onPasswordChanged();
    void onDatabaseConnectionChanged(bool connected);
    void onDatabaseError(const QString &error);

private:
    Ui::MainWindow *ui;
    DatabaseManager *m_dbManager;
    MainApplication *m_mainApp; // Pointer to main application
    
    void setupLoginConnections();
    void setupDatabaseConnection();
    bool validateInput();
    void performLogin(const QString &username, const QString &password);
    void showConnectionStatus(bool connected);
    void enableLoginControls(bool enabled);
    void launchMainApplication(const QString &username, const UserInfo &userInfo);
    void closeLoginWindow();
};
#endif // MAINWINDOW_H
