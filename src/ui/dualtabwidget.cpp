#include "ui/dualtabwidget.h"
#include <QIcon>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QHoverEvent>
#include <QTimer>
#include <QTabBar>
#include <QMessageBox>
#include <QProxyStyle>
#include <QFont>
#include <QToolButton>
#include <QStyle>
#include <QStyleOption>
#include <QApplication>
#include <QPalette>
#include <QFontDatabase>

// Forward declaration for logging helper used below
void logDebug(const QString &message);

// Custom style to remove built-in horizontal padding inside QTabBar tabs
class MinimalTabStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    int pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const override {
        // Debug: Log key pixelMetric calls to verify our style is being used
        static int pmDebugCount = 0;
        if (pmDebugCount < 20) {
            if (metric == PM_TabCloseIndicatorWidth || metric == PM_TabCloseIndicatorHeight || 
                metric == PM_TabBarTabHSpace || metric == PM_TabBarTabVSpace) {
                QString metricName = "Unknown";
                switch (metric) {
                    case PM_TabCloseIndicatorWidth: metricName = "PM_TabCloseIndicatorWidth"; break;
                    case PM_TabCloseIndicatorHeight: metricName = "PM_TabCloseIndicatorHeight"; break;
                    case PM_TabBarTabHSpace: metricName = "PM_TabBarTabHSpace"; break;
                    case PM_TabBarTabVSpace: metricName = "PM_TabBarTabVSpace"; break;
                    default: metricName = QString("PM_%1").arg(metric); break;
                }
                logDebug(QString("pixelMetric called: %1").arg(metricName));
                pmDebugCount++;
            }
        }
        
        if (metric == PM_TabBarTabHSpace) {
            return 0; // no extra left/right spacing around the tab label
        }
        if (metric == PM_TabBarTabVSpace) {
            return 0; // remove extra top/bottom spacing to reduce height
        }
    // Provide a sensible close indicator size for layout calculations
        if (metric == PM_TabCloseIndicatorWidth || metric == PM_TabCloseIndicatorHeight) {
            return 12;
        }
    return QProxyStyle::pixelMetric(metric, option, widget);
    }
    // Provide a tiny safe inset for the text rect only (not full tab padding)
    // to avoid first-glyph clipping when visual left padding is 0.
    QRect subElementRect(SubElement element, const QStyleOption *option, const QWidget *widget) const override {
    QRect r = QProxyStyle::subElementRect(element, option, widget);
    
    // Debug: Log all subElementRect calls to see what Qt is requesting
    static int debugCallCount = 0;
    if (debugCallCount < 50) { // Limit to first 50 calls to avoid spam
        QString elementName = "Unknown";
        switch (element) {
            case SE_TabBarTabText: elementName = "SE_TabBarTabText"; break;
            case SE_TabBarTabRightButton: elementName = "SE_TabBarTabRightButton"; break;
            case SE_TabBarTabLeftButton: elementName = "SE_TabBarTabLeftButton"; break;
            case SE_TabBarScrollLeftButton: elementName = "SE_TabBarScrollLeftButton"; break;
            case SE_TabBarScrollRightButton: elementName = "SE_TabBarScrollRightButton"; break;
            case SE_TabBarTearIndicator: elementName = "SE_TabBarTearIndicator"; break;
            default: elementName = QString("Element_%1").arg(element); break;
        }
        logDebug(QString("subElementRect called: %1 widget=%2").arg(elementName).arg(widget ? widget->objectName() : "null"));
        debugCallCount++;
    }
    
    if (element == SE_TabBarTabText) {
            // Use the effective style-provided metrics (reflects QSS font and bold for selected)
            QFontMetrics fm = option ? option->fontMetrics : QFontMetrics(widget ? widget->font() : QFont());

            // Probe a representative set of first characters and use the worst-case negative left bearing.
            static const QString probes = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-.,()");
            int worstLb = 0;
            for (QChar ch : probes) {
                worstLb = qMin(worstLb, fm.leftBearing(ch));
            }

            // Keep a small safety inset; higher minimum to avoid clipping with bold fonts
            const int leftInset = qMax(4, (worstLb < 0) ? -worstLb : 0);

            // Reserve space on the right for our custom close button using style metrics
            const int btnW = pixelMetric(QStyle::PM_TabCloseIndicatorWidth, option, widget);
            const int kRightMargin = 8; // updated to match SE_TabBarTabRightButton below
            const int rightReserve = qMax(18, btnW + kRightMargin + 4); // extra padding for safety

            r.adjust(leftInset, 0, -rightReserve, 0);
            if (r.width() < 0) r.setWidth(0);
        } else if (element == SE_TabBarTabRightButton) {
            // Compute a flush-right rect, centered vertically, with a small right margin
            if (option) {
                const QRect tabRect = option->rect;
                const int pmW = pixelMetric(QStyle::PM_TabCloseIndicatorWidth, option, widget);
                const int pmH = pixelMetric(QStyle::PM_TabCloseIndicatorHeight, option, widget);
                const int rightMargin = 8; // increased margin to ensure it stays well inside tab bounds
                
                // Ensure button fits entirely within tab bounds with proper margins
                const int availableWidth = tabRect.width() - rightMargin - 2; // extra 2px safety margin
                const int buttonWidth = qMin(pmW, availableWidth);
                const int x = tabRect.left() + tabRect.width() - rightMargin - buttonWidth;
                const int y = tabRect.top() + (tabRect.height() - pmH) / 2;
                
                r = QRect(x, y, buttonWidth, pmH);
                
                // Strict bounds checking - ensure button is completely inside tab
                if (r.right() >= tabRect.right()) {
                    r.moveRight(tabRect.right() - 2); // 2px safety from edge
                }
                if (r.left() <= tabRect.left()) {
                    r.moveLeft(tabRect.left() + 2); // 2px safety from edge
                }
                if (r.bottom() > tabRect.bottom()) {
                    r.moveBottom(tabRect.bottom());
                }
                if (r.top() < tabRect.top()) {
                    r.moveTop(tabRect.top());
                }
                
                // Debug log the button rect calculation
                logDebug(QString("SE_TabBarTabRightButton: tabRect=[%1,%2 %3x%4] buttonRect=[%5,%6 %7x%8]")
                             .arg(tabRect.left()).arg(tabRect.top()).arg(tabRect.width()).arg(tabRect.height())
                             .arg(r.left()).arg(r.top()).arg(r.width()).arg(r.height()));
            }
            if (r.width() < 0) r.setWidth(0);
        }
        return r;
    }
};

static MinimalTabStyle g_minimalTabStyle;

// Force a compact tab bar height derived from font metrics
static void applyCompactTabBar(QTabBar* bar)
{
    if (!bar) return;
    // Remove extra margins and ensure small, scrollable tabs
    bar->setContentsMargins(0, 0, 0, 0);
    bar->setExpanding(false);
    bar->setUsesScrollButtons(true);
    bar->setElideMode(Qt::ElideRight);
    // Compute a compact height: font height + small vertical padding
    const int fh = bar->fontMetrics().height();
    const int target = qMax(20, fh + 6); // slightly taller: ~3px top/bottom padding
    bar->setMinimumHeight(target);
}

// Close button functionality removed

// Helper: toggle visibility removed
// Close button visibility function removed

// Positioning function removed
// Positioning function removed

// Close button creation function removed
// Close button creation function removed

// Debug logging function
void logDebug(const QString &message) {
    const QString stamped = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz") + " - " + message + "\n";
    // 1) User Downloads (existing path)
    static QString downloadsPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/dualtab_debug.txt";
    {
        QFile f(downloadsPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Append)) { QTextStream s(&f); s << stamped; }
    }
    // 2) App folder (next to exe) for easy sharing with builds
    static QString appLogPath = QCoreApplication::applicationDirPath() + "/tab_debug.txt";
    {
        QFile f(appLogPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Append)) { QTextStream s(&f); s << stamped; }
    }
}

// Helper: log key metrics of a QTabBar and its tabs
static void logTabBarState(QTabBar* bar, const char* when, const char* tag)
{
    if (!bar) return;
    QStringList lines;
    lines << QString("[TabBar %1] tag=%2 when=%3 count=%4 elide=%5 usesScroll=%6 size=%7x%8")
                .arg(reinterpret_cast<quintptr>(bar), 0, 16)
                .arg(tag)
                .arg(when)
                .arg(bar->count())
                .arg(static_cast<int>(bar->elideMode()))
                .arg(bar->usesScrollButtons())
                .arg(bar->width()).arg(bar->height());
    const int pmHSpace = bar->style()->pixelMetric(QStyle::PM_TabBarTabHSpace, nullptr, bar);
    const int pmIcon   = bar->style()->pixelMetric(QStyle::PM_TabBarIconSize, nullptr, bar);
    const int pmCloseW = bar->style()->pixelMetric(QStyle::PM_TabCloseIndicatorWidth, nullptr, bar);
    const QSize iconSz = bar->iconSize();
    lines << QString("  pixelMetric HSpace=%1 IconPM=%2 CloseW=%3 iconSizeProp=%4x%5")
                .arg(pmHSpace).arg(pmIcon).arg(pmCloseW).arg(iconSz.width()).arg(iconSz.height());
    QFontMetrics fm(bar->font());
    for (int i = 0; i < bar->count(); ++i) {
        const QRect r = bar->tabRect(i);
        const QString t = bar->tabText(i);
        const int textMaxW = qMax(0, r.width() - pmHSpace); // approx available width
        const QString elided = fm.elidedText(t, bar->elideMode(), textMaxW);
        const ushort first = t.isEmpty() ? 0 : t.at(0).unicode();
    lines << QString("  idx=%1 rect=[%2,%3 %4x%5] textMaxW=%6 firstU+%7 text='%8' elided='%9'")
            .arg(i)
            .arg(r.left()).arg(r.top()).arg(r.width()).arg(r.height())
            .arg(textMaxW)
            .arg(first, 4, 16, QLatin1Char('0'))
            .arg(t)
            .arg(elided);
    }
    logDebug(lines.join('\n'));
}

// Returns a user-friendly display name from a label/path by removing
// any directory components and the final file extension (e.g., .pdf, .pcb)
// Shorten very long names while preserving the most important prefix and a tiny suffix
static QString smartShorten(const QString &name, int maxChars = 40, int tailChars = 8)
{
    if (name.length() <= maxChars) return name;
    tailChars = qBound(0, tailChars, qMax(0, maxChars - 4));
    const int headChars = qMax(0, maxChars - tailChars - 3); // 3 for ellipsis
    return name.left(headChars).trimmed() + QStringLiteral(" … ") + name.right(tailChars).trimmed();
}

static QString displayNameFromLabel(const QString &label)
{
    if (label.isEmpty()) return label;

    // Normalize and strip common prefixes like "PDF File:" or "PCB File:" (case-insensitive)
    QString cleaned = label.trimmed();
    auto stripPrefix = [&cleaned](const QString &prefix){
        if (cleaned.startsWith(prefix, Qt::CaseInsensitive)) {
            cleaned = cleaned.mid(prefix.length()).trimmed();
        }
    };
    stripPrefix("PDF File:");
    stripPrefix("PCB File:");
    stripPrefix("PDF:");
    stripPrefix("PCB:");

    // Extract last path segment (supports both '/' and '\\')
    int posSlash = cleaned.lastIndexOf('/');
    int posBack = cleaned.lastIndexOf('\\');
    int pos = qMax(posSlash, posBack);
    QString name = (pos >= 0) ? cleaned.mid(pos + 1) : cleaned;

    // Remove extension if it is .pdf or .pcb (case-insensitive)
    int dot = name.lastIndexOf('.');
    if (dot > 0 && dot < name.length() - 1) {
        const QString ext = name.mid(dot + 1).toLower();
        if (ext == "pdf" || ext == "pcb") {
            name = name.left(dot);
        }
    }
    // Remove any leading punctuation that might remain (e.g., ":" or dashes)
    while (!name.isEmpty()) {
        const QChar ch = name.at(0);
        if (ch == ' ' || ch == '\t' || ch == ':' || ch == '.' || ch == '-') {
            name.remove(0, 1);
        } else {
            break;
        }
    }
    // Apply smart shortening to keep the start visible and a small tail for uniqueness
    return smartShorten(name, 40, 8);
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

// Create a flat, minimal-height page used only to host a tab label
static QWidget* makeFlatTabPage()
{
    auto *w = new QWidget();
    // Zero margins and minimal height so only the tab bar contributes to height
    w->setContentsMargins(0, 0, 0, 0);
    // Avoid strictly zero height to keep style/layout engines happy
    w->setMinimumHeight(1);
    w->setMaximumHeight(1);
    w->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    return w;
}

void DualTabWidget::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    
    // Create PDF tab widget (Row 1)
    m_pdfTabWidget = new QTabWidget();
    // We'll manage hover-only close buttons ourselves
    m_pdfTabWidget->setTabsClosable(false);
    // Disable tab dragging/reordering for PDF tabs  
    m_pdfTabWidget->setMovable(false);
    m_pdfTabWidget->setDocumentMode(true); // flatter tabs with less chrome
    // Apply minimal tab style to remove built-in label h-padding
    m_pdfTabWidget->tabBar()->setStyle(&g_minimalTabStyle);
    // Enable hover tracking
    m_pdfTabWidget->tabBar()->setMouseTracking(true);
    m_pdfTabWidget->tabBar()->setAttribute(Qt::WA_Hover, true);
    // Show hover state events without pressing
    m_pdfTabWidget->tabBar()->setMouseTracking(true);
    
    // Enable scrollable tabs when there are too many
    m_pdfTabWidget->tabBar()->setUsesScrollButtons(true);
    m_pdfTabWidget->tabBar()->setElideMode(Qt::ElideRight);  // Show beginning, ellipsis at end
    m_pdfTabWidget->tabBar()->setExpanding(false);
    // Disable icon space completely - this is critical for proper text alignment
    m_pdfTabWidget->setIconSize(QSize(0, 0));
    applyCompactTabBar(m_pdfTabWidget->tabBar());
    // Styles will be applied by theme helper
    m_pcbTabWidget = new QTabWidget();
    // We'll manage hover-only close buttons ourselves
    m_pcbTabWidget->setTabsClosable(false);
    // Disable tab dragging/reordering for PCB tabs
    m_pcbTabWidget->setMovable(false);
    m_pcbTabWidget->setDocumentMode(true); // flatter tabs with less chrome
    // Apply minimal tab style to remove built-in label h-padding
    m_pcbTabWidget->tabBar()->setStyle(&g_minimalTabStyle);
    // Show hover state events without pressing
    m_pcbTabWidget->tabBar()->setMouseTracking(true);
    m_pcbTabWidget->tabBar()->setAttribute(Qt::WA_Hover, true);
    
    // Enable scrollable tabs when there are too many
    m_pcbTabWidget->tabBar()->setUsesScrollButtons(true);
    m_pcbTabWidget->tabBar()->setElideMode(Qt::ElideRight);  // Show beginning, ellipsis at end
    m_pcbTabWidget->tabBar()->setExpanding(false);
    // Disable icon space completely - this is critical for proper text alignment
    m_pcbTabWidget->setIconSize(QSize(0, 0));
    applyCompactTabBar(m_pcbTabWidget->tabBar());
    
    // Apply theme and create close buttons deferred to avoid constructor-time style engine churn
    QTimer::singleShot(0, this, [this]{ deferredStyleInit(); });
    // We only need hover on the tab bars (avoid global event filter churn)
    
    // DEBUG: Test with obvious colors (comment out after testing)
    // testObviousStyle();
    
    // Optional: Debug styling (uncomment to troubleshoot)
    // debugStyleConflicts();
    
    // Create separate content areas for complete isolation
    m_pdfContentArea = new QStackedWidget();
    // Style via theme helper
    
    m_pcbContentArea = new QStackedWidget();
    // Style via theme helper
    
    // Wrap content areas in a single switcher to avoid flicker from hide/show
    m_contentSwitcher = new QStackedWidget();
    m_contentSwitcher->addWidget(m_pdfContentArea);
    m_contentSwitcher->addWidget(m_pcbContentArea);

    // Add to main layout
    m_mainLayout->addWidget(m_pdfTabWidget);
    m_mainLayout->addWidget(m_pcbTabWidget);
    m_mainLayout->addWidget(m_contentSwitcher, 1); // single stack handles both
    
    // Connect signals
    connect(m_pdfTabWidget, &QTabWidget::tabCloseRequested, this, &DualTabWidget::onPdfTabCloseRequested);
    connect(m_pcbTabWidget, &QTabWidget::tabCloseRequested, this, &DualTabWidget::onPcbTabCloseRequested);
    connect(m_pdfTabWidget, &QTabWidget::currentChanged, this, &DualTabWidget::onPdfCurrentChanged);
    connect(m_pcbTabWidget, &QTabWidget::currentChanged, this, &DualTabWidget::onPcbCurrentChanged);
    
    // Install event filters on tab bars to ensure clicks are always detected
    m_pdfTabWidget->tabBar()->installEventFilter(this);
    m_pcbTabWidget->tabBar()->installEventFilter(this);
    logDebug("Event filters installed on both tab bars");
    
    logDebug("Signal connections established for both tab widgets");
    
    // Initially no active content
    hideAllContent();
    updateVisibility();
    updateTabBarStates();
}

void DualTabWidget::deferredStyleInit()
{
    logDebug("deferredStyleInit: begin");
    // Apply theme (default: light). This sets tab bar and content styles coherently.
    applyCurrentThemeStyles();
    // Log initial tab bar state after startup style
    if (m_pdfTabWidget && m_pdfTabWidget->tabBar())
        logTabBarState(m_pdfTabWidget->tabBar(), "after-startup-style", "PDF");
    if (m_pcbTabWidget && m_pcbTabWidget->tabBar())
        logTabBarState(m_pcbTabWidget->tabBar(), "after-startup-style", "PCB");
    // Close button functionality removed
    logDebug("deferredStyleInit: end");
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

int DualTabWidget::addTab(QWidget *widget, const QIcon &/*icon*/, const QString &label, TabType type)
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
    QWidget *dummyWidget = makeFlatTabPage();
    // Show only the base file name without extension
    const QString cleanedFull = displayNameFromLabel(label); // shortened already
    const QString display = cleanedFull;
    tabIndex = m_pdfTabWidget->addTab(dummyWidget, display); // icon intentionally ignored
    m_pdfTabWidget->setTabToolTip(tabIndex, label); // show full original path/label on hover
    // Close button functionality removed
    // Log the state after adding a tab
    logTabBarState(m_pdfTabWidget->tabBar(), "after-add-tab", "PDF");
        
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
    QWidget *dummyWidget = makeFlatTabPage();
    // Show only the base file name without extension
    const QString cleanedFull = displayNameFromLabel(label);
    const QString display = cleanedFull;
    tabIndex = m_pcbTabWidget->addTab(dummyWidget, display); // icon intentionally ignored
    m_pcbTabWidget->setTabToolTip(tabIndex, label);
    // Close button functionality removed
    // Log the state after adding a tab
    logTabBarState(m_pcbTabWidget->tabBar(), "after-add-tab", "PCB");
        
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
    const QString display = displayNameFromLabel(text);
    if (type == PDF_TAB) {
        m_pdfTabWidget->setTabText(index, display);
        m_pdfTabWidget->setTabToolTip(index, text);
    logTabBarState(m_pdfTabWidget->tabBar(), "after-setTabText", "PDF");
    } else {
        m_pcbTabWidget->setTabText(index, display);
        m_pcbTabWidget->setTabToolTip(index, text);
    logTabBarState(m_pcbTabWidget->tabBar(), "after-setTabText", "PCB");
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
    // Icons disabled - tabs will only show text labels
    Q_UNUSED(index);
    Q_UNUSED(icon);
    Q_UNUSED(type);
    // Do nothing - this prevents any icons from being set on tabs
}

void DualTabWidget::setTabsClosable(bool closable)
{
    Q_UNUSED(closable);
    // Keep native close indicators disabled; we'll manage custom right-side buttons ourselves.
    if (m_pdfTabWidget) m_pdfTabWidget->setTabsClosable(false);
    if (m_pcbTabWidget) m_pcbTabWidget->setTabsClosable(false);
    logDebug("setTabsClosable() override: using custom hover-only close buttons on the right");
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
    // Fast path: if this tab is already active, do nothing
    if (m_hasActiveTab && type == m_activeTabType) {
        if ((type == PDF_TAB && index == m_activePdfIndex) || (type == PCB_TAB && index == m_activePcbIndex)) {
            logDebug("activateTab(): requested tab already active - skipping");
            return;
        }
    }
    
    // Validate the requested tab
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

    // Set active tab type and activate the specific tab
    logDebug("Setting active tab type");
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
    logDebug("Set hasActiveTab to true");
    
    // Show the active content via the switcher (no hide/show churn)
    logDebug("Calling showActiveContent()");
    showActiveContent();
    
    // Update UI states (cheap)
    logDebug("Updating tab bar states");
    updateTabBarStates();
    
    // Emit signal about active tab change
    logDebug("Emitting signals");
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
    // Keep tab bars enabled so users can click to switch
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
    // Keep both tab bars enabled so users can click to switch
    m_pdfTabWidget->setEnabled(true);
    m_pcbTabWidget->setEnabled(true);
    // No heavy restyling on switch (avoid flicker)
    } else {
        logDebug("Tab type unchanged - no action needed");
    }
}

void DualTabWidget::hideAllContent()
{
    if (m_contentSwitcher) m_contentSwitcher->hide();
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
        logDebug("Switching to PDF content area");
        if (m_contentSwitcher) {
            m_contentSwitcher->setCurrentWidget(m_pdfContentArea);
            m_contentSwitcher->show();
        }
    } else {
        logDebug("Switching to PCB content area");
        if (m_contentSwitcher) {
            m_contentSwitcher->setCurrentWidget(m_pcbContentArea);
            m_contentSwitcher->show();
        }
    }
}

void DualTabWidget::updateTabBarStates()
{
    // Keep both tab bars enabled so users can click to switch between tab types
    m_pdfTabWidget->setEnabled(true);
    m_pcbTabWidget->setEnabled(true);
    // No heavy stylesheet reapply on every switch
    updateTabBarVisualState();
}

void DualTabWidget::updateTabBarVisualState()
{
    // Keep icon space disabled; avoid re-assigning stylesheets here
    if (m_pdfTabWidget) m_pdfTabWidget->setIconSize(QSize(0, 0));
    if (m_pcbTabWidget) m_pcbTabWidget->setIconSize(QSize(0, 0));
    // Close button functionality removed
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
    // Log state after activation
    logTabBarState(m_pdfTabWidget->tabBar(), "after-activate", "PDF");
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
    // Log state after activation
    logTabBarState(m_pcbTabWidget->tabBar(), "after-activate", "PCB");
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
    // Hover tracking for showing/hiding per-tab close buttons
    if (event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove || event->type() == QEvent::Enter) {
        auto computeIndex = [](QTabBar* bar, QEvent* ev) -> int {
            if (!bar) return -1;
            QPoint local;
            switch (ev->type()) {
                case QEvent::MouseMove:
                    local = static_cast<QMouseEvent*>(ev)->pos();
                    break;
                case QEvent::HoverMove:
                    local = static_cast<QHoverEvent*>(ev)->position().toPoint();
                    break;
                default:
                    local = bar->mapFromGlobal(QCursor::pos());
                    break;
            }
            return bar->rect().contains(local) ? bar->tabAt(local) : -1;
        };

        if (obj == m_pdfTabWidget->tabBar()) {
            auto *bar = m_pdfTabWidget->tabBar();
            const int idx = computeIndex(bar, event);
            if (idx != m_pdfHoveredIndex) {
                m_pdfHoveredIndex = idx;
                // Close button functionality removed
                logDebug(QString("hover-change: PDF idx=%1").arg(idx));
            }
        } else if (obj == m_pcbTabWidget->tabBar()) {
            auto *bar = m_pcbTabWidget->tabBar();
            const int idx = computeIndex(bar, event);
            if (idx != m_pcbHoveredIndex) {
                m_pcbHoveredIndex = idx;
                // Close button functionality removed
                logDebug(QString("hover-change: PCB idx=%1").arg(idx));
            }
        }
    }
    if (event->type() == QEvent::Leave) {
        if (m_pdfTabWidget && obj == m_pdfTabWidget->tabBar()) {
            m_pdfHoveredIndex = -1;
            // Close button functionality removed
        } else if (m_pcbTabWidget && obj == m_pcbTabWidget->tabBar()) {
            m_pcbHoveredIndex = -1;
            // Close button functionality removed
        }
    }
    // Log geometry/metrics on resize events for both tab bars
    if (event->type() == QEvent::Resize) {
        if (obj == m_pdfTabWidget->tabBar()) {
            logTabBarState(m_pdfTabWidget->tabBar(), "on-resize", "PDF");
        } else if (obj == m_pcbTabWidget->tabBar()) {
            logTabBarState(m_pcbTabWidget->tabBar(), "on-resize", "PCB");
        }
        // continue default processing
    }

    // Handle mouse press events on tab bars to ensure clicks are always detected
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            
            // Check if it's PDF tab bar being clicked
            if (obj == m_pdfTabWidget->tabBar()) {
                int clickedIndex = m_pdfTabWidget->tabBar()->tabAt(mouseEvent->pos());
                if (clickedIndex >= 0) {
                    logDebug(QString("Event filter caught PDF tab click - index: %1").arg(clickedIndex));
                    // Also log state at click time
                    logTabBarState(m_pdfTabWidget->tabBar(), "on-click", "PDF");
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
                    // Also log state at click time
                    logTabBarState(m_pcbTabWidget->tabBar(), "on-click", "PCB");
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

void DualTabWidget::setDarkTheme(bool dark)
{
    if (m_darkTheme == dark) return;
    m_darkTheme = dark;
    this->setProperty("explicitTheme", true);
    applyCurrentThemeStyles();
}

void DualTabWidget::setMaterialTheme(bool enabled)
{
    if (m_materialTheme == enabled) return;
    m_materialTheme = enabled;
    this->setProperty("explicitTheme", true);
    applyCurrentThemeStyles();
}

void DualTabWidget::applyCurrentThemeStyles()
{
    logDebug("applyCurrentThemeStyles: begin");
    // Base palette-derived hint (optional): if not explicitly set, detect from app palette
    bool dark = m_darkTheme;
    if (!this->property("explicitTheme").toBool()) {
        // Detect if app palette is dark and adopt it as default
        const bool appDark = qApp && qApp->palette().color(QPalette::Window).lightness() < 128;
        dark = m_darkTheme || appDark;
        m_darkTheme = dark; // lock in
    }

    // When Material theme is disabled, use the existing compact bordered style
    if (!m_materialTheme) {
        const QString pdfLight =
            "QTabWidget {"
            "    background: #ffffff;"
            "    font-family: 'Segoe UI Variable Text','Segoe UI','Inter',Arial,sans-serif;"
            "}"
            "QTabWidget::pane { border:0; background:transparent; margin:0; padding:0; }"
            "QTabBar { qproperty-drawBase:0; background:#e8e8e8; }"
            "QTabBar::tab { background:#f0f0f0; border:1px solid #888; color:#333;  margin:1px; min-height:20px; min-width:140px; max-width:300px; font-size:11px; font-weight:500; letter-spacing:0.2px; }"
            "QTabBar::tab:selected { background:#ffffff; color:#0066cc; border:1px solid #4A90E2; font-weight:600; padding-left:5px; }"
            "QTabBar::tab:hover:!selected { background:rgba(227,242,253,0.8); border:1px solid #90caf9; color:#1976d2; }"
            "QTabBar::tab:first { margin-left:6px; } QTabBar::tab:last { margin-right:0; } QTabBar::tab:focus { outline:none; }"
            ;

        const QString pcbLight =
            "QTabWidget { background:#ffffff; font-family:'Segoe UI Variable Text','Segoe UI','Inter',Arial,sans-serif; }"
            "QTabWidget::pane { border:0; background:transparent; margin:0; padding:0; }"
            "QTabBar { qproperty-drawBase:0; background:#e8e8e8; }"
            "QTabBar::tab { background:#f8f8f8; border:1px solid #888; color:#333; padding:3px 6px 3px 4px; margin:1px; min-height:20px; min-width:140px; max-width:300px; font-size:11px; font-weight:500; letter-spacing:.2px; }"
            "QTabBar::tab:selected { background:#ffffff; color:#c62828; border:1px solid #E53935; font-weight:600; padding-left:5px; }"
            "QTabBar::tab:hover:!selected { background:rgba(255,235,238,.85); border:1px solid #ef9a9a; color:#d32f2f; }"
            "QTabBar::tab:first { margin-left:6px; } QTabBar::tab:last { margin-right:0; } QTabBar::tab:focus { outline:none; }"
            ;

        const QString pdfDark =
            "QTabWidget { background:#111; color:#e8eaed; font-family:'Segoe UI Variable Text','Segoe UI','Inter',Arial,sans-serif; }"
            "QTabWidget::pane { border:0; background:transparent; margin:0; padding:0; }"
            "QTabBar { qproperty-drawBase:0; background:#202124; }"
            "QTabBar::tab { background:#2a2b2d; border:1px solid rgba(255,255,255,0.35); color:#e8eaed; margin:1px; min-height:20px; min-width:140px; max-width:320px; font-size:11px; font-weight:500; letter-spacing:.2px; }"
            "QTabBar::tab:selected { background:#1f2937; color:#8ab4f8; border:1px solid #1976d2; font-weight:600; padding-left:5px; }"
            "QTabBar::tab:hover:!selected { background:#263238; border:1px solid #4f89d3; color:#90caf9; }"
            "QTabBar::tab:first { margin-left:6px; } QTabBar::tab:last { margin-right:0; } QTabBar::tab:focus { outline:none; }"
            ;

        const QString pcbDark =
            "QTabWidget { background:#111; color:#f8dddd; font-family:'Segoe UI Variable Text','Segoe UI','Inter',Arial,sans-serif; }"
            "QTabWidget::pane { border:0; background:transparent; margin:0; padding:0; }"
            "QTabBar { qproperty-drawBase:0; background:#202124; }"
            "QTabBar::tab { background:#2a2b2d; border:1px solid rgba(255,255,255,0.35); color:#e8eaed; padding:3px 6px 3px 4px; margin:1px; min-height:20px; min-width:140px; max-width:320px; font-size:11px; font-weight:500; letter-spacing:.2px; }"
            "QTabBar::tab:selected { background:#2b1f1f; color:#ff8a80; border:1px solid #b71c1c; font-weight:600; padding-left:5px; }"
            "QTabBar::tab:hover:!selected { background:#332222; border:1px solid #cf6679; color:#ef9a9a; }"
            "QTabBar::tab:first { margin-left:6px; } QTabBar::tab:last { margin-right:0; } QTabBar::tab:focus { outline:none; }"
            ;

        if (m_pdfTabWidget) applyStyleWithTag(m_pdfTabWidget, dark ? pdfDark : pdfLight, dark ? "pdfDark" : "pdfLight");
        if (m_pcbTabWidget) applyStyleWithTag(m_pcbTabWidget, dark ? pcbDark : pcbLight, dark ? "pcbDark" : "pcbLight");
    } else {
        // Material Design-inspired QSS: flat tabs with premium typography applied via QFont
    const QString baseFamily = "'Segoe UI Variable Text','Segoe UI','Inter',Arial,sans-serif";
        const QString surfaceL = "#FAFAFA";
        const QString surfaceD = "#121212";
    const QString onSurfaceL = "#1F1F1F";
        const QString onSurfaceD = "#EDEDED";
        const QString pdfPrimaryL = "#1976D2"; // Blue 700
        const QString pcbPrimaryL = "#D32F2F"; // Red 700
        const QString pdfPrimaryD = "#90CAF9"; // Blue 200
        const QString pcbPrimaryD = "#EF9A9A"; // Red 200
    const QString hoverL = "rgba(0,0,0,0.06)";
    const QString hoverD = "rgba(255,255,255,0.08)";
    // Slightly darker neutral borders for inactive tabs (more definition)
    const QString borderNeutralL = "#AFB8C1"; // was #D0D7DE
    const QString borderNeutralD = "#2B3035"; // was #383838
    // Emphasized hover border and pressed background for premium feedback
    const QString hoverBorderL = "#8C96A0";   // a bit darker than neutral
    const QString hoverBorderD = "#3A4046";   // a bit lighter than bg but darker than neutral
    const QString pressedL = "rgba(0,0,0,0.10)";
    const QString pressedD = "rgba(255,255,255,0.12)";

        const QString commonPane =
            "QTabWidget { background:%1; color:%2; }"
            "QTabWidget::pane { border:0; background:transparent; margin:0; padding:0; }"
            "QTabBar { qproperty-drawBase:0; background:transparent; }"
            "QTabBar::tear { width:0; height:0; }";

        const QString tabsBase =
            "QTabBar::tab { background: transparent; border:1px solid transparent; border-radius:2px;"
            " padding:2px 10px; margin:0 6px; min-height:22px; min-width:150px; font-weight:500; color:%1; }"
            "QTabBar::tab:hover { background:%2; }"
            "QTabBar::tab:pressed { background:%3; }"
            "QTabBar::tab:focus { outline: none; }"
            "QTabBar::tab:!selected { background: transparent; }";

        const QString pdfTabsL = tabsBase.arg(onSurfaceL, hoverL, pressedL) +
            "QTabBar::tab:!selected { border-color:" + borderNeutralL + "; }"
            "QTabBar::tab:hover:!selected { border-color:" + hoverBorderL + "; }"
            "QTabBar::tab:selected { background:" + pdfPrimaryL + "; color:#FFFFFF; border-color:" + pdfPrimaryL + "; font-weight:600; }"
            "QTabBar::tab:focus:!selected { border-color:" + hoverBorderL + "; }";
        const QString pcbTabsL = tabsBase.arg(onSurfaceL, hoverL, pressedL) +
            "QTabBar::tab:!selected { border-color:" + borderNeutralL + "; }"
            "QTabBar::tab:hover:!selected { border-color:" + hoverBorderL + "; }"
            "QTabBar::tab:selected { background:" + pcbPrimaryL + "; color:#FFFFFF; border-color:" + pcbPrimaryL + "; font-weight:600; }"
            "QTabBar::tab:focus:!selected { border-color:" + hoverBorderL + "; }";

        const QString pdfTabsD = tabsBase.arg(onSurfaceD, hoverD, pressedD) +
            "QTabBar::tab:!selected { border-color:" + borderNeutralD + "; }"
            "QTabBar::tab:hover:!selected { border-color:" + hoverBorderD + "; }"
            "QTabBar::tab:selected { background:" + pdfPrimaryD + "; color:#FFFFFF; border-color:" + pdfPrimaryD + "; font-weight:600; }"
            "QTabBar::tab:focus:!selected { border-color:" + hoverBorderD + "; }";
        const QString pcbTabsD = tabsBase.arg(onSurfaceD, hoverD, pressedD) +
            "QTabBar::tab:!selected { border-color:" + borderNeutralD + "; }"
            "QTabBar::tab:hover:!selected { border-color:" + hoverBorderD + "; }"
            "QTabBar::tab:selected { background:" + pcbPrimaryD + "; color:#FFFFFF; border-color:" + pcbPrimaryD + "; font-weight:600; }"
            "QTabBar::tab:focus:!selected { border-color:" + hoverBorderD + "; }";

        const QString pdfQss = (commonPane.arg(dark ? surfaceD : surfaceL, dark ? onSurfaceD : onSurfaceL)) + (dark ? pdfTabsD : pdfTabsL);
        const QString pcbQss = (commonPane.arg(dark ? surfaceD : surfaceL, dark ? onSurfaceD : onSurfaceL)) + (dark ? pcbTabsD : pcbTabsL);

    if (m_pdfTabWidget) applyStyleWithTag(m_pdfTabWidget, pdfQss, dark ? "pdfMaterialDark" : "pdfMaterialLight");
    if (m_pcbTabWidget) applyStyleWithTag(m_pcbTabWidget, pcbQss, dark ? "pcbMaterialDark" : "pcbMaterialLight");
    }

    // Content areas
    if (m_pdfContentArea) {
        m_pdfContentArea->setStyleSheet(QStringLiteral(
            "QStackedWidget { border:1px solid %1; border-radius:0; background:%2; }"
        ).arg(dark ? "#3c4043" : "#e0e0e0",
              dark ? "#111111" : "#ffffff"));
    }
    if (m_pcbContentArea) {
        m_pcbContentArea->setStyleSheet(QStringLiteral(
            "QStackedWidget { border:1px solid %1; border-radius:0; background:%2; }"
        ).arg(dark ? "#3c4043" : "#e0e0e0",
              dark ? "#111111" : "#ffffff"));
    }

    // No close buttons to theme
    
    // Apply premium font to tab bars (family, size, spacing) after styles for maximum effect
    auto applyPremiumTabFont = [](QTabBar* bar){
        if (!bar) return;
        QFontDatabase db;
        const QStringList preferred = {
            QStringLiteral("Segoe UI Variable Text"),
            QStringLiteral("Segoe UI Variable Display"),
            QStringLiteral("Segoe UI"),
            QStringLiteral("Inter"),
            QStringLiteral("Roboto"),
            QStringLiteral("Noto Sans"),
            QStringLiteral("Calibri"),
            QStringLiteral("Arial")
        };
        QString chosen = QStringLiteral("Segoe UI");
        const QStringList families = db.families();
        for (const auto &cand : preferred) {
            if (families.contains(cand)) { chosen = cand; break; }
        }
        QFont f(chosen);
        // Pixel size keeps consistency with our compact 22px tab height
        f.setPixelSize(12);
        f.setWeight(QFont::Medium); // base weight; selected will go to 600 via QSS
        f.setKerning(true);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        f.setHintingPreference(QFont::PreferFullHinting);
#endif
    f.setStyleStrategy(QFont::PreferAntialias);
        // Subtle tracking improves clarity without crowding
        f.setLetterSpacing(QFont::PercentageSpacing, 102.0);
        bar->setFont(f);
    };

    if (m_pdfTabWidget && m_pdfTabWidget->tabBar()) applyPremiumTabFont(m_pdfTabWidget->tabBar());
    if (m_pcbTabWidget && m_pcbTabWidget->tabBar()) applyPremiumTabFont(m_pcbTabWidget->tabBar());
    // Re-apply sizing after stylesheet changes
    if (m_pdfTabWidget) {
        if (m_materialTheme) {
            const int target = 22;
            m_pdfTabWidget->tabBar()->setMinimumHeight(target);
        } else {
            applyCompactTabBar(m_pdfTabWidget->tabBar());
        }
    }
    if (m_pcbTabWidget) {
        if (m_materialTheme) {
            const int target = 22;
            m_pcbTabWidget->tabBar()->setMinimumHeight(target);
        } else {
            applyCompactTabBar(m_pcbTabWidget->tabBar());
        }
    }
    logDebug("applyCurrentThemeStyles: end");
}

// Close button functionality removed - resizeEvent no longer needed

// (Removed) updateCloseButtonsTheme: no longer needed
