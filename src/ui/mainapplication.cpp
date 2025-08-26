
#include "ui/mainapplication.h"
#include "ui/dualtabwidget.h"
#include "viewers/pdf/pdfviewerwidget.h"
#include "viewers/pcb/PCBViewerWidget.h"
#include <QApplication>
#include <QCoreApplication>
#include <QScreen>
#include <QHeaderView>
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QTabWidget>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>
#include <QShortcut>
#include <QThread>
#include <QToolTip>
#include <QCursor>
#include "toastnotifier.h"
#include <QPainter>
#include <QEasingCurve>
#include <QMouseEvent>
#include <QLineEdit>
#include <QToolButton>
#include <QPushButton>
#include <QScrollBar>

namespace {
inline void writeTransitionLog(const QString &msg) {
    const QString logPath = QCoreApplication::applicationDirPath() + "/tab_debug.txt";
    QFile f(logPath);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
           << " [main] " << msg << '\n';
    }
}
}

MainApplication::MainApplication(const UserSession &userSession, QWidget *parent)
    : QMainWindow(parent)
    , m_userSession(userSession)
    , m_dbManager(new DatabaseManager(this))
    , m_rootFolderPath("C:\\W2R_Schematics")  // Local default folder
    , m_serverRootPath("\\\\192.168.1.2\\SharedFiles\\W2R_Schematics") // Default server UNC path
    // Server-side initialization (commented out for local file loading)
    //, m_networkManager(new QNetworkAccessManager(this))
    //, m_baseUrl("http://localhost/api") // WAMP server API endpoint
{
    writeTransitionLog("ctor: begin");
    setupUI();
    writeTransitionLog("ctor: after setupUI");
    setupMenuBar();
    setupStatusBar();
    updateUserInfo();
    
    // Setup keyboard shortcuts
    setupKeyboardShortcuts();
    
    // Set window properties
    setWindowTitle("Way2Repair - Equipment Maintenance System");
    setMinimumSize(1200, 800);
    resize(1400, 900);
    
    // Center the window (defensive: primaryScreen can be null on some systems briefly)
    if (QScreen *screen = QApplication::primaryScreen()) {
        const QRect screenGeometry = screen->availableGeometry();
        const int x = (screenGeometry.width() - width()) / 2;
        const int y = (screenGeometry.height() - height()) / 2;
        move(x, y);
    }
    
    // Set application icon (you can add an icon file later)
    setWindowIcon(QIcon(":/icons/app_icon.png"));
    
    // Load initial tree: default to Server mode; fallback to Local until a server path is set
    if (m_serverRootPath.isEmpty()) {
        // No server path yet
        // Ensure toggle reflects Local and load
        // setTreeSource defined later; safe to call
        setTreeSource(TreeSource::Local, true);
    } else {
        setTreeSource(TreeSource::Server, true);
    }
    writeTransitionLog("ctor: after loadLocalFiles");
    
    // Add welcome tab
    addWelcomeTab();
    writeTransitionLog("ctor: after addWelcomeTab");
    writeTransitionLog("ctor: end");
}

MainApplication::~MainApplication()
{
    // Clean up
}

void MainApplication::setupUI()
{
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    
    // Create main layout
    QHBoxLayout *mainLayout = new QHBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    
    // Create splitter for tree view and tab widget
    m_splitter = new QSplitter(Qt::Horizontal, this);
    
    // Setup tree view and tab widget
    setupTreeView();
    setupTabWidget();
    
    // Add to splitter
    m_splitter->addWidget(m_treePanel);
    m_splitter->addWidget(m_tabWidget);
    
    // Set splitter proportions (tree view: 25%, tabs: 75%)
    m_splitter->setSizes({300, 900});
    m_splitter->setCollapsible(0, true); // Allow tree view to collapse completely
    m_splitter->setCollapsible(1, false); // Don't allow tab widget to collapse
    
    // Set splitter handle width for better visibility
    m_splitter->setHandleWidth(3);
    m_splitter->setStyleSheet(
        "QSplitter::handle {"
        "    background-color: #d4e1f5;"
        "    border: 1px solid #a0b0c0;"
        "}"
        "QSplitter::handle:hover {"
        "    background-color: #4285f4;"
        "}"
    );
    
    // Initialize tree view state
    m_treeViewVisible = true;
    m_splitterSizes = {300, 900};
    
    mainLayout->addWidget(m_splitter);
}

void MainApplication::setupMenuBar()
{
    QMenuBar *menuBar = this->menuBar();
    menuBar->setStyleSheet(
        "QMenuBar {"
        "    background-color: #f8f9ff;"
        "    color: #2c3e50;"
        "    border-bottom: 1px solid #d4e1f5;"
        "    font-family: 'Segoe UI', Arial, sans-serif;"
        "}"
        "QMenuBar::item {"
        "    padding: 6px 12px;"
        "    background: transparent;"
        "}"
        "QMenuBar::item:selected {"
        "    background-color: #4285f4;"
        "    color: white;"
        "    border-radius: 3px;"
        "}"
    );
    
    // File menu
    QMenu *fileMenu = menuBar->addMenu("&File");
    QAction *logoutAction = fileMenu->addAction("&Logout");
    logoutAction->setShortcut(QKeySequence("Ctrl+L"));
    connect(logoutAction, &QAction::triggered, this, &MainApplication::onLogoutClicked);
    
    fileMenu->addSeparator();
    QAction *exitAction = fileMenu->addAction("E&xit");
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    
    // View menu
    QMenu *viewMenu = menuBar->addMenu("&View");
    
    QAction *toggleTreeAction = viewMenu->addAction("&Toggle Tree View");
    toggleTreeAction->setShortcut(QKeySequence("Ctrl+T"));
    toggleTreeAction->setCheckable(true);
    toggleTreeAction->setChecked(true);
    connect(toggleTreeAction, &QAction::triggered, this, &MainApplication::toggleTreeView);
    
    QAction *fullScreenPDFAction = viewMenu->addAction("&Full Screen PDF");
    fullScreenPDFAction->setShortcut(QKeySequence("F11"));
    connect(fullScreenPDFAction, &QAction::triggered, this, &MainApplication::toggleFullScreenPDF);
    
    viewMenu->addSeparator();
    QAction *fullUpdateAction = viewMenu->addAction("&Full Update UI");
    fullUpdateAction->setShortcut(QKeySequence("Ctrl+Shift+R"));
    connect(fullUpdateAction, &QAction::triggered, this, &MainApplication::onFullUpdateUI);
    
    viewMenu->addSeparator();
    viewMenu->addAction("&Refresh Tree", this, &MainApplication::refreshCurrentTree);
    viewMenu->addAction("&Expand All", this, [this]() { m_treeWidget->expandAll(); });
    viewMenu->addAction("&Collapse All", this, [this]() { m_treeWidget->collapseAll(); });
    
    // Help menu
    QMenu *helpMenu = menuBar->addMenu("&Help");
    helpMenu->addAction("&About", this, &MainApplication::onAboutClicked);
}

void MainApplication::setupStatusBar()
{
    m_statusBar = statusBar();
    m_statusBar->setStyleSheet(
        "QStatusBar {"
        "    background-color: #f8f9ff;"
        "    color: #2c3e50;"
        "    border-top: 1px solid #d4e1f5;"
        "    font-family: 'Segoe UI', Arial, sans-serif;"
        "}"
    );
    
    // Add permanent widgets to status bar
    QLabel *userLabel = new QLabel(QString("Logged in as: %1").arg(m_userSession.fullName), this);
    m_statusBar->addPermanentWidget(userLabel);
    
    QLabel *timeLabel = new QLabel(QString("Session started: %1").arg(m_userSession.loginTime.toString("hh:mm:ss")), this);
    m_statusBar->addPermanentWidget(timeLabel);
    
    m_statusBar->showMessage("Ready");
}

void MainApplication::onFullUpdateUI()
{
    if (m_tabWidget) {
        statusBar()->showMessage("Refreshing UI styles…", 1500);
        // Force reapply styles on both tab widgets
        m_tabWidget->forceStyleRefresh();
        // Also update any dynamic visuals
        // updateTabBarVisualState is private to DualTabWidget; forceStyleRefresh re-applies styles.
    }
}

// --- SmoothTreeDelegate paint implementation ---
void SmoothTreeDelegate::paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);

    // Determine animation state
    double t = 0.0;
    if (index == m_hovered) {
        t = m_progress; // fade in
    } else if (index == m_last) {
        t = 1.0 - m_progress; // fade out
    }
    t = std::clamp(t, 0.0, 1.0);
    // Ease for smoother feel
    t = QEasingCurve(QEasingCurve::OutCubic).valueForProgress(t);

    QColor baseCol = m_base;
    QColor hoverCol = m_hover;
    QColor blended = baseCol;
    blended.setRedF(baseCol.redF() + (hoverCol.redF() - baseCol.redF()) * t);
    blended.setGreenF(baseCol.greenF() + (hoverCol.greenF() - baseCol.greenF()) * t);
    blended.setBlueF(baseCol.blueF() + (hoverCol.blueF() - baseCol.blueF()) * t);
    blended.setAlphaF(baseCol.alphaF() + (hoverCol.alphaF() - baseCol.alphaF()) * t);

    // Selected state already handled by style sheet; only custom paint hover blend when not selected
    bool selected = (opt.state & QStyle::State_Selected);
    if (!selected && t > 0.0) {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, false);
        // Avoid painting over branch toggle region (indentation + 16px icon)
        const QTreeView *tv = qobject_cast<const QTreeView*>(parent());
        int indentation = tv ? tv->indentation() : 20;
        // Compute depth
        int depth = 0; QModelIndex ancestor = index.parent();
        while (ancestor.isValid()) { depth++; ancestor = ancestor.parent(); }
        int iconArea = depth * indentation + 16; // branch indicator + icon width
        QRect fillRect = opt.rect;
        fillRect.setX(fillRect.x() + iconArea);
        if (fillRect.x() < opt.rect.right()) {
            p->fillRect(fillRect, blended);
        }
        p->restore();
    }
    // Draw default text/icon
    QStyledItemDelegate::paint(p, opt, index);
}

// --- SmoothTreeWidget implementation ---
SmoothTreeWidget::SmoothTreeWidget(QWidget *parent) : QTreeWidget(parent) {
    m_delegate = new SmoothTreeDelegate(this);
    setItemDelegate(m_delegate);
    setMouseTracking(true);
    m_animTimer.setInterval(16); // ~60fps
    connect(&m_animTimer, &QTimer::timeout, this, [this]() {
        if (m_delegate->advance())
            viewport()->update();
        else if (!underMouse())
            m_animTimer.stop();
    });
}
void SmoothTreeWidget::mouseMoveEvent(QMouseEvent *e) {
    QTreeWidget::mouseMoveEvent(e);
    QModelIndex idx = indexAt(e->pos());
    m_delegate->setHovered(idx);
    if (!m_animTimer.isActive()) m_animTimer.start();
    viewport()->update();
}
void SmoothTreeWidget::leaveEvent(QEvent *e) {
    QTreeWidget::leaveEvent(e);
    m_delegate->setHovered(QModelIndex());
    if (!m_animTimer.isActive()) m_animTimer.start();
    viewport()->update();
}

void MainApplication::setupTreeView()
{
    // Container panel with vertical layout for floating search + tree
    m_treePanel = new QWidget(this);
    m_treePanel->setMinimumWidth(250);
    m_treePanel->setMaximumWidth(420);
    QVBoxLayout *panelLayout = new QVBoxLayout(m_treePanel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(6);

    // No title bar row; search bar will be the top row

    // Search bar row (with Server/Local toggle on the right)
    m_treeSearchBar = new QWidget(m_treePanel);
    QHBoxLayout *searchLayout = new QHBoxLayout(m_treeSearchBar);
    searchLayout->setContentsMargins(6, 6, 6, 0);
    searchLayout->setSpacing(6);

    m_treeSearchEdit = new QLineEdit(m_treeSearchBar);
    m_treeSearchEdit->setPlaceholderText("Search files by name… (Enter or Find)");
    // Use the built-in clear icon to avoid an extra button cluttering the UI
    m_treeSearchEdit->setClearButtonEnabled(true);
    m_treeSearchEdit->setMinimumHeight(30);
    // Prevent autofocusing at startup; only focus when clicked or via shortcut
    m_treeSearchEdit->setFocusPolicy(Qt::ClickFocus);

    // Remove separate clear button (kept member for compatibility but not shown)
    m_treeSearchClearButton = new QToolButton(m_treeSearchBar);
    m_treeSearchClearButton->setVisible(false);

    m_treeSearchButton = new QPushButton(m_treeSearchBar);
    m_treeSearchButton->setText(" Find");
    m_treeSearchButton->setCursor(Qt::PointingHandCursor);
    m_treeSearchButton->setMinimumHeight(30);
    m_treeSearchButton->setMinimumWidth(68);
    // Try to use app search icon if available
    QIcon searchIcon;
    if (QFile(":/images/icons/search_next.svg").exists()) {
        searchIcon.addFile(":/images/icons/search_next.svg");
    } else if (QFile(":/images/icons/zoom_in.svg").exists()) {
        searchIcon.addFile(":/images/icons/zoom_in.svg");
    }
    if (!searchIcon.isNull()) {
        m_treeSearchButton->setIcon(searchIcon);
        m_treeSearchButton->setIconSize(QSize(16,16));
    }

    searchLayout->addWidget(m_treeSearchEdit, 1);
    // No explicit clear button inserted; QLineEdit shows its own clear icon
    searchLayout->addWidget(m_treeSearchButton, 0);
    // Add the source toggle bar to the right of the search controls
    setupSourceToggleBar();
    searchLayout->addSpacing(6);
    searchLayout->addWidget(m_sourceToggleBar, 0, Qt::AlignVCenter);
    m_treeSearchBar->setLayout(searchLayout);

    // Tree widget
    m_treeWidget = new SmoothTreeWidget(m_treePanel);
    m_treeWidget->setHeaderLabel("Treeview");
    m_treeWidget->setMinimumWidth(250);
    m_treeWidget->setMaximumWidth(400);
    
    applyTreeViewTheme();
    
    // Connect signals
    connect(m_treeWidget, &QTreeWidget::itemClicked, this, &MainApplication::onTreeItemClicked);
    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked, this, &MainApplication::onTreeItemDoubleClicked);
    connect(m_treeWidget, &QTreeWidget::itemExpanded, this, &MainApplication::onTreeItemExpanded);
    connect(m_treeWidget, &QTreeWidget::itemCollapsed, this, &MainApplication::onTreeItemCollapsed);
    // Search connections
    connect(m_treeSearchButton, &QPushButton::clicked, this, &MainApplication::onTreeSearchTriggered);
    // Built-in clear button handles field clearing; also clear results when text becomes empty
    connect(m_treeSearchEdit, &QLineEdit::textChanged, this, [this](const QString &t){
        if (!t.trimmed().isEmpty()) return;
        m_searchResultPaths.clear();
        m_searchResultIndex = -1;
        if (m_isSearchView) {
            m_isSearchView = false;
            refreshCurrentTree();
        } else if (m_searchResultsRoot) {
            m_searchResultsRoot->takeChildren();
            m_searchResultsRoot->setHidden(true);
            m_searchResultsRoot->setExpanded(false);
        }
        statusBar()->showMessage("Search cleared");
    });
    connect(m_treeSearchEdit, &QLineEdit::returnPressed, this, &MainApplication::onTreeSearchTriggered);

    panelLayout->addWidget(m_treeSearchBar);
    panelLayout->addWidget(m_treeWidget, 1);
    m_treePanel->setLayout(panelLayout);
    
    // Tree will be populated via local file loading in constructor
}

void MainApplication::applyTreeViewTheme()
{
    if (!m_treeWidget)
        return;

    // Determine light vs dark based on window background lightness
    QColor base = palette().color(QPalette::Window);
    bool dark = base.lightness() < 128; // heuristic

    // Clean Design Palette - Pure White Background
    QString border        = dark ? "#39424c" : "#d7dbe2";           // Softer neutral border
    QString bg            = dark ? "#20262c" : "#ffffff";           // Pure white background - no alternating colors
    QString bgAlt         = dark ? "#262d33" : "#ffffff";           // Same as main background for uniform look
    QString text          = dark ? "#e2e8ef" : "#1a1a1a";           // Deep black for high contrast and readability
    QString textDisabled  = dark ? "#7a8794" : "#999999";           // Muted gray for disabled text
    QString placeholder   = dark ? "#8a96a3" : "#8b9197";          // Placeholder text color
    QString hover         = dark ? "#2d3640" : "#f3f5f7";           // Neutral hover
    QString selectedBg    = dark ? "#2f7dd8" : "#0078d4";           // Microsoft blue for selections
    QString selectedBgInactive = dark ? "#30485e" : "#e6f2ff";     // Light blue for inactive selection
    QString selectedText  = "#ffffff";                              // White text on selection
    QString focusOutline  = dark ? "#4da3ff" : "#0078d4";           // Blue focus outline
    QString scrollbarGroove = dark ? "#1b2126" : "#f5f5f5";        // Light gray scrollbar track
    QString scrollbarHandle = dark ? "#3a4753" : "#d0d0d0";        // Medium gray scrollbar handle
    QString scrollbarHandleHover = dark ? "#4a5a68" : "#b0b0b0";   // Darker gray on hover
    QString branchClosedIcon = dark ? ":/icons/images/icons/tree_branch_open.svg"   : ":/icons/images/icons/tree_branch_open_light.svg";   // plus symbol (for closed nodes)
    QString branchOpenIcon   = dark ? ":/icons/images/icons/tree_branch_closed.svg" : ":/icons/images/icons/tree_branch_closed_light.svg"; // minus symbol (for open nodes)
    QString altRow        = dark ? "#242b31" : "#ffffff";           // Same as main background for uniform appearance
    
    // Build clean stylesheet with smaller fonts and uniform background
    QString style = QString(
        "QTreeWidget {"
        "  border: 1px solid %1;"
        "  border-radius: 8px;"                                     // Slightly larger radius for modern look
        "  background: %2;"
        "  font-family: 'Segoe UI', 'SF Pro Display', -apple-system, BlinkMacSystemFont, sans-serif;" // Premium system fonts
        "  font-size: 12px;"                                        // Smaller font size for compact appearance
        "  font-weight: 500;"                                       // Medium weight for all text
        "  color: %3;"
        "  outline: none;"
        "  show-decoration-selected: 1;"
        "  selection-background-color: %6;"
        "  gridline-color: transparent;"                            // Remove grid lines for cleaner look
        "  alternate-background-color: %2;"                         // Same as main background - no alternating colors
        "}"
        "QTreeView::item, QTreeWidget::item {"
        "  padding: 6px 10px;"                                      // Reduced padding for compact look
        "  margin: 0px;"                                            // No margin for uniform appearance
        "  border: none;"
        "  color: %3;"
        "  font-size: 12px;"                                        // Smaller consistent font size
        "  font-weight: 500;"                                       // Medium weight for all items (files and folders same)
        "  min-height: 20px;"                                       // Smaller minimum height
        "  border-radius: 4px;"                                     // Smaller rounded corners for items
        "  background: transparent;"                                 // Transparent background by default
        "}"
        "QTreeWidget::item:disabled { "
        "  color: %4; "
        "  font-weight: 400;"                                       // Normal weight for disabled items
        "}"
        "QTreeWidget::item:hover {"
        "  background: %5;"                                         // Subtle hover effect
        "  border-radius: 4px;"
        "}"
        "QTreeWidget::item:selected {"
        "  background: %6;"
        "  color: %7;"
        "  border-radius: 4px;"
        "  font-weight: 500;"                                       // Same weight for selected items
        "}"
        "QTreeWidget::item:selected:!active {"
        "  background: %8;"
        "  color: %3;"
        "  border-radius: 4px;"
        "  font-weight: 500;"                                       // Same weight for inactive selection
        "}"
        "QTreeWidget::item:focus {"
        "  outline: none;"
        "  border-radius: 4px;"
        "}"
        "QTreeWidget::item:selected:focus {"
        "  box-shadow: 0 0 0 1px %11;"                              // Subtle focus outline
        "  border-radius: 4px;"
        "}"
        "QTreeWidget::header {"
        "  background: %2;"
        "  border: none;"
        "  font-weight: 600;"                                       // Bold header text
        "  font-size: 13px;"                                        // Slightly larger header font
        "  padding: 6px 10px;"
        "}"
        // Clean scrollbar styling
        "QScrollBar:vertical {"
        "  background: %12;"
        "  width: 12px;"                                            // Standard width
        "  margin: 0;"
        "  border: none;"
        "  border-radius: 6px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: %13;"
        "  min-height: 24px;"                                       // Standard handle size
        "  border-radius: 6px;"
        "  margin: 1px;"                                            // Small margin for visual separation
        "}"
        "QScrollBar::handle:vertical:hover {"
        "  background: %14;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar:horizontal {"
        "  background: %12;"
        "  height: 12px;"
        "  margin: 0;"
        "  border: none;"
        "  border-radius: 6px;"
        "}"
        "QScrollBar::handle:horizontal {"
        "  background: %13;"
        "  min-width: 24px;"
        "  border-radius: 6px;"
        "  margin: 1px;"
        "}"
        "QScrollBar::handle:horizontal:hover {"
        "  background: %14;"
        "}"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        ).arg(border, bg, text, textDisabled, hover, selectedBg, selectedText, selectedBgInactive,
              branchClosedIcon, branchOpenIcon, focusOutline, scrollbarGroove, scrollbarHandle,
              scrollbarHandleHover, altRow);

    m_treeWidget->setStyleSheet(style);
    m_treeWidget->setIndentation(20);                               // Standard indentation
    m_treeWidget->setIconSize(QSize(16, 16));                       // Standard icon size
    m_treeWidget->setRootIsDecorated(true);                         // Ensure branch decorations are shown
    m_treeWidget->setUniformRowHeights(true);                       // Uniform row heights for cleaner look
    m_treeWidget->setHeaderHidden(false);                           // Show header for professional look
    m_treeWidget->setAnimated(true);                                // Enable smooth expand/collapse animations
    
    // Configure clean styling for uniform appearance
    if (auto *smooth = qobject_cast<SmoothTreeWidget*>(m_treeWidget)) {
        QColor baseCol = QColor(bg);
        QColor hovCol = QColor(hover);
        hovCol.setAlphaF(0.5);                                      // Subtle hover effect
        smooth->smoothDelegate()->setColors(baseCol, hovCol);
    }
    
    // Set up clean uniform background - no alternating rows
    QPalette pal = m_treeWidget->palette();
    pal.setColor(QPalette::Base, QColor(bg));                       // Pure white background
    pal.setColor(QPalette::AlternateBase, QColor(bg));              // Same as base - no alternating colors
    pal.setColor(QPalette::Highlight, QColor(selectedBg));
    pal.setColor(QPalette::HighlightedText, QColor(selectedText));
    pal.setColor(QPalette::Text, QColor(text));
    m_treeWidget->setPalette(pal);
    m_treeWidget->setAlternatingRowColors(false);                   // Disable alternating row colors for uniform look
    
    // Additional clean UI settings
    m_treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);  // Smooth scrolling
    m_treeWidget->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_treeWidget->setFocusPolicy(Qt::StrongFocus);
    
    qDebug() << "Applied clean TreeView theme - Dark mode:" << dark;

    // Style search bar container and widgets to match theme (light/dark aware)
    if (m_treeSearchBar) {
        m_treeSearchBar->setStyleSheet(QString(
            "QWidget {"
            "  background: %1;"
            "}"
        ).arg(bg));
    }
    // Style search widgets to match theme
    if (m_treeSearchBar) {
        // Clean, compact field
        if (m_treeSearchEdit) {
            m_treeSearchEdit->setStyleSheet(QString(
                "QLineEdit {"
                "  border: 1px solid %1;"
                "  border-radius: 8px;"
                "  padding: 6px 10px;"
                "}"
                "QLineEdit::placeholder { color: %5; }"
                "QLineEdit:focus {"
                "  border: 1px solid %4;"
                "}")
                .arg(border, bg, text, focusOutline, placeholder, selectedBg, selectedText));
        }
    }
    // No title bar to style
    if (m_treeSearchButton) {
        // Outlined button, neutral until hovered/pressed
        m_treeSearchButton->setStyleSheet(QString(
            "QPushButton {"
            "  background: %2;"
            "  color: %3;"
            "  border: 1px solid %1;"
            "  border-radius: 8px;"
            "  padding: 6px 12px;"
            "}"
            "QPushButton:hover {"
            "  background: %5;"
            "}"
            "QPushButton:pressed {"
            "  background: %12;"
            "}")
            .arg(border, bg, text, focusOutline, hover, selectedBg, selectedText, selectedBgInactive, scrollbarGroove, scrollbarHandle, scrollbarHandleHover, bgAlt));
    }
    if (m_treeSearchClearButton) {
        // No visible clear button; style reset to avoid theme artifacts if ever shown
        m_treeSearchClearButton->setStyleSheet("QToolButton { border: none; background: transparent; } QToolButton:hover { background: transparent; }");
    }
}

void MainApplication::setupSourceToggleBar()
{
    m_sourceToggleBar = new QWidget(m_treePanel);
    auto *hl = new QHBoxLayout(m_sourceToggleBar);
    hl->setContentsMargins(6, 6, 6, 0);
    hl->setSpacing(6);

    m_btnServer = new QPushButton("Server", m_sourceToggleBar);
    m_btnLocal  = new QPushButton("Local", m_sourceToggleBar);
    m_btnServer->setCheckable(true);
    m_btnLocal->setCheckable(true);
    m_sourceGroup = new QButtonGroup(this);
    m_sourceGroup->setExclusive(true);
    m_sourceGroup->addButton(m_btnServer, 1);
    m_sourceGroup->addButton(m_btnLocal, 2);

    // Default visual selection: Server (constructor will set actual source)
    m_btnServer->setChecked(true);

    auto applyBtnStyle = [this](QPushButton *b){
        const QColor base = palette().color(QPalette::Window);
        bool dark = base.lightness() < 128;
        QString border = dark ? "#39424c" : "#d7dbe2";
        QString bg = dark ? "#20262c" : "#ffffff";
        QString text = dark ? "#e2e8ef" : "#1a1a1a";
        QString hover = dark ? "#2d3640" : "#f3f5f7";
        QString active = dark ? "#2f7dd8" : "#0078d4";
        QString activeText = "#ffffff";
        b->setStyleSheet(QString(
            "QPushButton { padding: 6px 12px; border: 1px solid %1; background: %2; color: %3; border-radius: 8px; }"
            "QPushButton:hover { background: %4; }"
            "QPushButton:checked { background: %5; color: %6; border-color: %5; }"
        ).arg(border, bg, text, hover, active, activeText));
    };
    applyBtnStyle(m_btnServer);
    applyBtnStyle(m_btnLocal);

    hl->addWidget(m_btnServer, 0);
    hl->addWidget(m_btnLocal, 0);
    hl->addStretch(1);

    connect(m_btnServer, &QPushButton::toggled, this, [this](bool on){ if (on) setTreeSource(TreeSource::Server, true); });
    connect(m_btnLocal,  &QPushButton::toggled, this, [this](bool on){ if (on) setTreeSource(TreeSource::Local, true);  });
}

// Create and style the search widgets (called within setupTreeView)
void MainApplication::setupTreeSearchBar() { /* kept for future extension if needed */ }

// Perform filename search under root and reveal results sequentially
void MainApplication::onTreeSearchTriggered()
{
    const QString term = m_treeSearchEdit ? m_treeSearchEdit->text().trimmed() : QString();
    if (term.isEmpty()) {
        statusBar()->showMessage("Enter a file name to search");
        return;
    }

    // Recompute full result list when term changes
    if (term.compare(m_lastSearchTerm, Qt::CaseInsensitive) != 0) {
        m_searchResultPaths = findMatchingFiles(term, -1);
        m_lastSearchTerm = term;
        m_searchResultIndex = -1; // kept for potential future use, not used to navigate anymore

        if (m_searchResultPaths.isEmpty()) {
            // If we were in a search-only view, restore the tree
            if (m_isSearchView) {
                m_isSearchView = false;
                loadLocalFiles();
            }
            // Hide Search Results section if present
            if (m_searchResultsRoot) {
                m_searchResultsRoot->takeChildren();
                m_searchResultsRoot->setHidden(true);
                m_searchResultsRoot->setExpanded(false);
            }
            statusBar()->showMessage("No files found");
            return;
        }

        // Show a flat list view with only the matching files
        renderSearchResultsFlat(m_searchResultPaths, term);
        statusBar()->showMessage(QString("%1 match(es)").arg(m_searchResultPaths.size()));
        return; // do not auto-select or navigate on first Find
    }

    // Term unchanged -> do not navigate; just reaffirm results count
    if (m_searchResultPaths.isEmpty()) {
        statusBar()->showMessage("No files found");
    } else {
        statusBar()->showMessage(QString("%1 match(es)").arg(m_searchResultPaths.size()));
    }
}

// Replace the tree with a flat list of search results
void MainApplication::renderSearchResultsFlat(const QVector<QString> &results, const QString &term)
{
    Q_UNUSED(term);
    m_isSearchView = true;
    m_treeWidget->clear();
    // Header label remains "Treeview" for consistency

    // Create a simple flat list: one top-level item per result
    QDir root(currentRootPath());
    for (const QString &path : results) {
        QFileInfo fi(path);
        QString relDir = root.relativeFilePath(fi.absolutePath());
        QString display = QString("%1 — %2").arg(fi.fileName(), relDir);
        QTreeWidgetItem *it = new QTreeWidgetItem(m_treeWidget);
        it->setText(0, display);
        it->setData(0, Qt::UserRole, fi.absoluteFilePath());
        it->setIcon(0, getFileIcon(fi.absoluteFilePath()));
        it->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
        it->setToolTip(0, fi.absoluteFilePath());
    }

    // Sort alphabetically for predictability and apply compact premium look
    m_treeWidget->sortItems(0, Qt::AscendingOrder);
    m_treeWidget->setAlternatingRowColors(true);
    m_treeWidget->setStyleSheet(m_treeWidget->styleSheet() + QString(
        "\nQTreeWidget::item { padding: 4px 6px; }\n"
        "QTreeWidget::item:selected { border-radius: 8px; }\n"));
}

QVector<QString> MainApplication::findMatchingFiles(const QString &term, int maxResults) const
{
    QVector<QString> results;
    QString rootPath = currentRootPath();
    if (rootPath.isEmpty()) return results;
    QDir root(rootPath);
    if (!root.exists()) return results;

    // Breadth-first traversal using a queue to be responsive for large trees
    QList<QString> dirs; dirs << root.absolutePath();
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    const bool noCap = (maxResults < 0);
    while (!dirs.isEmpty() && (noCap || results.size() < maxResults)) {
        const QString d = dirs.takeFirst();
        QDir dir(d);
        QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                                                  QDir::DirsFirst | QDir::Name);
        for (const QFileInfo &fi : entries) {
            if (fi.isDir()) {
                dirs << fi.absoluteFilePath();
            } else {
                if (fi.fileName().contains(term, cs)) {
                    results.push_back(fi.absoluteFilePath());
                    if (!noCap && results.size() >= maxResults) break;
                }
            }
        }
    }
    return results;
}

// Expand parent folders and select the file item if present (lazy-load children as needed)
bool MainApplication::revealPathInTree(const QString &absPath)
{
    if (!m_treeWidget) return false;
    QFileInfo fi(absPath);
    if (!fi.exists()) return false;

    // Build list of folder names from root to file's parent
    QString rel = QDir(currentRootPath()).relativeFilePath(fi.absolutePath());
    QStringList parts = rel.split(QDir::separator(), Qt::SkipEmptyParts);

    // Find the root item matching m_rootFolderPath; top-level items correspond to entries of the root
    // For our tree, folders are top-level items initially; iterate accordingly
    auto matchChildByName = [](QTreeWidgetItem *item, const QString &name) -> QTreeWidgetItem* {
        for (int i = 0; i < (item ? item->childCount() : 0); ++i) {
            QTreeWidgetItem *c = item->child(i);
            if (c && c->text(0).compare(name, Qt::CaseInsensitive) == 0) return c;
        }
        return nullptr;
    };

    // Navigate/expand down the path
    QTreeWidgetItem *current = nullptr;
    QTreeWidget *tw = m_treeWidget;
    // At top level, search among tw->topLevelItem(i)
    for (const QString &folderName : parts) {
        if (!current) {
            QTreeWidgetItem *next = nullptr;
            for (int i = 0; i < tw->topLevelItemCount(); ++i) {
                QTreeWidgetItem *ti = tw->topLevelItem(i);
                if (ti->data(0, Qt::UserRole + 1).toString().endsWith(folderName, Qt::CaseInsensitive) ||
                    ti->text(0).compare(folderName, Qt::CaseInsensitive) == 0) {
                    next = ti; break;
                }
            }
            if (!next) return false;
            current = next;
        } else {
            // Ensure children are loaded (expand triggers load)
            if (!current->isExpanded()) {
                current->setExpanded(true);
                // If had a dummy, onTreeItemExpanded will populate
            }
            QCoreApplication::processEvents();
            // Refresh to ensure children are accessible
            QTreeWidgetItem *next = matchChildByName(current, folderName);
            if (!next) {
                // Try after forcing expansion signal to run population
                onTreeItemExpanded(current);
                QCoreApplication::processEvents();
                next = matchChildByName(current, folderName);
            }
            if (!next) return false;
            current = next;
        }
    }

    // Now select the file within current
    if (current && !current->isExpanded()) {
        current->setExpanded(true);
        onTreeItemExpanded(current);
        QCoreApplication::processEvents();
    }

    QString baseName = fi.baseName();
    QTreeWidgetItem *fileItem = nullptr;
    if (!current) {
        // Could be a file at top level
        for (int i = 0; i < tw->topLevelItemCount(); ++i) {
            QTreeWidgetItem *ti = tw->topLevelItem(i);
            if (ti->data(0, Qt::UserRole).toString() == fi.absoluteFilePath() ||
                ti->text(0).compare(baseName, Qt::CaseInsensitive) == 0) { fileItem = ti; break; }
        }
    } else {
        for (int i = 0; i < current->childCount(); ++i) {
            QTreeWidgetItem *c = current->child(i);
            if (c->data(0, Qt::UserRole).toString() == fi.absoluteFilePath() ||
                c->text(0).compare(baseName, Qt::CaseInsensitive) == 0) { fileItem = c; break; }
        }
    }
    if (!fileItem) return false;

    expandToItem(fileItem);
    tw->setCurrentItem(fileItem);
    tw->scrollToItem(fileItem, QAbstractItemView::PositionAtCenter);
    return true;
}

void MainApplication::expandToItem(QTreeWidgetItem *item)
{
    if (!item) return;
    QTreeWidgetItem *p = item->parent();
    while (p) { p->setExpanded(true); p = p->parent(); }
}

void MainApplication::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::StyleChange) {
        applyTreeViewTheme();
    }
}

void MainApplication::setupTabWidget()
{
    m_tabWidget = new DualTabWidget();
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    
    // Connect dual tab widget signals
    connect(m_tabWidget, &DualTabWidget::tabCloseRequested, 
            this, &MainApplication::onTabCloseRequestedByType);
    connect(m_tabWidget, &DualTabWidget::currentChanged, 
            this, &MainApplication::onTabChangedByType);
    // Non-blocking notification when user hits tab limit (avoid modal dialog that interrupts rendering)
    connect(m_tabWidget, &DualTabWidget::tabLimitReached, this, [this](DualTabWidget::TabType type, int maxTabs){
        QString kind = (type == DualTabWidget::PDF_TAB) ? "PDF" : "PCB";
        statusBar()->showMessage(QString("%1 tab limit (%2) reached. Close a tab before opening another.").arg(kind).arg(maxTabs), 5000);
    });
}

void MainApplication::loadLocalFiles()
{
    statusBar()->showMessage("Loading files from local directory...");
    
    // Clear existing tree items
    m_treeWidget->clear();
    m_searchResultsRoot = nullptr;
    
    // Check if root folder exists
    QDir rootDir(m_rootFolderPath);
    if (!rootDir.exists()) {
        // Create the folder if it doesn't exist
        if (!rootDir.mkpath(m_rootFolderPath)) {
            statusBar()->showMessage("Error: Could not create or access folder: " + m_rootFolderPath);
            return;
        }
    }
    
    // Populate tree from directory
    populateTreeFromDirectory(m_rootFolderPath);

    // Add (collapsed) Search Results section
    m_searchResultsRoot = new QTreeWidgetItem(m_treeWidget);
    m_searchResultsRoot->setText(0, "Search Results");
    m_searchResultsRoot->setIcon(0, style()->standardIcon(QStyle::SP_FileDialogContentsView));
    m_searchResultsRoot->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
    m_searchResultsRoot->setHidden(true);
    m_searchResultsRoot->setExpanded(false);
    
    // Collapse all folders by default for a clean view
    m_treeWidget->collapseAll();
    
    statusBar()->showMessage(QString("Loaded files from: %1").arg(m_rootFolderPath));
}

void MainApplication::loadServerFiles()
{
    statusBar()->showMessage("Loading files from server directory...");
    m_treeWidget->clear();
    m_searchResultsRoot = nullptr;

    if (m_serverRootPath.isEmpty()) {
        statusBar()->showMessage("Server path not set. Switch to Local or provide server path.");
        return;
    }
    QDir rootDir(m_serverRootPath);
    if (!rootDir.exists()) {
        statusBar()->showMessage("Error: Server folder not accessible: " + m_serverRootPath);
        return;
    }
    populateTreeFromDirectory(m_serverRootPath);

    m_searchResultsRoot = new QTreeWidgetItem(m_treeWidget);
    m_searchResultsRoot->setText(0, "Search Results");
    m_searchResultsRoot->setIcon(0, style()->standardIcon(QStyle::SP_FileDialogContentsView));
    m_searchResultsRoot->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
    m_searchResultsRoot->setHidden(true);
    m_searchResultsRoot->setExpanded(false);

    m_treeWidget->collapseAll();
    statusBar()->showMessage(QString("Loaded files from: %1").arg(m_serverRootPath));
}

void MainApplication::setServerRootPath(const QString &path)
{
    m_serverRootPath = path;
    if (m_treeSource == TreeSource::Server)
        refreshCurrentTree();
}

void MainApplication::setTreeSource(TreeSource src, bool forceReload)
{
    if (!forceReload && m_treeSource == src) return;
    m_treeSource = src;
    if (m_btnServer && m_btnLocal) {
        m_btnServer->blockSignals(true);
        m_btnLocal->blockSignals(true);
        m_btnServer->setChecked(src == TreeSource::Server);
        m_btnLocal->setChecked(src == TreeSource::Local);
        m_btnServer->blockSignals(false);
        m_btnLocal->blockSignals(false);
    }
    // No title label to update
    refreshCurrentTree();
}

QString MainApplication::currentRootPath() const
{
    return (m_treeSource == TreeSource::Server && !m_serverRootPath.isEmpty())
            ? m_serverRootPath
            : m_rootFolderPath;
}

void MainApplication::refreshCurrentTree()
{
    if (m_treeSource == TreeSource::Server) {
        if (m_serverRootPath.isEmpty()) {
            statusBar()->showMessage("Server path not set. Showing Local.");
            loadLocalFiles();
        } else {
            loadServerFiles();
        }
    } else {
        loadLocalFiles();
    }
}

void MainApplication::populateTreeFromDirectory(const QString &dirPath, QTreeWidgetItem *parentItem)
{
    QDir dir(dirPath);
    if (!dir.exists()) {
        return;
    }
    
    // Get all entries (files and directories)
    QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    
    for (const QFileInfo &entry : entries) {
        QTreeWidgetItem *item;
        
        if (parentItem) {
            item = new QTreeWidgetItem(parentItem);
        } else {
            item = new QTreeWidgetItem(m_treeWidget);
        }
        
        // Set up the tree item appearance
        setupTreeItemAppearance(item, entry);
        
        if (entry.isDir()) {
            // This is a directory
            item->setData(0, Qt::UserRole + 1, entry.absoluteFilePath()); // Store folder path
            
            // Add a dummy child item to make the folder expandable
            // The actual children will be loaded when the folder is expanded
            QTreeWidgetItem *dummyItem = new QTreeWidgetItem(item);
            dummyItem->setText(0, "Loading...");
            dummyItem->setData(0, Qt::UserRole + 2, true); // Mark as dummy
            
            // Don't expand folders by default - let user expand them
            item->setExpanded(false);
        } else {
            // This is a file
            item->setData(0, Qt::UserRole, entry.absoluteFilePath()); // Store file path
            
            // Make files non-expandable by ensuring they have no children
            // and can't be expanded
            item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
        }
    }
}

void MainApplication::openFileInTab(const QString &filePath)
{
    // Show loading message
    statusBar()->showMessage("Loading file...");
    
    // Check if it's a PDF file
    QFileInfo fileInfo(filePath);
    QString extension = fileInfo.suffix().toLower();
    
    if (extension == "pdf") {
        openPDFInTab(filePath);
        return;
    }
    
    // Check if it's a PCB file
    if (extension == "xzz" || extension == "pcb" || extension == "xzzpcb") {
        openPCBInTab(filePath);
        return;
    }
    
    // For non-PDF/PCB files, show a message
    statusBar()->showMessage("Only PDF and PCB files are supported in tabs");
    QMessageBox::information(this, "File Type Not Supported", 
        "Only PDF files (.pdf) and PCB files (.xzz, .pcb, .xzzpcb) can be opened in tabs.\n\n"
        "Selected file: " + fileInfo.fileName() + "\n"
        "File type: " + (extension.isEmpty() ? "Unknown" : extension.toUpper()));
}

void MainApplication::openPDFInTab(const QString &filePath)
{
    // First: see if this PDF is already open (should NOT be blocked by limit)
    auto normalizePath = [](QString p){ QString n = p; n.replace("\\", "/"); return n.toLower(); };
    const QString targetNorm = normalizePath(filePath);
    for (int i = 0; i < m_tabWidget->count(DualTabWidget::PDF_TAB); ++i) {
        QWidget *tabWidget = m_tabWidget->widget(i, DualTabWidget::PDF_TAB);
        if (tabWidget) {
            QString existing = tabWidget->property("filePath").toString();
            if (!existing.isEmpty() && normalizePath(existing) == targetNorm) {
                m_tabWidget->setCurrentIndex(i, DualTabWidget::PDF_TAB);
                statusBar()->showMessage("PDF already open (switched)", 4000);
                return;
            }
        }
    }

    // Enforce max PDF tab limit ONLY for new tabs
    const int kMaxPdfTabs = 5;
    if (m_tabWidget->count(DualTabWidget::PDF_TAB) >= kMaxPdfTabs) {
        const QString msg = QString("PDF tab limit (%1) reached. Close a tab before opening another.").arg(kMaxPdfTabs);
        statusBar()->showMessage(msg, 6000);
        QToolTip::showText(QCursor::pos(), msg, this, QRect(), 2500);
        QApplication::beep();
        return;
    }

    // Proceed with load
    statusBar()->showMessage("Loading PDF file...");
    
    // (Duplicate check already performed above)
    
    // Verify file exists and is readable
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isReadable()) {
        statusBar()->showMessage("Error: Cannot access PDF file: " + filePath);
        QMessageBox::warning(this, "PDF Error", "Cannot access PDF file:\n" + filePath);
        return;
    }
    
    // Create PDF viewer widget
    PDFViewerWidget *pdfViewer = new PDFViewerWidget();
    pdfViewer->setProperty("filePath", filePath);
    
    // No toolbar to hide - PDF viewer is now toolbar-free
    
    // Connect PDF viewer signals
    connect(pdfViewer, &PDFViewerWidget::pdfLoaded, this, [this, filePath](const QString &loadedPath) {
        Q_UNUSED(loadedPath)
        QFileInfo fileInfo(filePath);
        statusBar()->showMessage(QString("PDF loaded: %1").arg(fileInfo.fileName()));
    });
    
    connect(pdfViewer, &PDFViewerWidget::errorOccurred, this, [this](const QString &error) {
        statusBar()->showMessage("PDF Error: " + error);
        QMessageBox::warning(this, "PDF Error", error);
    });
    
    connect(pdfViewer, &PDFViewerWidget::pageChanged, this, [this](int currentPage, int totalPages) {
        statusBar()->showMessage(QString("PDF Page %1 of %2").arg(currentPage).arg(totalPages));
    });
    
    connect(pdfViewer, &PDFViewerWidget::zoomChanged, this, [this](double zoomLevel) {
        statusBar()->showMessage(QString("PDF Zoom: %1%").arg(static_cast<int>(zoomLevel * 100)));
    });
    
    // Split view removed: embedding and tree view toggle connections omitted
    
    // Add PDF viewer to PDF tab row (will fail with -1 if limit race condition)
    QString tabName = fileInfo.fileName();
    QIcon tabIcon = getFileIcon(filePath);
    int tabIndex = m_tabWidget->addTab(pdfViewer, tabIcon, tabName, DualTabWidget::PDF_TAB);
    if (tabIndex < 0) {
        // Safety: dual tab widget rejected addition (limit). Clean up and exit.
        pdfViewer->deleteLater();
        statusBar()->showMessage("Cannot open PDF: tab limit reached.", 5000);
        return;
    }
    
    // Switch to the new tab
    m_tabWidget->setCurrentIndex(tabIndex, DualTabWidget::PDF_TAB);
    
    // Load the PDF after the widget is properly initialized
    QTimer::singleShot(100, this, [this, pdfViewer, filePath, tabIndex, fileInfo]() {
        // Try to load the PDF after the widget is properly initialized
    pdfViewer->requestLoad(filePath); // Phase 1 async scaffold
    if (false && !pdfViewer->loadPDF(filePath)) { // legacy path disabled
            // If loading fails, remove the tab and show error
            if (tabIndex < m_tabWidget->count(DualTabWidget::PDF_TAB)) {
                m_tabWidget->removeTab(tabIndex, DualTabWidget::PDF_TAB);
            }
            statusBar()->showMessage("Error: Failed to load PDF file: " + filePath);
            
            QString errorMessage = QString(
                "Failed to load PDF file:\n\n"
                "File: %1\n"
                "Path: %2\n\n"
                "This PDF file may be:\n"
                "• Corrupted or invalid\n"
                "• Incompatible with PDFium library\n" 
                "• Not accessible due to permissions\n\n"
                "The PDF viewer's internal tab system has been disabled to work with the main application tabs."
            ).arg(fileInfo.fileName()).arg(filePath);
            
            QMessageBox::warning(this, "PDF Loading Error", errorMessage);
            return;
        }
    });
    
    // Set tab tooltip
    QString tooltip = QString("PDF File: %1\nPath: %2\nSize: %3 bytes")
                        .arg(fileInfo.fileName())
                        .arg(filePath)
                        .arg(fileInfo.size());
    m_tabWidget->setTabToolTip(tabIndex, tooltip, DualTabWidget::PDF_TAB);
    
    statusBar()->showMessage(QString("Opened PDF: %1").arg(fileInfo.fileName()));
    ensureAutoPairing();
    refreshViewerLinkNames();
}

void MainApplication::openPCBInTab(const QString &filePath)
{
    // Duplicate first (not blocked by limit)
    for (int i = 0; i < m_tabWidget->count(DualTabWidget::PCB_TAB); ++i) {
        QWidget *tabWidget = m_tabWidget->widget(i, DualTabWidget::PCB_TAB);
        if (tabWidget && tabWidget->property("filePath").toString() == filePath) {
            m_tabWidget->setCurrentIndex(i, DualTabWidget::PCB_TAB);
            statusBar()->showMessage("PCB already open (switched)", 4000);
            return;
        }
    }

    // Limit only for new tabs
    const int kMaxPcbTabs = 5;
    if (m_tabWidget->count(DualTabWidget::PCB_TAB) >= kMaxPcbTabs) {
        const QString msg = QString("PCB tab limit (%1) reached. Close a tab before opening another.").arg(kMaxPcbTabs);
        statusBar()->showMessage(msg, 6000);
        QToolTip::showText(QCursor::pos(), msg, this, QRect(), 2500);
        QApplication::beep();
        return;
    }

    statusBar()->showMessage("Loading PCB file...");
    
    // Verify file exists and is readable
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isReadable()) {
        statusBar()->showMessage("Error: Cannot access PCB file: " + filePath);
        QMessageBox::warning(this, "PCB Error", "Cannot access PCB file:\n" + filePath);
        return;
    }
    
    // Create PCB viewer widget
    PCBViewerWidget *pcbViewer = new PCBViewerWidget();
    pcbViewer->setProperty("filePath", filePath);
    
    // Start with toolbar hidden - it will be shown when tab becomes active
    pcbViewer->setToolbarVisible(false);
    
    // Connect PCB viewer signals
    connect(pcbViewer, &PCBViewerWidget::pcbLoaded, this, [this, filePath](const QString &loadedPath) {
        Q_UNUSED(loadedPath)
        QFileInfo fileInfo(filePath);
        statusBar()->showMessage(QString("PCB loaded: %1").arg(fileInfo.fileName()));
    });
    
    connect(pcbViewer, &PCBViewerWidget::errorOccurred, this, [this](const QString &error) {
        statusBar()->showMessage("PCB Error: " + error);
        QMessageBox::warning(this, "PCB Error", error);
    });
    
    // Split view removed: embedding and tree view toggle connections omitted
    
    // Add PCB viewer to PCB tab row
    QString tabName = fileInfo.fileName();
    QIcon tabIcon = getFileIcon(filePath);
    int tabIndex = m_tabWidget->addTab(pcbViewer, tabIcon, tabName, DualTabWidget::PCB_TAB);
    
    // Switch to the new tab
    m_tabWidget->setCurrentIndex(tabIndex, DualTabWidget::PCB_TAB);
    
    // Load the PCB after the widget is properly initialized
    QTimer::singleShot(100, this, [this, pcbViewer, filePath, tabIndex, fileInfo]() {
        // Try to load the PCB after the widget is properly initialized
    pcbViewer->requestLoad(filePath); // Phase 1 async scaffold
    if (false && !pcbViewer->loadPCB(filePath)) { // legacy path disabled
            // If loading fails, remove the tab and show error
            if (tabIndex < m_tabWidget->count(DualTabWidget::PCB_TAB)) {
                m_tabWidget->removeTab(tabIndex, DualTabWidget::PCB_TAB);
            }
            statusBar()->showMessage("Error: Failed to load PCB file: " + filePath);
            
            QString errorMessage = QString(
                "Failed to load PCB file:\n\n"
                "File: %1\n"
                "Path: %2\n\n"
                "This PCB file may be:\n"
                "• Corrupted or invalid\n"
                "• Incompatible with XZZPCB format\n" 
                "• Not accessible due to permissions\n\n"
                "Supported formats: .xzz, .pcb, .xzzpcb"
            ).arg(fileInfo.fileName()).arg(filePath);
            
            QMessageBox::warning(this, "PCB Loading Error", errorMessage);
            return;
        }
    });
    
    // Set tab tooltip
    QString tooltip = QString("PCB File: %1\nPath: %2\nSize: %3 bytes")
                        .arg(fileInfo.fileName())
                        .arg(filePath)
                        .arg(fileInfo.size());
    m_tabWidget->setTabToolTip(tabIndex, tooltip, DualTabWidget::PCB_TAB);
    
    statusBar()->showMessage(QString("Opened PCB: %1").arg(fileInfo.fileName()));
    ensureAutoPairing();
    refreshViewerLinkNames();
}

void MainApplication::onTabCloseRequestedByType(int index, DualTabWidget::TabType type)
{
    if (index >= 0 && index < m_tabWidget->count(type)) {
        QWidget *tabWidget = m_tabWidget->widget(index, type);
        if (tabWidget) {
            QString filePath = tabWidget->property("filePath").toString();
            
            // Remove the tab
            m_tabWidget->removeTab(index, type);
            
            // Update status bar
            if (!filePath.isEmpty()) {
                QFileInfo fileInfo(filePath);
                QString fileType = (type == DualTabWidget::PDF_TAB) ? "PDF" : "PCB";
                statusBar()->showMessage(QString("Closed %1 file: %2").arg(fileType, fileInfo.fileName()));
            } else {
                statusBar()->showMessage("Closed tab");
            }
        }
    }
}

void MainApplication::onTabChangedByType(int index, DualTabWidget::TabType type)
{
    qDebug() << "=== Tab Changed to Index:" << index << "Type:" << (type == DualTabWidget::PDF_TAB ? "PDF" : "PCB") << "===";
    
    // Light isolation: just ensure other toolbars hidden (avoid hiding GL widgets to prevent context glitches)
    // Additionally, throttle PDF viewers in inactive tabs to cut GPU usage.
    for (int t = 0; t < m_tabWidget->count(DualTabWidget::PDF_TAB); ++t) {
        if (QWidget *w = m_tabWidget->widget(t, DualTabWidget::PDF_TAB)) {
            if (auto pdf = qobject_cast<PDFViewerWidget*>(w)) {
                // If not the newly selected tab, hide (stops its timer in hideEvent)
                if (!(type == DualTabWidget::PDF_TAB && t == index)) {
                    pdf->hide();
                } else {
                    pdf->show();
                }
            }
        }
    }
    hideAllViewerToolbars();
    
    // If no valid tab is selected, return
    if (index < 0 || index >= m_tabWidget->count(type)) {
        statusBar()->showMessage("No active tab");
        qDebug() << "Invalid tab index, returning";
        return;
    }
    
    // Get the current widget
    QWidget *currentWidget = m_tabWidget->widget(index, type);
    if (!currentWidget) {
        statusBar()->showMessage("Invalid tab selected");
        qDebug() << "Current widget is null, returning";
        return;
    }
    
    QString tabName = m_tabWidget->tabText(index, type);
    qDebug() << "Switching to tab:" << tabName;
    
    // Force focus and bring current widget to front
    currentWidget->setFocus();
    currentWidget->raise();
    currentWidget->activateWindow();
    
    // Removed sleep; unnecessary and could stall UI
    
    // Split view removed: no cross-embedded viewers to restore

    // Show appropriate toolbar based on widget type
    if (type == DualTabWidget::PDF_TAB) {
        if (auto pdfViewer = qobject_cast<PDFViewerWidget*>(currentWidget)) {
            qDebug() << "Activating PDF viewer for tab:" << tabName;
            
            // Ensure PDF viewer is properly initialized and visible
            pdfViewer->setVisible(true);
            pdfViewer->raise();
            
            // No toolbar to show - PDF viewer is now toolbar-free
            
            // Force layout updates
            pdfViewer->updateGeometry();
            pdfViewer->update();

            // Ensure viewport/camera sync after activation (fixes horizontal gap)
            QTimer::singleShot(0, this, [pdfViewer]() { pdfViewer->ensureViewportSync(); });
            
            statusBar()->showMessage("PDF viewer active - Use keyboard shortcuts for navigation");
        }
    } else if (type == DualTabWidget::PCB_TAB) {
        if (auto pcbViewer = qobject_cast<PCBViewerWidget*>(currentWidget)) {
            qDebug() << "Activating PCB viewer for tab:" << tabName;
            
            // Ensure PCB viewer is properly initialized and visible
            pcbViewer->setVisible(true);
            pcbViewer->raise();
            
            // Enable PCB toolbar
            pcbViewer->setToolbarVisible(true);
            
            // Force layout updates
            pcbViewer->updateGeometry();
            pcbViewer->update();

            // Ensure viewport/camera sync after activation
            QTimer::singleShot(0, this, [pcbViewer]() { pcbViewer->ensureViewportSync(); });
            
            statusBar()->showMessage("PCB viewer active - Qt toolbar controls available");
        }
    }
    
    // Force multiple event processing cycles to ensure complete showing
    QApplication::processEvents();
    QApplication::processEvents();
    
    qDebug() << "=== Tab Change Complete ===";
    refreshViewerLinkNames();
}

    int MainApplication::linkedPcbForPdf(int pdfIndex) const {
            for (const auto &l : m_tabLinks) {
                if (l.pdfIndex == pdfIndex)
                    return l.pcbIndex;
            }
            return -1;
    }
    int MainApplication::linkedPdfForPcb(int pcbIndex) const {
            for (const auto &l : m_tabLinks) {
                if (l.pcbIndex == pcbIndex)
                    return l.pdfIndex;
            }
            return -1;
    }
    void MainApplication::ensureAutoPairing() {
        int pdfCount = m_tabWidget->count(DualTabWidget::PDF_TAB);
        int pcbCount = m_tabWidget->count(DualTabWidget::PCB_TAB);
        for (int p = 0; p < pdfCount; ++p) {
            if (linkedPcbForPdf(p) >= 0) continue;
            for (int c = 0; c < pcbCount; ++c) {
                if (linkedPdfForPcb(c) < 0) { m_tabLinks.push_back({p,c}); break; }
            }
        }
    }
    void MainApplication::refreshViewerLinkNames() {
        int pdfCount = m_tabWidget->count(DualTabWidget::PDF_TAB);
        int pcbCount = m_tabWidget->count(DualTabWidget::PCB_TAB);
        for (int p=0; p<pdfCount; ++p) {
            if (auto pdf = qobject_cast<PDFViewerWidget*>(m_tabWidget->widget(p, DualTabWidget::PDF_TAB))) {
                int pcbIdx = linkedPcbForPdf(p); QString name; if (pcbIdx>=0) name = m_tabWidget->tabText(pcbIdx, DualTabWidget::PCB_TAB); pdf->setLinkedPcbFileName(name);
                connect(pdf, &PDFViewerWidget::crossSearchRequest, this, &MainApplication::onCrossSearchRequest, Qt::UniqueConnection);
            }
        }
        for (int c=0; c<pcbCount; ++c) {
            if (auto pcb = qobject_cast<PCBViewerWidget*>(m_tabWidget->widget(c, DualTabWidget::PCB_TAB))) {
                int pdfIdx = linkedPdfForPcb(c); QString name; if (pdfIdx>=0) name = m_tabWidget->tabText(pdfIdx, DualTabWidget::PDF_TAB); pcb->setLinkedPdfFileName(name);
                connect(pcb, &PCBViewerWidget::crossSearchRequest, this, &MainApplication::onCrossSearchRequest, Qt::UniqueConnection);
            }
        }
    }

    void MainApplication::onCrossSearchRequest(const QString &term, bool isNet, bool targetIsOther) {
        Q_UNUSED(targetIsOther);
        auto pdfSender = qobject_cast<PDFViewerWidget*>(sender());
        auto pcbSender = qobject_cast<PCBViewerWidget*>(sender());
        if (pdfSender) {
            int pdfIdx=-1; int pdfCount=m_tabWidget->count(DualTabWidget::PDF_TAB);
            for (int i=0;i<pdfCount;++i) if (m_tabWidget->widget(i, DualTabWidget::PDF_TAB)==pdfSender) { pdfIdx=i; break; }
            // Dynamic target resolution: prefer currently selected PCB tab (independent row selection), fallback to auto-pair mapping
            int pcbIdx = m_tabWidget->getSelectedIndex(DualTabWidget::PCB_TAB);
            if (pcbIdx < 0 || pcbIdx >= m_tabWidget->count(DualTabWidget::PCB_TAB)) {
                pcbIdx = (pdfIdx>=0)? linkedPcbForPdf(pdfIdx) : -1; // fallback
            }
            if (pcbIdx<0) { ToastNotifier::show(this, "Select a PCB tab to target"); return; }
            auto pcbW = qobject_cast<PCBViewerWidget*>(m_tabWidget->widget(pcbIdx, DualTabWidget::PCB_TAB));
            if (!pcbW) { ToastNotifier::show(this, "No linked file found"); return; }
            bool ok = isNet ? pcbW->externalSearchNet(term) : pcbW->externalSearchComponent(term);
            if (!ok) ToastNotifier::show(this, "No matches found"); else m_tabWidget->setCurrentIndex(pcbIdx, DualTabWidget::PCB_TAB);
        } else if (pcbSender) {
            int pcbIdx=-1; int pcbCount=m_tabWidget->count(DualTabWidget::PCB_TAB);
            for (int i=0;i<pcbCount;++i) if (m_tabWidget->widget(i, DualTabWidget::PCB_TAB)==pcbSender) { pcbIdx=i; break; }
            // Dynamic target resolution: prefer currently selected PDF tab, fallback to auto-pair mapping
            int pdfIdx = m_tabWidget->getSelectedIndex(DualTabWidget::PDF_TAB);
            if (pdfIdx < 0 || pdfIdx >= m_tabWidget->count(DualTabWidget::PDF_TAB)) {
                pdfIdx = (pcbIdx>=0)? linkedPdfForPcb(pcbIdx) : -1; // fallback
            }
            if (pdfIdx<0) { ToastNotifier::show(this, "Select a PDF tab to target"); return; }
            auto pdfW = qobject_cast<PDFViewerWidget*>(m_tabWidget->widget(pdfIdx, DualTabWidget::PDF_TAB));
            if (!pdfW) { ToastNotifier::show(this, "No linked file found"); return; }
            bool ok = pdfW->externalFindText(term);
            if (!ok) ToastNotifier::show(this, "No matches found"); else m_tabWidget->setCurrentIndex(pdfIdx, DualTabWidget::PDF_TAB);
        }
    }

// Old onTabChanged method - replaced by onTabChangedByType
/*
void MainApplication::onTabChanged(int index)
{
    // This method is no longer used - replaced by onTabChangedByType
    // which handles the dual tab widget system with PDF/PCB type awareness
}
*/

void MainApplication::hideAllViewerToolbars()
{
    qDebug() << "=== Hiding All Viewer Toolbars ===";
    
    // Iterate through all PDF tabs and hide their toolbars
    for (int i = 0; i < m_tabWidget->count(DualTabWidget::PDF_TAB); ++i) {
        QWidget *widget = m_tabWidget->widget(i, DualTabWidget::PDF_TAB);
        if (!widget) continue;
        
        QString tabName = m_tabWidget->tabText(i, DualTabWidget::PDF_TAB);
        
        if (auto pdfViewer = qobject_cast<PDFViewerWidget*>(widget)) {
            qDebug() << "Hiding PDF viewer for tab:" << tabName;
            // No toolbar currently; do NOT hide the OpenGL child window to avoid corrupted buffer on restore
            // Just ensure geometry up to date
            pdfViewer->updateGeometry();
            pdfViewer->update();
            
            qDebug() << "PDF toolbar hidden for tab:" << i;
        }
    }
    
    // Iterate through all PCB tabs and hide their toolbars  
    for (int i = 0; i < m_tabWidget->count(DualTabWidget::PCB_TAB); ++i) {
        QWidget *widget = m_tabWidget->widget(i, DualTabWidget::PCB_TAB);
        if (!widget) continue;
        
        QString tabName = m_tabWidget->tabText(i, DualTabWidget::PCB_TAB);
        
        if (auto pcbViewer = qobject_cast<PCBViewerWidget*>(widget)) {
            qDebug() << "Hiding PCB viewer toolbar for tab:" << tabName;
            
            // Hide only the toolbar; keep GL widget visible to preserve context
            pcbViewer->setToolbarVisible(false);
            pcbViewer->updateGeometry();
            pcbViewer->update();
            
            qDebug() << "PCB toolbar hidden for tab:" << i;
        }
    }
    
    // Force immediate processing of hide events multiple times
    QApplication::processEvents();
    QApplication::processEvents(); // Extra processing for complex layouts
    
    qDebug() << "=== All Viewer Toolbars Hidden ===";
}

void MainApplication::debugToolbarStates()
{
    qDebug() << "=== Toolbar Debug Info ===";
    qDebug() << "Current PDF tab index:" << m_tabWidget->currentIndex(DualTabWidget::PDF_TAB);
    qDebug() << "Current PCB tab index:" << m_tabWidget->currentIndex(DualTabWidget::PCB_TAB);
    qDebug() << "Total PDF tabs:" << m_tabWidget->count(DualTabWidget::PDF_TAB);
    qDebug() << "Total PCB tabs:" << m_tabWidget->count(DualTabWidget::PCB_TAB);
    
    // Debug PDF tabs
    for (int i = 0; i < m_tabWidget->count(DualTabWidget::PDF_TAB); ++i) {
        QWidget *widget = m_tabWidget->widget(i, DualTabWidget::PDF_TAB);
        QString tabName = m_tabWidget->tabText(i, DualTabWidget::PDF_TAB);
        
        if (qobject_cast<PDFViewerWidget*>(widget)) {
            // PDF viewer now has no toolbar - always report as simplified viewer
            qDebug() << "PDF Tab" << i << "(" << tabName << "): toolbar-free viewer";
        }
    }
    
    // Debug PCB tabs
    for (int i = 0; i < m_tabWidget->count(DualTabWidget::PCB_TAB); ++i) {
        QWidget *widget = m_tabWidget->widget(i, DualTabWidget::PCB_TAB);
        QString tabName = m_tabWidget->tabText(i, DualTabWidget::PCB_TAB);
        
        if (auto pcbViewer = qobject_cast<PCBViewerWidget*>(widget)) {
            bool toolbarVisible = pcbViewer->isToolbarVisible();
            qDebug() << "PCB Tab" << i << "(" << tabName << "): toolbar visible:" << toolbarVisible;
        }
    }
    qDebug() << "=========================";
}

void MainApplication::forceToolbarIsolation()
{
    // Simplified: legacy aggressive isolation removed to prevent GL corruption.
    // Retained for backward compatibility if future selective isolation needed.
    hideAllViewerToolbars();
}

/*
// This method was used for the old single content area
// Now replaced by tab-based interface
void MainApplication::setupContentArea()
{
    m_contentWidget = new QWidget();
    m_contentWidget->setStyleSheet(
        "QWidget {"
        "    border: 1px solid #d4e1f5;"
        "    border-radius: 6px;"
        "    background-color: white;"
        "}"
    );
    
    QVBoxLayout *contentLayout = new QVBoxLayout(m_contentWidget);
    
    // Welcome message in content area
    QLabel *welcomeLabel = new QLabel(QString("Welcome to Way2Repair, %1!").arg(m_userSession.fullName));
    welcomeLabel->setStyleSheet(
        "QLabel {"
        "    font-size: 24px;"
        "    font-weight: bold;"
        "    color: #2c3e50;"
        "    padding: 40px;"
        "    font-family: 'Segoe UI', Arial, sans-serif;"
        "    border: none;"
        "}"
    );
    welcomeLabel->setAlignment(Qt::AlignCenter);
    contentLayout->addWidget(welcomeLabel);
    
    // Instructions
    QLabel *instructionLabel = new QLabel(
        "Select a file or folder from the tree view on the left to view its contents.\n\n"
        "Use the toolbar buttons to:\n"
        "• Refresh - Reload the file tree\n"
        "• Expand All - Expand all folders\n"
        "• Collapse All - Collapse all folders\n\n"
        "Double-click on files to open them."
    );
    instructionLabel->setStyleSheet(
        "QLabel {"
        "    font-size: 14px;"
        "    color: #666;"
        "    padding: 20px;"
        "    font-family: 'Segoe UI', Arial, sans-serif;"
        "    border: none;"
        "    line-height: 1.5;"
        "}"
    );
    instructionLabel->setAlignment(Qt::AlignCenter);
    instructionLabel->setWordWrap(true);
    contentLayout->addWidget(instructionLabel);
    
    contentLayout->addStretch();
}
*/

void MainApplication::addWelcomeTab()
{
    // No welcome tab needed since we only support PDF and PCB files
    // Users will see empty dual tab widget until they open a supported file
    statusBar()->showMessage("Ready - Open PDF or PCB files from the tree view");
}

// ==========================
// SERVER-SIDE METHODS (COMMENTED OUT)
// ==========================

/*
// These methods were used for server-side file loading
// They are commented out to focus on local file loading

void MainApplication::loadFileList()
{
    statusBar()->showMessage("Loading file list from server...");
    
    QString urlString = m_baseUrl + "/files.php";
    QUrl url(urlString);
    
    qDebug() << "Making request to URL:" << urlString;
    qDebug() << "URL is valid:" << url.isValid();
    
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("User-Agent", "Qt Application");
    
    // Add timeout
    request.setTransferTimeout(10000); // 10 seconds
    
    QNetworkReply *reply = m_networkManager->get(request);
    reply->setProperty("requestType", "fileList");
    
    // Connect to the individual reply's signals instead of the manager's
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onHttpRequestFinished(reply);
    });
    connect(reply, &QNetworkReply::errorOccurred, this, &MainApplication::onNetworkError);
    
    qDebug() << "Network request sent successfully";
}

void MainApplication::loadFileContent(const QString &filePath)
{
    statusBar()->showMessage(QString("Loading file: %1").arg(filePath));
    
    qDebug() << "Loading file content for path:" << filePath;
    
    QUrl url(m_baseUrl + "/file_content.php");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    
    // Add timeout
    request.setTransferTimeout(10000); // 10 seconds
    
    // Send file path as POST data - properly encode for URL form data
    QByteArray postData;
    postData.append("file_path=");
    postData.append(QUrl::toPercentEncoding(filePath));
    
    qDebug() << "POST data:" << postData;
    
    QNetworkReply *reply = m_networkManager->post(request, postData);
    reply->setProperty("requestType", "fileContent");
    reply->setProperty("filePath", filePath);
    
    // Connect to the individual reply's signals
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onHttpRequestFinished(reply);
    });
    connect(reply, &QNetworkReply::errorOccurred, this, &MainApplication::onNetworkError);
}

void MainApplication::onHttpRequestFinished(QNetworkReply *reply)
{
    // Server response handling code...
}

void MainApplication::onNetworkError(QNetworkReply::NetworkError error)
{
    // Network error handling code...
}

void MainApplication::parseFileListJson(const QJsonDocument &doc)
{
    // JSON parsing code...
}

void MainApplication::addTreeItem(QTreeWidgetItem *parent, const QJsonObject &item)
{
    // Tree item creation from JSON...
}

void MainApplication::loadFallbackData()
{
    // Fallback data loading...
}

void MainApplication::updateContentArea(const QString &title, const QString &content)
{
    // Content area updating (replaced by tab system)...
}
*/

// ==========================
// SLOT IMPLEMENTATIONS
// ==========================

void MainApplication::onTreeItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)
    
    if (!item) return;
    
    QString itemText = item->text(0);
    
    // Check if item has a file path data (files)
    QString filePath = item->data(0, Qt::UserRole).toString();
    // Check if item has a folder path data (folders)
    QString folderPath = item->data(0, Qt::UserRole + 1).toString();
    
    if (!filePath.isEmpty()) {
    // File (could be search result or real tree) - just show status
    statusBar()->showMessage(QString("Selected: %1 (double-click to open)").arg(itemText));
    } else if (!folderPath.isEmpty()) {
    // Folder: toggle immediately for faster UX
    bool willExpand = !item->isExpanded();
    item->setExpanded(willExpand);
    statusBar()->showMessage(QString("%1 folder: %2")
                 .arg(willExpand ? "Expanded" : "Collapsed")
                 .arg(itemText));
    } else {
        // No specific data, just show the item name
        statusBar()->showMessage(QString("Selected: %1").arg(itemText));
    }
}

void MainApplication::onTreeItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)
    
    if (!item) return;
    
    QString itemText = item->text(0);
    QString filePath = item->data(0, Qt::UserRole).toString();
    
    if (!filePath.isEmpty()) {
        // This is a file - open it in a new tab (double-click to open)
        statusBar()->showMessage(QString("Opening file: %1...").arg(itemText));
        openFileInTab(filePath);
    } else {
        // This is a folder - toggle expansion
        if (item->isExpanded()) {
            item->setExpanded(false);
        } else {
            item->setExpanded(true);
        }
        statusBar()->showMessage(QString("Toggled folder: %1").arg(itemText));
    }
}

void MainApplication::onTreeItemExpanded(QTreeWidgetItem *item)
{
    if (!item) return;
    
    // Update folder icon to "open" state
    updateTreeItemIcon(item, true);
    
    // Check if this folder has dummy children and needs to be populated
    if (item->childCount() == 1) {
        QTreeWidgetItem *child = item->child(0);
        if (child && child->data(0, Qt::UserRole + 2).toBool()) {
            // This is a dummy item, remove it and load actual contents
            delete child;
            
            // Load the actual folder contents
            QString folderPath = item->data(0, Qt::UserRole + 1).toString();
            if (!folderPath.isEmpty()) {
                statusBar()->showMessage(QString("Loading folder: %1...").arg(item->text(0)));
                populateTreeFromDirectory(folderPath, item);
                statusBar()->showMessage(QString("Loaded folder: %1").arg(item->text(0)));
            }
        }
    }
}

void MainApplication::onTreeItemCollapsed(QTreeWidgetItem *item)
{
    if (!item) return;
    
    // Update folder icon to "closed" state
    updateTreeItemIcon(item, false);
}

void MainApplication::setupTreeItemAppearance(QTreeWidgetItem *item, const QFileInfo &fileInfo)
{
    // Set display name - show full name for directories, base name (no extension) for files
    if (fileInfo.isDir()) {
        item->setText(0, fileInfo.fileName());
    } else {
        item->setText(0, fileInfo.baseName()); // Hide file extension
    }
    
    if (fileInfo.isDir()) {
        // This is a directory
        item->setIcon(0, getFolderIcon(false));
        
        // Add tooltip with folder info
        QString tooltip = QString("Folder: %1\nPath: %2\nModified: %3")
                            .arg(fileInfo.fileName())
                            .arg(fileInfo.absoluteFilePath())
                            .arg(fileInfo.lastModified().toString("yyyy-MM-dd hh:mm:ss"));
        item->setToolTip(0, tooltip);
        
        // Set folder-specific properties
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    } else {
        // This is a file
        item->setIcon(0, getFileIcon(fileInfo.absoluteFilePath()));
        
        // Add tooltip with file info
        QString extension = getFileExtension(fileInfo.absoluteFilePath());
        QString tooltip = QString("File: %1\nSize: %2 bytes\nType: %3\nModified: %4")
                            .arg(fileInfo.fileName())
                            .arg(fileInfo.size())
                            .arg(extension.isEmpty() ? "Unknown" : extension.toUpper())
                            .arg(fileInfo.lastModified().toString("yyyy-MM-dd hh:mm:ss"));
        item->setToolTip(0, tooltip);
        
        // Set file-specific properties
        item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
    }
}

void MainApplication::updateTreeItemIcon(QTreeWidgetItem *item, bool isExpanded)
{
    if (!item) return;
    
    // Check if this is a folder
    QString folderPath = item->data(0, Qt::UserRole + 1).toString();
    if (!folderPath.isEmpty()) {
        item->setIcon(0, getFolderIcon(isExpanded));
    }
}

QIcon MainApplication::getFileIcon(const QString &filePath)
{
    QString extension = getFileExtension(filePath);
    
    // Use Qt's built-in standard icons based on file type
    if (isCodeFile(extension)) {
        return style()->standardIcon(QStyle::SP_ComputerIcon);
    } else if (isImageFile(extension)) {
        return style()->standardIcon(QStyle::SP_FileIcon);
    } else if (isArchiveFile(extension)) {
        return style()->standardIcon(QStyle::SP_DriveHDIcon);
    } else if (isOfficeFile(extension)) {
        return style()->standardIcon(QStyle::SP_FileDialogDetailedView);
    } else if (extension == "pdf") {
        // Custom PDF icon from resources (added PDF_icon.svg)
        QIcon pdfIcon(":/icons/images/icons/PDF_icon.svg");
        if (!pdfIcon.isNull()) return pdfIcon;
        return style()->standardIcon(QStyle::SP_FileDialogListView);
    } else if (extension == "pcb" || extension == "xzz" || extension == "xzzpcb") {
        // Custom PCB icon
        QIcon pcbIcon(":/icons/images/icons/PCB_icon.svg");
        if (!pcbIcon.isNull()) return pcbIcon;
        return style()->standardIcon(QStyle::SP_FileIcon);
    } else if (extension == "txt" || extension == "log" || extension == "md") {
        return style()->standardIcon(QStyle::SP_FileIcon);
    } else {
        // Default file icon
        return style()->standardIcon(QStyle::SP_FileIcon);
    }
}

QIcon MainApplication::getFolderIcon(bool isOpen)
{
    // Choose themed variant (could later switch to dark-specific artwork if added)
    QString basePath = ":/icons/images/icons/";
    QString file = isOpen ? "folder_open.svg" : "folder_closed.svg";
    QIcon icon(basePath + file);
    if (!icon.isNull())
        return icon;
    // Fallback to system icon if resource missing
    return style()->standardIcon(isOpen ? QStyle::SP_DirOpenIcon : QStyle::SP_DirClosedIcon);
}

QString MainApplication::getFileExtension(const QString &filePath)
{
    QFileInfo fileInfo(filePath);
    return fileInfo.suffix().toLower();
}

bool MainApplication::isCodeFile(const QString &extension)
{
    static const QStringList codeExtensions = {
        "cpp", "c", "h", "hpp", "cc", "cxx", "hxx",
        "js", "ts", "jsx", "tsx", "py", "java", "cs",
        "html", "htm", "css", "scss", "sass", "less",
        "json", "xml", "yaml", "yml", "sql", "php",
        "rb", "go", "rs", "swift", "kt", "dart", "r",
        "m", "mm", "scala", "groovy", "pl", "sh", "bat",
        "ps1", "cmake", "make", "makefile", "pro", "pri"
    };
    return codeExtensions.contains(extension);
}

bool MainApplication::isImageFile(const QString &extension)
{
    static const QStringList imageExtensions = {
        "png", "jpg", "jpeg", "gif", "bmp", "tiff", "tif",
        "svg", "ico", "webp", "psd", "ai", "eps"
    };
    return imageExtensions.contains(extension);
}

bool MainApplication::isArchiveFile(const QString &extension)
{
    static const QStringList archiveExtensions = {
        "zip", "rar", "7z", "tar", "gz", "bz2", "xz",
        "cab", "iso", "dmg", "pkg", "deb", "rpm"
    };
    return archiveExtensions.contains(extension);
}

bool MainApplication::isOfficeFile(const QString &extension)
{
    static const QStringList officeExtensions = {
        "doc", "docx", "xls", "xlsx", "ppt", "pptx",
        "odt", "ods", "odp", "rtf", "pages", "numbers", "keynote"
    };
    return officeExtensions.contains(extension);
}

void MainApplication::onAboutClicked()
{
    QMessageBox::about(this, "About Way2Repair",
        "<h2>Way2Repair v4.75</h2>"
        "<p>Inquiry System for Intelligent Terminal Equipment Maintenance</p>"
        "<p>Professional equipment maintenance management solution with local file management.</p>"
        "<br>"
        "<p><b>New Features:</b></p>"
        "<ul>"
        "<li>Local file browser with tab interface</li>"
        "<li>Multiple file viewing support</li>"
        "<li>Drag and drop tab reordering</li>"
        "<li>Syntax highlighting for code files</li>"
        "<li>Real-time file tree refresh</li>"
        "</ul>"
        "<br>"
        "<p><b>How to use:</b></p>"
        "<p>• Use the tree view on the left to navigate local files<br>"
        "• Click on files to open them in tabs<br>"
        "• Close tabs using the X button<br>"
        "• Use toolbar buttons to refresh and manage the tree view</p>"
        "<br>"
        "<p>© 2025 Way2Repair Systems. All rights reserved.</p>"
    );
}

void MainApplication::onLogoutClicked()
{
    QMessageBox::StandardButton reply = QMessageBox::question(this, 
        "Logout Confirmation",
        "Are you sure you want to logout?",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        // Close all PDF tabs first
        while (m_tabWidget->count(DualTabWidget::PDF_TAB) > 0) {
            m_tabWidget->removeTab(0, DualTabWidget::PDF_TAB);
        }
        
        // Close all PCB tabs
        while (m_tabWidget->count(DualTabWidget::PCB_TAB) > 0) {
            m_tabWidget->removeTab(0, DualTabWidget::PCB_TAB);
        }
        
        // Emit logout signal
        emit logoutRequested();
        
        // Close the application
        this->close();
    }
}

void MainApplication::updateUserInfo()
{
    // Update window title with user info
    setWindowTitle(QString("Way2Repair - Equipment Maintenance System - %1").arg(m_userSession.fullName));
}

void MainApplication::toggleTreeView()
{
    setTreeViewVisible(!m_treeViewVisible);
}

void MainApplication::toggleFullScreenPDF()
{
    if (!m_tabWidget) {
        statusBar()->showMessage("No tabs available");
        return;
    }
    // Check which tab type is currently active and get the current widget
    DualTabWidget::TabType currentType = m_tabWidget->getCurrentTabType();
    QWidget *currentWidget = nullptr;
    
    if (currentType == DualTabWidget::PDF_TAB) {
        int currentIndex = m_tabWidget->currentIndex(DualTabWidget::PDF_TAB);
        if (currentIndex >= 0) {
            currentWidget = m_tabWidget->widget(currentIndex, DualTabWidget::PDF_TAB);
        }
    } else if (currentType == DualTabWidget::PCB_TAB) {
        int currentIndex = m_tabWidget->currentIndex(DualTabWidget::PCB_TAB);
        if (currentIndex >= 0) {
            currentWidget = m_tabWidget->widget(currentIndex, DualTabWidget::PCB_TAB);
        }
    }
    
    PDFViewerWidget *pdfViewer = qobject_cast<PDFViewerWidget*>(currentWidget);
    
    if (pdfViewer) {
        // It's a PDF tab - toggle full screen mode
        if (m_treeViewVisible) {
            setTreeViewVisible(false);
            statusBar()->showMessage("PDF in full screen mode - Press F11 or Ctrl+T to restore tree view");
        } else {
            setTreeViewVisible(true);
            statusBar()->showMessage("PDF in normal mode - Tree view restored");
        }
    } else {
        // Not a PDF tab, just hide tree view for full screen experience
        setTreeViewVisible(false);
        statusBar()->showMessage("Full screen mode - Press F11 or Ctrl+T to restore tree view");
    }
}

void MainApplication::setTreeViewVisible(bool visible)
{
    if (m_treeViewVisible == visible) {
        return; // Already in desired state
    }
    
    m_treeViewVisible = visible;
    
    // Defensive: splitter must exist before we try to resize panes
    if (!m_splitter) {
        if (visible) {
            if (m_treePanel) m_treePanel->show();
        } else {
            if (m_treePanel) m_treePanel->hide();
        }
        return;
    }

    if (visible) {
        // Show tree view - restore original sizes
        if (m_treePanel) m_treePanel->show();
        if (!m_splitterSizes.isEmpty()) {
            m_splitter->setSizes(m_splitterSizes);
        }
        statusBar()->showMessage("Tree view shown");
    } else {
        // Hide tree view - save current sizes and collapse completely
        m_splitterSizes = m_splitter->sizes();
        if (m_treePanel) m_treePanel->hide();
        
        // Force all space to the tab widget (PDF viewer)
        QList<int> fullScreenSizes;
        fullScreenSizes << 0 << this->width(); // Give all width to tab widget
        m_splitter->setSizes(fullScreenSizes);
        
        statusBar()->showMessage("Tree view hidden - Full screen PDF mode");
    }
    
    // Update menu action state if it exists
    QList<QAction*> actions = menuBar()->actions();
    for (QAction *action : actions) {
        if (action->text().contains("View")) {
            QMenu *viewMenu = action->menu();
            if (viewMenu) {
                QList<QAction*> viewActions = viewMenu->actions();
                for (QAction *viewAction : viewActions) {
                    if (viewAction->text().contains("Toggle Tree")) {
                        viewAction->setChecked(visible);
                        break;
                    }
                }
            }
            break;
        }
    }
}

bool MainApplication::isTreeViewVisible() const
{
    return m_treeViewVisible;
}

void MainApplication::setupKeyboardShortcuts()
{
    // Tree view toggle shortcut
    QShortcut *toggleTreeShortcut = new QShortcut(QKeySequence("Ctrl+T"), this);
    connect(toggleTreeShortcut, &QShortcut::activated, this, &MainApplication::toggleTreeView);
    
    // PDF full screen shortcut
    QShortcut *fullScreenShortcut = new QShortcut(QKeySequence("F11"), this);
    connect(fullScreenShortcut, &QShortcut::activated, this, &MainApplication::toggleFullScreenPDF);
    
    // Quick tree view restore (for when in PDF full screen)
    QShortcut *showTreeShortcut = new QShortcut(QKeySequence("Ctrl+Shift+T"), this);
    connect(showTreeShortcut, &QShortcut::activated, this, [this]() {
        setTreeViewVisible(true);
    });
    
    // Tab navigation shortcuts
    QShortcut *nextTabShortcut = new QShortcut(QKeySequence("Ctrl+Tab"), this);
    connect(nextTabShortcut, &QShortcut::activated, this, [this]() {
        DualTabWidget::TabType currentType = m_tabWidget->getCurrentTabType();
        
        if (currentType == DualTabWidget::PDF_TAB && m_tabWidget->count(DualTabWidget::PDF_TAB) > 1) {
            int currentIndex = m_tabWidget->currentIndex(DualTabWidget::PDF_TAB);
            int nextIndex = (currentIndex + 1) % m_tabWidget->count(DualTabWidget::PDF_TAB);
            m_tabWidget->setCurrentIndex(nextIndex, DualTabWidget::PDF_TAB);
        } else if (currentType == DualTabWidget::PCB_TAB && m_tabWidget->count(DualTabWidget::PCB_TAB) > 1) {
            int currentIndex = m_tabWidget->currentIndex(DualTabWidget::PCB_TAB);
            int nextIndex = (currentIndex + 1) % m_tabWidget->count(DualTabWidget::PCB_TAB);
            m_tabWidget->setCurrentIndex(nextIndex, DualTabWidget::PCB_TAB);
        }
    });
    
    QShortcut *prevTabShortcut = new QShortcut(QKeySequence("Ctrl+Shift+Tab"), this);
    connect(prevTabShortcut, &QShortcut::activated, this, [this]() {
        DualTabWidget::TabType currentType = m_tabWidget->getCurrentTabType();
        
        if (currentType == DualTabWidget::PDF_TAB && m_tabWidget->count(DualTabWidget::PDF_TAB) > 1) {
            int currentIndex = m_tabWidget->currentIndex(DualTabWidget::PDF_TAB);
            int prevIndex = (currentIndex - 1 + m_tabWidget->count(DualTabWidget::PDF_TAB)) % m_tabWidget->count(DualTabWidget::PDF_TAB);
            m_tabWidget->setCurrentIndex(prevIndex, DualTabWidget::PDF_TAB);
        } else if (currentType == DualTabWidget::PCB_TAB && m_tabWidget->count(DualTabWidget::PCB_TAB) > 1) {
            int currentIndex = m_tabWidget->currentIndex(DualTabWidget::PCB_TAB);
            int prevIndex = (currentIndex - 1 + m_tabWidget->count(DualTabWidget::PCB_TAB)) % m_tabWidget->count(DualTabWidget::PCB_TAB);
            m_tabWidget->setCurrentIndex(prevIndex, DualTabWidget::PCB_TAB);
        }
    });

    // Focus tree search on demand
    QShortcut *focusSearchShortcut = new QShortcut(QKeySequence("Ctrl+F"), this);
    connect(focusSearchShortcut, &QShortcut::activated, this, [this]() {
        if (m_treeSearchEdit) {
            m_treeSearchEdit->setFocus();
            m_treeSearchEdit->selectAll();
        }
    });
}
