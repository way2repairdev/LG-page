#include "viewers/pcb/PCBViewerWidget.h"
#include "viewers/pcb/PCBViewerEmbedder.h"
#include "ui/LoadingOverlay.h"
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>

#include <QResizeEvent>
#include <QPaintEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QFocusEvent>
#include <QPainter>
#include <QDebug>
#include <QFileInfo>
#include <QMessageBox>
#include <QApplication>
#include <QStyle>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QMenu>
#include <QDateTime>
// New includes for debug/resource checks and events
#include <QFile>
#include <QMouseEvent>
// Toolbar/UI widgets used below
#include <QToolBar>
#include <QAction>
#include <QComboBox>
#include <QPushButton>
#include <QPalette>
// Added for premium icon tinting and hover handling
#include <QToolButton>
#include <QIcon>
#include <QPixmap>
#include <QImage>
// QWidgetAction header may not be available in current include paths; we'll use addWidget helpers instead.

// Enhanced debug logging for PCBViewerWidget
void WritePCBDebugToFile(const QString& message) {
    static QFile debugFile;
    static bool fileInitialized = false;
    
    if (!fileInitialized) {
        debugFile.setFileName("pcb_debug.txt");
        debugFile.open(QIODevice::WriteOnly | QIODevice::Append);
        fileInitialized = true;
    }
    
    if (debugFile.isOpen()) {
        QTextStream stream(&debugFile);
        stream << "[PCB-DEBUG] " << message << Qt::endl;
        stream.flush();
    }
    
    qDebug() << "[PCB-DEBUG]" << message;
}

PCBViewerWidget::PCBViewerWidget(QWidget *parent)
    : QWidget(parent)
    , m_pcbEmbedder(std::make_unique<PCBViewerEmbedder>())
    , m_mainLayout(nullptr)
    , m_toolbar(nullptr)
    , m_viewerContainer(nullptr)
    , m_updateTimer(new QTimer(this))
    , m_viewerInitialized(false)
    , m_pcbLoaded(false)
    , m_usingFallback(false)
    , m_toolbarVisible(true)
    , m_currentFilePath("")
    , m_needsUpdate(false)
    , m_isUpdating(false)
{
    WritePCBDebugToFile("PCBViewerWidget constructor started");
    
    // Set up the widget
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    
    // Setup UI
    setupUI();
    connectSignals();
    m_loadingOverlay = new LoadingOverlay(this);
    connect(m_loadingOverlay, &LoadingOverlay::cancelRequested, this, &PCBViewerWidget::cancelLoad);
    
    // Initialize the PCB viewer
    initializePCBViewer();
    
    // Setup update timer
    m_updateTimer->setSingleShot(false);
    m_updateTimer->setInterval(16); // ~60 FPS
    connect(m_updateTimer, &QTimer::timeout, this, &PCBViewerWidget::updateViewer);
    
    WritePCBDebugToFile("PCBViewerWidget constructor completed");
}

PCBViewerWidget::~PCBViewerWidget()
{
    WritePCBDebugToFile("PCBViewerWidget destructor");
    
    // Stop update timer
    if (m_updateTimer) {
        m_updateTimer->stop();
    }
    
    // Clean up PCB embedder
    if (m_pcbEmbedder) {
        m_pcbEmbedder->cleanup();
    }
}

bool PCBViewerWidget::loadPCB(const QString &filePath)
{
    WritePCBDebugToFile("Loading PCB file: " + filePath);
    
    if (!m_pcbEmbedder) {
        WritePCBDebugToFile("PCB embedder not available");
        return false;
    }
    
    if (!m_viewerInitialized) {
        WritePCBDebugToFile("PCB viewer not initialized, attempting initialization");
        initializePCBViewer();
        if (!m_viewerInitialized) {
            WritePCBDebugToFile("Failed to initialize PCB viewer");
            return false;
        }
    }
    
    // Attempt to load the PCB file
    bool success = m_pcbEmbedder->loadPCB(filePath.toStdString());
    
    if (success) {
        m_pcbLoaded = true;
        m_currentFilePath = filePath;
    populateNetAndComponentList();
        
        // Start update timer if not already running
        if (!m_updateTimer->isActive()) {
            m_updateTimer->start();
        }
        
        WritePCBDebugToFile("PCB file loaded successfully");
        emit pcbLoaded(filePath);
    } else {
        WritePCBDebugToFile("Failed to load PCB file");
        emit errorOccurred("Failed to load PCB file: " + filePath);
    }
    
    return success;
}

void PCBViewerWidget::requestLoad(const QString &filePath)
{
    cancelLoad();
    m_pendingFilePath = filePath;
    m_cancelRequested = false;
    ++m_currentLoadId;
    int loadId = m_currentLoadId;
    if (m_loadingOverlay) m_loadingOverlay->showOverlay(QString("Loading %1...").arg(QFileInfo(filePath).fileName()));

    if (!m_loadWatcher) {
        m_loadWatcher = new QFutureWatcher<bool>(this);
        connect(m_loadWatcher, &QFutureWatcher<bool>::finished, this, [this]() {
            bool ok = m_loadWatcher->result();
            int id = m_loadWatcher->property("loadId").toInt();
            if (m_cancelRequested || id != m_currentLoadId) {
                if (m_loadingOverlay) m_loadingOverlay->hideOverlay();
                emit loadCancelled();
                return;
            }
            if (ok) {
                bool loaded = loadPCB(m_pendingFilePath); // synchronous Phase 1
                if (m_loadingOverlay) m_loadingOverlay->hideOverlay();
                if (!loaded) emit errorOccurred(QString("Failed to load %1").arg(m_pendingFilePath));
            } else {
                if (m_loadingOverlay) m_loadingOverlay->hideOverlay();
                emit errorOccurred(QString("Failed to pre-read %1").arg(m_pendingFilePath));
            }
        });
    }

    QFuture<bool> fut = QtConcurrent::run([filePath]() {
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly)) return false;
        f.read(512 * 1024); // first 512 KB
        return true;
    });
    m_loadWatcher->setProperty("loadId", loadId);
    m_loadWatcher->setFuture(fut);
}

void PCBViewerWidget::cancelLoad()
{
    if (m_loadWatcher && m_loadWatcher->isRunning()) {
        m_cancelRequested = true;
    }
    if (m_loadingOverlay && m_loadingOverlay->isVisible()) m_loadingOverlay->hideOverlay();
}

void PCBViewerWidget::closePCB()
{
    WritePCBDebugToFile("Closing PCB");
    
    // Stop update timer
    if (m_updateTimer->isActive()) {
        m_updateTimer->stop();
    }
    
    // Close PCB in embedder
    if (m_pcbEmbedder) {
        m_pcbEmbedder->closePCB();
    }
    
    m_pcbLoaded = false;
    m_currentFilePath.clear();
    
    emit pcbClosed();
    
    WritePCBDebugToFile("PCB closed");
}

bool PCBViewerWidget::isPCBLoaded() const
{
    return m_pcbLoaded && m_pcbEmbedder && m_pcbEmbedder->isPCBLoaded();
}

QString PCBViewerWidget::getCurrentFilePath() const
{
    return m_currentFilePath;
}

void PCBViewerWidget::setToolbarVisible(bool visible)
{
    WritePCBDebugToFile("Setting PCB toolbar visible: " + QString(visible ? "true" : "false"));
    
    m_toolbarVisible = visible;
    if (m_toolbar) {
        m_toolbar->setVisible(visible);
        m_toolbar->setEnabled(visible);
        
        updateGeometry();
        update();
    }
}

bool PCBViewerWidget::isToolbarVisible() const
{
    return m_toolbarVisible;
}

QToolBar* PCBViewerWidget::getToolbar() const
{
    return m_toolbar;
}

// Public slots

void PCBViewerWidget::updateViewer()
{
    if (m_isUpdating) {
        return;
    }
    
    m_isUpdating = true;
    
    // Render the PCB viewer
    if (m_pcbEmbedder && m_viewerInitialized) {
        m_pcbEmbedder->render();
    }
    
    m_isUpdating = false;
}

// Protected event handlers

void PCBViewerWidget::resizeEvent(QResizeEvent *event)
{
    WritePCBDebugToFile(QString("PCB widget resized to %1x%2")
                       .arg(event->size().width())
                       .arg(event->size().height()));
    
    QWidget::resizeEvent(event);
    
    if (m_pcbEmbedder && m_viewerInitialized) {
        QSize containerSize = m_viewerContainer ? m_viewerContainer->size() : size();
        m_pcbEmbedder->resize(containerSize.width(), containerSize.height());
    }
}

void PCBViewerWidget::showEvent(QShowEvent *event)
{
    WritePCBDebugToFile("PCB widget show event");
    QWidget::showEvent(event);
    
    if (m_pcbEmbedder) {
        m_pcbEmbedder->show();
    }
    
    if (!m_updateTimer->isActive() && m_pcbLoaded) {
        m_updateTimer->start();
    }
}

void PCBViewerWidget::hideEvent(QHideEvent *event)
{
    WritePCBDebugToFile("PCB widget hide event");
    QWidget::hideEvent(event);
    
    if (m_pcbEmbedder) {
        m_pcbEmbedder->hide();
    }
    
    if (m_updateTimer->isActive()) {
        m_updateTimer->stop();
    }
}

void PCBViewerWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);
    QWidget::paintEvent(event);
}

void PCBViewerWidget::ensureViewportSync()
{
    if (!m_pcbEmbedder || !m_viewerInitialized)
        return;

    QSize containerSize = m_viewerContainer ? m_viewerContainer->size() : size();
    int w = containerSize.width();
    int h = containerSize.height();
    if (w > 0 && h > 0) {
        // resize() in embedder clamps/centers camera; call explicitly post-activation
        m_pcbEmbedder->resize(w, h);
    }
}

void PCBViewerWidget::focusInEvent(QFocusEvent *event)
{
    WritePCBDebugToFile("PCB widget focus in");
    QWidget::focusInEvent(event);
}

void PCBViewerWidget::focusOutEvent(QFocusEvent *event)
{
    WritePCBDebugToFile("PCB widget focus out");
    QWidget::focusOutEvent(event);
}

// Private slots

void PCBViewerWidget::onPCBViewerError(const QString &error)
{
    WritePCBDebugToFile("PCB viewer error: " + error);
    emit errorOccurred(error);
}

// Private methods

void PCBViewerWidget::initializePCBViewer()
{
    WritePCBDebugToFile("Initializing PCB viewer");
    
    if (!m_pcbEmbedder) {
        WritePCBDebugToFile("PCB embedder not available");
        return;
    }
    
    // Get window handle for embedding
    WId windowHandle = m_viewerContainer ? m_viewerContainer->winId() : winId();
    QSize containerSize = m_viewerContainer ? m_viewerContainer->size() : size();
    
    // Setup callbacks
    m_pcbEmbedder->setErrorCallback([this](const std::string& error) {
        QMetaObject::invokeMethod(this, [this, error]() {
            onPCBViewerError(QString::fromStdString(error));
        }, Qt::QueuedConnection);
    });
    
    // Disable ImGui UI - use external Qt toolbar only
    m_pcbEmbedder->setImGuiUIEnabled(false);
    WritePCBDebugToFile("ImGui UI disabled - using external Qt toolbar only");
    
    // Initialize the embedder
    bool success = m_pcbEmbedder->initialize(reinterpret_cast<void*>(windowHandle), containerSize.width(), containerSize.height());

    if (success) {
        // Register quick right-click callback
        m_pcbEmbedder->setQuickRightClickCallback([this](const std::string &part, const std::string &net){
            if (!m_crossSearchEnabled) return;
            // If we just programmatically re-opened a menu due to an outside right-click,
            // skip this callback once to avoid duplicate menus.
            if (m_suppressNextEmbedderQuickMenu) { m_suppressNextEmbedderQuickMenu = false; return; }
            // If a context menu is already active, don't open another one
            if (m_contextMenuActive) return;
            QString candidate;
            if (!part.empty()) candidate = QString::fromStdString(part);
            else if (!net.empty()) candidate = QString::fromStdString(net);
            if (candidate.isEmpty()) return;
            QMetaObject::invokeMethod(this, [this, candidate]() {
                // Double-check active state on the GUI thread
                if (!m_contextMenuActive)
                    showCrossContextMenu(QCursor::pos(), candidate);
            }, Qt::QueuedConnection);
        });
        m_viewerInitialized = true;
        m_usingFallback = m_pcbEmbedder->isUsingFallback();
        
        WritePCBDebugToFile("PCB viewer initialized successfully");
    } else {
        WritePCBDebugToFile("Failed to initialize PCB viewer");
        m_usingFallback = true;
    }
}

void PCBViewerWidget::setupUI()
{
    WritePCBDebugToFile("Setting up PCB viewer UI with split view support");
    
    // Main layout
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    
    // Setup main toolbar
    setupToolbar();
    
    // Single viewer container (split view removed)
    m_viewerContainer = new QWidget(this);
    m_viewerContainer->setMinimumSize(400, 300);
    m_viewerContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_viewerContainer->setFocusPolicy(Qt::StrongFocus);
    m_viewerContainer->installEventFilter(this);
    // Add toolbar and viewer container to main layout
    if (m_toolbar) {
        m_mainLayout->addWidget(m_toolbar);
    }
    m_mainLayout->addWidget(m_viewerContainer, 1);
    
    WritePCBDebugToFile("PCB viewer UI setup completed (single-pane)");
}

void PCBViewerWidget::setupToolbar()
{
    WritePCBDebugToFile("Setting up PCB viewer Qt toolbar to match PDF viewer styling");

    // Create toolbar with same styling as PDF viewer
    m_toolbar = new QToolBar(this);
    m_toolbar->setIconSize(QSize(16, 16));
    m_toolbar->setMovable(false);
    // Theme-aware red accent (match tab styling: #E53935/#b71c1c family)
    const bool dark = qApp && qApp->palette().color(QPalette::Window).lightness() < 128;
    const QString tbStyleLight =
        "QToolBar{background:#fafafa;border-bottom:1px solid #d0d0d0;min-height:30px;}"
        "QToolBar QToolButton{border:1px solid transparent;border-radius:6px;padding:4px;margin:2px;}"
        "QToolBar QToolButton:hover{background:rgba(229,57,53,0.10);border-color:rgba(229,57,53,0.35);}" 
        "QToolBar QToolButton:pressed{background:rgba(229,57,53,0.18);border-color:#E53935;}"
        "QToolBar QToolButton:checked{background:rgba(229,57,53,0.14);border-color:#E53935;}"
        "QToolBar QToolButton:disabled{color:#9e9e9e;background:transparent;border-color:transparent;}"
        "QToolBar::separator{background:rgba(0,0,0,0.12);width:1px;margin:0 6px;}";
    const QString tbStyleDark =
        "QToolBar{background:#202124;border-bottom:1px solid #3c4043;min-height:30px;}"
        "QToolBar QToolButton{color:#e8eaed;border:1px solid transparent;border-radius:6px;padding:4px;margin:2px;}"
        "QToolBar QToolButton:hover{background:rgba(183,28,28,0.22);border-color:#cf6679;}"
        "QToolBar QToolButton:pressed{background:rgba(183,28,28,0.30);border-color:#b71c1c;}"
        "QToolBar QToolButton:checked{background:rgba(183,28,28,0.28);border-color:#cf6679;color:#e8eaed;}"
        "QToolBar QToolButton:disabled{color:#9aa0a6;background:transparent;border-color:transparent;}"
        "QToolBar::separator{background:rgba(255,255,255,0.12);width:1px;margin:0 6px;}";
    m_toolbar->setStyleSheet(dark ? tbStyleDark : tbStyleLight);
    
    // Split view removed: no split window action
    // Rotation actions (match PDF viewer icon style)
    // Zoom actions (manual zoom + fit)
    m_actionZoomIn = m_toolbar->addAction(QIcon(":/icons/images/icons/zoom_in.svg"), "");
    m_actionZoomIn->setToolTip("Zoom In");
    connect(m_actionZoomIn, &QAction::triggered, this, &PCBViewerWidget::zoomIn);

    m_actionZoomOut = m_toolbar->addAction(QIcon(":/icons/images/icons/zoom_out.svg"), "");
    m_actionZoomOut->setToolTip("Zoom Out");
    connect(m_actionZoomOut, &QAction::triggered, this, &PCBViewerWidget::zoomOut);

    m_actionZoomFit = m_toolbar->addAction(QIcon(":/icons/images/icons/zoom_fit.svg"), "");
    m_actionZoomFit->setToolTip("Zoom To Fit");
    connect(m_actionZoomFit, &QAction::triggered, this, &PCBViewerWidget::zoomToFit);

    // (Net selection UI moved to after diode toggle)

    m_actionRotateLeft = m_toolbar->addAction(QIcon(":/icons/images/icons/rotate_left.svg"), "");
    m_actionRotateLeft->setToolTip("Rotate Left");
    connect(m_actionRotateLeft, &QAction::triggered, this, &PCBViewerWidget::rotateLeft);

    m_actionRotateRight = m_toolbar->addAction(QIcon(":/icons/images/icons/rotate_right.svg"), "");
    m_actionRotateRight->setToolTip("Rotate Right");
    connect(m_actionRotateRight, &QAction::triggered, this, &PCBViewerWidget::rotateRight);

    // Flip actions
    m_actionFlipH = m_toolbar->addAction(QIcon(":/icons/images/icons/flip_horizontal.svg"), "");
    m_actionFlipH->setToolTip("Flip Left/Right");
    connect(m_actionFlipH, &QAction::triggered, this, &PCBViewerWidget::flipHorizontal);

    m_actionFlipV = m_toolbar->addAction(QIcon(":/icons/images/icons/flip_vertical.svg"), "");
    m_actionFlipV->setToolTip("Flip Up/Down");
    connect(m_actionFlipV, &QAction::triggered, this, &PCBViewerWidget::flipVertical);

    m_toolbar->addSeparator();
    // Diode readings toggle
    m_actionToggleDiode = m_toolbar->addAction(QIcon(":/icons/images/icons/next.svg"), ""); // TODO: replace with dedicated diode icon
    m_actionToggleDiode->setCheckable(true);
    m_actionToggleDiode->setChecked(true);
    m_actionToggleDiode->setToolTip("Toggle Diode Readings");
    connect(m_actionToggleDiode, &QAction::triggered, this, &PCBViewerWidget::toggleDiodeReadings);

    // Pad Ratsnet toggle
    m_actionPadRatsnet = m_toolbar->addAction(QIcon(":/icons/images/icons/pad_ratsnet.svg"), "");
    m_actionPadRatsnet->setCheckable(true);
    m_actionPadRatsnet->setChecked(false);
    m_actionPadRatsnet->setToolTip("Toggle Pad Ratsnet");
    connect(m_actionPadRatsnet, &QAction::triggered, this, &PCBViewerWidget::togglePadRatsnet);

    // Net / Component selection combo box + search button
    m_toolbar->addSeparator();
    m_netCombo = new QComboBox(m_toolbar);
    m_netCombo->setEditable(true); // user can type either net or component reference
    // Increased width for better usability and parity with PDF search
    m_netCombo->setMinimumWidth(260);
    m_netCombo->setFixedHeight(26);
    m_netCombo->setInsertPolicy(QComboBox::NoInsert);
    m_netCombo->setToolTip("Type or pick a Net or Component name");
    // Professional drop-down styling with visible button and themed popup
    {
        const QString border = dark ? "#5f6368" : "#ccc";
        const QString bg = dark ? "#2a2b2d" : "white";
        const QString fg = dark ? "#e8eaed" : "#111";
        const QString focus = dark ? "#cf6679" : "#E53935";
        // Make the drop-down visually integrated with the field (no separate-looking background)
        const QString dropBg = bg; // same as field
        const QString dropHover = dark ? "#2f3542" : "#e9eef7"; // subtle premium hover tint
        const QString dropBorder = border; // same border so it feels like one control
        const QString viewBg = dark ? "#1f2023" : "white";
        const QString viewFg = fg;
        const QString viewSelBg = dark ? "#3b1f22" : "#fdeaea";
        const QString viewSelFg = fg;
        const QString arrowRes = dark ? ":/icons/images/icons/chevron_down_light.svg"
                                      : ":/icons/images/icons/chevron_down.svg";
    const int dropW = 32;            // width of the drop-down clickable area (a bit wider for safety)
    const int padR  = dropW + 12;    // extra right padding so text never goes under arrow
        // Log resource presence for troubleshooting
        WritePCBDebugToFile(QString("Chevron resource exists (dark=%1): %2 / %3")
                                .arg(dark ? "true" : "false")
                                .arg(QFile::exists(":/icons/images/icons/chevron_down.svg") ? "yes" : "no")
                                .arg(QFile::exists(":/icons/images/icons/chevron_down_light.svg") ? "yes" : "no"));

        QString comboStyle = QString(
            "QComboBox, QComboBox:editable{border:1px solid %1;border-radius:3px;padding:2px %8px 2px 8px;background:%2;color:%3;}"
            "QComboBox:focus, QComboBox:editable:focus{border-color:%4;}"
            "QComboBox::drop-down, QComboBox::drop-down:editable{subcontrol-origin:border; subcontrol-position:top right; width:%9px; border-left:1px solid %1; background:%5; border-top-right-radius:3px; border-bottom-right-radius:3px;}"
            "QComboBox::drop-down:hover, QComboBox::drop-down:editable:hover{background:%6;}"
            "QComboBox::down-arrow, QComboBox::down-arrow:editable{image:url(%7); width:12px; height:12px; margin-right:8px;}"
            "QComboBox QLineEdit{border:none; background:transparent; color:%3; padding:0; padding-right:%8px;}"
        ).arg(border, bg, fg, focus, dropBg, dropHover, arrowRes, QString::number(padR), QString::number(dropW));
        comboStyle += QString("QComboBox QAbstractItemView{background:%1; color:%2; border:1px solid %3; outline:0; selection-background-color:%4; selection-color:%5;}")
                           .arg(viewBg, viewFg, border, viewSelBg, viewSelFg);
        m_netCombo->setStyleSheet(comboStyle);
    }
    m_toolbar->addWidget(m_netCombo);
    m_netSearchButton = new QPushButton("Go", m_toolbar);
    m_netSearchButton->setToolTip("Highlight & zoom to Net or Component");
    m_netSearchButton->setFixedHeight(26);
    m_netSearchButton->setStyleSheet(QStringLiteral(
        "QPushButton{border:1px solid %1;border-radius:3px;padding:2px 8px;background:%2;color:%3;}"
        "QPushButton:hover{background:%4;}"
        "QPushButton:pressed{background:%5;}"
    ).arg(dark ? "#5f6368" : "#ccc",
          dark ? "#2a2b2d" : "#f8f8f8",
          dark ? "#e8eaed" : "#111",
          dark ? "#332222" : "#f0f0f0",
          dark ? "#2b1f1f" : "#e8e8e8"));
    m_toolbar->addWidget(m_netSearchButton);
    connect(m_netSearchButton, &QPushButton::clicked, this, &PCBViewerWidget::onNetSearchClicked);
    // Auto trigger navigation when user picks from list (but still allow manual typing then Enter/Go)
    connect(m_netCombo, QOverload<int>::of(&QComboBox::activated), this, &PCBViewerWidget::onNetComboActivated);
    m_toolbar->addSeparator();

    WritePCBDebugToFile("Zoom and rotation/flip actions added to PCB toolbar");
    WritePCBDebugToFile("Split window action removed (feature deprecated)");
    WritePCBDebugToFile("PCB Qt toolbar setup completed with PDF viewer styling");

    // Optional: Icon tinting to red on hover/checked similar to PDF (local inline since helpers are in PDF file)
    {
    const QSize iconSz(16, 16);
        const QColor accent = QColor(dark ? "#cf6679" : "#E53935");
        const QColor normal = dark ? QColor("#c7cacf") : QColor("#5f6368");
        const QColor disabled = dark ? QColor("#6f7379") : QColor("#9e9e9e");
        auto makeTint = [&](const QIcon &base, const QColor &c){
            QPixmap src = base.pixmap(iconSz);
            if (src.isNull()) return base;
            QImage img = src.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
            QImage colored(img.size(), QImage::Format_ARGB32_Premultiplied);
            colored.fill(Qt::transparent);
            QPainter p(&colored); p.fillRect(colored.rect(), c); p.setCompositionMode(QPainter::CompositionMode_DestinationIn); p.drawImage(0,0,img); p.end();
            return QIcon(QPixmap::fromImage(colored));
        };
        for (QAction *act : m_toolbar->actions()) {
            if (!act || act->isSeparator()) continue;
            if (auto *btn = qobject_cast<QToolButton*>(m_toolbar->widgetForAction(act))) {
                QIcon n = makeTint(act->icon(), normal);
                QIcon h = makeTint(act->icon(), accent);
                QIcon d = makeTint(act->icon(), disabled);
                btn->setIcon(act->isEnabled() ? n : d);
                btn->setIconSize(iconSz);
                btn->setAttribute(Qt::WA_Hover, true);
                btn->installEventFilter(new QObject(btn));
                QObject::connect(btn, &QToolButton::toggled, btn, [btn, n, h](){ if (btn->isEnabled()) btn->setIcon(btn->isChecked() ? h : (btn->underMouse()?h:n)); });
                QObject::connect(act, &QAction::changed, btn, [btn, act, n, d](){ btn->setIcon(act->isEnabled()?n:d); });
            }
        }
    }
}

void PCBViewerWidget::applyToolbarTheme()
{
    if (!m_toolbar) return;
    const bool dark = qApp && qApp->palette().color(QPalette::Window).lightness() < 128;
    const QString tbStyleLight =
        "QToolBar{background:#fafafa;border-bottom:1px solid #d0d0d0;min-height:30px;}"
        "QToolBar QToolButton{border:1px solid transparent;border-radius:6px;padding:4px;margin:2px;}"
        "QToolBar QToolButton:hover{background:rgba(229,57,53,0.10);border-color:rgba(229,57,53,0.35);}" 
        "QToolBar QToolButton:pressed{background:rgba(229,57,53,0.18);border-color:#E53935;}"
        "QToolBar QToolButton:checked{background:rgba(229,57,53,0.14);border-color:#E53935;}"
        "QToolBar QToolButton:disabled{color:#9e9e9e;background:transparent;border-color:transparent;}"
        "QToolBar::separator{background:rgba(0,0,0,0.12);width:1px;margin:0 6px;}";
    const QString tbStyleDark =
        "QToolBar{background:#202124;border-bottom:1px solid #3c4043;min-height:30px;}"
        "QToolBar QToolButton{color:#e8eaed;border:1px solid transparent;border-radius:6px;padding:4px;margin:2px;}"
        "QToolBar QToolButton:hover{background:rgba(183,28,28,0.22);border-color:#cf6679;}"
        "QToolBar QToolButton:pressed{background:rgba(183,28,28,0.30);border-color:#b71c1c;}"
        "QToolBar QToolButton:checked{background:rgba(183,28,28,0.28);border-color:#cf6679;color:#e8eaed;}"
        "QToolBar QToolButton:disabled{color:#9aa0a6;background:transparent;border-color:transparent;}"
        "QToolBar::separator{background:rgba(255,255,255,0.12);width:1px;margin:0 6px;}";
    m_toolbar->setStyleSheet(dark ? tbStyleDark : tbStyleLight);
    // Update net combo and button styles to match theme
    if (m_netCombo) {
        const QString border = dark ? "#5f6368" : "#ccc";
        const QString bg = dark ? "#2a2b2d" : "white";
        const QString fg = dark ? "#e8eaed" : "#111";
        const QString focus = dark ? "#cf6679" : "#E53935";
        const QString dropBg = bg;
        const QString dropHover = dark ? "#2f3542" : "#e9eef7";
        const QString dropBorder = border;
        const QString viewBg = dark ? "#1f2023" : "white";
        const QString viewFg = fg;
        const QString viewSelBg = dark ? "#3b1f22" : "#fdeaea";
        const QString viewSelFg = fg;
        const QString arrowRes = dark ? ":/icons/images/icons/chevron_down_light.svg"
                                      : ":/icons/images/icons/chevron_down.svg";
    const int dropW = 32;
    const int padR  = dropW + 12;
        // Log resource presence for troubleshooting
        WritePCBDebugToFile(QString("Chevron resource exists (dark=%1): %2 / %3")
                                .arg(dark ? "true" : "false")
                                .arg(QFile::exists(":/icons/images/icons/chevron_down.svg") ? "yes" : "no")
                                .arg(QFile::exists(":/icons/images/icons/chevron_down_light.svg") ? "yes" : "no"));

        QString comboStyle = QString(
            "QComboBox, QComboBox:editable{border:1px solid %1;border-radius:3px;padding:2px %8px 2px 8px;background:%2;color:%3;}"
            "QComboBox:focus, QComboBox:editable:focus{border-color:%4;}"
            "QComboBox::drop-down, QComboBox::drop-down:editable{subcontrol-origin:border; subcontrol-position:top right; width:%9px; border-left:1px solid %1; background:%5; border-top-right-radius:3px; border-bottom-right-radius:3px;}"
            "QComboBox::drop-down:hover, QComboBox::drop-down:editable:hover{background:%6;}"
            "QComboBox::down-arrow, QComboBox::down-arrow:editable{image:url(%7); width:12px; height:12px; margin-right:8px;}"
            "QComboBox QLineEdit{border:none; background:transparent; color:%3; padding:0; padding-right:%8px;}"
        ).arg(border, bg, fg, focus, dropBg, dropHover, arrowRes, QString::number(padR), QString::number(dropW));
        comboStyle += QString("QComboBox QAbstractItemView{background:%1; color:%2; border:1px solid %3; outline:0; selection-background-color:%4; selection-color:%5;}")
                           .arg(viewBg, viewFg, border, viewSelBg, viewSelFg);
        m_netCombo->setStyleSheet(comboStyle);
    }
    if (m_netSearchButton) {
        m_netSearchButton->setStyleSheet(QStringLiteral(
            "QPushButton{border:1px solid %1;border-radius:3px;padding:2px 8px;background:%2;color:%3;}"
            "QPushButton:hover{background:%4;}"
            "QPushButton:pressed{background:%5;}"
        ).arg(dark ? "#5f6368" : "#ccc",
              dark ? "#2a2b2d" : "#f8f8f8",
              dark ? "#e8eaed" : "#111",
              dark ? "#332222" : "#f0f0f0",
              dark ? "#2b1f1f" : "#e8e8e8"));
    }
    // Retint icons for new theme (reuse local inline block logic)
    const QSize iconSz(16, 16);
    const QColor accent = QColor(dark ? "#cf6679" : "#E53935");
    const QColor normal = dark ? QColor("#c7cacf") : QColor("#5f6368");
    const QColor disabled = dark ? QColor("#6f7379") : QColor("#9e9e9e");
    auto makeTint = [&](const QIcon &base, const QColor &c){
        QPixmap src = base.pixmap(iconSz);
        if (src.isNull()) return base;
        QImage img = src.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QImage colored(img.size(), QImage::Format_ARGB32_Premultiplied);
        colored.fill(Qt::transparent);
        QPainter p(&colored); p.fillRect(colored.rect(), c); p.setCompositionMode(QPainter::CompositionMode_DestinationIn); p.drawImage(0,0,img); p.end();
        return QIcon(QPixmap::fromImage(colored));
    };
    for (QAction *act : m_toolbar->actions()) {
        if (!act || act->isSeparator()) continue;
        if (auto *btn = qobject_cast<QToolButton*>(m_toolbar->widgetForAction(act))) {
            QIcon n = makeTint(act->icon(), normal);
            QIcon h = makeTint(act->icon(), accent);
            QIcon d = makeTint(act->icon(), disabled);
            btn->setIcon(act->isEnabled() ? n : d);
            btn->setIconSize(iconSz);
            // Hover filter reuses existing QObject; icons are reset here only.
        }
    }
}

void PCBViewerWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        applyToolbarTheme();
    }
    QWidget::changeEvent(event);
}

void PCBViewerWidget::connectSignals()
{
    WritePCBDebugToFile("Connecting PCB viewer signals");
    if (m_pcbEmbedder) {
        // Pin selection callback to sync net combo
        m_pcbEmbedder->setPinSelectedCallback([this](const std::string &pinName, const std::string &netName){
            QMetaObject::invokeMethod(this, [this, pinName, netName](){
                onPinSelectedFromViewer(pinName, netName);
            });
        });
        
        // Part selection callback to sync component combo
        m_pcbEmbedder->setPartSelectedCallback([this](const std::string &partName){
            QMetaObject::invokeMethod(this, [this, partName](){
                onPartSelectedFromViewer(partName);
            });
        });
    }
    
    WritePCBDebugToFile("PCB viewer signals connected");
}

void PCBViewerWidget::rotateLeft()
{
    if (m_pcbEmbedder && m_pcbLoaded) {
        m_pcbEmbedder->rotateLeft();
        WritePCBDebugToFile("Rotated PCB view left (CCW)");
    }
}

void PCBViewerWidget::rotateRight()
{
    if (m_pcbEmbedder && m_pcbLoaded) {
        m_pcbEmbedder->rotateRight();
        WritePCBDebugToFile("Rotated PCB view right (CW)");
    }
}

void PCBViewerWidget::flipHorizontal()
{
    if (m_pcbEmbedder && m_pcbLoaded) {
        m_pcbEmbedder->flipHorizontal();
        WritePCBDebugToFile("Flipped PCB view horizontally");
    }
}

void PCBViewerWidget::flipVertical()
{
    if (m_pcbEmbedder && m_pcbLoaded) {
        m_pcbEmbedder->flipVertical();
        WritePCBDebugToFile("Flipped PCB view vertically");
    }
}

void PCBViewerWidget::toggleDiodeReadings()
{
    if (m_pcbEmbedder && m_pcbLoaded) {
        m_pcbEmbedder->toggleDiodeReadings();
        bool enabled = m_pcbEmbedder->isDiodeReadingsEnabled();
        if (m_actionToggleDiode) m_actionToggleDiode->setChecked(enabled);
    }
}

void PCBViewerWidget::togglePadRatsnet()
{
    WritePCBDebugToFile("Pad Ratsnet toggle clicked");
    
    if (m_pcbEmbedder && m_pcbLoaded) {
        m_pcbEmbedder->toggleRatsnet();
        bool enabled = m_pcbEmbedder->isRatsnetEnabled();
        if (m_actionPadRatsnet) m_actionPadRatsnet->setChecked(enabled);
        WritePCBDebugToFile(QString("Pad Ratsnet %1").arg(enabled ? "enabled" : "disabled"));
    }
}

void PCBViewerWidget::zoomIn()
{
    if (m_pcbEmbedder && m_pcbLoaded) {
        m_pcbEmbedder->zoomIn();
        WritePCBDebugToFile("Zoom In action");
    }
}

void PCBViewerWidget::zoomOut()
{
    if (m_pcbEmbedder && m_pcbLoaded) {
        m_pcbEmbedder->zoomOut();
        WritePCBDebugToFile("Zoom Out action");
    }
}

void PCBViewerWidget::zoomToFit()
{
    if (m_pcbEmbedder && m_pcbLoaded) {
        m_pcbEmbedder->zoomToFit();
        WritePCBDebugToFile("Zoom To Fit action");
    }
}

void PCBViewerWidget::populateNetList() {
    if (!m_pcbEmbedder || !m_pcbLoaded || !m_netCombo) return;
    auto nets = m_pcbEmbedder->getNetNames();
    QString current = m_netCombo->currentText();
    m_netCombo->blockSignals(true);
    m_netCombo->clear();
    m_netCombo->addItem(""); // empty = clear highlight
    for (const auto &n : nets) m_netCombo->addItem(QString::fromStdString(n));
    int idx = m_netCombo->findText(current);
    if (idx >= 0) m_netCombo->setCurrentIndex(idx);
    m_netCombo->blockSignals(false);
}

void PCBViewerWidget::populateNetAndComponentList() {
    if (!m_pcbEmbedder || !m_pcbLoaded || !m_netCombo) return;
    auto nets = m_pcbEmbedder->getNetNames(); // nets first
    auto comps = m_pcbEmbedder->getComponentNames();
    QString current = m_netCombo->currentText();
    m_netCombo->blockSignals(true);
    m_netCombo->clear();
    m_netCombo->addItem("");
    // Add nets first
    for (const auto &n : nets) m_netCombo->addItem(QString::fromStdString(n));
    // Then add components (skip duplicates if same string)
    for (const auto &c : comps) {
        if (m_netCombo->findText(QString::fromStdString(c)) < 0)
            m_netCombo->addItem(QString::fromStdString(c));
    }
    int idx = m_netCombo->findText(current);
    if (idx >= 0) m_netCombo->setCurrentIndex(idx);
    m_netCombo->blockSignals(false);
}

void PCBViewerWidget::highlightCurrentNet() {
    if (!m_pcbEmbedder) return;
    QString net = m_netCombo ? m_netCombo->currentText() : QString();
    if (net.isEmpty()) {
        m_pcbEmbedder->clearHighlights();
    } else {
        m_pcbEmbedder->highlightNet(net.toStdString());
    }
}

void PCBViewerWidget::onPinSelectedFromViewer(const std::string &pinName, const std::string &netName) {
    Q_UNUSED(pinName);
    if (!m_netCombo) return;
    if (m_netCombo->count() == 0) populateNetAndComponentList();
    QString qnet = QString::fromStdString(netName);
    int idx = m_netCombo->findText(qnet);
    if (idx >= 0) m_netCombo->setCurrentIndex(idx);
}

void PCBViewerWidget::onPartSelectedFromViewer(const std::string &partName) {
    if (!m_netCombo) return;
    if (m_netCombo->count() == 0) populateNetAndComponentList();
    QString qpart = QString::fromStdString(partName);
    int idx = m_netCombo->findText(qpart);
    if (idx >= 0) m_netCombo->setCurrentIndex(idx);
}

void PCBViewerWidget::onNetSearchClicked() {
    if (!m_pcbEmbedder || !m_netCombo) return;
    QString text = m_netCombo->currentText().trimmed();
    if (text.isEmpty()) { m_pcbEmbedder->clearHighlights(); m_pcbEmbedder->clearSelection(); return; }
    // Determine if matches component
    auto comps = m_pcbEmbedder->getComponentNames();
    bool isComp = std::find(comps.begin(), comps.end(), text.toStdString()) != comps.end();
    if (isComp) {
        // Clear any existing net/pin highlights, then highlight & zoom to component
        m_pcbEmbedder->clearHighlights();
        m_pcbEmbedder->clearSelection();
        m_pcbEmbedder->highlightComponent(text.toStdString());
        m_pcbEmbedder->zoomToComponent(text.toStdString());
    } else {
    // Clear any existing component/pin highlight, then highlight & zoom to net
    // Important: clearHighlights() clears both component and net highlights to avoid stale component glow
    m_pcbEmbedder->clearHighlights();
        m_pcbEmbedder->clearSelection();
        // Note: highlightNet replaces any prior net highlight state internally
        m_pcbEmbedder->highlightNet(text.toStdString());
        m_pcbEmbedder->zoomToNet(text.toStdString());
    }
}

void PCBViewerWidget::onNetComboActivated(int index) {
    Q_UNUSED(index);
    // Reuse the same logic as clicking Go
    onNetSearchClicked();
}

bool PCBViewerWidget::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_viewerContainer) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::RightButton) {
                m_rightPressPos = me->pos();
                m_rightPressTimeMs = QDateTime::currentMSecsSinceEpoch();
                m_rightDragging = false;
            }
        } else if (event->type() == QEvent::MouseMove) {
            if (m_rightPressTimeMs) {
                QMouseEvent *me = static_cast<QMouseEvent*>(event);
                if ((me->pos() - m_rightPressPos).manhattanLength() > 6) m_rightDragging = true;
            }
    } else if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
        // Remove Qt-side menu opening path to avoid duplicate menus; rely on embedder callback
            if (me->button() == Qt::RightButton) { m_rightPressTimeMs = 0; m_rightDragging = false; }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PCBViewerWidget::showCrossContextMenu(const QPoint &globalPos, const QString &candidate) {
    // Reentrancy guard: ensure only one menu is active at a time
    if (m_contextMenuActive) return;
    m_contextMenuActive = true;
    QString target = m_linkedPdfFileName.isEmpty() ? QStringLiteral("Linked PDF") : m_linkedPdfFileName;
        class ThemedMenu : public QMenu { public: ThemedMenu(QWidget* p=nullptr):QMenu(p){ setWindowFlags(windowFlags()|Qt::NoDropShadowWindowHint); setAttribute(Qt::WA_TranslucentBackground);} void apply(bool dark){ if(dark){ setStyleSheet(
                "QMenu { background:rgba(30,33,40,0.94); border:1px solid #3d4452; border-radius:8px; padding:6px; font:13px 'Segoe UI'; color:#dfe3ea; }"
                "QMenu::item { background:transparent; padding:6px 14px; border-radius:5px; }"
                "QMenu::item:selected { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #2563eb, stop:1 #1d4ed8); color:white; }"
                "QMenu::separator { height:1px; background:#465061; margin:6px 4px; }" ); }
            else { setStyleSheet(
                "QMenu { background:rgba(252,252,253,0.97); border:1px solid #d0d7e2; border-radius:8px; padding:6px; font:13px 'Segoe UI'; color:#2d3744; }"
                "QMenu::item { background:transparent; padding:6px 14px; border-radius:5px; }"
                "QMenu::item:selected { background: #1a73e8; color:white; }"
                "QMenu::separator { height:1px; background:#e1e6ed; margin:6px 4px; }" ); } } }; ThemedMenu menu; bool dark = qApp->palette().color(QPalette::Window).lightness() < 128; menu.apply(dark);
    
    // Get part and net information from both selected pins and highlighted parts
    std::string selPart = m_pcbEmbedder ? m_pcbEmbedder->getSelectedPinPart() : std::string();
    std::string selNet  = m_pcbEmbedder ? m_pcbEmbedder->getSelectedPinNet()  : std::string();
    
    // If no pin is selected, check for highlighted part
    if (selPart.empty() && m_pcbEmbedder) {
        selPart = m_pcbEmbedder->getHighlightedPartName();
    }
    
    bool havePart = !selPart.empty(); 
    bool haveNet = !selNet.empty();
    
    QAction *title = menu.addAction(QString("Cross Search → %1").arg(target)); title->setEnabled(false);
    if (!candidate.isEmpty()) {
        QAction *cand = menu.addAction(QString("Candidate: '%1'").arg(candidate));
        cand->setEnabled(false);
    }
    menu.addSeparator();
    QAction *actComp = menu.addAction(QIcon(":/icons/images/icons/find_component.svg"), havePart ? QString("Find Component '%1'").arg(QString::fromStdString(selPart)) : QString("Find Component"));
    QAction *actNet  = menu.addAction(QIcon(":/icons/images/icons/find_net.svg"), haveNet ? QString("Find Net '%1'").arg(QString::fromStdString(selNet)) : QString("Find Net"));
    if (!havePart) actComp->setEnabled(false);
    if (!haveNet) actNet->setEnabled(false);
    menu.addSeparator();
    QAction *actCancel = menu.addAction("Cancel");

    // While the menu is open, capture the very next outside click (left or right)
    // and forward it to the embedded viewer as a selection. This lets users
    // select another pin/part in a single click (instead of click-to-close, then click-to-select).
    class OutsideClickForwarder : public QObject {
    public:
        OutsideClickForwarder(QMenu *menu, QWidget *viewerContainer, PCBViewerEmbedder *embedder, PCBViewerWidget *owner)
            : m_menu(menu), m_container(viewerContainer), m_embedder(embedder), m_owner(owner) {}
    protected:
        bool eventFilter(QObject *obj, QEvent *event) override {
            Q_UNUSED(obj);
            if (!m_menu || !m_menu->isVisible() || !m_container || !m_embedder) return false;
            if (event->type() == QEvent::MouseButtonPress) {
                auto *me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton || me->button() == Qt::RightButton) {
                    // Ignore clicks inside the menu; only handle outside
                    if (!m_menu->geometry().contains(me->globalPosition().toPoint())) {
                        QPoint local = m_container->mapFromGlobal(me->globalPosition().toPoint());
                        if (local.x() >= 0 && local.y() >= 0 && local.x() < m_container->width() && local.y() < m_container->height()) {
                            // Clear any previous highlight and selection so we move cleanly to the new target
                            m_embedder->clearHighlights();
                            m_embedder->clearSelection();
                            // Forward to viewer: update hover first for accuracy, then select
                            m_embedder->handleMouseMove(local.x(), local.y());
                            m_embedder->handleMouseClick(local.x(), local.y(), /*GLFW left*/ 0);
                            
                            // After selection, if a pin was selected, also highlight its parent component
                            if (m_embedder->hasSelection()) {
                                std::string partName = m_embedder->getSelectedPinPart();
                                if (!partName.empty()) {
                                    m_embedder->highlightComponent(partName);
                                }
                            }
                            
                            // Draw a frame so the new highlight is visible immediately under the current menu
                            m_embedder->render();

                            // If it was a right click, open the context menu again for the newly selected target
                            if (me->button() == Qt::RightButton && m_owner) {
                                // Record a pending reopen at this position and suppress embedder callback once
                                m_owner->m_pendingReopenRequested = true;
                                m_owner->m_pendingReopenGlobalPos = me->globalPosition().toPoint();
                                m_owner->suppressNextEmbedderMenuOnce();
                            }
                        }
                    }
                }
            }
            return false; // don't consume; allow normal closing of the menu
        }
    private:
    QMenu *m_menu {nullptr};
    QWidget *m_container {nullptr};
    PCBViewerEmbedder *m_embedder {nullptr};
    PCBViewerWidget *m_owner {nullptr};
    };

    // Ensure the latest selection/highlight state is drawn before opening the menu
    if (m_pcbEmbedder) m_pcbEmbedder->render();

    OutsideClickForwarder forwarder(&menu, m_viewerContainer, m_pcbEmbedder.get(), this);
    qApp->installEventFilter(&forwarder);
    QAction *chosen = menu.exec(globalPos);
    qApp->removeEventFilter(&forwarder);
    m_contextMenuActive = false;
    // If user right-clicked outside, schedule a reopen now that this menu is closed
    if (m_pendingReopenRequested) {
        QPoint reopenPos = m_pendingReopenGlobalPos;
        m_pendingReopenRequested = false;
        // Use singleShot to post after current event processing
        QTimer::singleShot(0, this, [this, reopenPos]() {
            if (!m_contextMenuActive) showCrossContextMenu(reopenPos, QString());
        });
    }
    if (!chosen || chosen==actCancel || chosen==title) return;
    if (chosen == actComp && havePart) emit crossSearchRequest(QString::fromStdString(selPart), false, true);
    else if (chosen == actNet && haveNet) emit crossSearchRequest(QString::fromStdString(selNet), true, true);
}

bool PCBViewerWidget::externalSearchNet(const QString &net) {
    if (!m_pcbEmbedder || !m_pcbLoaded) return false;
    QString t = net.trimmed(); if (t.isEmpty()) return false;
    auto nets = m_pcbEmbedder->getNetNames();
    bool found = std::find(nets.begin(), nets.end(), t.toStdString()) != nets.end();
    if (found) {
    // Ensure no previous component highlight remains when switching to a net highlight
    m_pcbEmbedder->clearHighlights();
    m_pcbEmbedder->clearSelection();
    m_pcbEmbedder->highlightNet(t.toStdString());
        m_pcbEmbedder->zoomToNet(t.toStdString());
    }
    return found;
}

bool PCBViewerWidget::externalSearchComponent(const QString &comp) {
    if (!m_pcbEmbedder || !m_pcbLoaded) return false;
    QString t = comp.trimmed(); if (t.isEmpty()) return false;
    auto comps = m_pcbEmbedder->getComponentNames();
    bool found = std::find(comps.begin(), comps.end(), t.toStdString()) != comps.end();
    if (found) {
    // Clear previous highlights AND any selected pin so no single pin/net glow remains
    m_pcbEmbedder->clearHighlights();
    m_pcbEmbedder->clearSelection();
    m_pcbEmbedder->highlightComponent(t.toStdString());
    m_pcbEmbedder->zoomToComponent(t.toStdString());
    }
    return found;
}

// Split view removed: onSplitWindowClicked deleted

// Split view removed: embedding PDF viewer no longer supported

// isSplitViewActive() removed
