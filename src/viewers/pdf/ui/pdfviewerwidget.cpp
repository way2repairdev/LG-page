// Clean single-pane PDFViewerWidget implementation (split view removed)
#include "viewers/pdf/pdfviewerwidget.h"
#include "viewers/pdf/PDFViewerEmbedder.h"
#include "ui/LoadingOverlay.h"

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
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QElapsedTimer>
#include "viewers/pdf/PDFPreviewLoader.h"
#ifdef _WIN32
#include <windows.h>
#endif

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
    m_toolbar->setStyleSheet("QToolBar{background:#fafafa;border-bottom:1px solid #d0d0d0;}");

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
    m_pageLabel->setStyleSheet("QLabel{color:#333;font-weight:bold;margin:0 5px;}");
    m_toolbar->addWidget(m_pageLabel);

    m_pageInput = new QLineEdit(this);
    m_pageInput->setFixedWidth(60);
    m_pageInput->setAlignment(Qt::AlignCenter);
    m_pageInput->setText("1");
    m_pageInput->setStyleSheet("QLineEdit{border:1px solid #ccc;border-radius:3px;padding:2px 4px;background:white;}QLineEdit:focus{border-color:#4285f4;}");
    connect(m_pageInput, &QLineEdit::returnPressed, this, &PDFViewerWidget::onPageInputChanged);
    connect(m_pageInput, &QLineEdit::editingFinished, this, &PDFViewerWidget::onPageInputChanged);
    m_toolbar->addWidget(m_pageInput);

    m_totalPagesLabel = new QLabel("/ 0", this);
    m_totalPagesLabel->setStyleSheet("QLabel{color:#666;margin:0 5px;}");
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
    m_searchLabel->setStyleSheet("QLabel{color:#333;font-weight:bold;margin:0 5px;}");
    m_toolbar->addWidget(m_searchLabel);

    m_searchInput = new QLineEdit(this);
    m_searchInput->setFixedWidth(140);
    m_searchInput->setPlaceholderText("Search text...");
    m_searchInput->setStyleSheet("QLineEdit{border:1px solid #ccc;border-radius:3px;padding:2px 6px;background:white;}QLineEdit:focus{border-color:#4285f4;}");
    connect(m_searchInput, &QLineEdit::returnPressed, this, &PDFViewerWidget::onSearchInputChanged);
    connect(m_searchInput, &QLineEdit::textChanged, this, &PDFViewerWidget::onSearchInputChanged);
    m_toolbar->addWidget(m_searchInput);

    m_actionFindPrevious = m_toolbar->addAction(QIcon(":/icons/images/icons/search_previous.svg"), "");
    m_actionFindPrevious->setToolTip("Find Previous");
    connect(m_actionFindPrevious, &QAction::triggered, this, &PDFViewerWidget::findPrevious);

    m_actionFindNext = m_toolbar->addAction(QIcon(":/icons/images/icons/search_next.svg"), "");
    m_actionFindNext->setToolTip("Find Next");
    connect(m_actionFindNext, &QAction::triggered, this, &PDFViewerWidget::findNext);

    // Compact status area
    m_toolbar->addSeparator();
    m_statusInfoLabel = new QLabel("No PDF", this);
    m_statusInfoLabel->setObjectName("statusInfoLabel");
    m_statusInfoLabel->setMinimumWidth(110);
    m_statusInfoLabel->setStyleSheet("QLabel#statusInfoLabel{color:#444;padding:0 4px;font:10pt 'Segoe UI';}");
    m_toolbar->addWidget(m_statusInfoLabel);

    syncToolbarStates();
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
    static bool globalInitInProgress = false;
    if (m_viewerInitialized)
        return;
    if (globalInitInProgress) { // retry shortly if another instance is initializing
        QTimer::singleShot(50, this, [this]() { if (!m_viewerInitialized) initializePDFViewer(); });
        return;
    }
    if (!m_viewerContainer)
        return;
    if (m_viewerContainer->width() <= 0 || m_viewerContainer->height() <= 0) {
        QTimer::singleShot(50, this, [this]() { initializePDFViewer(); });
        return;
    }

    globalInitInProgress = true;
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(m_viewerContainer->winId());
#else
    void* hwnd = m_viewerContainer->winId();
#endif
    if (!m_pdfEmbedder->initialize(hwnd, m_viewerContainer->width(), m_viewerContainer->height())) {
        emit errorOccurred("Failed to initialize PDF rendering engine");
        globalInitInProgress = false;
        return;
    }
    m_viewerInitialized = true;
    m_updateTimer->start();
    globalInitInProgress = false;
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

void PDFViewerWidget::checkForSelectedText()
{
    if (!isPDFLoaded() || !m_pdfEmbedder || !m_searchInput)
        return;
    std::string selStd = m_pdfEmbedder->getSelectedText();
    QString sel = QString::fromStdString(selStd);
    if (!sel.isEmpty() && sel != m_lastSelectedText) {
        m_lastSelectedText = sel;
        bool old = m_searchInput->blockSignals(true);
        m_searchInput->setText(sel);
        m_searchInput->blockSignals(old);
        m_pdfEmbedder->findText(selStd);
    } else if (sel.isEmpty()) {
        m_lastSelectedText.clear();
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
        if (!term.isEmpty())
            m_pdfEmbedder->findText(term.toStdString());
    }
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
    if (m_pdfEmbedder && m_viewerInitialized && m_viewerContainer)
        m_pdfEmbedder->resize(m_viewerContainer->width(), m_viewerContainer->height());
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
}

void PDFViewerWidget::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
}

void PDFViewerWidget::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
}

bool PDFViewerWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_viewerContainer && event->type() == QEvent::MouseButtonPress) {
        if (m_pageInput && m_pageInput->hasFocus())
            m_pageInput->clearFocus();
        if (m_searchInput && m_searchInput->hasFocus())
            m_searchInput->clearFocus();
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
