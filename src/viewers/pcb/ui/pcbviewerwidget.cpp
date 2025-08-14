#include "viewers/pcb/PCBViewerWidget.h"
#include "viewers/pcb/PCBViewerEmbedder.h"

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
    // Add toolbar and viewer container to main layout
    if (m_toolbar) {
        m_mainLayout->addWidget(m_toolbar);
    }
    m_mainLayout->addWidget(m_viewerContainer, 1);
    
    WritePCBDebugToFile("PCB viewer UI setup completed (single-pane)");
}

void PCBViewerWidget::setupToolbar()
{
    WritePCBDebugToFile("Setting up PCB viewer Qt toolbar with PDF viewer styling");
    
    // Create toolbar with same specifications as PDF viewer
    m_toolbar = new QToolBar(this);
    m_toolbar->setFixedHeight(30);
    m_toolbar->setIconSize(QSize(30, 30));  // Adjust icon size for 30px height
    m_toolbar->setStyleSheet(
        "QToolBar {"
        "    background-color: #ffffff;"
        "    border: none;"
        "    border-bottom: 1px solid #d0d0d0;"
        "    spacing: 5px;"
        "    padding: 4px;"
        "}"
        "QToolButton {"
        "    background-color: transparent;"
        "    border: 1px solid transparent;"
        "    border-radius: 2px;"
        "    padding: 4px;"
        "    min-width: 30px;"
        "    min-height: 20px;"
        "    font-size: 16px;"
        "}"
        "QToolButton:hover {"
        "    background-color: #e6f3ff;"
        "    border-color: #b3d9ff;"
        "}"
        "QToolButton:pressed {"
        "    background-color: #cce7ff;"
        "    border-color: #99ccff;"
        "}"
    );
    
    // Split view removed: no split window action
    // Rotation actions (match PDF viewer icon style)
    m_actionRotateLeft = m_toolbar->addAction(QIcon(":/icons/images/icons/rotate_left.svg"), "");
    m_actionRotateLeft->setToolTip("Rotate Left");
    connect(m_actionRotateLeft, &QAction::triggered, this, &PCBViewerWidget::rotateLeft);

    m_actionRotateRight = m_toolbar->addAction(QIcon(":/icons/images/icons/rotate_right.svg"), "");
    m_actionRotateRight->setToolTip("Rotate Right");
    connect(m_actionRotateRight, &QAction::triggered, this, &PCBViewerWidget::rotateRight);

    WritePCBDebugToFile("Rotation actions added to PCB toolbar");
    WritePCBDebugToFile("Split window action removed (feature deprecated)");
    WritePCBDebugToFile("PCB Qt toolbar setup completed with PDF viewer styling");
}

void PCBViewerWidget::connectSignals()
{
    WritePCBDebugToFile("Connecting PCB viewer signals");
    
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

// Split view removed: onSplitWindowClicked deleted

// Split view removed: embedding PDF viewer no longer supported

// isSplitViewActive() removed
