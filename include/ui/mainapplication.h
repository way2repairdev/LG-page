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
#include <QTabWidget>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include "ui/dualtabwidget.h"

// Forward declaration for PDF viewer
class PDFViewerWidget;
// Forward declaration for PCB viewer
class PCBViewerWidget;
// Forward declaration for dual tab widget
class DualTabWidget;
// Server-side includes (commented out for local file loading)
//#include <QNetworkAccessManager>
//#include <QNetworkRequest>
//#include <QNetworkReply>
//#include <QJsonDocument>
//#include <QJsonObject>
//#include <QJsonArray>
//#include <QUrl>
#include "database/databasemanager.h"

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
    void onTreeItemExpanded(QTreeWidgetItem *item);
    void onTreeItemCollapsed(QTreeWidgetItem *item);
    void onAboutClicked();
    void onLogoutClicked();
    // Dual tab widget slots
    void onTabCloseRequestedByType(int index, DualTabWidget::TabType type);
    void onTabChangedByType(int index, DualTabWidget::TabType type);
    // Server-side slots (commented out for local file loading)
    //void onHttpRequestFinished(QNetworkReply *reply);
    //void onNetworkError(QNetworkReply::NetworkError error);

public slots:
    void toggleTreeView();
    void toggleFullScreenPDF();

private:
    UserSession m_userSession;
    DatabaseManager *m_dbManager;
    QString m_rootFolderPath;  // Local folder path to load files from
    
    // Server-side members (commented out for local file loading)
    //QNetworkAccessManager *m_networkManager;
    //QString m_baseUrl;
    
    // UI Components
    QWidget *m_centralWidget;
    QSplitter *m_splitter;
    QTreeWidget *m_treeWidget;
    DualTabWidget *m_tabWidget;  // Changed from QTabWidget to DualTabWidget
    QStatusBar *m_statusBar;
    
    // Tree view state
    bool m_treeViewVisible;
    QList<int> m_splitterSizes; // Store original splitter sizes
    
    void setupUI();
    void setupMenuBar();
    void setupStatusBar();
    void setupKeyboardShortcuts();
    void setupTreeView();
    void setupTabWidget();  // Changed from setupContentArea
    void updateUserInfo();
    void loadLocalFiles();  // Changed from loadFileList
    void loadLocalFileContent(const QString &filePath);  // Changed from loadFileContent
    void populateTreeFromDirectory(const QString &dirPath, QTreeWidgetItem *parentItem = nullptr);
    void openFileInTab(const QString &filePath);
    void openPDFInTab(const QString &filePath);
    void openPCBInTab(const QString &filePath);
    void addWelcomeTab();
    QIcon getFileIcon(const QString &filePath);
    QIcon getFolderIcon(bool isOpen = false);
    QString getFileExtension(const QString &filePath);
    bool isCodeFile(const QString &extension);
    bool isImageFile(const QString &extension);
    bool isArchiveFile(const QString &extension);
    bool isOfficeFile(const QString &extension);
    bool isPDFFile(const QString &extension);
    void setupTreeItemAppearance(QTreeWidgetItem *item, const QFileInfo &fileInfo);
    void updateTreeItemIcon(QTreeWidgetItem *item, bool isExpanded);
    void setTreeViewVisible(bool visible);
    bool isTreeViewVisible() const;
    void hideAllViewerToolbars();
    void debugToolbarStates(); // Debug helper method
    void forceToolbarIsolation(); // Complete toolbar isolation method
    
    // Server-side methods (commented out for local file loading)
    //void loadFileList();
    //void loadFileContent(const QString &filePath);
    //void parseFileListJson(const QJsonDocument &doc);
    //void addTreeItem(QTreeWidgetItem *parent, const QJsonObject &item);
    //void loadFallbackData();
    //void updateContentArea(const QString &title, const QString &content);

signals:
    void logoutRequested();
};

#endif // MAINAPPLICATION_H
