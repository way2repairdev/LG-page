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
#include <QStyledItemDelegate>
#include <QPointer>
#include <QTimer>
#include <QSplitter>
#include <QDateTime>
#include <QTabWidget>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <vector>
#include <QLineEdit>
#include <QToolButton>
#include <QVector>
#include <QButtonGroup>
#include <QWidget>
#include <QWidgetAction>
#include <QWindow>
#include <QPointer>
#include <QStringList>
#include <optional>
#include "network/awsclient.h"
#include "ui/awsconfigdialog.h"
#include "network/authservice.h"
// Reuse the lightweight overlay used by PDF viewer
#include "ui/LoadingOverlay.h"
class QParallelAnimationGroup;
class QPropertyAnimation;

#include "ui/dualtabwidget.h"
#include "ui/titlebarwidget.h"

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
    // Smooth entrance animation when shown after login
    void animateEnter();
    // Configure AWS client from auth server response (optional)
    void configureAwsFromAuth(const AuthAwsCreds& creds, const QString& authToken);

private slots:
    void onTreeItemClicked(QTreeWidgetItem *item, int column);
    void onTreeItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onTreeItemExpanded(QTreeWidgetItem *item);
    void onTreeItemCollapsed(QTreeWidgetItem *item);
    void onAboutClicked();
    void onLogoutClicked();
    void onHomeClicked();
    void onThemeToggleChanged(bool checked);
    // Dual tab widget slots
    void onTabCloseRequestedByType(int index, DualTabWidget::TabType type);
    void onTabChangedByType(int index, DualTabWidget::TabType type);
    void performTabSwitch(int index, DualTabWidget::TabType type, QWidget *currentWidget, const QString &tabName);
    void onCrossSearchRequest(const QString &term, bool isNet, bool targetIsOther);
    void onFullUpdateUI();
    // Server-side slots (commented out for local file loading)
    //void onHttpRequestFinished(QNetworkReply *reply);
    //void onNetworkError(QNetworkReply::NetworkError error);

public slots:
    void toggleTreeView();
    void toggleFullScreenPDF();
    // Allow configuring server root path at runtime
    void setServerRootPath(const QString &path);
    // Maximize using our custom frameless logic
    void maximizeWindow();
    void switchToAwsTreeview(); // Switch to AWS treeview programmatically (e.g., after login)

protected:
    void changeEvent(QEvent *event) override; // Re-apply theme on palette changes
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#endif
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    UserSession m_userSession;
    DatabaseManager *m_dbManager;
    QString m_rootFolderPath;  // Local folder path to load files from
    QString m_serverRootPath;  // Server folder path (will be provided later)
    QString m_awsRootPath;     // AWS-mounted/local sync folder path (optional)
    AwsClient m_aws;           // Simple S3 client (dev mode with access keys)
    enum class TreeSource { Server, Local, AWS };
    TreeSource m_treeSource { TreeSource::AWS }; // default to AWS (AWS-only mode)
    
    // Server-side members (commented out for local file loading)
    //QNetworkAccessManager *m_networkManager;
    //QString m_baseUrl;
    
    // UI Components
    QWidget *m_centralWidget;
    QSplitter *m_splitter;
    QWidget *m_treePanel;           // container for search bar + tree
    QWidget *m_treeSearchBar;       // search bar container (for theming)
    QWidget *m_sourceToggleBar { nullptr };     // Local/Server/AWS toggle bar (AWS-only mode: unused)
    QTreeWidget *m_treeWidget;
    QLineEdit *m_treeSearchEdit;    // search input
    QPushButton *m_treeSearchButton; // search/next button
    QToolButton *m_treeSearchClearButton; // clear input
    QToolButton *m_homeButton { nullptr }; // Home button in menu bar (top-right)
    // Theme toggle removed - application now uses static light mode only
    QWidget *m_brandContainer { nullptr };   // Left-side brand container
    QLabel *m_brandLabel { nullptr };        // Brand logo label
    QPushButton *m_btnLocal { nullptr };        // Local button (unused)
    QPushButton *m_btnServer { nullptr };       // Server button (unused)
    QPushButton *m_btnAws { nullptr };          // AWS button (unused)
    QButtonGroup *m_sourceGroup { nullptr };    // Exclusive selection (unused)
    DualTabWidget *m_tabWidget;  // Changed from QTabWidget to DualTabWidget
    QStatusBar *m_statusBar;
    TitleBarWidget *m_titleBar { nullptr }; // Custom, tall title bar
    QMenuBar *m_customMenuBar { nullptr };   // Menu bar used inside custom title area when frameless
    
    // Tree view state
    bool m_treeViewVisible;
    QList<int> m_splitterSizes; // Store original splitter sizes

    // Search state
    QString m_lastSearchTerm;
    QVector<QString> m_searchResultPaths;
    int m_searchResultIndex { -1 };
    QTreeWidgetItem *m_searchResultsRoot { nullptr }; // top-level node for search results
    bool m_isSearchView { false }; // when true, tree shows only search results (flat)
    
    void setupUI();
    void setupTitleBar();
    void setupMenuBar();
    void setupStatusBar();
    void setupKeyboardShortcuts();
    void setupTreeView();
    void setupSourceToggleBar(); // create Local/Server toggle UI
    void setupTabWidget();  // Changed from setupContentArea
    void applyTreeViewTheme(); // Apply light theme stylesheet to tree view (dark mode disabled)
    void applyMenuBarMaterialStyle(); // Apply Material-like style to menu bar and right controls
    void applyAppPalette(bool dark);  // Force light palette (dark parameter ignored)
    void updateUserInfo();
    void toggleMaximizeRestore();
    void doMaximize();
    void doRestore();
    void setTreeSource(TreeSource src, bool forceReload=false);
    void refreshCurrentTree();
    QString currentRootPath() const;
    void loadLocalFiles();  // Changed from loadFileList
    void loadServerFiles(); // Mirror of local loader using m_serverRootPath
    void loadAwsFiles();    // Mirror of local loader using m_awsRootPath
    void setAwsRootPath(const QString &path);
    void autoLoadAwsCredentials(); // Auto-load saved AWS credentials if remember is enabled
    void loadLocalFileContent(const QString &filePath);  // Changed from loadFileContent
    void populateTreeFromDirectory(const QString &dirPath, QTreeWidgetItem *parentItem = nullptr);
    void openFileInTab(const QString &filePath);
    void openFileFromMemory(const QString &memoryId, const QString &originalKey);
    void openPDFInTab(const QString &filePath);
    void openPCBInTab(const QString &filePath);
    void addWelcomeTab();
    // Cross-linking structure
    struct TabLink { int pdfIndex; int pcbIndex; };
    std::vector<TabLink> m_tabLinks; // simple mapping list
    int linkedPcbForPdf(int pdfIndex) const; // returns pcb index or -1
    int linkedPdfForPcb(int pcbIndex) const; // returns pdf index or -1
    void refreshViewerLinkNames();
    void ensureAutoPairing(); // naive auto pairing for initial implementation
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

    // Window animations
    QPointer<QParallelAnimationGroup> m_enterAnim;
    void animateMinimize();
    void animateClose();

    // Tree search UI and logic
    void setupTreeSearchBar();
    void onTreeSearchTriggered();
    QVector<QString> findMatchingFiles(const QString &term, int maxResults = -1) const;
    QVector<QString> findMatchingFilesAsync(const QString &term, int maxResults = -1) const;
    bool revealPathInTree(const QString &absPath);
    static void expandToItem(QTreeWidgetItem *item);
    void renderSearchResultsFlat(const QVector<QString> &results, const QString &term);

    // Animation state
    QPointer<QParallelAnimationGroup> m_minimizeAnim;
    QPointer<QParallelAnimationGroup> m_closeAnim;
    bool m_closingNow { false }; // guard to allow close after animation
    // Custom maximize/restore state (for frameless window reliability on Windows)
    bool m_customMaximized { false };
    QRect m_savedNormalGeometry;

    // Tree/AWS loading UX and queueing
    QPointer<LoadingOverlay> m_treeLoadingOverlay; // overlay on tree panel during downloads/listing
    QPointer<LoadingOverlay> m_globalLoadingOverlay; // full-app overlay (main content area)
    bool m_treeBusy { false };                     // prevents re-entrancy
    bool m_cancelAwsQueue { false };               // stop after current item
    QStringList m_awsQueue;                        // pending S3 keys to download/open
    int m_awsQueueIndex { 0 };                     // current index in queue
    void startAwsDownloadQueue(const QStringList &keys);
    void processNextAwsDownload();
    void showTreeLoading(const QString &message, bool cancellable = true);
    void hideTreeLoading();
    void showGlobalLoading(const QString &message, bool cancellable = true);
    void hideGlobalLoading();

    // Simple notice dialog (non-loading) shown for user-facing messages like tab limits
    void showNoticeDialog(const QString &message, const QString &title = QStringLiteral("Notice"));
    
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

// --- Smooth hover enhancement (no new file created) ---
class SmoothTreeDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    SmoothTreeDelegate(QObject *parent=nullptr) : QStyledItemDelegate(parent) {}
    void setColors(const QColor &base, const QColor &hover) { m_base = base; m_hover = hover; }
    void setHovered(const QModelIndex &idx) {
        if (idx == m_hovered)
            return;
        m_last = m_hovered;
        m_hovered = idx;
        m_progress = 0.0;
    }
    bool advance() {
        if (!m_hovered.isValid() && !m_last.isValid()) return false;
        m_progress = std::min(1.0, m_progress + 0.12); // ease speed
        if (m_progress >= 1.0 && !m_hovered.isValid()) {
            m_last = QModelIndex();
        }
        return true;
    }
    void clearHover() { m_hovered = QModelIndex(); m_progress = 0.0; }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
private:
    QModelIndex m_hovered;
    QModelIndex m_last;
    QColor m_base;      // tree background
    QColor m_hover;     // target hover color
    double m_progress {0.0}; // 0..1
};

class SmoothTreeWidget : public QTreeWidget {
    Q_OBJECT
public:
    SmoothTreeWidget(QWidget *parent=nullptr);
    SmoothTreeDelegate *smoothDelegate() const { return m_delegate; }
protected:
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;
private:
    SmoothTreeDelegate *m_delegate;
    QTimer m_animTimer;
};


#endif // MAINAPPLICATION_H
