// Clean single-pane PDFViewerWidget implementation (split view removed)
#include "viewers/pdf/pdfviewerwidget.h"
#include "viewers/pdf/PDFViewerEmbedder.h"
#include "ui/LoadingOverlay.h"
#include "core/memoryfilemanager.h"

#include <QResizeEvent>
#include <QPaintEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QFocusEvent>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QTimer>
#include <QToolBar>
#include <QAction>
#include <QMouseEvent>
#include <QDebug>
#include <QMenu>
#include <QDateTime>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QPalette>
#include <QToolButton>
#include <QPainter>
#include <QStyle>
#include <QGraphicsDropShadowEffect>
#include <QIcon>
#include <QPixmap>
#include <QImage>
#include <QClipboard>
#include <QGuiApplication>
#include <QCursor>
#include "viewers/pdf/PDFPreviewLoader.h"
#ifdef _WIN32
#include <windows.h>
#endif

// --- Premium toolbar helpers (icon tinting, hover/checked states) ---
static QIcon makeTintedIcon(const QIcon &base, const QColor &color, const QSize &size)
{
    if (base.isNull()) return base;
    QPixmap src = base.pixmap(size);
    if (src.isNull()) return base;
    QImage img = src.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage colored(img.size(), QImage::Format_ARGB32_Premultiplied);
    colored.fill(Qt::transparent);
    QPainter p(&colored);
    p.fillRect(colored.rect(), color);
    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    p.drawImage(0, 0, img);
    p.end();
    return QIcon(QPixmap::fromImage(colored));
}

// Simple event filter to swap icons on hover/enable/checked states
class PremiumButtonFilter : public QObject {
public:
    PremiumButtonFilter(QToolButton* btn, const QIcon &normal, const QIcon &hover, const QIcon &disabled)
        : QObject(btn), m_btn(btn), m_normal(normal), m_hover(hover), m_disabled(disabled) {}
protected:
    bool eventFilter(QObject *obj, QEvent *ev) override {
        if (obj != m_btn) return QObject::eventFilter(obj, ev);
        switch (ev->type()) {
            case QEvent::Enter:
                if (m_btn->isEnabled()) m_btn->setIcon(m_hover);
                break;
            case QEvent::Leave:
                if (m_btn->isEnabled()) m_btn->setIcon((m_btn->isCheckable() && m_btn->isChecked()) ? m_hover : m_normal);
                break;
            case QEvent::EnabledChange:
                m_btn->setIcon(m_btn->isEnabled() ? (m_btn->underMouse() || (m_btn->isCheckable() && m_btn->isChecked()) ? m_hover : m_normal) : m_disabled);
                break;
            default: break;
        }
        return QObject::eventFilter(obj, ev);
    }
private:
    QToolButton* m_btn {nullptr};
    QIcon m_normal, m_hover, m_disabled;
};

static void installPremiumButtonStyling(QToolBar *bar, const QColor &accent, bool dark)
{
    if (!bar) return;
    const QSize iconSz(16, 16);
    const QColor normal = dark ? QColor("#c7cacf") : QColor("#5f6368");
    const QColor disabled = dark ? QColor("#6f7379") : QColor("#9e9e9e");

    for (QAction *act : bar->actions()) {
        if (!act || act->isSeparator()) continue;
        if (auto *btn = qobject_cast<QToolButton*>(bar->widgetForAction(act))) {
            const QIcon baseIcon = act->icon();
            QIcon iconNormal = makeTintedIcon(baseIcon, normal, iconSz);
            QIcon iconHover  = makeTintedIcon(baseIcon, accent, iconSz);
            QIcon iconDis    = makeTintedIcon(baseIcon, disabled, iconSz);
            btn->setAutoRaise(true);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setIconSize(iconSz);
            btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
            btn->setIcon(act->isEnabled() ? iconNormal : iconDis);
            btn->setAttribute(Qt::WA_Hover, true);
            auto *filter = new PremiumButtonFilter(btn, iconNormal, iconHover, iconDis);
            btn->installEventFilter(filter);
            QObject::connect(btn, &QToolButton::toggled, btn, [btn, iconNormal, iconHover]() {
                if (!btn->isEnabled()) return;
                btn->setIcon(btn->isChecked() ? iconHover : (btn->underMouse() ? iconHover : iconNormal));
            });
            QObject::connect(act, &QAction::changed, btn, [btn, act, iconNormal, iconDis]() {
                btn->setIcon(act->isEnabled() ? iconNormal : iconDis);
            });
        }
    }
}

PDFViewerWidget::PDFViewerWidget(QWidget *parent)
    : QWidget(parent)
    , m_pdfEmbedder(std::make_unique<PDFViewerEmbedder>())
    , m_mainLayout(nullptr)
    , m_toolbar(nullptr)
    , m_viewerContainer(nullptr)
    , m_actionRotateLeft(nullptr)
    , m_actionRotateRight(nullptr)
    , m_actionPreviousPage(nullptr)
    , m_actionNextPage(nullptr)
    , m_actionZoomIn(nullptr)
    , m_actionZoomOut(nullptr)
    , m_actionFindPrevious(nullptr)
    , m_actionFindNext(nullptr)
    , m_pageLabel(nullptr)
    , m_pageInput(nullptr)
    , m_totalPagesLabel(nullptr)
    , m_searchLabel(nullptr)
    , m_searchInput(nullptr)
    , m_updateTimer(new QTimer(this))
    , m_navigationTimer(new QTimer(this))
    , m_searchDebounceTimer(new QTimer(this))
    , m_viewerInitialized(false)
    , m_pdfLoaded(false)
    , m_usingFallback(false)
    , m_navigationInProgress(false)
{
    setupUI();
    // Configure performance defaults (tighter memory + no mipmaps + minimal preload)
    if (m_pdfEmbedder) {
        m_pdfEmbedder->setMemoryBudgetMB(128);      // Lower per-tab budget to reduce aggregate GPU usage
        m_pdfEmbedder->setTextureMipmapsEnabled(false); // We already disabled globally; ensure explicit
        m_pdfEmbedder->setPreloadPageMargin(0);     // Only visible pages until user scrolls
    }
    // Create loading overlay
    m_loadingOverlay = new LoadingOverlay(this);
    connect(m_loadingOverlay, &LoadingOverlay::cancelRequested, this, &PDFViewerWidget::cancelLoad);
    m_updateTimer->setInterval(UPDATE_INTERVAL_MS);
    connect(m_updateTimer, &QTimer::timeout, this, &PDFViewerWidget::updateViewer);
    connect(m_updateTimer, &QTimer::timeout, this, &PDFViewerWidget::checkForSelectedText);
    m_navigationTimer->setSingleShot(true);
    m_navigationTimer->setInterval(100);
    connect(m_navigationTimer, &QTimer::timeout, this, [this]() { m_navigationInProgress = false; });

    // Debounced search setup
    if (m_searchDebounceTimer) {
        m_searchDebounceTimer->setSingleShot(true);
        m_searchDebounceTimer->setInterval(SEARCH_DEBOUNCE_MS);
        connect(m_searchDebounceTimer, &QTimer::timeout, this, [this]() {
            if (!m_searchInput)
                return;
            QString term = m_searchInput->text().trimmed();
            if (term.isEmpty()) {
                if (isPDFLoaded() && m_pdfEmbedder)
                    m_pdfEmbedder->clearSelection();
                m_lastSearchTerm.clear();
            } else {
                // Only execute if term changed from last executed to avoid redundant find cycles
                if (term != m_lastSearchTerm) {
                    m_lastSearchTerm = term;
                    searchText();
                }
            }
            updateStatusInfo();
            syncToolbarStates();
        });
    }
}

PDFViewerWidget::~PDFViewerWidget()
{
    if (m_updateTimer && m_updateTimer->isActive()) {
        m_updateTimer->stop();
    }
    if (m_pdfEmbedder) {
        m_pdfEmbedder->shutdown();
    }
}

void PDFViewerWidget::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    setupToolbar();
    setupViewerArea();

    if (m_toolbar)
        m_mainLayout->addWidget(m_toolbar);
    if (m_viewerContainer)
        m_mainLayout->addWidget(m_viewerContainer, 1);

    setStyleSheet("PDFViewerWidget{background:#f5f5f5;border:1px solid #d0d0d0;}");
}

void PDFViewerWidget::setupToolbar()
{
    m_toolbar = new QToolBar(this);
    m_toolbar->setIconSize(QSize(16, 16));
    m_toolbar->setMovable(false);
    // Force light theme - ignore palette darkness detection
    const bool dark = false;
    const QString tbStyleLight =
    "QToolBar{background:#fafafa;border-bottom:1px solid #d0d0d0;min-height:30px;}"
    "QToolBar QToolButton{border:1px solid transparent;border-radius:6px;padding:4px;margin:2px;}"
        "QToolBar QToolButton:hover{background:rgba(25,118,210,0.12);border-color:rgba(25,118,210,0.35);}" 
        "QToolBar QToolButton:pressed{background:rgba(25,118,210,0.20);border-color:#1976d2;}"
        "QToolBar QToolButton:checked{background:rgba(25,118,210,0.16);border-color:#1976d2;}"
        "QToolBar QToolButton:disabled{color:#9e9e9e;background:transparent;border-color:transparent;}"
    "QToolBar::separator{background:rgba(0,0,0,0.12);width:1px;margin:0 6px;}";
    const QString tbStyleDark =
    "QToolBar{background:#202124;border-bottom:1px solid #3c4043;min-height:30px;}"
    "QToolBar QToolButton{color:#e8eaed;border:1px solid transparent;border-radius:6px;padding:4px;margin:2px;}"
        "QToolBar QToolButton:hover{background:rgba(25,118,210,0.20);border-color:#4f89d3;}"
        "QToolBar QToolButton:pressed{background:rgba(25,118,210,0.30);border-color:#1976d2;}"
        "QToolBar QToolButton:checked{background:rgba(25,118,210,0.28);border-color:#4f89d3;color:#e8eaed;}"
        "QToolBar QToolButton:disabled{color:#9aa0a6;background:transparent;border-color:transparent;}"
        "QToolBar::separator{background:rgba(255,255,255,0.12);width:1px;margin:0 6px;}";
    m_toolbar->setStyleSheet(dark ? tbStyleDark : tbStyleLight);

    // Subtle drop shadow for depth
    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(12);
    shadow->setOffset(0, 1);
    shadow->setColor(dark ? QColor(0,0,0,140) : QColor(0,0,0,60));
    m_toolbar->setGraphicsEffect(shadow);

    // Rotation
    m_actionRotateLeft = m_toolbar->addAction(QIcon(":/icons/images/icons/rotate_left.svg"), "");
    m_actionRotateLeft->setToolTip("Rotate Left");
    connect(m_actionRotateLeft, &QAction::triggered, this, &PDFViewerWidget::rotateLeft);

    m_actionRotateRight = m_toolbar->addAction(QIcon(":/icons/images/icons/rotate_right.svg"), "");
    m_actionRotateRight->setToolTip("Rotate Right");
    connect(m_actionRotateRight, &QAction::triggered, this, &PDFViewerWidget::rotateRight);

    m_toolbar->addSeparator();

    // Page navigation
    m_pageLabel = new QLabel("Page:", this);
    m_pageLabel->setStyleSheet(QStringLiteral("QLabel{color:%1;font-weight:bold;margin:0 5px;}")
                                   .arg(dark ? "#e8eaed" : "#333"));
    m_toolbar->addWidget(m_pageLabel);

    m_pageInput = new QLineEdit(this);
    m_pageInput->setFixedWidth(60);
    m_pageInput->setFixedHeight(26);
    m_pageInput->setAlignment(Qt::AlignCenter);
    m_pageInput->setText("1");
    m_pageInput->setStyleSheet(
        QStringLiteral(
            "QLineEdit{border:1px solid %1;border-radius:3px;padding:2px 4px;background:%2;color:%3;}"
            "QLineEdit:focus{border-color:#1976d2;}"
        ).arg(dark ? "#5f6368" : "#ccc",
              dark ? "#2a2b2d" : "white",
              dark ? "#e8eaed" : "#111")
    );
    connect(m_pageInput, &QLineEdit::returnPressed, this, &PDFViewerWidget::onPageInputChanged);
    connect(m_pageInput, &QLineEdit::editingFinished, this, &PDFViewerWidget::onPageInputChanged);
    m_toolbar->addWidget(m_pageInput);

    m_totalPagesLabel = new QLabel("/ 0", this);
    m_totalPagesLabel->setStyleSheet(QStringLiteral("QLabel{color:%1;margin:0 5px;}")
                                         .arg(dark ? "#bdbdbd" : "#666"));
    m_toolbar->addWidget(m_totalPagesLabel);

    m_actionPreviousPage = m_toolbar->addAction(QIcon(":/icons/images/icons/previous.svg"), "");
    m_actionPreviousPage->setToolTip("Previous Page");
    connect(m_actionPreviousPage, &QAction::triggered, this, &PDFViewerWidget::previousPage);

    m_actionNextPage = m_toolbar->addAction(QIcon(":/icons/images/icons/next.svg"), "");
    m_actionNextPage->setToolTip("Next Page");
    connect(m_actionNextPage, &QAction::triggered, this, &PDFViewerWidget::nextPage);

    m_toolbar->addSeparator();

    // Zoom
    m_actionZoomIn = m_toolbar->addAction(QIcon(":/icons/images/icons/zoom_in.svg"), "");
    m_actionZoomIn->setToolTip("Zoom In");
    connect(m_actionZoomIn, &QAction::triggered, this, &PDFViewerWidget::zoomIn);

    m_actionZoomOut = m_toolbar->addAction(QIcon(":/icons/images/icons/zoom_out.svg"), "");
    m_actionZoomOut->setToolTip("Zoom Out");
    connect(m_actionZoomOut, &QAction::triggered, this, &PDFViewerWidget::zoomOut);

    m_toolbar->addSeparator();

    // Search
    m_searchLabel = new QLabel("Search:", this);
    m_searchLabel->setStyleSheet(QStringLiteral("QLabel{color:%1;font-weight:bold;margin:0 5px;}")
                                     .arg(dark ? "#e8eaed" : "#333"));
    m_toolbar->addWidget(m_searchLabel);

    m_searchInput = new QLineEdit(this);
    // Increased width for better usability
    m_searchInput->setFixedWidth(240);
    m_searchInput->setFixedHeight(26);
    m_searchInput->setPlaceholderText("Search text...");
    m_searchInput->setStyleSheet(
        QStringLiteral(
            "QLineEdit{border:1px solid %1;border-radius:3px;padding:2px 6px;background:%2;color:%3;}"
            "QLineEdit:focus{border-color:#1976d2;}"
        ).arg(dark ? "#5f6368" : "#ccc",
              dark ? "#2a2b2d" : "white",
              dark ? "#e8eaed" : "#111")
    );
    connect(m_searchInput, &QLineEdit::returnPressed, this, &PDFViewerWidget::onSearchReturnPressed);
    connect(m_searchInput, &QLineEdit::textChanged, this, &PDFViewerWidget::onSearchInputChanged);
    m_toolbar->addWidget(m_searchInput);

    m_actionFindPrevious = m_toolbar->addAction(QIcon(":/icons/images/icons/search_previous.svg"), "");
    m_actionFindPrevious->setToolTip("Find Previous");
    connect(m_actionFindPrevious, &QAction::triggered, this, &PDFViewerWidget::findPrevious);

    m_actionFindNext = m_toolbar->addAction(QIcon(":/icons/images/icons/search_next.svg"), "");
    m_actionFindNext->setToolTip("Find Next");
    connect(m_actionFindNext, &QAction::triggered, this, &PDFViewerWidget::findNext);

    // Whole-word search toggle
    m_actionWholeWord = m_toolbar->addAction(QIcon(":/icons/images/icons/whole_word.svg"), "");
    m_actionWholeWord->setToolTip("Whole word: Off");
    m_actionWholeWord->setCheckable(true);
    connect(m_actionWholeWord, &QAction::toggled, this, &PDFViewerWidget::toggleWholeWord);

    // Removed compact status area (page/zoom label) per request
    // Previously: trailing separator + m_statusInfoLabel showing "Pg x/y 35%"

    // Premium icon tinting on hover/checked
    installPremiumButtonStyling(m_toolbar, QColor(dark ? "#4f89d3" : "#1976d2"), dark);

    syncToolbarStates();
}

void PDFViewerWidget::applyToolbarTheme()
{
    if (!m_toolbar) return;
    const bool dark = false; // Force light theme
    const QString tbStyleLight =
    "QToolBar{background:#fafafa;border-bottom:1px solid #d0d0d0;min-height:30px;}"
    "QToolBar QToolButton{border:1px solid transparent;border-radius:6px;padding:4px;margin:2px;}"
        "QToolBar QToolButton:hover{background:rgba(25,118,210,0.12);border-color:rgba(25,118,210,0.35);}"
        "QToolBar QToolButton:pressed{background:rgba(25,118,210,0.20);border-color:#1976d2;}"
        "QToolBar QToolButton:checked{background:rgba(25,118,210,0.16);border-color:#1976d2;}"
        "QToolBar QToolButton:disabled{color:#9e9e9e;background:transparent;border-color:transparent;}"
        "QToolBar::separator{background:rgba(0,0,0,0.12);width:1px;margin:0 6px;}";
    const QString tbStyleDark =
    "QToolBar{background:#202124;border-bottom:1px solid #3c4043;min-height:30px;}"
    "QToolBar QToolButton{color:#e8eaed;border:1px solid transparent;border-radius:6px;padding:4px;margin:2px;}"
        "QToolBar QToolButton:hover{background:rgba(25,118,210,0.20);border-color:#4f89d3;}"
        "QToolBar QToolButton:pressed{background:rgba(25,118,210,0.30);border-color:#1976d2;}"
        "QToolBar QToolButton:checked{background:rgba(25,118,210,0.28);border-color:#4f89d3;color:#e8eaed;}"
        "QToolBar QToolButton:disabled{color:#9aa0a6;background:transparent;border-color:transparent;}"
        "QToolBar::separator{background:rgba(255,255,255,0.12);width:1px;margin:0 6px;}";
    m_toolbar->setStyleSheet(dark ? tbStyleDark : tbStyleLight);
    // Retint icons for new theme
    installPremiumButtonStyling(m_toolbar, QColor(dark ? "#4f89d3" : "#1976d2"), dark);
    // Update label and input styles to match theme
    if (m_pageLabel)
        m_pageLabel->setStyleSheet(QStringLiteral("QLabel{color:%1;font-weight:bold;margin:0 5px;}").arg(dark ? "#e8eaed" : "#333"));
    if (m_totalPagesLabel)
        m_totalPagesLabel->setStyleSheet(QStringLiteral("QLabel{color:%1;margin:0 5px;}").arg(dark ? "#bdbdbd" : "#666"));
    if (m_searchLabel)
        m_searchLabel->setStyleSheet(QStringLiteral("QLabel{color:%1;font-weight:bold;margin:0 5px;}").arg(dark ? "#e8eaed" : "#333"));
    if (m_statusInfoLabel)
        m_statusInfoLabel->setStyleSheet(QStringLiteral("QLabel#statusInfoLabel{color:%1;padding:0 4px;font:10pt 'Segoe UI';}").arg(dark ? "#e8eaed" : "#444"));
    if (m_pageInput)
        m_pageInput->setStyleSheet(QStringLiteral(
            "QLineEdit{border:1px solid %1;border-radius:3px;padding:2px 4px;background:%2;color:%3;}"
            "QLineEdit:focus{border-color:#1976d2;}" )
            .arg(dark ? "#5f6368" : "#ccc",
                 dark ? "#2a2b2d" : "white",
                 dark ? "#e8eaed" : "#111"));
    if (m_searchInput)
        m_searchInput->setStyleSheet(QStringLiteral(
            "QLineEdit{border:1px solid %1;border-radius:3px;padding:2px 6px;background:%2;color:%3;}"
            "QLineEdit:focus{border-color:#1976d2;}" )
            .arg(dark ? "#5f6368" : "#ccc",
                 dark ? "#2a2b2d" : "white",
                 dark ? "#e8eaed" : "#111"));
}

void PDFViewerWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::StyleChange) {
        applyToolbarTheme();
    }
    QWidget::changeEvent(event);
}

void PDFViewerWidget::setupViewerArea()
{
    m_viewerContainer = new QWidget(this);
    m_viewerContainer->setMinimumSize(400, 300);
    m_viewerContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_viewerContainer->setStyleSheet("QWidget{background:#ffffff;border:1px solid #cccccc;}");
    m_viewerContainer->installEventFilter(this);
    // Preview label overlay
    if (!m_previewLabel) {
        m_previewLabel = new QLabel(m_viewerContainer);
        m_previewLabel->setAlignment(Qt::AlignCenter);
        m_previewLabel->setStyleSheet("QLabel{background:#ffffff;}");
        m_previewLabel->hide();
    }
}

void PDFViewerWidget::setupIndividualToolbar(QToolBar*, bool)
{
    // No-op in single-pane mode
}

bool PDFViewerWidget::loadPDF(const QString &filePath)
{
    if (!QFileInfo::exists(filePath)) {
        emit errorOccurred(QString("PDF file does not exist: %1").arg(filePath));
        return false;
    }

    if (!m_viewerInitialized)
        initializePDFViewer();
    if (!m_viewerInitialized)
        return false;

    if (!m_pdfEmbedder->loadPDF(filePath.toStdString())) {
        emit errorOccurred(QString("Failed to load PDF: %1").arg(filePath));
        return false;
    }

    m_currentFilePath = filePath;
    m_pdfLoaded = true;
    emit pdfLoaded(filePath);
    emit pageChanged(getCurrentPage(), getPageCount());
    syncToolbarStates();
    updateStatusInfo();
    return true;
}

bool PDFViewerWidget::loadPDFFromMemory(const QString& memoryId, const QString& originalKey) {
    // Get the data from memory manager
    MemoryFileManager* memMgr = MemoryFileManager::instance();
    QByteArray data = memMgr->getFileData(memoryId);
    
    if (data.isEmpty()) {
        qWarning() << "PDFViewerWidget: memory data empty for" << memoryId << "key=" << originalKey;
        emit errorOccurred(QString("PDF data not found in memory for ID: %1").arg(memoryId));
        return false;
    }

    // Quick magic check for diagnostics
    if (data.size() >= 5) {
        const QByteArray head = data.left(8);
        qDebug() << "PDFViewerWidget: buffer size=" << data.size()
                 << " head(hex)=" << head.toHex(' ').constData()
                 << " looksPDF=" << data.startsWith("%PDF-");
    }

    if (!m_viewerInitialized)
        initializePDFViewer();
    if (!m_viewerInitialized)
        return false;

    QString displayName = originalKey.isEmpty() ? memoryId : originalKey;
    if (!m_pdfEmbedder->loadPDFFromMemory(data.constData(), data.size(), displayName.toStdString())) {
        emit errorOccurred(QString("Failed to load PDF from memory: %1").arg(displayName));
        return false;
    }

    m_currentFilePath = memoryId; // Store memory ID as current file path
    m_pdfLoaded = true;
    emit pdfLoaded(displayName);
    emit pageChanged(getCurrentPage(), getPageCount());
    syncToolbarStates();
    
    if (m_updateTimer && !m_updateTimer->isActive()) {
        m_updateTimer->start();
    }
    
    return true;
}

void PDFViewerWidget::requestLoad(const QString &filePath)
{
    cancelLoad();
    m_pendingFilePath = filePath;
    m_cancelRequested = false;
    ++m_currentLoadId;
    int loadId = m_currentLoadId;
    if (m_loadingOverlay) m_loadingOverlay->showOverlay(QString("Loading %1...").arg(QFileInfo(filePath).fileName()));

    if (!m_previewWatcher) {
        m_previewWatcher = new QFutureWatcher<PDFPreviewResult>(this);
        connect(m_previewWatcher, &QFutureWatcher<PDFPreviewResult>::finished, this, [this]() {
            PDFPreviewResult res = m_previewWatcher->result();
            int id = m_previewWatcher->property("loadId").toInt();
            if (m_cancelRequested || id != m_currentLoadId) {
                if (m_loadingOverlay) m_loadingOverlay->hideOverlay();
                emit loadCancelled();
                return;
            }
            if (!res.success) {
                if (m_loadingOverlay) m_loadingOverlay->hideOverlay();
                emit errorOccurred(res.error.isEmpty() ? QString("Failed to load preview: %1").arg(m_pendingFilePath) : res.error);
                return;
            }
            if (m_previewLabel) {
                QPixmap pm = QPixmap::fromImage(res.firstPage);
                if (!pm.isNull()) {
                    m_previewLabel->setPixmap(pm.scaled(m_viewerContainer->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    m_previewLabel->show();
                }
            }
            emit firstPreviewReady(res.firstPage);
            if (m_loadingOverlay) m_loadingOverlay->setMessage("Optimizing viewer...");
            QTimer::singleShot(0, this, [this, id]() {
                if (m_cancelRequested || id != m_currentLoadId) return;
                bool loaded = loadPDF(m_pendingFilePath);
                if (m_loadingOverlay) m_loadingOverlay->hideOverlay();
                if (!loaded) {
                    emit errorOccurred(QString("Failed to load %1").arg(m_pendingFilePath));
                } else {
                    if (m_previewLabel) m_previewLabel->hide();
                }
            });
        });
    }
    QFuture<PDFPreviewResult> fut = QtConcurrent::run(LoadPdfFirstPagePreview, filePath, 1024);
    m_previewWatcher->setProperty("loadId", loadId);
    m_previewWatcher->setFuture(fut);
}

void PDFViewerWidget::cancelLoad()
{
    if (m_previewWatcher && m_previewWatcher->isRunning()) m_cancelRequested = true;
    if (m_loadWatcher && m_loadWatcher->isRunning()) m_cancelRequested = true;
    if (m_loadingOverlay && m_loadingOverlay->isVisible()) m_loadingOverlay->hideOverlay();
    if (m_previewLabel && m_previewLabel->isVisible()) m_previewLabel->hide();
}

void PDFViewerWidget::initializePDFViewer()
{
    // Prevent concurrent initialization across multiple widgets. Ensure the flag
    // is ALWAYS cleared even on early returns (RAII-style guard below).
    static bool globalInitInProgress = false;
    if (m_viewerInitialized)
        return;
    if (globalInitInProgress) { // retry shortly if another instance is initializing
        QTimer::singleShot(50, this, [this]() { if (!m_viewerInitialized) initializePDFViewer(); });
        return;
    }
    if (!m_viewerContainer) {
        qWarning() << "PDFViewerWidget: initializePDFViewer() aborted - no viewer container";
        return;
    }
    if (m_viewerContainer->width() <= 0 || m_viewerContainer->height() <= 0) {
        // Defer until we have a valid size
        QTimer::singleShot(50, this, [this]() { initializePDFViewer(); });
        return;
    }

    globalInitInProgress = true;
    struct FlagReset { bool &f; ~FlagReset(){ f = false; } } _{ globalInitInProgress };
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(m_viewerContainer->winId());
#else
    void* hwnd = m_viewerContainer->winId();
#endif
    int w = m_viewerContainer->width();
    int h = m_viewerContainer->height();
    if (w <= 0 || h <= 0) {
        qWarning() << "PDFViewerWidget: initializePDFViewer() invalid size (" << w << "x" << h << ") - deferring";
        QTimer::singleShot(50, this, [this]() { initializePDFViewer(); });
        return;
    }
    if (!m_pdfEmbedder || !m_pdfEmbedder->initialize(hwnd, w, h)) {
        qWarning() << "PDFViewerWidget: initializePDFViewer() - embedder init failed (hwnd, w, h) ="
                   << (void*)hwnd << w << h;
        emit errorOccurred("Failed to initialize PDF viewer");
        // Do NOT leave the guard set; it will be cleared by FlagReset dtor.
        // Optionally, retry once after a brief delay in case of transient GL context issues.
        QTimer::singleShot(120, this, [this]() {
            if (!m_viewerInitialized) initializePDFViewer();
        });
        return;
    }

    // Register quick right-click callback for context menu (fallback when native events bypass Qt)
    m_pdfEmbedder->setQuickRightClickCallback([this](const std::string &sel){
        if (!m_crossSearchEnabled) return;
        QString text = QString::fromStdString(sel).trimmed();
        if (text.isEmpty()) return;
        QMetaObject::invokeMethod(this, [this, text]() {
            showCrossContextMenu(QCursor::pos(), text);
        }, Qt::QueuedConnection);
    });

    m_viewerInitialized = true;
    if (m_updateTimer && !m_updateTimer->isActive()) m_updateTimer->start();
}

bool PDFViewerWidget::isPDFLoaded() const
{
    return m_pdfLoaded && m_pdfEmbedder && m_pdfEmbedder->isPDFLoaded();
}

int PDFViewerWidget::getPageCount() const
{
    return isPDFLoaded() ? m_pdfEmbedder->getPageCount() : 0;
}

double PDFViewerWidget::getCurrentZoom() const
{
    return isPDFLoaded() ? m_pdfEmbedder->getCurrentZoom() : 1.0;
}

int PDFViewerWidget::getCurrentPage() const
{
    return isPDFLoaded() ? m_pdfEmbedder->getCurrentPage() : 1;
}

bool PDFViewerWidget::isReady() const
{
    return m_viewerInitialized && isPDFLoaded();
}

void PDFViewerWidget::syncToolbarStates()
{
    if (!m_toolbar)
        return;

    if (!isPDFLoaded()) {
        for (QAction *a : { m_actionPreviousPage, m_actionNextPage, m_actionZoomIn, m_actionZoomOut, m_actionFindPrevious, m_actionFindNext }) {
            if (a) a->setEnabled(false);
        }
        if (m_totalPagesLabel)
            m_totalPagesLabel->setText("/ 0");
        return;
    }

    int cur = getCurrentPage();
    int total = getPageCount();

    if (m_actionPreviousPage) m_actionPreviousPage->setEnabled(cur > 1);
    if (m_actionNextPage)     m_actionNextPage->setEnabled(cur < total);
    if (m_actionZoomIn)       m_actionZoomIn->setEnabled(true);
    if (m_actionZoomOut)      m_actionZoomOut->setEnabled(true);

    bool hasSearch = m_searchInput && !m_searchInput->text().trimmed().isEmpty();
    if (m_actionFindNext)     m_actionFindNext->setEnabled(hasSearch);
    if (m_actionFindPrevious) m_actionFindPrevious->setEnabled(hasSearch);
    if (m_actionWholeWord)    m_actionWholeWord->setEnabled(true);
    if (m_totalPagesLabel)    m_totalPagesLabel->setText(QString("/ %1").arg(total));
}

void PDFViewerWidget::updatePageInputSafely(int currentPage)
{
    if (!m_pageInput) return;
    // Don't overwrite while user is actively typing (has focus and navigation not programmatic)
    bool userTyping = m_pageInput->hasFocus() && !m_navigationInProgress;
    QString want = QString::number(currentPage);
    if (!userTyping && m_pageInput->text() != want) {
        m_pageInput->setText(want);
    }
}

void PDFViewerWidget::updateViewer()
{
    if (m_pdfEmbedder && m_viewerInitialized) {
        m_pdfEmbedder->update();
        if (isPDFLoaded()) {
            int cur = getCurrentPage();
            double z = getCurrentZoom();
            // Only emit if changed to reduce signal noise
            if (cur != m_lastKnownPage) {
                updatePageInputSafely(cur);
                emit pageChanged(cur, getPageCount());
                m_lastKnownPage = cur;
                syncToolbarStates();
                updateStatusInfo();
            }
            if (std::abs(z - m_lastKnownZoom) > 1e-6) {
                emit zoomChanged(z);
                m_lastKnownZoom = z;
                updateStatusInfo();
            }
        }
    }
}

void PDFViewerWidget::onPageInputChanged()
{
    if (!m_pageInput)
        return;

    bool ok = false;
    int page = m_pageInput->text().trimmed().toInt(&ok);
    if (ok && page >= 1 && page <= getPageCount()) {
        m_navigationInProgress = true;
        m_navigationTimer->start();
        if (m_pdfEmbedder)
            m_pdfEmbedder->goToPage(page);
    } else {
        m_pageInput->setText(QString::number(getCurrentPage()));
    }
    m_pageInput->clearFocus();
    updatePageInputSafely(getCurrentPage());
    syncToolbarStates();
}

void PDFViewerWidget::onSearchInputChanged()
{
    if (!m_searchInput)
        return;
    QString term = m_searchInput->text().trimmed();
    if (term.isEmpty()) {
        if (isPDFLoaded() && m_pdfEmbedder)
            m_pdfEmbedder->clearSelection();
        m_lastSearchTerm.clear();
    } else {
        scheduleDebouncedSearch();
    }
    syncToolbarStates();
    updateStatusInfo();
}

void PDFViewerWidget::onSearchReturnPressed()
{
    if (!m_searchInput)
        return;
    const QString term = m_searchInput->text().trimmed();
    if (term.isEmpty()) {
        if (isPDFLoaded() && m_pdfEmbedder) m_pdfEmbedder->clearSelection();
        m_lastSearchTerm.clear();
        syncToolbarStates();
        updateStatusInfo();
        return;
    }
    // If the user pressed Enter and the term hasn't changed since last executed search,
    // treat it as "Find Next". Otherwise, run a fresh search immediately.
    if (term == m_lastSearchTerm) {
        findNext();
    } else {
        m_lastSearchTerm = term;
        if (isPDFLoaded() && m_pdfEmbedder)
            m_pdfEmbedder->findText(term.toStdString());
        syncToolbarStates();
        updateStatusInfo();
    }
}

void PDFViewerWidget::checkForSelectedText()
{
    if (!isPDFLoaded() || !m_pdfEmbedder || !m_searchInput)
        return;
    std::string selStd = m_pdfEmbedder->getSelectedText();
    QString sel = QString::fromStdString(selStd);
    if (!sel.isEmpty() && sel != m_lastSelectedText) {
        m_lastSelectedText = sel;
        // Only auto-update search input if it's not already showing this text
        // This prevents interference with external/cross-searches
        if (m_searchInput->text().trimmed() != sel) {
            bool old = m_searchInput->blockSignals(true);
            m_searchInput->setText(sel);
            m_searchInput->blockSignals(old);
            // Only search if this isn't already our last search term
            if (sel != m_lastSearchTerm) {
                m_pdfEmbedder->findText(selStd);
                m_lastSearchTerm = sel;
            }
        }
        // Enable next/previous search buttons now that we have selected text
        syncToolbarStates();
    } else if (sel.isEmpty()) {
        m_lastSelectedText.clear();
        // Disable next/previous search buttons when no text is selected
        syncToolbarStates();
    }
}

void PDFViewerWidget::goToPage(int pageNumber)
{
    if (!isPDFLoaded() || !m_pdfEmbedder)
        return;
    int total = getPageCount();
    if (pageNumber < 1 || pageNumber > total)
        return;
    m_navigationInProgress = true;
    m_navigationTimer->start();
    m_pdfEmbedder->goToPage(pageNumber);
    updatePageInputSafely(pageNumber);
    syncToolbarStates();
    updateStatusInfo();
}

void PDFViewerWidget::nextPage()
{
    if (isPDFLoaded() && m_pdfEmbedder && getCurrentPage() < getPageCount()) {
        m_navigationInProgress = true;
        m_navigationTimer->start();
        m_pdfEmbedder->nextPage();
        syncToolbarStates();
    updateStatusInfo();
    }
}

void PDFViewerWidget::previousPage()
{
    if (isPDFLoaded() && m_pdfEmbedder && getCurrentPage() > 1) {
        m_navigationInProgress = true;
        m_navigationTimer->start();
        m_pdfEmbedder->previousPage();
        syncToolbarStates();
    updateStatusInfo();
    }
}

void PDFViewerWidget::zoomIn()
{
    if (isPDFLoaded() && m_pdfEmbedder) {
        m_pdfEmbedder->zoomIn();
        syncToolbarStates();
    updateStatusInfo();
    }
}

void PDFViewerWidget::zoomOut()
{
    if (isPDFLoaded() && m_pdfEmbedder) {
        m_pdfEmbedder->zoomOut();
        syncToolbarStates();
    updateStatusInfo();
    }
}

void PDFViewerWidget::rotateLeft()
{
    if (isPDFLoaded() && m_pdfEmbedder)
        m_pdfEmbedder->rotateLeft();
    updateStatusInfo();
}

void PDFViewerWidget::rotateRight()
{
    if (isPDFLoaded() && m_pdfEmbedder)
        m_pdfEmbedder->rotateRight();
    updateStatusInfo();
}

void PDFViewerWidget::searchText()
{
    if (isPDFLoaded() && m_pdfEmbedder && m_searchInput) {
        QString term = m_searchInput->text().trimmed();
        if (!term.isEmpty()) {
            // Apply current whole-word mode before searching
            if (m_pdfEmbedder) m_pdfEmbedder->setWholeWordSearch(m_wholeWordEnabled);
            m_pdfEmbedder->findText(term.toStdString());
        }
    }
}

void PDFViewerWidget::toggleWholeWord(bool checked)
{
    m_wholeWordEnabled = checked;
    if (m_actionWholeWord)
        m_actionWholeWord->setToolTip(checked ? "Whole word: On" : "Whole word: Off");
    if (m_pdfEmbedder)
        m_pdfEmbedder->setWholeWordSearch(checked);
    // If there's an active term, rerun search to update results
    if (m_searchInput) {
        QString term = m_searchInput->text().trimmed();
        if (!term.isEmpty() && m_pdfEmbedder) {
            m_pdfEmbedder->findText(term.toStdString());
        }
    }
    syncToolbarStates();
}

void PDFViewerWidget::findNext()
{
    if (isPDFLoaded() && m_pdfEmbedder)
        m_pdfEmbedder->findNext();
    updateStatusInfo();
}

void PDFViewerWidget::findPrevious()
{
    if (isPDFLoaded() && m_pdfEmbedder)
        m_pdfEmbedder->findPrevious();
    updateStatusInfo();
}

void PDFViewerWidget::ensureViewportSync()
{
    if (!isVisible()) return; // avoid zero-size/hidden resizes
    if (m_pdfEmbedder && m_viewerInitialized && m_viewerContainer) {
        const int w = m_viewerContainer->width();
        const int h = m_viewerContainer->height();
        if (w > 0 && h > 0) {
            m_pdfEmbedder->resize(w, h);
        }
    }
}

void PDFViewerWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    ensureViewportSync();
}

void PDFViewerWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_viewerInitialized && isVisible() && width() > 0 && height() > 0) {
        QTimer::singleShot(10, this, [this]() {
            if (!m_viewerInitialized && isVisible())
                initializePDFViewer();
        });
    }
    // Resume updates
    if (m_updateTimer && !m_updateTimer->isActive()) m_updateTimer->start();
    // When switching back to PDF tab, ensure the embedder claims active context
    // and schedules a high-quality refresh so pages are crisp, not scaled.
    if (m_pdfEmbedder && m_viewerInitialized && isPDFLoaded()) {
        // Small delay to allow layout to settle, then refresh crisply
        QTimer::singleShot(30, this, [this]() {
            if (m_pdfEmbedder && isVisible() && isPDFLoaded()) {
                m_pdfEmbedder->activateForCrossSearchAndRefresh(true);
            }
        });
    }
    // Restore last view state if we have one - with proper timing
    // BUT: Skip restoration if we're in the middle of a cross-search operation
    if (m_pdfEmbedder && m_viewerInitialized && m_lastViewState.valid) {
        // Use a timer to ensure the viewer is fully ready before restoring state
        QTimer::singleShot(50, this, [this]() {
            // Double-check that state is still valid and not cleared by external search
            if (m_pdfEmbedder && m_viewerInitialized && m_lastViewState.valid) {
                PDFViewerEmbedder::ViewState s;
                s.zoom = m_lastViewState.zoom;
                s.scrollOffset = m_lastViewState.scrollOffset;
                s.horizontalOffset = m_lastViewState.horizontalOffset;
                s.page = m_lastViewState.page;
                s.valid = m_lastViewState.valid;
                qDebug() << "Restoring PDF view state - zoom:" << s.zoom << "page:" << s.page;
                m_pdfEmbedder->restoreViewState(s);
                // After restoration, schedule a crisp refresh to avoid stale preview textures
                m_pdfEmbedder->activateForCrossSearchAndRefresh(true);
                // Clear to avoid reapplying repeatedly
                m_lastViewState = {};
            }
        });
    }
}

void PDFViewerWidget::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    // Capture current view and pause heavy updates when hidden
    if (m_pdfEmbedder && m_viewerInitialized) {
        auto s = m_pdfEmbedder->captureViewState();
        if (s.valid) {
            m_lastViewState.zoom = s.zoom;
            m_lastViewState.scrollOffset = s.scrollOffset;
            m_lastViewState.horizontalOffset = s.horizontalOffset;
            m_lastViewState.page = s.page;
            m_lastViewState.valid = s.valid;
            qDebug() << "Capturing PDF view state - zoom:" << s.zoom << "page:" << s.page;
        }
    }
    if (m_updateTimer && m_updateTimer->isActive()) m_updateTimer->stop();
}

void PDFViewerWidget::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
}

bool PDFViewerWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_viewerContainer) {
        if (event->type() == QEvent::MouseButtonPress) {
            if (m_pageInput && m_pageInput->hasFocus()) m_pageInput->clearFocus();
            if (m_searchInput && m_searchInput->hasFocus()) m_searchInput->clearFocus();
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::RightButton) {
                m_rightPressPos = me->pos();
                m_rightPressTimeMs = QDateTime::currentMSecsSinceEpoch();
                m_rightDragging = false;
            }
        } else if (event->type() == QEvent::MouseMove) {
            if (m_rightPressTimeMs > 0) {
                QMouseEvent *me = static_cast<QMouseEvent*>(event);
                if ((me->pos() - m_rightPressPos).manhattanLength() > 6) {
                    m_rightDragging = true; // treat as pan
                }
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::RightButton && m_crossSearchEnabled) {
                qint64 dt = QDateTime::currentMSecsSinceEpoch() - m_rightPressTimeMs;
                if (!m_rightDragging && dt < 350) {
                    // Quick click -> context menu
                    QString sel = captureCurrentSelection();
                    if (!sel.isEmpty()) {
                        showCrossContextMenu(me->globalPosition().toPoint(), sel);
                        // Prevent further handling (optional) but allow existing panning to stop
                        return true;
                    }
                }
            }
            if (me->button() == Qt::RightButton) {
                m_rightPressTimeMs = 0;
                m_rightDragging = false;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PDFViewerWidget::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
}

// --- New helper methods ---
void PDFViewerWidget::updateStatusInfo()
{
    if (!m_statusInfoLabel) return;
    if (!isPDFLoaded()) {
        if (m_statusInfoLabel->text() != "No PDF")
            m_statusInfoLabel->setText("No PDF");
        return;
    }
    int cur = getCurrentPage();
    int total = getPageCount();
    double zoomPct = getCurrentZoom() * 100.0;
    // Choose precision: below 100% show one decimal if needed
    QString zoomStr = (zoomPct < 100.0 && std::abs(zoomPct - std::round(zoomPct)) > 0.05)
        ? QString::number(zoomPct, 'f', 1)
        : QString::number(std::round(zoomPct));
    QString composed = QString("Pg %1/%2  %3%")
        .arg(cur)
        .arg(total)
        .arg(zoomStr);
    if (m_statusInfoLabel->text() != composed)
        m_statusInfoLabel->setText(composed);
}

void PDFViewerWidget::scheduleDebouncedSearch()
{
    if (!m_searchDebounceTimer)
        return;
    m_searchDebounceTimer->start();
}

QString PDFViewerWidget::captureCurrentSelection() const {
    if (m_pdfEmbedder) {
        std::string s = m_pdfEmbedder->getSelectedText();
        return QString::fromStdString(s).trimmed();
    }
    return {};
}

void PDFViewerWidget::showCrossContextMenu(const QPoint &globalPos, const QString &text) {
    QString target = m_linkedPcbFileName.isEmpty() ? QStringLiteral("Linked PCB") : m_linkedPcbFileName;
    class ThemedMenu : public QMenu { public: ThemedMenu(QWidget* p=nullptr):QMenu(p) { setWindowFlags(windowFlags()|Qt::NoDropShadowWindowHint); setAttribute(Qt::WA_TranslucentBackground);} void apply(bool dark){ if(dark){ setStyleSheet(
                "QMenu { background:rgba(30,33,40,0.94); border:1px solid #3d4452; border-radius:8px; padding:6px; font:13px 'Segoe UI'; color:#dfe3ea; }"
                "QMenu::item { background:transparent; padding:6px 14px; border-radius:5px; }"
                "QMenu::item:selected { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #2563eb, stop:1 #1d4ed8); color:white; }"
                "QMenu::separator { height:1px; background:#465061; margin:6px 4px; }" ); }
            else { setStyleSheet(
                "QMenu { background:rgba(252,252,253,0.97); border:1px solid #d0d7e2; border-radius:8px; padding:6px; font:13px 'Segoe UI'; color:#2d3744; }"
                "QMenu::item { background:transparent; padding:6px 14px; border-radius:5px; }"
                "QMenu::item:selected { background: #1a73e8; color:white; }"
                "QMenu::separator { height:1px; background:#e1e6ed; margin:6px 4px; }" ); } } };
    ThemedMenu menu;
    bool dark = false; // Force light theme
    menu.apply(dark);
    QAction *actCopy = menu.addAction(QIcon(":/icons/images/icons/copy.svg"), "Copy");
    QAction *actPaste = menu.addAction(QIcon(":/icons/images/icons/paste.svg"), "Paste");
    menu.addSeparator();
    menu.addAction(QIcon(":/icons/images/icons/find_component.svg"), QString("Find Component in %1").arg(target));
    QAction *actNet  = menu.addAction(QIcon(":/icons/images/icons/find_net.svg"), QString("Find Net in %1").arg(target));
    menu.addSeparator();
    QAction *actCancel = menu.addAction("Cancel");
    QAction *chosen = menu.exec(globalPos);
    if (!chosen || chosen==actCancel) return;
    if (chosen == actCopy) {
        QClipboard *clipboard = QGuiApplication::clipboard();
        clipboard->setText(text);
        return;
    }
    if (chosen == actPaste) {
        QClipboard *clipboard = QGuiApplication::clipboard();
        QString pasteText = clipboard->text();
        if (!pasteText.isEmpty()) {
            emit crossSearchRequest(pasteText, false, true);
        }
        return;
    }
    bool isNet = (chosen == actNet);
    emit crossSearchRequest(text, isNet, true);
}

bool PDFViewerWidget::externalFindText(const QString &term) {
    if (!isPDFLoaded() || !m_pdfEmbedder) return false;
    QString t = term.trimmed();
    if (t.isEmpty()) return false;
    
    // CRITICAL: Clear any pending view state restoration that could interfere
    m_lastViewState = {};  // Clear saved state to prevent restoration conflicts
    
    // Stop any pending debounced searches that might interfere
    if (m_searchDebounceTimer && m_searchDebounceTimer->isActive()) {
        m_searchDebounceTimer->stop();
    }
    
    // Pause update timer during cross-search to prevent cursor interference
    bool wasUpdateTimerActive = m_updateTimer && m_updateTimer->isActive();
    if (wasUpdateTimerActive) {
        m_updateTimer->stop();
    }
    
    // Clear previous highlights/state and search term tracking
    m_pdfEmbedder->clearSearchHighlights();
    m_lastSearchTerm.clear();  // Reset to avoid confusion with internal searches
    
    // Update search input to reflect the external search term (for UI consistency)
    if (m_searchInput) {
        m_searchInput->blockSignals(true);  // Prevent triggering internal search
        m_searchInput->setText(t);
        m_searchInput->blockSignals(false);
    }
    
    // Perform the optimized cross-search (deferred regeneration for performance)
    bool ok = m_pdfEmbedder->findTextFreshAndFocusFirstOptimized(t.toStdString());
    if (ok) {
        // Update our tracking to match the successful external search
        m_lastSearchTerm = t;
        // Force UI updates
        syncToolbarStates();
        updateStatusInfo();
    // Ensure the embedder activates and refreshes visible pages crisply after switch
    // This fixes blurry/old textures when jumping from PCB to PDF.
    m_pdfEmbedder->activateForCrossSearchAndRefresh(true);
        
        // Resume update timer after successful navigation with slight delay
        if (wasUpdateTimerActive) {
            QTimer::singleShot(150, this, [this]() {
                if (m_updateTimer && !m_updateTimer->isActive()) {
                    m_updateTimer->start();
                }
            });
        }
    } else {
        // No matches: ensure no stale highlights remain
        m_pdfEmbedder->clearSearchHighlights();
        if (m_searchInput) {
            m_searchInput->blockSignals(true);
            m_searchInput->clear();  // Clear the search box if no results
            m_searchInput->blockSignals(false);
        }
        m_lastSearchTerm.clear();
        emit errorOccurred(QString("No matches found for '%1'").arg(t));
        
        // Resume update timer immediately if search failed
        if (wasUpdateTimerActive && m_updateTimer && !m_updateTimer->isActive()) {
            m_updateTimer->start();
        }
    }
    return ok;
}

void PDFViewerWidget::saveViewState()
{
    if (m_pdfEmbedder && m_viewerInitialized) {
        auto s = m_pdfEmbedder->captureViewState();
        if (s.valid) {
            m_lastViewState.zoom = s.zoom;
            m_lastViewState.scrollOffset = s.scrollOffset;
            m_lastViewState.horizontalOffset = s.horizontalOffset;
            m_lastViewState.page = s.page;
            m_lastViewState.valid = s.valid;
            qDebug() << "Explicitly saving PDF view state - zoom:" << s.zoom << "page:" << s.page;
        }
    }
}

void PDFViewerWidget::restoreViewState()
{
    if (m_pdfEmbedder && m_viewerInitialized && m_lastViewState.valid) {
        // Use a timer to ensure the viewer is fully ready before restoring state
        QTimer::singleShot(100, this, [this]() {
            if (m_pdfEmbedder && m_viewerInitialized && m_lastViewState.valid) {
                PDFViewerEmbedder::ViewState s;
                s.zoom = m_lastViewState.zoom;
                s.scrollOffset = m_lastViewState.scrollOffset;
                s.horizontalOffset = m_lastViewState.horizontalOffset;
                s.page = m_lastViewState.page;
                s.valid = m_lastViewState.valid;
                qDebug() << "Explicitly restoring PDF view state - zoom:" << s.zoom << "page:" << s.page;
                m_pdfEmbedder->restoreViewState(s);
                // Don't clear the state here - let it persist until next save
            }
        });
    }
}
