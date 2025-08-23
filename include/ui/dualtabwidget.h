#ifndef DUALTABWIDGET_H
#define DUALTABWIDGET_H

#include <QWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QStackedWidget>

class DualTabWidget : public QWidget
{
    Q_OBJECT

public:
    enum TabType {
        PDF_TAB,
        PCB_TAB
    };

    explicit DualTabWidget(QWidget *parent = nullptr);
    
    // Theme control
    void setDarkTheme(bool dark);
    bool isDarkTheme() const { return m_darkTheme; }
    // Enable/disable Material-style tabs (underlined indicator, surface/primary tokens)
    void setMaterialTheme(bool enabled);
    bool isMaterialTheme() const { return m_materialTheme; }
    
    // Main interface methods (similar to QTabWidget)
    int addTab(QWidget *widget, const QString &label, TabType type);
    int addTab(QWidget *widget, const QIcon &icon, const QString &label, TabType type);
    void removeTab(int index, TabType type);
    QWidget* widget(int index, TabType type = PDF_TAB) const;
    void setCurrentIndex(int index, TabType type = PDF_TAB);
    int currentIndex(TabType type = PDF_TAB) const;
    int count(TabType type = PDF_TAB) const;
    bool isRowVisible(TabType type) const;
    TabType getCurrentTabType() const;
    
    // Tab properties
    void setTabText(int index, const QString &text, TabType type);
    QString tabText(int index, TabType type) const;
    void setTabToolTip(int index, const QString &tip, TabType type);
    void setTabIcon(int index, const QIcon &icon, TabType type);
    
    // Properties
    void setTabsClosable(bool closable);
    void setMovable(bool movable);
    
    // Content isolation and mutual exclusion
    void activateTab(int index, TabType type);
    void deactivateAllTabs();
    bool hasActiveTab() const;
    QWidget* getActiveWidget() const;

    // Independent selection per row (doesn't switch visible content)
    // Use this to determine split pairing: the selected PDF and selected PCB tabs.
    int getSelectedIndex(TabType type) const;

    // Ensure a widget is present in its content area after being reparented externally
    void ensureContentWidgetPresent(QWidget* widget, TabType type);
    
    // Debug methods for stylesheet conflicts
    void debugStyleConflicts();
    void testObviousStyle();
    void clearAllStyles();
    void forceStyleRefresh();

    // Debug helper: apply stylesheet and tag the widget so we can tell at runtime which style was applied
    void applyStyleWithTag(QWidget* w, const QString &style, const QString &tag);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    // resizeEvent removed - close button functionality removed

signals:
    void tabCloseRequested(int index, TabType type);
    void currentChanged(int index, TabType type);
    void activeTabChanged(TabType type); // New signal for active tab type changes
    void tabLimitReached(TabType type, int maxTabs); // Emitted when user tries to exceed per-group tab limit

private slots:
    void onPdfTabCloseRequested(int index);
    void onPcbTabCloseRequested(int index);
    void onPdfCurrentChanged(int index);
    void onPcbCurrentChanged(int index);

private:
    void setupUI();
    void updateVisibility();
    void setActiveTabType(TabType type);
    void hideAllContent();
    void showActiveContent();
    void updateTabBarStates();
    void updateTabBarVisualState();
    void applyCurrentThemeStyles();
    // Perform style application and close-button setup deferred to event loop
    void deferredStyleInit();
    
    QVBoxLayout *m_mainLayout;
    QTabWidget *m_pdfTabWidget;    // Row 1: PDF tabs
    QTabWidget *m_pcbTabWidget;    // Row 2: PCB tabs
    
    // Single switcher to avoid flicker from hide/show of large areas
    QStackedWidget *m_contentSwitcher {nullptr};

    // Separate content areas for complete isolation
    QStackedWidget *m_pdfContentArea; // PDF-only content area
    QStackedWidget *m_pcbContentArea; // PCB-only content area
    
    // Active tab tracking for mutual exclusion
    TabType m_activeTabType;
    int m_activePdfIndex;
    int m_activePcbIndex;
    bool m_hasActiveTab;
    
    // Separate widget lists for content isolation
    QList<QWidget*> m_pdfWidgets;
    QList<QWidget*> m_pcbWidgets;

    // Track selection per row independent of active content
    int m_selectedPdfIndex = -1;
    int m_selectedPcbIndex = -1;

    // Hover state cache to avoid per-mouse-move relayout churn
    int m_pdfHoveredIndex = -1;
    int m_pcbHoveredIndex = -1;

    // Theme flag (default: light)
    bool m_darkTheme = false;
    // Material theme flag (default: enabled)
    bool m_materialTheme = true;
};

#endif // DUALTABWIDGET_H
