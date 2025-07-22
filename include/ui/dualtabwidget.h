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
    
    // Global content management
    void showContent(QWidget *widget);

signals:
    void tabCloseRequested(int index, TabType type);
    void currentChanged(int index, TabType type);

private slots:
    void onPdfTabCloseRequested(int index);
    void onPcbTabCloseRequested(int index);
    void onPdfCurrentChanged(int index);
    void onPcbCurrentChanged(int index);

private:
    void setupUI();
    void updateVisibility();
    
    QVBoxLayout *m_mainLayout;
    QTabWidget *m_pdfTabWidget;    // Row 1: PDF tabs
    QTabWidget *m_pcbTabWidget;    // Row 2: PCB tabs
    QStackedWidget *m_contentArea; // Shared content area
    
    // Keep track of widgets and their mapping
    QList<QWidget*> m_pdfWidgets;
    QList<QWidget*> m_pcbWidgets;
    QList<QWidget*> m_allWidgets; // All widgets in stacked widget
};

#endif // DUALTABWIDGET_H
