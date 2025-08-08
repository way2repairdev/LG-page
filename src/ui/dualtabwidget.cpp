#include "ui/dualtabwidget.h"
#include <QIcon>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>
#include <QMouseEvent>
#include <QTimer>
#include <QTabBar>

// Debug logging function
void logDebug(const QString &message) {
    static QString debugFilePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/dualtab_debug.txt";
    QFile debugFile(debugFilePath);
    if (debugFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
        QTextStream stream(&debugFile);
        stream << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz") 
               << " - " << message << "\n";
        debugFile.close();
    }
}

DualTabWidget::DualTabWidget(QWidget *parent) 
    : QWidget(parent)
    , m_activeTabType(PDF_TAB)
    , m_activePdfIndex(-1)
    , m_activePcbIndex(-1)
    , m_hasActiveTab(false)
{
    logDebug("DualTabWidget constructor called");
    setupUI();
    logDebug("DualTabWidget constructor completed");
}

void DualTabWidget::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    
    // Create PDF tab widget (Row 1)
    m_pdfTabWidget = new QTabWidget();
    m_pdfTabWidget->setTabsClosable(true);
    m_pdfTabWidget->setMovable(true);
    
    // Style PDF tab widget with blue theme (default active styling)
    m_pdfTabWidget->setStyleSheet(
        "QTabWidget {"
        "    border: 2px solid #1a73e8;"
        "    border-radius: 6px;"
        "    background-color: white;"
        "}"
        "QTabWidget::pane {"
        "    border: 2px solid #1a73e8;"
        "    border-radius: 6px;"
        "    background-color: white;"
        "}"
        "QTabBar::tab {"
        "    background-color: #e8f0fe;"
        "    border: 1px solid #1a73e8;"
        "    border-bottom: none;"
        "    border-radius: 4px 4px 0 0;"
        "    padding: 8px 12px;"
        "    margin-right: 2px;"
        "    color: #1a73e8;"
        "    font-weight: bold;"
        "}"
        "QTabBar::tab:selected {"
        "    background-color: #1a73e8;"
        "    color: white;"
        "}"
        "QTabBar::tab:hover {"
        "    background-color: #4285f4;"
        "    color: white;"
        "}"
    );
    
    // Create PCB tab widget (Row 2)  
    m_pcbTabWidget = new QTabWidget();
    m_pcbTabWidget->setTabsClosable(true);
    m_pcbTabWidget->setMovable(true);
    
    // Style PCB tab widget with green theme (default inactive styling)
    m_pcbTabWidget->setStyleSheet(
        "QTabWidget {"
        "    border: 1px solid #cccccc;"
        "    border-radius: 6px;"
        "    background-color: #f8f8f8;"
        "}"
        "QTabWidget::pane {"
        "    border: 1px solid #cccccc;"
        "    border-radius: 6px;"
        "    background-color: #f8f8f8;"
        "}"
        "QTabBar::tab {"
        "    background-color: #f0f0f0;"
        "    border: 1px solid #cccccc;"
        "    border-bottom: none;"
        "    border-radius: 4px 4px 0 0;"
        "    padding: 8px 12px;"
        "    margin-right: 2px;"
        "    color: #888888;"
        "    font-weight: normal;"
        "}"
        "QTabBar::tab:selected {"
        "    background-color: #e0e0e0;"
        "    color: #666666;"
        "}"
        "QTabBar::tab:hover {"
        "    background-color: #0d7c2a;"
        "    color: white;"
        "    border-color: #0d7c2a;"
        "}"
    );
    
    // Create separate content areas for complete isolation
    m_pdfContentArea = new QStackedWidget();
    m_pdfContentArea->setStyleSheet(
        "QStackedWidget {"
        "    border: 1px solid #1a73e8;"
        "    border-radius: 6px;"
        "    background-color: white;"
        "}"
    );
    
    m_pcbContentArea = new QStackedWidget();
    m_pcbContentArea->setStyleSheet(
        "QStackedWidget {"
        "    border: 1px solid #0d7c2a;"
        "    border-radius: 6px;"
        "    background-color: white;"
        "}"
    );
    
    // Add to main layout
    m_mainLayout->addWidget(m_pdfTabWidget);
    m_mainLayout->addWidget(m_pcbTabWidget);
    m_mainLayout->addWidget(m_pdfContentArea, 1); // Give content areas most space
    m_mainLayout->addWidget(m_pcbContentArea, 1);
    
    // Connect signals
    connect(m_pdfTabWidget, &QTabWidget::tabCloseRequested, this, &DualTabWidget::onPdfTabCloseRequested);
    connect(m_pcbTabWidget, &QTabWidget::tabCloseRequested, this, &DualTabWidget::onPcbTabCloseRequested);
    connect(m_pdfTabWidget, &QTabWidget::currentChanged, this, &DualTabWidget::onPdfCurrentChanged);
    connect(m_pcbTabWidget, &QTabWidget::currentChanged, this, &DualTabWidget::onPcbCurrentChanged);
    
    // Install event filters on tab bars to ensure clicks are always detected
    m_pdfTabWidget->tabBar()->installEventFilter(this);
    m_pcbTabWidget->tabBar()->installEventFilter(this);
    
    logDebug("Signal connections established for both tab widgets");
    
    // Initially hide all content and set up proper state
    hideAllContent();
    updateVisibility();
    updateTabBarStates();
}

int DualTabWidget::addTab(QWidget *widget, const QString &label, TabType type)
{
    return addTab(widget, QIcon(), label, type);
}

int DualTabWidget::addTab(QWidget *widget, const QIcon &icon, const QString &label, TabType type)
{
    logDebug(QString("addTab() called - label: %1, type: %2").arg(label).arg((int)type));
    
    if (!widget) {
        logDebug("addTab() failed - widget is null");
        return -1;
    }
    
    int tabIndex = -1;
    
    if (type == PDF_TAB) {
        // Add to PDF content area only
        m_pdfContentArea->addWidget(widget);
        m_pdfWidgets.append(widget);
        
        // Add dummy widget to PDF tab bar (actual content is in PDF content area)
        QWidget *dummyWidget = new QWidget();
        tabIndex = m_pdfTabWidget->addTab(dummyWidget, icon, label);
        
        logDebug(QString("Added PDF tab - index: %1, total PDF tabs: %2").arg(tabIndex).arg(m_pdfWidgets.count()));
        
        // If this is the first PDF tab, activate it
        if (m_pdfWidgets.count() == 1) {
            logDebug("First PDF tab - activating it");
            activateTab(0, PDF_TAB);
        }
    } else {
        // Add to PCB content area only
        m_pcbContentArea->addWidget(widget);
        m_pcbWidgets.append(widget);
        
        // Add dummy widget to PCB tab bar
        QWidget *dummyWidget = new QWidget();
        tabIndex = m_pcbTabWidget->addTab(dummyWidget, icon, label);
        
        logDebug(QString("Added PCB tab - index: %1, total PCB tabs: %2").arg(tabIndex).arg(m_pcbWidgets.count()));
        
        // If this is the first PCB tab, activate it
        if (m_pcbWidgets.count() == 1) {
            logDebug("First PCB tab - activating it");
            activateTab(0, PCB_TAB);
        }
    }
    
    updateVisibility();
    logDebug(QString("addTab() completed - returned index: %1").arg(tabIndex));
    return tabIndex;
}

void DualTabWidget::removeTab(int index, TabType type)
{
    bool wasActive = false;
    
    if (type == PDF_TAB) {
        if (index >= 0 && index < m_pdfTabWidget->count()) {
            QWidget *widget = m_pdfWidgets.at(index);
            wasActive = (m_activeTabType == PDF_TAB && m_activePdfIndex == index);
            
            // Remove from tab widget
            m_pdfTabWidget->removeTab(index);
            
            // Remove from our lists
            m_pdfWidgets.removeAt(index);
            
            // Remove from PDF content area
            m_pdfContentArea->removeWidget(widget);
            
            // Update active index if needed
            if (m_activePdfIndex > index) {
                m_activePdfIndex--;
            } else if (m_activePdfIndex == index) {
                m_activePdfIndex = -1;
                if (wasActive) {
                    m_hasActiveTab = false;
                    // Try to activate the next available tab
                    if (m_pdfWidgets.count() > 0) {
                        activateTab(0, PDF_TAB);
                    } else if (m_pcbWidgets.count() > 0) {
                        activateTab(0, PCB_TAB);
                    }
                }
            }
        }
    } else {
        if (index >= 0 && index < m_pcbTabWidget->count()) {
            QWidget *widget = m_pcbWidgets.at(index);
            wasActive = (m_activeTabType == PCB_TAB && m_activePcbIndex == index);
            
            // Remove from tab widget
            m_pcbTabWidget->removeTab(index);
            
            // Remove from our lists
            m_pcbWidgets.removeAt(index);
            
            // Remove from PCB content area
            m_pcbContentArea->removeWidget(widget);
            
            // Update active index if needed
            if (m_activePcbIndex > index) {
                m_activePcbIndex--;
            } else if (m_activePcbIndex == index) {
                m_activePcbIndex = -1;
                if (wasActive) {
                    m_hasActiveTab = false;
                    // Try to activate the next available tab
                    if (m_pcbWidgets.count() > 0) {
                        activateTab(0, PCB_TAB);
                    } else if (m_pdfWidgets.count() > 0) {
                        activateTab(0, PDF_TAB);
                    }
                }
            }
        }
    }
    
    updateVisibility();
}

QWidget* DualTabWidget::widget(int index, TabType type) const
{
    if (type == PDF_TAB) {
        if (index >= 0 && index < m_pdfWidgets.count()) {
            return m_pdfWidgets.at(index);
        }
    } else {
        if (index >= 0 && index < m_pcbWidgets.count()) {
            return m_pcbWidgets.at(index);
        }
    }
    return nullptr;
}

void DualTabWidget::setCurrentIndex(int index, TabType type)
{
    // This now activates the tab with mutual exclusion
    activateTab(index, type);
}

int DualTabWidget::currentIndex(TabType type) const
{
    if (type == PDF_TAB) {
        return m_activeTabType == PDF_TAB ? m_activePdfIndex : -1;
    } else {
        return m_activeTabType == PCB_TAB ? m_activePcbIndex : -1;
    }
}

int DualTabWidget::count(TabType type) const
{
    if (type == PDF_TAB) {
        return m_pdfTabWidget->count();
    } else {
        return m_pcbTabWidget->count();
    }
}

void DualTabWidget::setTabText(int index, const QString &text, TabType type)
{
    if (type == PDF_TAB) {
        m_pdfTabWidget->setTabText(index, text);
    } else {
        m_pcbTabWidget->setTabText(index, text);
    }
}

QString DualTabWidget::tabText(int index, TabType type) const
{
    if (type == PDF_TAB) {
        return m_pdfTabWidget->tabText(index);
    } else {
        return m_pcbTabWidget->tabText(index);
    }
}

void DualTabWidget::setTabToolTip(int index, const QString &tip, TabType type)
{
    if (type == PDF_TAB) {
        m_pdfTabWidget->setTabToolTip(index, tip);
    } else {
        m_pcbTabWidget->setTabToolTip(index, tip);
    }
}

void DualTabWidget::setTabIcon(int index, const QIcon &icon, TabType type)
{
    if (type == PDF_TAB) {
        m_pdfTabWidget->setTabIcon(index, icon);
    } else {
        m_pcbTabWidget->setTabIcon(index, icon);
    }
}

void DualTabWidget::setTabsClosable(bool closable)
{
    m_pdfTabWidget->setTabsClosable(closable);
    m_pcbTabWidget->setTabsClosable(closable);
}

void DualTabWidget::setMovable(bool movable)
{
    m_pdfTabWidget->setMovable(movable);
    m_pcbTabWidget->setMovable(movable);
}

// Core new methods for mutual exclusion and content isolation
void DualTabWidget::activateTab(int index, TabType type)
{
    logDebug(QString("activateTab() called - index: %1, type: %2").arg(index).arg((int)type));
    
    // Step 1: Deactivate all tabs first
    logDebug("Step 1: Deactivating all tabs");
    deactivateAllTabs();
    
    // Step 2: Validate the requested tab
    if (type == PDF_TAB) {
        if (index < 0 || index >= m_pdfWidgets.count()) {
            logDebug(QString("Invalid PDF tab index: %1, widget count: %2").arg(index).arg(m_pdfWidgets.count()));
            return;
        }
    } else {
        if (index < 0 || index >= m_pcbWidgets.count()) {
            logDebug(QString("Invalid PCB tab index: %1, widget count: %2").arg(index).arg(m_pcbWidgets.count()));
            return;
        }
    }
    
    // Step 3: Set active tab type and activate the specific tab
    logDebug("Step 3: Setting active tab type");
    setActiveTabType(type);
    
    if (type == PDF_TAB) {
        logDebug(QString("Setting PDF tab as active - index: %1").arg(index));
        m_activePdfIndex = index;
        m_pdfTabWidget->setCurrentIndex(index);
        m_pdfContentArea->setCurrentWidget(m_pdfWidgets.at(index));
    } else {
        logDebug(QString("Setting PCB tab as active - index: %1").arg(index));
        m_activePcbIndex = index;
        m_pcbTabWidget->setCurrentIndex(index);
        m_pcbContentArea->setCurrentWidget(m_pcbWidgets.at(index));
    }
    
    m_hasActiveTab = true;
    logDebug("Step 4: Set hasActiveTab to true");
    
    // Step 4: Show only the active content
    logDebug("Step 5: Calling showActiveContent()");
    showActiveContent();
    
    // Step 5: Update UI states
    logDebug("Step 6: Updating tab bar states");
    updateTabBarStates();
    
    // Emit signal about active tab change
    logDebug("Step 7: Emitting signals");
    emit activeTabChanged(type);
    emit currentChanged(index, type);
    logDebug("activateTab() completed successfully");
}

void DualTabWidget::deactivateAllTabs()
{
    // Clear active state
    m_hasActiveTab = false;
    m_activePdfIndex = -1;
    m_activePcbIndex = -1;
    
    // Hide all content
    hideAllContent();
    
    // Keep tab bars enabled so users can click to switch
    // Visual styling will indicate the inactive state
    m_pdfTabWidget->setEnabled(true);
    m_pcbTabWidget->setEnabled(true);
}

bool DualTabWidget::hasActiveTab() const
{
    return m_hasActiveTab;
}

QWidget* DualTabWidget::getActiveWidget() const
{
    if (!m_hasActiveTab) {
        return nullptr;
    }
    
    if (m_activeTabType == PDF_TAB && m_activePdfIndex >= 0 && m_activePdfIndex < m_pdfWidgets.count()) {
        return m_pdfWidgets.at(m_activePdfIndex);
    } else if (m_activeTabType == PCB_TAB && m_activePcbIndex >= 0 && m_activePcbIndex < m_pcbWidgets.count()) {
        return m_pcbWidgets.at(m_activePcbIndex);
    }
    
    return nullptr;
}

void DualTabWidget::ensureContentWidgetPresent(QWidget* widget, TabType type)
{
    if (!widget) return;
    if (type == PDF_TAB) {
        // If widget is not in our list, nothing to do
        int idx = m_pdfWidgets.indexOf(widget);
        if (idx == -1) return;
        // If it's not currently parented under the PDF content area, re-add
    if (widget->parentWidget() != m_pdfContentArea) {
            // Remove from any layout it's currently in
            if (widget->parentWidget() && widget->parentWidget()->layout()) {
                widget->parentWidget()->layout()->removeWidget(widget);
            }
            widget->setParent(m_pdfContentArea);
            if (m_pdfContentArea->indexOf(widget) == -1) {
                m_pdfContentArea->addWidget(widget);
            }
    } else if (m_pdfContentArea->indexOf(widget) == -1) {
            m_pdfContentArea->addWidget(widget);
        }
    // Ensure it fills available space and is current
    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_pdfContentArea->setCurrentWidget(widget);
    widget->show();
    } else {
        int idx = m_pcbWidgets.indexOf(widget);
        if (idx == -1) return;
    if (widget->parentWidget() != m_pcbContentArea) {
            if (widget->parentWidget() && widget->parentWidget()->layout()) {
                widget->parentWidget()->layout()->removeWidget(widget);
            }
            widget->setParent(m_pcbContentArea);
            if (m_pcbContentArea->indexOf(widget) == -1) {
                m_pcbContentArea->addWidget(widget);
            }
    } else if (m_pcbContentArea->indexOf(widget) == -1) {
            m_pcbContentArea->addWidget(widget);
        }
    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_pcbContentArea->setCurrentWidget(widget);
    widget->show();
    }
}

void DualTabWidget::setActiveTabType(TabType type)
{
    logDebug(QString("setActiveTabType() called - current type: %1, new type: %2").arg((int)m_activeTabType).arg((int)type));
    
    if (m_activeTabType != type) {
        logDebug("Tab type changed - updating active tab type");
        m_activeTabType = type;
        
        // Hide all content first
        logDebug("Hiding all content before type change");
        hideAllContent();
        
        // Keep both tab bars enabled so users can click to switch
        // Visual styling will indicate which is active/inactive
        m_pdfTabWidget->setEnabled(true);
        m_pcbTabWidget->setEnabled(true);
        
        // Update visual styling to show active/inactive state
        logDebug("Updating visual state for new tab type");
        updateTabBarVisualState();
    } else {
        logDebug("Tab type unchanged - no action needed");
    }
}

void DualTabWidget::hideAllContent()
{
    m_pdfContentArea->hide();
    m_pcbContentArea->hide();
}

void DualTabWidget::showActiveContent()
{
    logDebug(QString("showActiveContent() called - hasActiveTab: %1, activeTabType: %2").arg(m_hasActiveTab).arg((int)m_activeTabType));
    
    if (!m_hasActiveTab) {
        logDebug("No active tab - hiding all content");
        hideAllContent();
        return;
    }
    
    if (m_activeTabType == PDF_TAB) {
        logDebug("Showing PDF content area");
        m_pdfContentArea->show();
        m_pcbContentArea->hide();
    } else {
        logDebug("Showing PCB content area");
        m_pcbContentArea->show();
        m_pdfContentArea->hide();
    }
}

void DualTabWidget::updateTabBarStates()
{
    // Keep both tab bars enabled so users can click to switch between tab types
    m_pdfTabWidget->setEnabled(true);
    m_pcbTabWidget->setEnabled(true);
    
    // Update visual styling to show which tab type is active
    updateTabBarVisualState();
}

void DualTabWidget::updateTabBarVisualState()
{
    logDebug("updateTabBarVisualState() called");
    
    // Update PDF tab bar styling based on active state
    if (m_activeTabType == PDF_TAB) {
        logDebug("Setting PDF tab as visually active, PCB as inactive");
        // PDF is active - use normal active styling
        m_pdfTabWidget->setStyleSheet(
            "QTabWidget {"
            "    border: 2px solid #1a73e8;"
            "    border-radius: 6px;"
            "    background-color: white;"
            "}"
            "QTabWidget::pane {"
            "    border: 2px solid #1a73e8;"
            "    border-radius: 6px;"
            "    background-color: white;"
            "}"
            "QTabBar::tab {"
            "    background-color: #e8f0fe;"
            "    border: 1px solid #1a73e8;"
            "    border-bottom: none;"
            "    border-radius: 4px 4px 0 0;"
            "    padding: 8px 12px;"
            "    margin-right: 2px;"
            "    color: #1a73e8;"
            "    font-weight: bold;"
            "}"
            "QTabBar::tab:selected {"
            "    background-color: #1a73e8;"
            "    color: white;"
            "}"
            "QTabBar::tab:hover {"
            "    background-color: #4285f4;"
            "    color: white;"
            "}"
        );
        
        // PCB is inactive - use dimmed styling but keep clickable
        m_pcbTabWidget->setStyleSheet(
            "QTabWidget {"
            "    border: 1px solid #cccccc;"
            "    border-radius: 6px;"
            "    background-color: #f8f8f8;"
            "}"
            "QTabWidget::pane {"
            "    border: 1px solid #cccccc;"
            "    border-radius: 6px;"
            "    background-color: #f8f8f8;"
            "}"
            "QTabBar::tab {"
            "    background-color: #f0f0f0;"
            "    border: 1px solid #cccccc;"
            "    border-bottom: none;"
            "    border-radius: 4px 4px 0 0;"
            "    padding: 8px 12px;"
            "    margin-right: 2px;"
            "    color: #888888;"
            "    font-weight: normal;"
            "    cursor: pointer;"
            "}"
            "QTabBar::tab:selected {"
            "    background-color: #e0e0e0;"
            "    color: #666666;"
            "    cursor: pointer;"
            "}"
            "QTabBar::tab:hover {"
            "    background-color: #0d7c2a;"
            "    color: white;"
            "    border-color: #0d7c2a;"
            "    cursor: pointer !important;"
            "}"
        );
        
        // Explicitly ensure PCB tab widget can receive events
        m_pcbTabWidget->setEnabled(true);
        m_pcbTabWidget->setAttribute(Qt::WA_TranslucentBackground, false);
        m_pcbTabWidget->setAttribute(Qt::WA_NoMousePropagation, false);
    } else {
        logDebug("Setting PCB tab as visually active, PDF as inactive");
        // PCB is active - use normal active styling
        m_pcbTabWidget->setStyleSheet(
            "QTabWidget {"
            "    border: 2px solid #0d7c2a;"
            "    border-radius: 6px;"
            "    background-color: white;"
            "}"
            "QTabWidget::pane {"
            "    border: 2px solid #0d7c2a;"
            "    border-radius: 6px;"
            "    background-color: white;"
            "}"
            "QTabBar::tab {"
            "    background-color: #e8f5e8;"
            "    border: 1px solid #0d7c2a;"
            "    border-bottom: none;"
            "    border-radius: 4px 4px 0 0;"
            "    padding: 8px 12px;"
            "    margin-right: 2px;"
            "    color: #0d7c2a;"
            "    font-weight: bold;"
            "}"
            "QTabBar::tab:selected {"
            "    background-color: #0d7c2a;"
            "    color: white;"
            "}"
            "QTabBar::tab:hover {"
            "    background-color: #34a853;"
            "    color: white;"
            "}"
        );
        
        // PDF is inactive - use dimmed styling but keep clickable  
        // IMPORTANT: Make sure hover and click events still work
        m_pdfTabWidget->setStyleSheet(
            "QTabWidget {"
            "    border: 1px solid #cccccc;"
            "    border-radius: 6px;"
            "    background-color: #f8f8f8;"
            "}"
            "QTabWidget::pane {"
            "    border: 1px solid #cccccc;"
            "    border-radius: 6px;"
            "    background-color: #f8f8f8;"
            "}"
            "QTabBar::tab {"
            "    background-color: #f0f0f0;"
            "    border: 1px solid #cccccc;"
            "    border-bottom: none;"
            "    border-radius: 4px 4px 0 0;"
            "    padding: 8px 12px;"
            "    margin-right: 2px;"
            "    color: #888888;"
            "    font-weight: normal;"
            "    cursor: pointer;"
            "}"
            "QTabBar::tab:selected {"
            "    background-color: #e0e0e0;"
            "    color: #666666;"
            "    cursor: pointer;"
            "}"
            "QTabBar::tab:hover {"
            "    background-color: #1a73e8 !important;"
            "    color: white !important;"
            "    border-color: #1a73e8 !important;"
            "    cursor: pointer !important;"
            "}"
        );
        
        // Explicitly ensure PDF tab widget can receive events
        m_pdfTabWidget->setEnabled(true);
        m_pdfTabWidget->setAttribute(Qt::WA_TranslucentBackground, false);
        m_pdfTabWidget->setAttribute(Qt::WA_NoMousePropagation, false);
    }
    
    logDebug("updateTabBarVisualState() completed");
}

// Slot methods for tab events
void DualTabWidget::onPdfTabCloseRequested(int index)
{
    emit tabCloseRequested(index, PDF_TAB);
}

void DualTabWidget::onPcbTabCloseRequested(int index)
{
    emit tabCloseRequested(index, PCB_TAB);
}

void DualTabWidget::onPdfCurrentChanged(int index)
{
    logDebug(QString("PDF tab clicked - index: %1, PDF widgets count: %2").arg(index).arg(m_pdfWidgets.count()));
    if (index >= 0 && index < m_pdfWidgets.count()) {
        // User clicked on PDF tab - activate it with mutual exclusion
        logDebug(QString("Activating PDF tab %1").arg(index));
        activateTab(index, PDF_TAB);
    } else {
        logDebug(QString("Invalid PDF tab index: %1").arg(index));
    }
}

void DualTabWidget::onPcbCurrentChanged(int index)
{
    logDebug(QString("PCB tab clicked - index: %1, PCB widgets count: %2").arg(index).arg(m_pcbWidgets.count()));
    if (index >= 0 && index < m_pcbWidgets.count()) {
        // User clicked on PCB tab - activate it with mutual exclusion
        logDebug(QString("Activating PCB tab %1").arg(index));
        activateTab(index, PCB_TAB);
    } else {
        logDebug(QString("Invalid PCB tab index: %1").arg(index));
    }
}

// Helper methods
void DualTabWidget::updateVisibility()
{
    // Show PDF tab widget only if it has tabs
    bool showPdfTabs = (m_pdfTabWidget->count() > 0);
    m_pdfTabWidget->setVisible(showPdfTabs);
    
    // Show PCB tab widget only if it has tabs
    bool showPcbTabs = (m_pcbTabWidget->count() > 0);
    m_pcbTabWidget->setVisible(showPcbTabs);
    
    // If no tabs are visible, hide all content
    if (!showPdfTabs && !showPcbTabs) {
        hideAllContent();
        m_hasActiveTab = false;
    }
}

bool DualTabWidget::eventFilter(QObject *obj, QEvent *event)
{
    // Handle mouse press events on tab bars to ensure clicks are always detected
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            
            // Check if it's PDF tab bar being clicked
            if (obj == m_pdfTabWidget->tabBar()) {
                int clickedIndex = m_pdfTabWidget->tabBar()->tabAt(mouseEvent->pos());
                if (clickedIndex >= 0) {
                    logDebug(QString("Event filter caught PDF tab click - index: %1").arg(clickedIndex));
                    // Trigger the PDF tab activation directly
                    QTimer::singleShot(0, [this, clickedIndex]() {
                        onPdfCurrentChanged(clickedIndex);
                    });
                }
                return false; // Let the event continue to be processed normally
            }
            
            // Check if it's PCB tab bar being clicked
            if (obj == m_pcbTabWidget->tabBar()) {
                int clickedIndex = m_pcbTabWidget->tabBar()->tabAt(mouseEvent->pos());
                if (clickedIndex >= 0) {
                    logDebug(QString("Event filter caught PCB tab click - index: %1").arg(clickedIndex));
                    // Trigger the PCB tab activation directly
                    QTimer::singleShot(0, [this, clickedIndex]() {
                        onPcbCurrentChanged(clickedIndex);
                    });
                }
                return false; // Let the event continue to be processed normally
            }
        }
    }
    
    // For all other events, use default processing
    return QWidget::eventFilter(obj, event);
}

bool DualTabWidget::isRowVisible(TabType type) const
{
    if (type == PDF_TAB) {
        return m_pdfTabWidget->isVisible();
    } else {
        return m_pcbTabWidget->isVisible();
    }
}

DualTabWidget::TabType DualTabWidget::getCurrentTabType() const
{
    return m_activeTabType;
}
