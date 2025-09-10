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
#include <QMouseEvent>
#include <QKeyEvent>
#include "database/databasemanager.h"
#include "network/authservice.h"
#include <QPointer>
class QParallelAnimationGroup;
class QPropertyAnimation;

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
    AuthService m_auth;
    MainApplication *m_mainApp; // Pointer to main application
    
    // Store current login attempt for callback
    QString m_currentUsername;
    QString m_currentPassword;
    
    void setupStaticLightTheme();
    void setupLoginConnections();
    void setupDatabaseConnection();
    bool validateInput();
    void performLogin(const QString &username, const QString &password);
    void configureAwsForMain(MainApplication* app, const AuthAwsCreds& aws, const QString& authToken);
    void showConnectionStatus(bool connected);
    void enableLoginControls(bool enabled);
    void launchMainApplication(const QString &username, const UserInfo &userInfo);
    void closeLoginWindow();
    void onAuthLoginFinished(bool success, const AuthResult& result, const QString& error);
    // Remember me persistence
    void loadSavedCredentials();
    void persistRememberChoice(const QString &username, const QString &password);
    
    // Light theme styled message box
    void showStyledMessageBox(QMessageBox::Icon icon, const QString &title, const QString &message);
    
    // Mouse event handlers for frameless window
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    QPoint m_dragPosition;
    bool m_dragging;
    // Animations
    void animateMinimize();
    void animateClose();
    void animateShow();
    void centerOnScreen();
    QPointer<QParallelAnimationGroup> m_minimizeAnim;
    QPointer<QParallelAnimationGroup> m_closeAnim;
    QPointer<QParallelAnimationGroup> m_showAnim;
    bool m_closingNow { false };
    bool m_firstShowDone { false };
};
#endif // MAINWINDOW_H
