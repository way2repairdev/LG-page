#include "ui/dualtabwidget.h"
#include <QIcon>

DualTabWidget::DualTabWidget(QWidget *parent) 
    : QWidget(parent)
{
    setupUI();
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
    
    // Style PDF tab widget with blue theme
    m_pdfTabWidget->setStyleSheet(
        "QTabWidget {"
        "    border: 1px solid #1a73e8;"
        "    border-radius: 6px;"
        "    background-color: white;"
        "}"
        "QTabWidget::pane {"
        "    border: 1px solid #1a73e8;"
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
    
    // Style PCB tab widget with green theme
    m_pcbTabWidget->setStyleSheet(
        "QTabWidget {"
        "    border: 1px solid #0d7c2a;"
        "    border-radius: 6px;"
        "    background-color: white;"
        "}"
        "QTabWidget::pane {"
        "    border: 1px solid #0d7c2a;"
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
    
    // Create shared content area
    m_contentArea = new QStackedWidget();
    m_contentArea->setStyleSheet(
        "QStackedWidget {"
        "    border: 1px solid #d4e1f5;"
        "    border-radius: 6px;"
        "    background-color: white;"
        "}"
    );
    
    // Add to main layout
    m_mainLayout->addWidget(m_pdfTabWidget);
    m_mainLayout->addWidget(m_pcbTabWidget);
    m_mainLayout->addWidget(m_contentArea, 1); // Give content area most space
    
    // Connect signals
    connect(m_pdfTabWidget, &QTabWidget::tabCloseRequested, this, &DualTabWidget::onPdfTabCloseRequested);
    connect(m_pcbTabWidget, &QTabWidget::tabCloseRequested, this, &DualTabWidget::onPcbTabCloseRequested);
    connect(m_pdfTabWidget, &QTabWidget::currentChanged, this, &DualTabWidget::onPdfCurrentChanged);
    connect(m_pcbTabWidget, &QTabWidget::currentChanged, this, &DualTabWidget::onPcbCurrentChanged);
    
    // Initially hide both tab widgets (will show when tabs are added)
    updateVisibility();
}

int DualTabWidget::addTab(QWidget *widget, const QString &label, TabType type)
{
    return addTab(widget, QIcon(), label, type);
}

int DualTabWidget::addTab(QWidget *widget, const QIcon &icon, const QString &label, TabType type)
{
    if (!widget) return -1;
    
    // Add to content area first
    m_contentArea->addWidget(widget);
    m_allWidgets.append(widget);
    
    int tabIndex = -1;
    if (type == PDF_TAB) {
        // Add dummy widget to PDF tab bar (actual content is in stacked widget)
        QWidget *dummyWidget = new QWidget();
        tabIndex = m_pdfTabWidget->addTab(dummyWidget, icon, label);
        m_pdfWidgets.append(widget);
    } else {
        // Add dummy widget to PCB tab bar
        QWidget *dummyWidget = new QWidget();
        tabIndex = m_pcbTabWidget->addTab(dummyWidget, icon, label);
        m_pcbWidgets.append(widget);
    }
    
    updateVisibility();
    return tabIndex;
}

void DualTabWidget::removeTab(int index, TabType type)
{
    if (type == PDF_TAB) {
        if (index >= 0 && index < m_pdfTabWidget->count()) {
            QWidget *widget = m_pdfWidgets.at(index);
            
            // Remove from tab widget
            m_pdfTabWidget->removeTab(index);
            
            // Remove from our lists
            m_pdfWidgets.removeAt(index);
            m_allWidgets.removeOne(widget);
            
            // Remove from content area
            m_contentArea->removeWidget(widget);
        }
    } else {
        if (index >= 0 && index < m_pcbTabWidget->count()) {
            QWidget *widget = m_pcbWidgets.at(index);
            
            // Remove from tab widget
            m_pcbTabWidget->removeTab(index);
            
            // Remove from our lists
            m_pcbWidgets.removeAt(index);
            m_allWidgets.removeOne(widget);
            
            // Remove from content area
            m_contentArea->removeWidget(widget);
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
    if (type == PDF_TAB) {
        if (index >= 0 && index < m_pdfTabWidget->count()) {
            m_pdfTabWidget->setCurrentIndex(index);
            // Also switch content
            if (index < m_pdfWidgets.count()) {
                showContent(m_pdfWidgets.at(index));
            }
        }
    } else {
        if (index >= 0 && index < m_pcbTabWidget->count()) {
            m_pcbTabWidget->setCurrentIndex(index);
            // Also switch content
            if (index < m_pcbWidgets.count()) {
                showContent(m_pcbWidgets.at(index));
            }
        }
    }
}

int DualTabWidget::currentIndex(TabType type) const
{
    if (type == PDF_TAB) {
        return m_pdfTabWidget->currentIndex();
    } else {
        return m_pcbTabWidget->currentIndex();
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

void DualTabWidget::showContent(QWidget *widget)
{
    if (widget && m_allWidgets.contains(widget)) {
        m_contentArea->setCurrentWidget(widget);
    }
}

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
    if (index >= 0 && index < m_pdfWidgets.count()) {
        showContent(m_pdfWidgets.at(index));
        emit currentChanged(index, PDF_TAB);
    }
}

void DualTabWidget::onPcbCurrentChanged(int index)
{
    if (index >= 0 && index < m_pcbWidgets.count()) {
        showContent(m_pcbWidgets.at(index));
        emit currentChanged(index, PCB_TAB);
    }
}

void DualTabWidget::updateVisibility()
{
    // Hide PDF tab widget if no PDF tabs
    m_pdfTabWidget->setVisible(m_pdfTabWidget->count() > 0);
    
    // Hide PCB tab widget if no PCB tabs
    m_pcbTabWidget->setVisible(m_pcbTabWidget->count() > 0);
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
    // Determine which tab type is currently active based on what's visible and selected
    if (m_pdfTabWidget->isVisible() && m_pdfTabWidget->currentIndex() >= 0) {
        if (m_pcbTabWidget->isVisible() && m_pcbTabWidget->currentIndex() >= 0) {
            // Both are visible, check which content is currently shown
            if (m_contentArea->currentWidget()) {
                // Check if current widget belongs to PDF or PCB
                if (m_pdfWidgets.contains(m_contentArea->currentWidget())) {
                    return PDF_TAB;
                } else if (m_pcbWidgets.contains(m_contentArea->currentWidget())) {
                    return PCB_TAB;
                }
            }
            // Default to PDF if both are visible
            return PDF_TAB;
        }
        return PDF_TAB;
    } else if (m_pcbTabWidget->isVisible() && m_pcbTabWidget->currentIndex() >= 0) {
        return PCB_TAB;
    }
    
    // Default fallback
    return PDF_TAB;
}
