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
    , m_splitter(nullptr)
    , m_leftPanel(nullptr)
    , m_rightPanel(nullptr)
    , m_leftToolbar(nullptr)
    , m_rightToolbar(nullptr)
    , m_embeddedPDFViewer(nullptr)
    , m_viewerInitialized(false)
    , m_pcbLoaded(false)
    , m_usingFallback(false)
    , m_toolbarVisible(true)
    , m_isSplitView(false)
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
    
    // Create splitter for split view support (same as PDF viewer)
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setCollapsible(0, false);
    m_splitter->setCollapsible(1, false);
    // Track user-resized sizes for restoration
    connect(m_splitter, &QSplitter::splitterMoved, this, [this](int, int){
        if (!m_splitter) return;
        QVariantList list;
        const QList<int> sz = m_splitter->sizes();
        for (int v : sz) list << v;
        m_splitter->setProperty("lastSizes", list);
    });

    m_leftPanel = new QWidget(this);
    QVBoxLayout* leftLayout = new QVBoxLayout(m_leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    
    // Create viewer container for PCB viewer
    m_viewerContainer = new QWidget(m_leftPanel);
    m_viewerContainer->setMinimumSize(400, 300);
    m_viewerContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_viewerContainer->setFocusPolicy(Qt::StrongFocus);
    
    // Add viewer to left panel
    leftLayout->addWidget(m_viewerContainer, 1);
    
    // Create right panel (for embedded PDF viewer)
    m_rightPanel = new QWidget(this);
    m_rightPanel->setMinimumSize(400, 300);
    m_rightPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_rightPanel->hide(); // Initially hidden
    
    // Add panels to splitter
    m_splitter->addWidget(m_leftPanel);
    m_splitter->addWidget(m_rightPanel);
    
    // Set initial equal sizes
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({400, 400});
    
    // Add toolbar and splitter to main layout
    if (m_toolbar) {
        m_mainLayout->addWidget(m_toolbar);
    }
    m_mainLayout->addWidget(m_splitter, 1);
    
    // Initialize split view state
    m_isSplitView = false;
    
    WritePCBDebugToFile("PCB viewer UI setup completed with split view support");
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
    
    // Add split window icon (same as PDF viewer)
    QAction* splitWindowAction = m_toolbar->addAction(QIcon(":/icons/images/icons/slit-tab.png"), "");
    splitWindowAction->setToolTip("Split Window");
    splitWindowAction->setObjectName("splitWindowAction");
    
    // Connect split window action
    connect(splitWindowAction, &QAction::triggered, this, &PCBViewerWidget::onSplitWindowClicked);
    
    WritePCBDebugToFile("Split window icon added to PCB toolbar");
    WritePCBDebugToFile("PCB Qt toolbar setup completed with PDF viewer styling");
}

void PCBViewerWidget::connectSignals()
{
    WritePCBDebugToFile("Connecting PCB viewer signals");
    
    // Split window action is connected in setupToolbar()
    
    WritePCBDebugToFile("PCB viewer signals connected");
}

// Split view functionality (same structure as PDF viewer)

void PCBViewerWidget::onSplitWindowClicked()
{
    WritePCBDebugToFile("PCB split window clicked");
    
    // Prevent re-entrancy during rapid toggles
    if (this->property("splitTransition").toBool()) return;
    this->setProperty("splitTransition", true);
    QTimer::singleShot(0, this, [this]() { this->setProperty("splitTransition", false); });

    // Toggle between single view and split view
    if (m_isSplitView) {
        // Switch to single view
        if (m_splitter) {
            m_splitter->setCollapsible(1, true);
        }
        if (m_rightPanel) {
            m_rightPanel->setMinimumSize(0, 0);
            m_rightPanel->hide();
            // Physically detach right panel to avoid leftover splitter handle/space
            if (m_splitter && m_rightPanel->parent() == m_splitter) {
                m_rightPanel->setParent(nullptr);
            }
        }
        if (m_splitter) {
            const int totalW = m_splitter->width();
            m_splitter->setStretchFactor(0, 1);
            m_splitter->setStretchFactor(1, 0);
            m_splitter->setSizes({ totalW > 0 ? totalW : 1, 0 });
            m_splitter->updateGeometry();
        }
        m_isSplitView = false;
        
        // Release PDF viewer from right panel
        if (m_embeddedPDFViewer) {
            emit releasePDFViewer();
            removePDFViewerFromRightPanel();
            // Clear our reference now that we've requested release
            m_embeddedPDFViewer = nullptr;
        }
        
        // Update tooltip
        QAction* splitAction = m_toolbar->findChild<QAction*>("splitWindowAction");
        if (splitAction) {
            splitAction->setToolTip("Split Window");
        }
        
        // Emit signal to show tree view when exiting split view
        emit splitViewDeactivated();
        
        WritePCBDebugToFile("PCB viewer: Switched to single view mode");
    } else {
        // Switch to split view
        if (m_rightPanel) {
            m_rightPanel->setMinimumSize(400, 300);
            if (m_splitter && m_rightPanel->parent() != m_splitter) {
                m_splitter->insertWidget(1, m_rightPanel);
            }
            m_rightPanel->show();
        }
        if (m_splitter) {
            m_splitter->setCollapsible(1, false);
        }
        m_isSplitView = true;
        
        // Request PDF viewer for the right panel if available
        emit requestCurrentPDFViewer();
        
        // Restore last user ratio if available; otherwise equal split
        if (m_splitter) {
            QVariant last = m_splitter->property("lastSizes");
            QList<int> sizesToSet;
            if (last.isValid() && last.canConvert<QVariantList>()) {
                const QVariantList vl = last.toList();
                if (vl.size() >= 2) {
                    sizesToSet << vl[0].toInt() << vl[1].toInt();
                }
            }
            if (sizesToSet.size() == 2 && sizesToSet[0] > 0 && sizesToSet[1] > 0) {
                m_splitter->setSizes(sizesToSet);
            } else {
                const int totalW = m_splitter->width();
                const int half = totalW > 0 ? qMax(1, totalW / 2) : 400;
                m_splitter->setSizes({ half, half });
            }
            m_splitter->updateGeometry();
        }
        
        // Update tooltip
        QAction* splitAction = m_toolbar->findChild<QAction*>("splitWindowAction");
        if (splitAction) {
            splitAction->setToolTip("Single View");
        }
        
        // Emit signal to hide tree view when entering split view
        emit splitViewActivated();
        
        WritePCBDebugToFile("PCB viewer: Switched to split view mode");
    }
    
    // Force resize if PCB viewer is initialized after layouts settle
    if (m_viewerInitialized && m_pcbEmbedder) {
        QTimer::singleShot(0, this, [this]() {
            QWidget* activeContainer = m_viewerContainer ? m_viewerContainer : this;
            QSize containerSize = activeContainer->size();
            m_pcbEmbedder->resize(containerSize.width(), containerSize.height());
        });
    }
}

void PCBViewerWidget::embedPDFViewerInRightPanel(QWidget* pdfViewer)
{
    WritePCBDebugToFile("Embedding PDF viewer in PCB right panel");
    
    if (!pdfViewer || !m_rightPanel) {
        WritePCBDebugToFile("Cannot embed PDF viewer - invalid parameters");
        return;
    }
    
    // Remove any existing embedded PDF viewer
    if (m_embeddedPDFViewer) {
        removePDFViewerFromRightPanel();
    }
    
    // Store reference to embedded PDF viewer
    m_embeddedPDFViewer = pdfViewer;
    
    // Reparent the PDF viewer to the right panel
    pdfViewer->setParent(m_rightPanel);
    
    // Create layout for right panel if it doesn't exist
    if (!m_rightPanel->layout()) {
        QVBoxLayout* rightLayout = new QVBoxLayout(m_rightPanel);
        rightLayout->setContentsMargins(0, 0, 0, 0);
        rightLayout->setSpacing(0);
    }
    
    // Add PDF viewer to right panel layout
    m_rightPanel->layout()->addWidget(pdfViewer);
    pdfViewer->show();
    
    WritePCBDebugToFile("PDF viewer embedded successfully in PCB right panel");
}

void PCBViewerWidget::removePDFViewerFromRightPanel()
{
    WritePCBDebugToFile("Removing PDF viewer from PCB right panel");
    
    if (!m_embeddedPDFViewer || !m_rightPanel) {
        WritePCBDebugToFile("No PDF viewer to remove");
        return;
    }
    
    // Remove from right panel layout
    if (m_rightPanel->layout()) {
        // Only detach if it's still a child of right panel
        if (m_embeddedPDFViewer->parentWidget() == m_rightPanel) {
            m_rightPanel->layout()->removeWidget(m_embeddedPDFViewer);
            m_embeddedPDFViewer->setParent(nullptr);
        } else {
            m_rightPanel->layout()->removeWidget(m_embeddedPDFViewer);
        }
    }
    
    // Do not clear the reference here; let the owner clear after reattachment
    
    WritePCBDebugToFile("PDF viewer removed from PCB right panel");
}

bool PCBViewerWidget::isSplitViewActive() const
{
    return m_isSplitView;
}
