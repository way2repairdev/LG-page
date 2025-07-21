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
#include <QGroupBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QSplitter>

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
    , m_toolbarLayout(nullptr)
    , m_viewerContainer(nullptr)
    , m_updateTimer(new QTimer(this))
    , m_zoomInAction(nullptr)
    , m_zoomOutAction(nullptr)
    , m_zoomToFitAction(nullptr)
    , m_resetViewAction(nullptr)
    , m_zoomSlider(nullptr)
    , m_zoomLabel(nullptr)
    , m_layerCombo(nullptr)
    , m_statusLabel(nullptr)
    , m_progressBar(nullptr)
    , m_viewerInitialized(false)
    , m_pcbLoaded(false)
    , m_usingFallback(false)
    , m_toolbarVisible(true)
    , m_currentFilePath("")
    , m_lastZoomLevel(1.0)
    , m_lastStatusMessage("")
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
        
        // Update toolbar state
        updateToolbarState();
        
        // Start update timer if not already running
        if (!m_updateTimer->isActive()) {
            m_updateTimer->start();
        }
        
        WritePCBDebugToFile("PCB file loaded successfully");
        emit pcbLoaded(filePath);
        emit statusMessage("PCB loaded: " + QFileInfo(filePath).fileName());
    } else {
        WritePCBDebugToFile("Failed to load PCB file");
        emit errorOccurred("Failed to load PCB file: " + filePath);
    }
    
    return success;
}

void PCBViewerWidget::closePCB()
{
    WritePCBDebugToFile("Closing PCB");
    
    if (m_pcbEmbedder) {
        m_pcbEmbedder->closePCB();
    }
    
    m_pcbLoaded = false;
    m_currentFilePath.clear();
    
    updateToolbarState();
    
    emit pcbClosed();
    emit statusMessage("PCB closed");
    
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

void PCBViewerWidget::zoomIn()
{
    WritePCBDebugToFile("Zoom in requested");
    if (m_pcbEmbedder) {
        m_pcbEmbedder->zoomIn();
    }
}

void PCBViewerWidget::zoomOut()
{
    WritePCBDebugToFile("Zoom out requested");
    if (m_pcbEmbedder) {
        m_pcbEmbedder->zoomOut();
    }
}

void PCBViewerWidget::zoomToFit()
{
    WritePCBDebugToFile("Zoom to fit requested");
    if (m_pcbEmbedder) {
        m_pcbEmbedder->zoomToFit();
    }
}

void PCBViewerWidget::resetView()
{
    WritePCBDebugToFile("Reset view requested");
    if (m_pcbEmbedder) {
        m_pcbEmbedder->resetView();
    }
}

void PCBViewerWidget::setZoomLevel(double zoom)
{
    if (m_pcbEmbedder) {
        m_pcbEmbedder->setZoomLevel(zoom);
    }
}

double PCBViewerWidget::getZoomLevel() const
{
    if (m_pcbEmbedder) {
        return m_pcbEmbedder->getZoomLevel();
    }
    return 1.0;
}

void PCBViewerWidget::clearSelection()
{
    if (m_pcbEmbedder) {
        m_pcbEmbedder->clearSelection();
    }
}

QString PCBViewerWidget::getSelectedPinInfo() const
{
    if (m_pcbEmbedder) {
        return QString::fromStdString(m_pcbEmbedder->getSelectedPinInfo());
    }
    return QString();
}

QStringList PCBViewerWidget::getComponentList() const
{
    QStringList components;
    if (m_pcbEmbedder) {
        auto componentList = m_pcbEmbedder->getComponentList();
        for (const auto& component : componentList) {
            components.append(QString::fromStdString(component));
        }
    }
    return components;
}

QStringList PCBViewerWidget::getLayerNames() const
{
    QStringList layers;
    if (m_pcbEmbedder) {
        auto layerList = m_pcbEmbedder->getLayerNames();
        for (const auto& layer : layerList) {
            layers.append(QString::fromStdString(layer));
        }
    }
    return layers;
}

void PCBViewerWidget::setToolbarVisible(bool visible)
{
    WritePCBDebugToFile("Setting PCB toolbar visible: " + QString(visible ? "true" : "false"));
    qDebug() << "PCB setToolbarVisible called with visible:" << visible;
    
    m_toolbarVisible = visible;
    if (m_toolbar) {
        // Aggressive toolbar management
        if (visible) {
            WritePCBDebugToFile("Showing PCB toolbar");
            qDebug() << "Showing PCB toolbar";
            
            m_toolbar->setVisible(true);
            m_toolbar->raise(); // Bring toolbar to front when showing
            m_toolbar->setEnabled(true);
            m_toolbar->activateWindow();
            m_toolbar->update(); // Force repaint
            
            // Ensure parent widget is also visible and raised
            this->setVisible(true);
            this->raise();
            
        } else {
            WritePCBDebugToFile("Hiding PCB toolbar");
            qDebug() << "Hiding PCB toolbar";
            
            m_toolbar->setVisible(false);
            m_toolbar->lower(); // Send toolbar to back when hiding
            m_toolbar->setEnabled(false);
            m_toolbar->clearFocus();
        }
        
        // Force layout recalculation with multiple update cycles
        updateGeometry();
        update();
        repaint(); // Force immediate repaint
        
        // Process events to ensure UI update
        QApplication::processEvents();
        
        qDebug() << "PCB toolbar visibility set to:" << m_toolbar->isVisible();
        WritePCBDebugToFile("PCB toolbar visibility: " + QString(m_toolbar->isVisible() ? "true" : "false"));
        
    } else {
        WritePCBDebugToFile("PCB toolbar is null!");
        qDebug() << "PCB toolbar is null!";
    }
}

bool PCBViewerWidget::isToolbarVisible() const
{
    return m_toolbarVisible;
}

void PCBViewerWidget::setStatusMessage(const QString &message)
{
    m_lastStatusMessage = message;
    // Status label was removed - only emit signal now
    emit statusMessage(message);
}

void PCBViewerWidget::setImGuiUIEnabled(bool enabled)
{
    if (m_pcbEmbedder) {
        m_pcbEmbedder->setImGuiUIEnabled(enabled);
        WritePCBDebugToFile("ImGui UI " + QString(enabled ? "enabled" : "disabled"));
    }
}

bool PCBViewerWidget::isImGuiUIEnabled() const
{
    if (m_pcbEmbedder) {
        return m_pcbEmbedder->isImGuiUIEnabled();
    }
    return false;
}

// Public slots

void PCBViewerWidget::onZoomInClicked()
{
    zoomIn();
}

void PCBViewerWidget::onZoomOutClicked()
{
    zoomOut();
}

void PCBViewerWidget::onZoomToFitClicked()
{
    zoomToFit();
}

void PCBViewerWidget::onResetViewClicked()
{
    resetView();
}

void PCBViewerWidget::onZoomSliderChanged(int value)
{
    // Convert slider value (0-200) to zoom level (0.1-5.0)
    double zoom = 0.1 + (value / 200.0) * 4.9;
    setZoomLevel(zoom);
}

void PCBViewerWidget::onLayerComboChanged(const QString &layerName)
{
    WritePCBDebugToFile("Layer changed: " + layerName);
    // Layer control will be implemented later
    setStatusMessage("Layer control not yet implemented: " + layerName);
}

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
    
    // Update zoom slider if zoom level changed
    double currentZoom = getZoomLevel();
    if (std::abs(currentZoom - m_lastZoomLevel) > 0.01) {
        m_lastZoomLevel = currentZoom;
        updateZoomSlider();
        emit zoomChanged(currentZoom);
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
    WritePCBDebugToFile("PCB widget shown");
    
    QWidget::showEvent(event);
    
    if (!m_viewerInitialized) {
        initializePCBViewer();
    }
    
    if (m_pcbEmbedder) {
        m_pcbEmbedder->setVisible(true);
    }
    
    // Start update timer
    if (!m_updateTimer->isActive()) {
        m_updateTimer->start();
    }
}

void PCBViewerWidget::hideEvent(QHideEvent *event)
{
    WritePCBDebugToFile("PCB widget hidden");
    
    QWidget::hideEvent(event);
    
    if (m_pcbEmbedder) {
        m_pcbEmbedder->setVisible(false);
    }
    
    // Stop update timer to save resources
    if (m_updateTimer->isActive()) {
        m_updateTimer->stop();
    }
}

void PCBViewerWidget::paintEvent(QPaintEvent *event)
{
    // For embedded GLFW window, we don't need to paint anything
    // The GLFW window handles its own rendering
    if (m_usingFallback) {
        // In fallback mode, we might want to draw something
        QPainter painter(this);
        painter.fillRect(rect(), QColor(64, 64, 64));
        painter.setPen(QColor(200, 200, 200));
        painter.drawText(rect(), Qt::AlignCenter, "PCB Viewer\n(Fallback Mode)");
    }
    
    QWidget::paintEvent(event);
}

void PCBViewerWidget::focusInEvent(QFocusEvent *event)
{
    WritePCBDebugToFile("PCB widget gained focus");
    QWidget::focusInEvent(event);
}

void PCBViewerWidget::focusOutEvent(QFocusEvent *event)
{
    WritePCBDebugToFile("PCB widget lost focus");
    QWidget::focusOutEvent(event);
}

// Private slots

void PCBViewerWidget::onPCBViewerError(const QString &error)
{
    WritePCBDebugToFile("PCB viewer error: " + error);
    setStatusMessage("Error: " + error);
    emit errorOccurred(error);
}

void PCBViewerWidget::onPCBViewerStatus(const QString &status)
{
    WritePCBDebugToFile("PCB viewer status: " + status);
    setStatusMessage(status);
}

void PCBViewerWidget::onPinSelected(const QString &pinName, const QString &netName)
{
    WritePCBDebugToFile(QString("Pin selected: %1 (Net: %2)").arg(pinName, netName));
    setStatusMessage(QString("Selected pin %1 on net %2").arg(pinName, netName));
    emit pinSelected(pinName, netName);
}

void PCBViewerWidget::onZoomLevelChanged(double zoom)
{
    WritePCBDebugToFile(QString("Zoom level changed to %1").arg(zoom));
    updateZoomSlider();
    emit zoomChanged(zoom);
}

// Private methods

void PCBViewerWidget::initializePCBViewer()
{
    if (m_viewerInitialized) {
        WritePCBDebugToFile("PCB viewer already initialized");
        return;
    }
    
    WritePCBDebugToFile("Initializing PCB viewer");
    
    if (!m_pcbEmbedder) {
        WritePCBDebugToFile("PCB embedder not available");
        return;
    }
    
    // Get the window handle for embedding
    void* windowHandle = nullptr;
    if (m_viewerContainer) {
        windowHandle = reinterpret_cast<void*>(m_viewerContainer->winId());
    } else {
        windowHandle = reinterpret_cast<void*>(winId());
    }
    
    // Get the size for initialization
    QSize containerSize = m_viewerContainer ? m_viewerContainer->size() : size();
    
    // Set up callbacks before initialization
    m_pcbEmbedder->setErrorCallback([this](const std::string& error) {
        QMetaObject::invokeMethod(this, [this, error]() {
            onPCBViewerError(QString::fromStdString(error));
        }, Qt::QueuedConnection);
    });
    
    m_pcbEmbedder->setStatusCallback([this](const std::string& status) {
        QMetaObject::invokeMethod(this, [this, status]() {
            onPCBViewerStatus(QString::fromStdString(status));
        }, Qt::QueuedConnection);
    });
    
    m_pcbEmbedder->setPinSelectedCallback([this](const std::string& pinName, const std::string& netName) {
        QMetaObject::invokeMethod(this, [this, pinName, netName]() {
            onPinSelected(QString::fromStdString(pinName), QString::fromStdString(netName));
        }, Qt::QueuedConnection);
    });
    
    m_pcbEmbedder->setZoomCallback([this](double zoom) {
        QMetaObject::invokeMethod(this, [this, zoom]() {
            onZoomLevelChanged(zoom);
        }, Qt::QueuedConnection);
    });
    
    // Disable ImGui UI - use Qt toolbar instead
    m_pcbEmbedder->setImGuiUIEnabled(false);
    WritePCBDebugToFile("ImGui UI disabled - using Qt toolbar only");
    
    // Initialize the embedder
    bool success = m_pcbEmbedder->initialize(windowHandle, containerSize.width(), containerSize.height());
    
    if (success) {
        m_viewerInitialized = true;
        m_usingFallback = m_pcbEmbedder->isUsingFallback();
        
        WritePCBDebugToFile("PCB viewer initialized successfully");
        setStatusMessage("PCB viewer initialized");
        
        // Update toolbar with available layers
        updateToolbarState();
    } else {
        WritePCBDebugToFile("Failed to initialize PCB viewer");
        setStatusMessage("Failed to initialize PCB viewer");
        m_usingFallback = true;
    }
}

void PCBViewerWidget::setupUI()
{
    WritePCBDebugToFile("Setting up PCB viewer UI");
    
    // Main layout
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    
    // Setup toolbar
    setupToolbar();
    
    // Create viewer container
    m_viewerContainer = new QWidget(this);
    m_viewerContainer->setMinimumSize(400, 300);
    m_viewerContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_viewerContainer->setFocusPolicy(Qt::StrongFocus);
    
    // Add widgets to main layout
    if (m_toolbar) {
        m_mainLayout->addWidget(m_toolbar);
    }
    m_mainLayout->addWidget(m_viewerContainer, 1);
    
    WritePCBDebugToFile("PCB viewer UI setup completed");
}

void PCBViewerWidget::setupToolbar()
{
    WritePCBDebugToFile("Setting up PCB viewer toolbar");
    
    m_toolbar = new QToolBar(this);
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    m_toolbar->setFloatable(false);
    m_toolbar->setMovable(false);
    m_toolbar->setFixedHeight(42); // Fixed height to prevent layout issues
    m_toolbar->setIconSize(QSize(16, 16)); // Smaller icons for compact layout
    
    // Set toolbar-wide stylesheet for consistent spacing
    m_toolbar->setStyleSheet(
        "QToolBar { spacing: 2px; padding: 2px; }"
        "QToolBar::separator { width: 1px; background-color: #C0C0C0; margin: 2px; }"
    );
    
    // Zoom controls with compact buttons - only buttons remain
    m_zoomInAction = m_toolbar->addAction(QIcon(), "In");
    m_zoomInAction->setToolTip("Zoom In");
    m_zoomOutAction = m_toolbar->addAction(QIcon(), "Out");
    m_zoomOutAction->setToolTip("Zoom Out");
    m_zoomToFitAction = m_toolbar->addAction(QIcon(), "Fit");
    m_zoomToFitAction->setToolTip("Zoom to Fit");
    m_resetViewAction = m_toolbar->addAction(QIcon(), "Reset");
    m_resetViewAction->setToolTip("Reset View");
    
    // Set all other UI element pointers to nullptr since they're removed
    m_zoomSlider = nullptr;
    m_zoomLabel = nullptr;
    m_layerCombo = nullptr;
    m_statusLabel = nullptr;
    m_progressBar = nullptr;
    
    WritePCBDebugToFile("PCB viewer toolbar setup completed - simplified to buttons only");
}

void PCBViewerWidget::connectSignals()
{
    WritePCBDebugToFile("Connecting PCB viewer signals");
    
    // Connect toolbar actions - only zoom buttons remain
    if (m_zoomInAction) {
        connect(m_zoomInAction, &QAction::triggered, this, &PCBViewerWidget::onZoomInClicked);
    }
    if (m_zoomOutAction) {
        connect(m_zoomOutAction, &QAction::triggered, this, &PCBViewerWidget::onZoomOutClicked);
    }
    if (m_zoomToFitAction) {
        connect(m_zoomToFitAction, &QAction::triggered, this, &PCBViewerWidget::onZoomToFitClicked);
    }
    if (m_resetViewAction) {
        connect(m_resetViewAction, &QAction::triggered, this, &PCBViewerWidget::onResetViewClicked);
    }
    
    // Note: m_zoomSlider, m_layerCombo, m_statusLabel, and m_progressBar have been removed
    
    WritePCBDebugToFile("PCB viewer signals connected - simplified toolbar");
}

void PCBViewerWidget::updateToolbarState()
{
    WritePCBDebugToFile("Updating toolbar state");
    
    bool pcbLoaded = isPCBLoaded();
    
    // Enable/disable toolbar actions based on PCB state - only zoom buttons remain
    if (m_zoomInAction) m_zoomInAction->setEnabled(pcbLoaded);
    if (m_zoomOutAction) m_zoomOutAction->setEnabled(pcbLoaded);
    if (m_zoomToFitAction) m_zoomToFitAction->setEnabled(pcbLoaded);
    if (m_resetViewAction) m_resetViewAction->setEnabled(pcbLoaded);
    
    // Note: m_zoomSlider, m_layerCombo removed - no longer updating them
}

void PCBViewerWidget::updateZoomSlider()
{
    // Method now empty since zoom slider was removed
    // This method is kept for compatibility but does nothing
}

void PCBViewerWidget::handleViewerError(const QString &error)
{
    WritePCBDebugToFile("Handling viewer error: " + error);
    emit errorOccurred(error);
}

void PCBViewerWidget::handleViewerStatus(const QString &status)
{
    WritePCBDebugToFile("Handling viewer status: " + status);
    setStatusMessage(status);
}

// Placeholder implementations for future features

void PCBViewerWidget::showLayer(const QString &layerName, bool visible)
{
    WritePCBDebugToFile(QString("Show layer %1: %2").arg(layerName).arg(visible));
    if (m_pcbEmbedder) {
        m_pcbEmbedder->showLayer(layerName.toStdString(), visible);
    }
}

void PCBViewerWidget::showAllLayers()
{
    WritePCBDebugToFile("Show all layers");
    if (m_pcbEmbedder) {
        m_pcbEmbedder->showAllLayers();
    }
}

void PCBViewerWidget::hideAllLayers()
{
    WritePCBDebugToFile("Hide all layers");
    if (m_pcbEmbedder) {
        m_pcbEmbedder->hideAllLayers();
    }
}

void PCBViewerWidget::highlightComponent(const QString &reference)
{
    WritePCBDebugToFile("Highlight component: " + reference);
    if (m_pcbEmbedder) {
        m_pcbEmbedder->highlightComponent(reference.toStdString());
    }
}

void PCBViewerWidget::highlightNet(const QString &netName)
{
    WritePCBDebugToFile("Highlight net: " + netName);
    if (m_pcbEmbedder) {
        m_pcbEmbedder->highlightNet(netName.toStdString());
    }
    emit netHighlighted(netName);
}

void PCBViewerWidget::clearHighlights()
{
    WritePCBDebugToFile("Clear highlights");
    if (m_pcbEmbedder) {
        m_pcbEmbedder->clearHighlights();
    }
}
