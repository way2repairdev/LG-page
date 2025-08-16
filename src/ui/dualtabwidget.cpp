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
#include <QMessageBox>

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
    // Disable tab dragging/reordering for PDF tabs  
    m_pdfTabWidget->setMovable(false);
    
    // Enable scrollable tabs when there are too many
    m_pdfTabWidget->tabBar()->setUsesScrollButtons(true);
    m_pdfTabWidget->tabBar()->setElideMode(Qt::ElideRight);
    m_pdfTabWidget->tabBar()->setExpanding(false);    // Style PDF tab widget with blue theme (default active styling)
    // Professional rectangular tabs with proper borders
    QString modernTabStyle = 
        "QTabWidget {"
        "    background: #000000 !important;"
        "    color: #ffffff !important;"
        "    border: none !important;"
        "    font-family: 'Segoe UI', Arial, sans-serif !important;"
        "}"
        "QTabWidget::pane {"
        "    border: 1px solid #080303 !important;"
        "    background: #000000 !important;"
        "    border-radius: 0px !important;"
        "    margin-top: 0px !important;"
        "}"
        "QTabWidget::tab-bar {"
        "    left: 0px !important;"
        "    alignment: left !important;"
        "}"
        "QTabBar {"
        "    qproperty-drawBase: 0 !important;"
        "    background: #000000 !important;"
        "    border: none !important;"
        "    border-bottom: 1px solid #c8c8c8 !important;"
        "    spacing: 0px !important;"
        "}"
        "QTabBar::tab {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2a2a2a, stop:1 #1a1a1a) !important;"
        "    border: 1px solid #555555 !important;"
        "    border-bottom: none !important;"
        "    border-top-left-radius: 6px !important;"
        "    border-top-right-radius: 6px !important;"
        "    padding: 5px 20px 5px 20px !important;"
        "    margin: 0px 2px 0px 0px !important;"
        "    color: #cccccc !important;"
        "    font-size: 13px !important;"
        "    font-weight: 500 !important;"
        "    font-family: 'Segoe UI', Arial, sans-serif !important;"
        "    min-width: 130px !important;"
        "    max-width: 220px !important;"
        "    height: 10px !important;"
        "}"
        "QTabBar::tab:selected {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #000000, stop:0.3 #1a1a1a, stop:0.7 #0a0a0a, stop:1 #000000) !important;"
        "    color: #ffffff !important;"
        "    font-weight: 600 !important;"
        "    border-color: #777777 !important;"
        "    border-bottom: 1px solid #333333 !important;"
        "    margin-bottom: -1px !important;"
        "    z-index: 10 !important;"
        "    box-shadow: inset 0 1px 3px rgba(255,255,255,0.1) !important;"
        "}"
        "QTabBar::tab:hover {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3a3a3a, stop:1 #2a2a2a) !important;"
        "    color: #ffffff !important;"
        "    border-color: #666666 !important;"
        "}"
        "QTabBar QToolButton:hover {"
        "    background: #333333 !important;"
        "    color: #ffffff !important;"
        "}";
    
    // Create PCB tab widget (Row 2)  
    m_pcbTabWidget = new QTabWidget();
    m_pcbTabWidget->setTabsClosable(true);
    // Disable tab dragging/reordering for PCB tabs
    m_pcbTabWidget->setMovable(false);
    
    // Enable scrollable tabs when there are too many
    m_pcbTabWidget->tabBar()->setUsesScrollButtons(true);
    m_pcbTabWidget->tabBar()->setElideMode(Qt::ElideRight);
    m_pcbTabWidget->tabBar()->setExpanding(false);
    
    // Apply the modern styling to both tab widgets (tagged for runtime debugging)
    applyStyleWithTag(m_pdfTabWidget, modernTabStyle, "modernTabStyle-startup");
    
    // Style PCB tab widget with the same modern styling
    applyStyleWithTag(m_pcbTabWidget, modernTabStyle, "modernTabStyle-startup");
    
    // Force style refresh to ensure it applies properly
    forceStyleRefresh();
    
    // DEBUG: Test with obvious colors (comment out after testing)
    // testObviousStyle();
    
    // Optional: Debug styling (uncomment to troubleshoot)
    // debugStyleConflicts();
    
    // Create separate content areas for complete isolation
    m_pdfContentArea = new QStackedWidget();
    m_pdfContentArea->setStyleSheet(
        "QStackedWidget {"
        "    border: 1px solid #e0e0e0;"
        "    border-radius: 0px;"
        "    background-color: black;"
        "}"
    );
    
    m_pcbContentArea = new QStackedWidget();
    m_pcbContentArea->setStyleSheet(
        "QStackedWidget {"
        "    border: 1px solid #e0e0e0;"
        "    border-radius: 0px;"
        "    background-color: black;"
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

void DualTabWidget::applyStyleWithTag(QWidget* w, const QString &style, const QString &tag)
{
    if (!w) return;
    // Store the tag as a dynamic property for runtime inspection
    w->setProperty("appliedStyleTag", tag);
    w->setStyleSheet(style);
    logDebug(QString("Applied style tag '%1' to widget %2").arg(tag).arg(w->objectName()));
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

    const int kMaxTabsPerGroup = 5;
    if (type == PDF_TAB) {
        if (m_pdfWidgets.count() >= kMaxTabsPerGroup) {
            logDebug("addTab() blocked - PDF tab limit reached (5) - emitting tabLimitReached");
            emit tabLimitReached(PDF_TAB, kMaxTabsPerGroup);
            return -1; // Do NOT disturb current active viewer
        }
    } else if (type == PCB_TAB) {
        if (m_pcbWidgets.count() >= kMaxTabsPerGroup) {
            logDebug("addTab() blocked - PCB tab limit reached (5) - emitting tabLimitReached");
            emit tabLimitReached(PCB_TAB, kMaxTabsPerGroup);
            return -1;
        }
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

void DualTabWidget::setMovable(bool /*movable*/)
{
    // Force immovable tabs regardless of requested value
    m_pdfTabWidget->setMovable(false);
    m_pcbTabWidget->setMovable(false);
    logDebug("setMovable() override: tab dragging disabled globally");
}

// Core new methods for mutual exclusion and content isolation
void DualTabWidget::activateTab(int index, TabType type)
{
    logDebug(QString("activateTab() called - index: %1, type: %2").arg(index).arg((int)type));
    // Fast path: if this tab is already active, do nothing to avoid unnecessary
    // deactivate/reactivate cycles that can trigger heavy redraws in child viewers.
    if (m_hasActiveTab && type == m_activeTabType) {
        if ((type == PDF_TAB && index == m_activePdfIndex) || (type == PCB_TAB && index == m_activePcbIndex)) {
            logDebug("activateTab(): requested tab already active - skipping");
            return;
        }
    }
    
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
    
    // Professional rectangular tabs with proper borders
    QString modernTabStyle = 
        "QTabWidget {"
        "    background: #000000ff !important;"
        "    color: #000000ff !important;"
        "    border: none !important;"
        "    font-family: 'Segoe UI', Arial, sans-serif !important;"
        "}"
        "QTabWidget::pane {"
        "    border: 1px solid #080303ff !important;"
        "    background: #000000ff !important;"
        "    border-radius: 0px !important;"
        "    margin-top: 0px !important;"
        "}"
        "QTabWidget::tab-bar {"
        "    left: 0px !important;"
        "    alignment: left !important;"
        "}"
        "QTabBar {"
        "    qproperty-drawBase: 0 !important;"
        "    background: #0c0808ff !important;"
        "    border: none !important;"
        "    border-bottom: 1px solid #c8c8c8 !important;"
        "    spacing: 0px !important;"
        "}"
        "QTabBar::tab {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2a2a2a, stop:1 #1a1a1a) !important;"
        "    border: 1px solid #555555 !important;"
        "    border-bottom: none !important;"
        "    border-top-left-radius: 6px !important;"
        "    border-top-right-radius: 6px !important;"
        "    padding: 5px 20px 5px 20px !important;"
        "    margin: 0px 2px 0px 0px !important;"
        "    color: #cccccc !important;"
        "    font-size: 13px !important;"
        "    font-weight: 500 !important;"
        "    font-family: 'Segoe UI', Arial, sans-serif !important;"
        "    min-width: 130px !important;"
        "    max-width: 220px !important;"
        "    height: 10px !important;"
        "}"
        "QTabBar::tab:hover {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3a3a3a, stop:1 #2a2a2a) !important;"
        "    color: #ffffff !important;"
        "    border-color: #666666 !important;"
        "}"
        "QTabBar::tab:selected {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #000000, stop:0.3 #1a1a1a, stop:0.7 #0a0a0a, stop:1 #000000) !important;"
        "    color: #ffffff !important;"
        "    font-weight: 600 !important;"
        "    border-color: #777777 !important;"
        "    border-bottom: 1px solid #333333 !important;"
        "    margin-bottom: -1px !important;"
        "    z-index: 10 !important;"
        "    box-shadow: inset 0 1px 3px rgba(255,255,255,0.1) !important;"
        "}"
        "QTabBar::close-button {"
        "    image: url(data:image/svg+xml;base64,PHN2ZyB3aWR0aD0iMTIiIGhlaWdodD0iMTIiIHZpZXdCb3g9IjAgMCAxMiAxMiIgZmlsbD0ibm9uZSIgeG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIj4KPHBhdGggZD0iTTkgM0w2IDZMMy4zIDMuMzUiIHN0cm9rZT0iIzk5OTk5OSIgc3Ryb2tlLXdpZHRoPSIxLjUiIGZpbGw9Im5vbmUiLz4KPHBhdGggZD0iTTMgOUw2IDZMOSA5IiBzdHJva2U9IiM5OTk5OTkiIHN0cm9rZS13aWR0aD0iMS41IiBmaWxsPSJub25lIi8+Cjwvc3ZnPgo=) !important;"
        "    subcontrol-origin: padding !important;"
        "    subcontrol-position: center right !important;"
        "    width: 12px !important;"
        "    height: 12px !important;"
        "    margin: 2px 6px 2px 2px !important;"
        "    border-radius: 2px !important;"
        "}"
        "QTabBar::close-button:hover {"
        "    background: #ec0606ff !important;"
        "    border-radius: 2px !important;"
        "}"
        "QTabBar::scroller {"
        "    width: 20px !important;"
        "}"
        "QTabBar QToolButton {"
        "    background: #f6f6f6 !important;"
        "    border: 1px solid #e0e0e0 !important;"
        "    border-radius: 0px !important;"
        "    margin: 0px !important;"
        "    color: #666666 !important;"
        "}"
        "QTabBar QToolButton:hover {"
        "    background: #eeeeee !important;"
        "    color: #333333 !important;"
        "}";
    
    // Apply unified modern styling to both tab widgets
    applyStyleWithTag(m_pdfTabWidget, modernTabStyle, "modernTabStyle-runtime");
    applyStyleWithTag(m_pcbTabWidget, modernTabStyle, "modernTabStyle-runtime");
    
    // Force style refresh
    forceStyleRefresh();
    
    logDebug("updateTabBarVisualState() completed with browser-style flat tabs");
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
    // Record selection for split pairing
    m_selectedPdfIndex = index;
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
    // Record selection for split pairing
    m_selectedPcbIndex = index;
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

int DualTabWidget::getSelectedIndex(TabType type) const
{
    if (type == PDF_TAB) return m_selectedPdfIndex;
    return m_selectedPcbIndex;
}

// Debug methods for stylesheet conflicts
void DualTabWidget::debugStyleConflicts()
{
    qDebug() << "=== DUALTABWIDGET STYLE DEBUGGING ===";
    qDebug() << "PDF TabWidget stylesheet:" << m_pdfTabWidget->styleSheet().length() << "characters";
    qDebug() << "PCB TabWidget stylesheet:" << m_pcbTabWidget->styleSheet().length() << "characters";
    qDebug() << "This widget stylesheet:" << this->styleSheet().length() << "characters";
    qDebug() << "=== END STYLE DEBUG ===";
}

void DualTabWidget::testObviousStyle()
{
    // Apply VERY obvious style to test if styling works at all
    QString testStyle = R"(
        QTabWidget::pane {
          
        }
        QTabBar::tab {
         
        }
        QTabBar::tab:selected {
           
        }
        QTabBar::tab:hover {
          
        }
    )";
    
    applyStyleWithTag(m_pdfTabWidget, testStyle, "testStyle-debug");
    applyStyleWithTag(m_pcbTabWidget, testStyle, "testStyle-debug");
    
    qDebug() << "Applied OBVIOUS test style - should see red/yellow/green colors!";
    qDebug() << "If you don't see these colors, there's a style conflict!";
}

void DualTabWidget::clearAllStyles()
{
    applyStyleWithTag(m_pdfTabWidget, QString(), "cleared");
    applyStyleWithTag(m_pcbTabWidget, QString(), "cleared");
    this->setStyleSheet("");
    
    qDebug() << "Cleared all DualTabWidget styles - should show default Qt style";
}

void DualTabWidget::forceStyleRefresh()
{
    // Simple style refresh by re-applying the stylesheet
    QString pdfStyle = m_pdfTabWidget->styleSheet();
    QString pcbStyle = m_pcbTabWidget->styleSheet();
    
    applyStyleWithTag(m_pdfTabWidget, QString(), "forced-clear");
    applyStyleWithTag(m_pcbTabWidget, QString(), "forced-clear");
    
    applyStyleWithTag(m_pdfTabWidget, pdfStyle, "forced-reapply");
    applyStyleWithTag(m_pcbTabWidget, pcbStyle, "forced-reapply");
    
    qDebug() << "Forced style refresh by re-applying stylesheets";
}
