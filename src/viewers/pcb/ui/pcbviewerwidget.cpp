#include "viewers/pcb/PCBViewerWidget.h"
#include "viewers/pcb/PCBViewerEmbedder.h"
#include "../rendering/PCBRenderer.h" // for ColorTheme enum values
#include "ui/LoadingOverlay.h"
#include "core/memoryfilemanager.h"
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>

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
#include <QCoreApplication>
#include "PCBTheme.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QColor>
#include <QVariant>
#include <QMetaType>
// QWidgetAction header may not be available in current include paths; we'll use addWidget helpers instead.

// --- Premium toolbar helpers (icon tinting, hover/checked states) ---
static QIcon makeTintedIcon(const QIcon &base, const QColor &color, const QSize &size)
{
    if (base.isNull()) return base;
    QPixmap src = base.pixmap(size);
    if (src.isNull()) return base;
    QImage img = src.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage colored(img.size(), QImage::Format_ARGB32_Premultiplied);
    colored.fill(Qt::transparent);
    QPainter p(&colored);
    p.fillRect(colored.rect(), color);
    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    p.drawImage(0, 0, img);
    p.end();
    return QIcon(QPixmap::fromImage(colored));
}

class PremiumButtonFilter : public QObject {
public:
    PremiumButtonFilter(QToolButton* btn, const QIcon &normal, const QIcon &hover, const QIcon &disabled)
        : QObject(btn), m_btn(btn), m_normal(normal), m_hover(hover), m_disabled(disabled) {}
protected:
    bool eventFilter(QObject *obj, QEvent *ev) override {
        if (obj != m_btn) return QObject::eventFilter(obj, ev);
        switch (ev->type()) {
            case QEvent::Enter:
                if (m_btn->isEnabled()) m_btn->setIcon(m_hover);
                break;
            case QEvent::Leave:
                if (m_btn->isEnabled()) m_btn->setIcon((m_btn->isCheckable() && m_btn->isChecked()) ? m_hover : m_normal);
                break;
            case QEvent::EnabledChange:
                m_btn->setIcon(m_btn->isEnabled() ? (m_btn->underMouse() || (m_btn->isCheckable() && m_btn->isChecked()) ? m_hover : m_normal) : m_disabled);
                break;
            default: break;
        }
        return QObject::eventFilter(obj, ev);
    }
private:
    QToolButton* m_btn {nullptr};
    QIcon m_normal, m_hover, m_disabled;
};

static void installPremiumButtonStyling(QToolBar *bar, const QColor &accent, bool dark)
{
    if (!bar) return;
    const QSize iconSz(16, 16);
    const QColor normal = dark ? QColor("#c7cacf") : QColor("#5f6368");
    const QColor disabled = dark ? QColor("#6f7379") : QColor("#9e9e9e");

    for (QAction *act : bar->actions()) {
        if (!act || act->isSeparator()) continue;
        if (auto *btn = qobject_cast<QToolButton*>(bar->widgetForAction(act))) {
            const QIcon baseIcon = act->icon();
            QIcon iconNormal = makeTintedIcon(baseIcon, normal, iconSz);
            QIcon iconHover  = makeTintedIcon(baseIcon, accent, iconSz);
            QIcon iconDis    = makeTintedIcon(baseIcon, disabled, iconSz);
            btn->setAutoRaise(true);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setIconSize(iconSz);
            btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
            btn->setIcon(act->isEnabled() ? iconNormal : iconDis);
            btn->setAttribute(Qt::WA_Hover, true);
            auto *filter = new PremiumButtonFilter(btn, iconNormal, iconHover, iconDis);
            btn->installEventFilter(filter);
            QObject::connect(btn, &QToolButton::toggled, btn, [btn, iconNormal, iconHover]() {
                if (!btn->isEnabled()) return;
                btn->setIcon(btn->isChecked() ? iconHover : (btn->underMouse() ? iconHover : iconNormal));
            });
            QObject::connect(act, &QAction::changed, btn, [btn, act, iconNormal, iconDis]() {
                btn->setIcon(act->isEnabled() ? iconNormal : iconDis);
            });
        }
    }
}

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
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(false);
    setStyleSheet("background: transparent;");
    
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
        updateLayerBarVisibility();
        if (m_pcbEmbedder) m_pcbEmbedder->setLayerFilter(-1);
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

bool PCBViewerWidget::loadPCBFromMemory(const QString& memoryId, const QString& originalKey) {
    // Get the data from memory manager
    MemoryFileManager* memMgr = MemoryFileManager::instance();
    QByteArray data = memMgr->getFileData(memoryId);
    
    if (data.isEmpty()) {
        emit errorOccurred(QString("PCB data not found in memory for ID: %1").arg(memoryId));
        return false;
    }

    if (!m_viewerInitialized) {
        initializePCBViewer();
    }
    
    if (!m_viewerInitialized) {
        emit errorOccurred("Failed to initialize PCB viewer");
        return false;
    }

    WritePCBDebugToFile("Loading PCB from memory: " + memoryId);

    QString displayName = originalKey.isEmpty() ? memoryId : originalKey;
    bool success = m_pcbEmbedder->loadPCBFromMemory(data.constData(), data.size(), displayName.toStdString());
    
    if (success) {
        m_pcbLoaded = true;
        m_currentFilePath = memoryId; // Store memory ID as current file path
        
        // Start update timer if not already running
        if (!m_updateTimer->isActive()) {
            m_updateTimer->start();
        }
        
        WritePCBDebugToFile("PCB file loaded successfully from memory");
        emit pcbLoaded(displayName);
    } else {
        WritePCBDebugToFile("Failed to load PCB file from memory");
        emit errorOccurred("Failed to load PCB file from memory: " + displayName);
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
    updateLayerBarVisibility();
    
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
    
    // Use thread-safe approach to prevent race conditions
    static QMutex toolbarMutex;
    QMutexLocker locker(&toolbarMutex);
    
    m_toolbarVisible = visible;
    if (m_toolbar) {
        // Use deferred operations to prevent UI freezing
        QTimer::singleShot(0, this, [this, visible]() {
            if (m_toolbar) {  // Double-check toolbar still exists
                m_toolbar->setVisible(visible);
                m_toolbar->setEnabled(visible);
                
                // Update child widgets asynchronously
                if (visible && m_netCombo) {
                    QTimer::singleShot(5, this, [this]() {
                        if (m_netCombo) {
                            m_netCombo->setEnabled(true);
                            
                            // If we have a pending selection, populate the list to ensure it's available
                            if (!m_pendingSelection.isEmpty()) {
                                qDebug() << "Toolbar became visible with pending selection:" << m_pendingSelection;
                                populateNetAndComponentList();
                            } else if (m_netCombo->count() == 0) {
                                // Only populate if empty to avoid unnecessary refreshes
                                populateNetAndComponentList();
                            }
                        }
                    });
                }
                
                // Deferred geometry updates
                QTimer::singleShot(10, this, [this]() {
                    updateGeometry();
                    update();
                });
            }
        });
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
    updateLayerBarVisibility();
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
    // Don’t force a white background; keep it transparent so the viewer’s
    // content/color shows through next to the layer toolbar.
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
    updateLayerBarVisibility();
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
    
    // Viewer container with optional left layer bar
    m_viewerContainer = new QWidget(this);
    m_viewerContainer->setMinimumSize(300, 300); // leave room for the left layer bar
    m_viewerContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_viewerContainer->setFocusPolicy(Qt::StrongFocus);
    m_viewerContainer->installEventFilter(this);
    m_viewerContainer->setAttribute(Qt::WA_StyledBackground, true);
    m_viewerContainer->setAutoFillBackground(false); // will be set true when layer bar shows
    m_viewerContainer->setStyleSheet("background: transparent;");
    // Add a left-side layer bar similar to the screenshot (hidden by default)
    setupLayerBar();
    // Add toolbar and viewer container to main layout
    if (m_toolbar) {
        m_mainLayout->addWidget(m_toolbar);
    }
    // Wrap viewer and layer bar in a horizontal layout (no overlay to avoid native window z-order issues)
    {
        auto *h = new QHBoxLayout();
        h->setContentsMargins(0,0,0,0);
        h->setSpacing(0);
        if (m_layerBar) {
            m_layerBar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
            h->addWidget(m_layerBar);
        }
        h->addWidget(m_viewerContainer, 1);
        m_mainLayout->addLayout(h, 1);
    }
    
    WritePCBDebugToFile("PCB viewer UI setup completed (single-pane)");
}

void PCBViewerWidget::setupToolbar()
{
    WritePCBDebugToFile("Setting up PCB viewer Qt toolbar to match PDF viewer styling");

    // Create toolbar with same styling as PDF viewer
    m_toolbar = new QToolBar(this);
    m_toolbar->setIconSize(QSize(16, 16));
    m_toolbar->setMovable(false);
    // Force light theme - ignore palette darkness detection
    const bool dark = false;
    // Use opaque backgrounds like PDF so theme switching is obvious visually
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

    // Color Theme selector (combo box): load from config/pcb_themes.json when present, else fallback to 3 presets
    {
        m_toolbar->addSeparator();
        auto *themeBox = new QComboBox(m_toolbar);
        themeBox->setObjectName("pcbThemeCombo");
        themeBox->setToolTip("Color Theme");
        themeBox->setFixedHeight(26);
        themeBox->setMinimumWidth(180);
        // Style similar to net combo for consistency
        const bool dark2 = false; // Force light theme
        const QString border2 = dark2 ? "#5f6368" : "#ccc";
        const QString bg2 = dark2 ? "#2a2b2d" : "white";
        const QString fg2 = dark2 ? "#e8eaed" : "#111";
        const QString focus2 = dark2 ? "#cf6679" : "#E53935";
        const QString viewBg2 = dark2 ? "#1f2023" : "white";
        const QString viewFg2 = fg2;
        const QString viewSelBg2 = dark2 ? "#3b1f22" : "#fdeaea";
        const QString viewSelFg2 = fg2;
        themeBox->setStyleSheet(QString(
            "QComboBox, QComboBox:editable{border:1px solid %1;border-radius:3px;padding:2px 8px;background:%2;color:%3;}"
            "QComboBox:focus, QComboBox:editable:focus{border-color:%4;}"
            "QComboBox QAbstractItemView{background:%5;color:%6;border:1px solid %1;outline:0;selection-background-color:%7;selection-color:%8;}"
        ).arg(border2, bg2, fg2, focus2, viewBg2, viewFg2, viewSelBg2, viewSelFg2));
        // Try to load JSON themes
        struct JsonThemeRow { QString name; QVariantMap map; };
        QList<JsonThemeRow> themeRows;
        {
            auto tryLoad = [&](const QString &path) -> bool {
                QFile f(path);
                if (!f.exists()) return false;
                if (!f.open(QIODevice::ReadOnly)) return false;
                QByteArray raw = f.readAll(); f.close();
                QJsonParseError err; QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
                if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
                auto obj = doc.object();
                auto arr = obj.value(QStringLiteral("themes")).toArray();
                for (const auto &it : arr) {
                    if (!it.isObject()) continue;
                    auto to = it.toObject();
                    QVariantMap map = to.toVariantMap();
                    QString nm = to.value(QStringLiteral("name")).toString();
                    if (nm.isEmpty()) nm = QStringLiteral("Theme %1").arg(themeRows.size()+1);
                    themeRows.push_back({nm, map});
                }
                return !themeRows.isEmpty();
            };

            // Try CWD, then relative to app dir (../config)
            if (!tryLoad(QStringLiteral("config/pcb_themes.json"))) {
                const QString alt = QCoreApplication::applicationDirPath() + "/../config/pcb_themes.json";
                (void)tryLoad(alt);
            }
        }
        // Populate combo: if JSON provided, use it. Otherwise, fall back to presets
        if (!themeRows.isEmpty()) {
            for (const auto &row : themeRows) {
                themeBox->addItem(row.name, row.map);
            }
        } else {
            // Only light theme options when JSON themes are not available
            themeBox->addItem("Light", 1);
            themeBox->addItem("Default (Light)", 1);
        }
        m_toolbar->addWidget(themeBox);

        // Initialize current selection
        if (m_pcbEmbedder) {
            if (!themeRows.isEmpty()) {
                // Try to match by name against current preset name
                const QString curName = QString::fromStdString(m_pcbEmbedder->currentThemeName());
                int found = -1;
                for (int i = 0; i < themeBox->count(); ++i) {
                    if (themeBox->itemText(i).compare(curName, Qt::CaseInsensitive) == 0) { found = i; break; }
                }
                themeBox->setCurrentIndex(found >= 0 ? found : 0);
            } else {
                // Force Light theme selection - always use index 0 which should be Light theme
                int idx = 0; // Always use Light theme (first in our updated JSON)
                themeBox->setCurrentIndex(idx);
            }
        }

        // Selection handler: apply JSON theme if present for selected row, else switch preset
        connect(themeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, themeBox](int idx){
            if (!m_pcbEmbedder || idx < 0) return;
            const QVariant data = themeBox->itemData(idx);
            if (data.typeId() == QMetaType::QVariantMap) {
                // Build PCBThemeSpec from JSON map
                PCBThemeSpec spec;
                QVariantMap m = data.toMap();
                auto str = [&](const char* k){ return m.value(k).toString(); };
                auto parseColorHex = [&](const QString &hex, float &r, float &g, float &b){
                    QColor c(hex);
                    if (!c.isValid()) return false;
                    r = c.redF(); g = c.greenF(); b = c.blueF();
                    return true;
                };

                spec.name = str("name").toStdString();
                spec.base = str("base").toStdString();
                spec.overridePinColors = m.value("overridePinColors").toBool();

                // Colors accept either hex: "#RRGGBB" or r/g/b floats 0..1
                auto applyColor = [&](const char* key, float &r, float &g, float &b){
                    if (m.contains(key)) {
                        const QVariant v = m.value(key);
                        if (v.typeId() == QMetaType::QString) {
                            parseColorHex(v.toString(), r, g, b);
                        } else if (v.typeId() == QMetaType::QVariantMap) {
                            auto mm = v.toMap();
                            r = (float)mm.value("r", r).toDouble();
                            g = (float)mm.value("g", g).toDouble();
                            b = (float)mm.value("b", b).toDouble();
                        }
                    }
                };

                applyColor("background", spec.background_r, spec.background_g, spec.background_b);
                applyColor("outline", spec.outline_r, spec.outline_g, spec.outline_b);
                applyColor("partOutline", spec.part_outline_r, spec.part_outline_g, spec.part_outline_b);
                applyColor("pin", spec.pin_r, spec.pin_g, spec.pin_b);
                applyColor("sameNetPin", spec.same_net_pin_r, spec.same_net_pin_g, spec.same_net_pin_b);
                applyColor("ncPin", spec.nc_pin_r, spec.nc_pin_g, spec.nc_pin_b);
                applyColor("groundPin", spec.ground_pin_r, spec.ground_pin_g, spec.ground_pin_b);
                applyColor("ratsnet", spec.ratsnet_r, spec.ratsnet_g, spec.ratsnet_b);
                applyColor("partHighlightBorder", spec.part_highlight_border_r, spec.part_highlight_border_g, spec.part_highlight_border_b);
                applyColor("partHighlightFill", spec.part_highlight_fill_r, spec.part_highlight_fill_g, spec.part_highlight_fill_b);
                // Text colors
                applyColor("pinText", spec.pin_text_r, spec.pin_text_g, spec.pin_text_b);
                applyColor("netText", spec.net_text_r, spec.net_text_g, spec.net_text_b);
                applyColor("diodeText", spec.diode_text_r, spec.diode_text_g, spec.diode_text_b);
                applyColor("componentNameText", spec.component_name_text_r, spec.component_name_text_g, spec.component_name_text_b);
                // Component name background with optional alpha
                applyColor("componentNameBg", spec.component_name_bg_r, spec.component_name_bg_g, spec.component_name_bg_b);
                if (m.contains("componentNameBgAlpha")) {
                    spec.component_name_bg_a = (float)m.value("componentNameBgAlpha", spec.component_name_bg_a).toDouble();
                }

                spec.part_alpha = (float)m.value("partAlpha", spec.part_alpha).toDouble();
                spec.pin_alpha = (float)m.value("pinAlpha", spec.pin_alpha).toDouble();
                spec.outline_alpha = (float)m.value("outlineAlpha", spec.outline_alpha).toDouble();
                spec.part_outline_alpha = (float)m.value("partOutlineAlpha", spec.part_outline_alpha).toDouble();

                m_pcbEmbedder->applyTheme(spec);
            } else {
                int preset = data.toInt();
                // Force Light theme - always use preset 1 (Light)
                ColorTheme theme = ColorTheme::Light;
                m_pcbEmbedder->setColorTheme(theme);
            }
            // Force a redraw so change is visible immediately
            if (m_updateTimer && !m_updateTimer->isActive()) updateViewer();
        });
    }
    m_toolbar->addSeparator();

    WritePCBDebugToFile("Zoom and rotation/flip actions added to PCB toolbar");
    WritePCBDebugToFile("Split window action removed (feature deprecated)");
    WritePCBDebugToFile("PCB Qt toolbar setup completed with PDF viewer styling");
    // Install premium icon tinting/hover behavior like PDF
    installPremiumButtonStyling(m_toolbar, QColor(dark ? "#cf6679" : "#E53935"), dark);
}

void PCBViewerWidget::positionLayerBar()
{
    if (!m_layerBar || !m_layerBar->isVisible()) return;
    // Pin the layer bar to the left edge with a small inset
    const int inset = 4;
    int h = m_viewerContainer ? m_viewerContainer->height() : height();
    // Determine preferred width based on button size
    int w = 40; // enough for 32px button width + margins
    m_layerBar->setGeometry(inset, inset, w, h - inset*2);
}

// Create the vertical bar with 1..10 and ALL buttons on the left
void PCBViewerWidget::setupLayerBar()
{
    if (m_layerBar) {
        WritePCBDebugToFile("setupLayerBar called but m_layerBar already exists");
        return;
    }
    const bool dark = false; // Force light theme
    m_layerBar = new QWidget(this);
    // Paint this widget fully (no translucency) so its black background is solid
    m_layerBar->setAttribute(Qt::WA_StyledBackground, true);
    m_layerBar->setAutoFillBackground(true);
    m_layerBar->setObjectName("LayerBar");
    m_layerBar->setMinimumWidth(44); // ensure it isn't squeezed to zero width
    m_layerBar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto *v = new QVBoxLayout(m_layerBar);
    v->setContentsMargins(4,4,4,4);
    v->setSpacing(6);

    const QColor textColor = dark ? QColor("#e8eaed") : QColor("#111111");

    // Material palette accents for each button (11 total inc. ALL)
    const QList<QColor> colors = {
        QColor("#F44336"), // 1  Red
        QColor("#E91E63"), // 2  Pink
        QColor("#9C27B0"), // 3  Purple
        QColor("#673AB7"), // 4  Deep Purple
        QColor("#3F51B5"), // 5  Indigo
        QColor("#2196F3"), // 6  Blue
        QColor("#03A9F4"), // 7  Light Blue
        QColor("#00BCD4"), // 8  Cyan
        QColor("#009688"), // 9  Teal
        QColor("#4CAF50"), // 10 Green
        QColor("#FF9800")  // ALL Orange
    };

    auto mkBtn = [&](const QString &text, const QColor &accent){
        auto *b = new QToolButton(m_layerBar);
        b->setText(text);
        b->setCheckable(true);
        b->setFixedSize(32, 24);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setAutoRaise(true);
        // Light white background for all states with subtle changes; keep colored borders per button
        const QString bgNormal = dark ? "rgba(255,255,255,0.12)" : "#FAFAFA";
        const QString bgHover  = dark ? "rgba(255,255,255,0.18)" : "#F2F2F2";
        const QString base = QString(
            "QToolButton{background:%1;border:1px solid %2;color:%3;border-radius:6px;}"
            "QToolButton:hover{background:%4;}"
            // When active, fill with the accent color (same as border) and use white text for contrast
            "QToolButton:checked{background:%2;border-color:%2;color:#FFFFFF;font-weight:600;}"
        ).arg(bgNormal, accent.name(), textColor.name(), bgHover);
        b->setStyleSheet(base);
        return b;
    };

    // Buttons 1..10 with distinct Material colors
    for (int i=1;i<=10;++i){
        auto *b = mkBtn(QString::number(i), colors[i-1]);
        v->addWidget(b);
        m_layerButtons.append(b);
        connect(b, &QToolButton::clicked, this, [this,i](){
            m_activeLayerFilter = i;
            for (int j=0;j<m_layerButtons.size();++j) m_layerButtons[j]->setChecked((j+1)==i);
            if (m_pcbEmbedder) m_pcbEmbedder->setLayerFilter(i);
        });
    }

    // ALL button gets its own color
    auto *bAll = mkBtn("ALL", colors.last());
    v->addWidget(bAll);
    m_layerButtons.append(bAll);
    connect(bAll, &QToolButton::clicked, this, [this,bAll](){
        m_activeLayerFilter = -1;
        for (auto *btn : std::as_const(m_layerButtons)) btn->setChecked(btn==bAll);
        if (m_pcbEmbedder) m_pcbEmbedder->setLayerFilter(-1);
    });
    v->addStretch(1);

    // Solid black background on the container only; buttons keep their own transparent styles
    m_layerBar->setStyleSheet("#LayerBar{background:#000000;border:none;margin:0;padding:0;}");
    WritePCBDebugToFile("LayerBar style applied: container background=#000000");
    m_layerBar->setVisible(false); // only show when file name contains "layer"
    WritePCBDebugToFile(QString("LayerBar created: ptr=%1 minW=%2")
                        .arg(reinterpret_cast<quintptr>(m_layerBar))
                        .arg(m_layerBar->minimumWidth()));
}

void PCBViewerWidget::updateLayerBarVisibility()
{
    // Ensure the layer bar exists; log state either way
    if (!m_layerBar) {
        WritePCBDebugToFile("updateLayerBarVisibility: m_layerBar is null; creating now");
        setupLayerBar();
    }
    if (!m_layerBar) {
        WritePCBDebugToFile("updateLayerBarVisibility: m_layerBar still null after setup; aborting");
        return;
    }
    const QString fp = m_currentFilePath;
    const bool show = fp.contains("layer", Qt::CaseInsensitive);
    m_layerBar->setVisible(show);
    // When showing the left bar, also ensure the viewer container paints black,
    // so there's no white seam beside the native OpenGL surface.
    if (m_viewerContainer) {
        if (show) {
            m_viewerContainer->setAttribute(Qt::WA_StyledBackground, true);
            m_viewerContainer->setAutoFillBackground(true);
            m_viewerContainer->setStyleSheet("background:#000000;");
        } else {
            // Restore original transparent background when the bar is hidden
            m_viewerContainer->setAttribute(Qt::WA_StyledBackground, true);
            m_viewerContainer->setAutoFillBackground(false);
            m_viewerContainer->setStyleSheet("background: transparent;");
        }
    }
    WritePCBDebugToFile(QString("LayerBar visibility check: file='%1' show=%2 width=%3")
                        .arg(fp)
                        .arg(show ? "true" : "false")
                        .arg(m_layerBar->width()));
    if (layout()) layout()->invalidate();
    updateGeometry();
}

void PCBViewerWidget::applyToolbarTheme()
{
    if (!m_toolbar) return;
    const bool dark = false; // Force light theme
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
    // Reinstall premium icon tinting for the new theme
    installPremiumButtonStyling(m_toolbar, QColor(dark ? "#cf6679" : "#E53935"), dark);
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
    // (Icon hover behavior is handled by premium filters above.)
}

void PCBViewerWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::StyleChange) {
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
    
    // Disable combo during population to prevent user interaction
    m_netCombo->setEnabled(false);
    QString current = m_netCombo->currentText();
    
    // Use async processing to prevent UI freezing with large component/net lists
    QTimer::singleShot(0, this, [this, current]() {
        if (!m_pcbEmbedder || !m_netCombo) {
            if (m_netCombo) m_netCombo->setEnabled(true);
            return;
        }
        
        try {
            // Get data from embedder
            auto nets = m_pcbEmbedder->getNetNames();
            auto comps = m_pcbEmbedder->getComponentNames();
            
            m_netCombo->blockSignals(true);
            m_netCombo->clear();
            m_netCombo->addItem("");  // Empty option to clear highlights
            
            // Add nets first with progress updates for large lists
            int count = 0;
            for (const auto &n : nets) {
                m_netCombo->addItem(QString::fromStdString(n));
                
                // Process events periodically to keep UI responsive
                if (++count % 50 == 0) {
                    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
                }
                
                // Safety limit for extremely large nets list
                if (count > 5000) {
                    qDebug() << "Net list truncated at 5000 items for performance";
                    break;
                }
            }
            
            // Then add components (skip duplicates if same string)
            count = 0;
            for (const auto &c : comps) {
                QString compName = QString::fromStdString(c);
                if (m_netCombo->findText(compName) < 0) {
                    m_netCombo->addItem(compName);
                }
                
                // Process events periodically to keep UI responsive
                if (++count % 50 == 0) {
                    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
                }
                
                // Safety limit for extremely large component list
                if (count > 5000) {
                    qDebug() << "Component list truncated at 5000 items for performance";
                    break;
                }
            }
            
            // Restore previous selection or handle pending selection
            if (!m_pendingSelection.isEmpty() && m_selectionType != SelectionType::None) {
                // Handle pending selection from viewer
                int idx = m_netCombo->findText(m_pendingSelection);
                if (idx >= 0) {
                    m_netCombo->setCurrentIndex(idx);
                    qDebug() << "Set pending selection:" << m_pendingSelection;
                } else {
                    qDebug() << "Pending selection not found in list:" << m_pendingSelection;
                }
                // Clear pending selection
                m_pendingSelection.clear();
                m_selectionType = SelectionType::None;
            } else {
                // Restore previous selection
                int idx = m_netCombo->findText(current);
                if (idx >= 0) m_netCombo->setCurrentIndex(idx);
            }
            
            m_netCombo->blockSignals(false);
            
        } catch (const std::exception &e) {
            qDebug() << "Exception during component list population:" << e.what();
            m_netCombo->blockSignals(false);
        } catch (...) {
            qDebug() << "Unknown exception during component list population";
            m_netCombo->blockSignals(false);
        }
        
        // Re-enable combo after population
        m_netCombo->setEnabled(true);
    });
}

void PCBViewerWidget::setComboBoxSelection(const QString &text) {
    if (!m_netCombo || text.isEmpty()) {
        qDebug() << "setComboBoxSelection: Invalid combo or empty text";
        return;
    }
    
    // Ensure combo is ready and enabled
    if (!m_netCombo->isEnabled()) {
        qDebug() << "setComboBoxSelection: Combo not enabled, deferring selection";
        // Store as pending selection and try again later
        m_pendingSelection = text;
        QTimer::singleShot(50, this, [this, text]() {
            setComboBoxSelection(text);
        });
        return;
    }
    
    // Block signals during selection to prevent recursive calls
    bool oldState = m_netCombo->blockSignals(true);
    
    int idx = m_netCombo->findText(text);
    if (idx >= 0) {
        m_netCombo->setCurrentIndex(idx);
        qDebug() << "Successfully set combo selection to:" << text << "at index:" << idx;
    } else {
        qDebug() << "Text not found in combo:" << text;
        // If not found, try partial match (useful for component names)
        for (int i = 0; i < m_netCombo->count(); ++i) {
            if (m_netCombo->itemText(i).contains(text, Qt::CaseInsensitive)) {
                m_netCombo->setCurrentIndex(i);
                qDebug() << "Set combo to partial match:" << m_netCombo->itemText(i);
                break;
            }
        }
    }
    
    m_netCombo->blockSignals(oldState);
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
    
    QString qnet = QString::fromStdString(netName);
    qDebug() << "Pin selected from viewer - Net:" << qnet;
    
    // If combo box is empty or being populated, ensure it's populated first
    if (m_netCombo->count() == 0 || !m_netCombo->isEnabled()) {
        // Store the target selection and populate async
        m_pendingSelection = qnet;
        m_selectionType = SelectionType::Net;
        
        // Ensure list is populated, then set selection
        if (m_netCombo->count() == 0) {
            populateNetAndComponentList();
        }
        
        // Use a timer to wait for population and then set selection
        QTimer::singleShot(100, this, [this, qnet]() {
            setComboBoxSelection(qnet);
        });
        
        // Backup timeout in case population takes too long
        QTimer::singleShot(500, this, [this, qnet]() {
            if (!m_pendingSelection.isEmpty() && m_pendingSelection == qnet) {
                qDebug() << "Selection timeout, forcing combo refresh for:" << qnet;
                if (m_netCombo && m_netCombo->isEnabled()) {
                    setComboBoxSelection(qnet);
                }
            }
        });
    } else {
        // Direct selection if combo is ready
        setComboBoxSelection(qnet);
    }
}

void PCBViewerWidget::onPartSelectedFromViewer(const std::string &partName) {
    if (!m_netCombo) return;
    
    QString qpart = QString::fromStdString(partName);
    qDebug() << "Part selected from viewer - Component:" << qpart;
    
    // If combo box is empty or being populated, ensure it's populated first
    if (m_netCombo->count() == 0 || !m_netCombo->isEnabled()) {
        // Store the target selection and populate async
        m_pendingSelection = qpart;
        m_selectionType = SelectionType::Component;
        
        // Ensure list is populated, then set selection
        if (m_netCombo->count() == 0) {
            populateNetAndComponentList();
        }
        
        // Use a timer to wait for population and then set selection
        QTimer::singleShot(100, this, [this, qpart]() {
            setComboBoxSelection(qpart);
        });
        
        // Backup timeout in case population takes too long
        QTimer::singleShot(500, this, [this, qpart]() {
            if (!m_pendingSelection.isEmpty() && m_pendingSelection == qpart) {
                qDebug() << "Selection timeout, forcing combo refresh for:" << qpart;
                if (m_netCombo && m_netCombo->isEnabled()) {
                    setComboBoxSelection(qpart);
                }
            }
        });
    } else {
        // Direct selection if combo is ready
        setComboBoxSelection(qpart);
    }
}

void PCBViewerWidget::onNetSearchClicked() {
    if (!m_pcbEmbedder || !m_netCombo) return;
    
    QString text = m_netCombo->currentText().trimmed();
    if (text.isEmpty()) { 
        m_pcbEmbedder->clearHighlights(); 
        m_pcbEmbedder->clearSelection(); 
        return; 
    }
    
    // Disable the search controls temporarily to prevent concurrent operations
    m_netCombo->setEnabled(false);
    if (m_netSearchButton) m_netSearchButton->setEnabled(false);
    
    // Use async processing to prevent UI freezing during search
    QTimer::singleShot(0, this, [this, text]() {
        try {
            // Check if the text matches a component
            auto comps = m_pcbEmbedder->getComponentNames();
            bool isComp = std::find(comps.begin(), comps.end(), text.toStdString()) != comps.end();
            
            if (isComp) {
                // Clear any existing net/pin highlights, then highlight & zoom to component
                m_pcbEmbedder->clearHighlights();
                m_pcbEmbedder->clearSelection();
                m_pcbEmbedder->highlightComponent(text.toStdString());
                
                // Deferred zoom to prevent UI blocking
                QTimer::singleShot(10, this, [this, text]() {
                    if (m_pcbEmbedder) {
                        m_pcbEmbedder->zoomToComponent(text.toStdString());
                    }
                });
            } else {
                // Clear any existing component/pin highlight, then highlight & zoom to net
                // Important: clearHighlights() clears both component and net highlights to avoid stale component glow
                m_pcbEmbedder->clearHighlights();
                m_pcbEmbedder->clearSelection();
                // Note: highlightNet replaces any prior net highlight state internally
                m_pcbEmbedder->highlightNet(text.toStdString());
                
                // Deferred zoom to prevent UI blocking
                QTimer::singleShot(10, this, [this, text]() {
                    if (m_pcbEmbedder) {
                        m_pcbEmbedder->zoomToNet(text.toStdString());
                    }
                });
            }
            
        } catch (const std::exception &e) {
            qDebug() << "Exception during PCB search:" << e.what();
        } catch (...) {
            qDebug() << "Unknown exception during PCB search";
        }
        
        // Re-enable controls after processing
        QTimer::singleShot(50, this, [this]() {
            if (m_netCombo) m_netCombo->setEnabled(true);
            if (m_netSearchButton) m_netSearchButton->setEnabled(true);
        });
    });
}

void PCBViewerWidget::onNetComboActivated(int index) {
    Q_UNUSED(index);
    // Reuse the same logic as clicking Go
    onNetSearchClicked();
}

bool PCBViewerWidget::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_viewerContainer) {
    // No overlay positioning required in layout mode
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
                "QMenu::separator { height:1px; background:#e1e6ed; margin:6px 4px; }" ); } } }; ThemedMenu menu; bool dark = false; menu.apply(dark); // Force light theme
    
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
