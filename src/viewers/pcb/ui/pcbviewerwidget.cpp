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
    m_toolbarVisible = visible;
    if (m_toolbar) {
        m_toolbar->setVisible(visible);
    }
}

bool PCBViewerWidget::isToolbarVisible() const
{
    return m_toolbarVisible;
}

void PCBViewerWidget::setStatusMessage(const QString &message)
{
    m_lastStatusMessage = message;
    if (m_statusLabel) {
        m_statusLabel->setText(message);
    }
    emit statusMessage(message);
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
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toolbar->setFloatable(false);
    m_toolbar->setMovable(false);
    
    // Zoom controls
    m_zoomInAction = m_toolbar->addAction(QIcon(), "Zoom In");
    m_zoomOutAction = m_toolbar->addAction(QIcon(), "Zoom Out");
    m_zoomToFitAction = m_toolbar->addAction(QIcon(), "Zoom to Fit");
    m_resetViewAction = m_toolbar->addAction(QIcon(), "Reset View");
    
    m_toolbar->addSeparator();
    
    // Zoom slider
    m_toolbar->addWidget(new QLabel("Zoom:"));
    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(0, 200);
    m_zoomSlider->setValue(100); // 1.0x zoom
    m_zoomSlider->setFixedWidth(150);
    m_toolbar->addWidget(m_zoomSlider);
    
    m_zoomLabel = new QLabel("100%");
    m_zoomLabel->setMinimumWidth(50);
    m_toolbar->addWidget(m_zoomLabel);
    
    m_toolbar->addSeparator();
    
    // Layer controls
    m_toolbar->addWidget(new QLabel("Layer:"));
    m_layerCombo = new QComboBox();
    m_layerCombo->setMinimumWidth(100);
    m_toolbar->addWidget(m_layerCombo);
    
    m_toolbar->addSeparator();
    
    // Status label
    m_statusLabel = new QLabel("Ready");
    m_toolbar->addWidget(m_statusLabel);
    
    // Progress bar (for future use)
    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    m_toolbar->addWidget(m_progressBar);
    
    WritePCBDebugToFile("PCB viewer toolbar setup completed");
}

void PCBViewerWidget::connectSignals()
{
    WritePCBDebugToFile("Connecting PCB viewer signals");
    
    // Connect toolbar actions
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
    
    // Connect zoom slider
    if (m_zoomSlider) {
        connect(m_zoomSlider, &QSlider::valueChanged, this, &PCBViewerWidget::onZoomSliderChanged);
    }
    
    // Connect layer combo
    if (m_layerCombo) {
        connect(m_layerCombo, QOverload<const QString &>::of(&QComboBox::currentTextChanged),
                this, &PCBViewerWidget::onLayerComboChanged);
    }
    
    WritePCBDebugToFile("PCB viewer signals connected");
}

void PCBViewerWidget::updateToolbarState()
{
    WritePCBDebugToFile("Updating toolbar state");
    
    bool pcbLoaded = isPCBLoaded();
    
    // Enable/disable toolbar actions based on PCB state
    if (m_zoomInAction) m_zoomInAction->setEnabled(pcbLoaded);
    if (m_zoomOutAction) m_zoomOutAction->setEnabled(pcbLoaded);
    if (m_zoomToFitAction) m_zoomToFitAction->setEnabled(pcbLoaded);
    if (m_resetViewAction) m_resetViewAction->setEnabled(pcbLoaded);
    if (m_zoomSlider) m_zoomSlider->setEnabled(pcbLoaded);
    
    // Update layer combo with available layers
    if (m_layerCombo && pcbLoaded) {
        m_layerCombo->clear();
        QStringList layers = getLayerNames();
        m_layerCombo->addItems(layers);
        m_layerCombo->setEnabled(!layers.isEmpty());
    } else if (m_layerCombo) {
        m_layerCombo->clear();
        m_layerCombo->setEnabled(false);
    }
}

void PCBViewerWidget::updateZoomSlider()
{
    if (m_zoomSlider && m_zoomLabel) {
        double zoom = getZoomLevel();
        
        // Convert zoom level (0.1-5.0) to slider value (0-200)
        int sliderValue = static_cast<int>((zoom - 0.1) / 4.9 * 200.0);
        
        m_zoomSlider->blockSignals(true);
        m_zoomSlider->setValue(sliderValue);
        m_zoomSlider->blockSignals(false);
        
        m_zoomLabel->setText(QString("%1%").arg(static_cast<int>(zoom * 100)));
    }
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
