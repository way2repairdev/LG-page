#ifndef MAINAPPLICATION_H
#define MAINAPPLICATION_H

#include <QMainWindow>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QAction>
#include <QMessageBox>
#include <QTextEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QSplitter>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include "databasemanager.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainApplication;
}
QT_END_NAMESPACE

struct UserSession {
    QString username;
    QString fullName;
    QString email;
    QDateTime loginTime;
};

class MainApplication : public QMainWindow
{
    Q_OBJECT

public:
    MainApplication(const UserSession &userSession, QWidget *parent = nullptr);
    ~MainApplication();

private slots:
    void onTreeItemClicked(QTreeWidgetItem *item, int column);
    void onTreeItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onAboutClicked();
    void onLogoutClicked();
    void onHttpRequestFinished(QNetworkReply *reply);
    void onNetworkError(QNetworkReply::NetworkError error);

private:
    UserSession m_userSession;
    DatabaseManager *m_dbManager;
    QNetworkAccessManager *m_networkManager;
    QString m_baseUrl;
    
    // UI Components
    QWidget *m_centralWidget;
    QSplitter *m_splitter;
    QTreeWidget *m_treeWidget;
    QWidget *m_contentWidget;
    QToolBar *m_toolbar;
    QStatusBar *m_statusBar;
    
    void setupUI();
    void setupMenuBar();
    void setupToolBar();
    void setupStatusBar();
    void setupTreeView();
    void setupContentArea();
    void updateUserInfo();
    void loadFileList();
    void loadFileContent(const QString &filePath);
    void parseFileListJson(const QJsonDocument &doc);
    void addTreeItem(QTreeWidgetItem *parent, const QJsonObject &item);
    void loadFallbackData();
    void updateContentArea(const QString &title, const QString &content);

signals:
    void logoutRequested();
};

#endif // MAINAPPLICATION_H
