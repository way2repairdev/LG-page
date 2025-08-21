#ifndef PDFVIEWERWIDGET_H
#define PDFVIEWERWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QLineEdit>
#include <QLabel>
#include <QEvent>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include "viewers/pdf/PDFPreviewLoader.h"
#include <memory>

class PDFViewerEmbedder;
// PDFPreviewResult defined in PDFPreviewLoader.h

/**
 * PDFViewerWidget - High-performance PDF viewer widget using embedded native renderer
 * 
 * This widget integrates your existing standalone PDF viewer (built with OpenGL, GLFW, PDFium)
 * into a Qt tabbed application. It preserves all advanced features like:
 * - High-performance rendering with background processing
 * - Smooth zooming and panning
 * - Text selection and search
 * - Scroll bars and navigation
 * 
 * The widget embeds the native Windows OpenGL renderer as a child window,
 * providing Qt-based controls while maintaining full rendering performance.
 */
class PDFViewerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PDFViewerWidget(QWidget *parent = nullptr);
    ~PDFViewerWidget();

    /**
     * Load a PDF file into the viewer
     * @param filePath Path to the PDF file
     * @return true if loaded successfully
     */
    bool loadPDF(const QString& filePath);
    // Asynchronous Phase 1 wrapper (non-blocking pre-read + deferred real load)
    void requestLoad(const QString &filePath);
    void cancelLoad();

    // Split view functionality removed

    /**
     * Check if a PDF is currently loaded
     */
    bool isPDFLoaded() const;

    // Right panel PDF state removed

    /**
     * Get the current page count
     */
    int getPageCount() const;

    /**
     * Get current zoom level (1.0 = 100%)
     */
    double getCurrentZoom() const;

    /**
     * Get current page number (1-based)
     */
    int getCurrentPage() const;

    /**
     * Check if the viewer is ready for interaction
     */
    bool isReady() const;

    // Cross-viewer linking helpers
    void setLinkedPcbFileName(const QString &name) { m_linkedPcbFileName = name; }
    QString linkedPcbFileName() const { return m_linkedPcbFileName; }
    void setCrossSearchEnabled(bool enabled) { m_crossSearchEnabled = enabled; }

public slots:
    // External search invoked from PCB viewer
    bool externalFindText(const QString &term);

signals:
    // Cross-search request (PDF -> PCB)
    void crossSearchRequest(const QString &term, bool isNet, bool targetIsPcb);

public slots:
    // Navigation controls
    void goToPage(int pageNumber);
    void nextPage();
    void previousPage();
    
    // Zoom controls
    void zoomIn();
    void zoomOut();
    
    // Rotation controls
    void rotateLeft();
    void rotateRight();
    
    // Search controls
    void searchText();
    void findNext();
    void findPrevious();

    // Ensure viewport and renderer are synced after activation/tab switch
    void ensureViewportSync();

signals:
    // Emitted when PDF is successfully loaded
    void pdfLoaded(const QString& filePath);
    void loadCancelled();
    void firstPreviewReady(const QImage &image); // placeholder for progressive loading
    
    // Emitted when there's an error loading or rendering
    void errorOccurred(const QString& error);
    
    // Emitted when the current page changes
    void pageChanged(int currentPage, int totalPages);
    
    // Emitted when zoom level changes
    void zoomChanged(double zoomLevel);

    // Split view signals removed

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void changeEvent(QEvent* event) override; // respond to palette/theme changes

private slots:
    void updateViewer();
    void onPageInputChanged();
    void onSearchInputChanged();
    void onSearchReturnPressed();
    void checkForSelectedText();
    // Split view action removed

protected:
    // Event handling to clear page input focus when clicking elsewhere
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUI();
    void setupToolbar();
    void setupViewerArea();
    void setupIndividualToolbar(QToolBar* toolbar, bool isLeftPanel);
    void syncToolbarStates();
    void applyToolbarTheme(); // reapply styles for current dark/light theme
    void initializePDFViewer();
    void updatePageInputSafely(int currentPage);
    void updateStatusInfo();
    void scheduleDebouncedSearch();
    
    // Core PDF viewer component (your existing renderer)
    std::unique_ptr<PDFViewerEmbedder> m_pdfEmbedder;
    
    // UI Components
    QVBoxLayout* m_mainLayout;
    QToolBar* m_toolbar;
    QWidget* m_viewerContainer;
    
    // Split view components removed
    
    // Dual toolbars removed
    
    // Toolbar actions (shared/main toolbar)
    // Slip/split action removed
    QAction* m_actionRotateLeft;
    QAction* m_actionRotateRight;
    QAction* m_actionPreviousPage;
    QAction* m_actionNextPage;
    QAction* m_actionZoomIn;
    QAction* m_actionZoomOut;
    QAction* m_actionFindPrevious;
    QAction* m_actionFindNext;
    
    // Panel-specific actions removed
    
    // Page navigation widgets (shared/main toolbar)
    QLabel* m_pageLabel;
    QLineEdit* m_pageInput;
    QLabel* m_totalPagesLabel;
    
    // Search widgets (shared/main toolbar)
    QLabel* m_searchLabel;
    QLineEdit* m_searchInput;
    QLabel* m_statusInfoLabel;    // Compact status (page & zoom)
    
    // Split view navigation widgets removed
    
    // Update timer for the embedded viewer
    QTimer* m_updateTimer;
    QTimer* m_navigationTimer;  // Timer to reset navigation flag
    QTimer* m_searchDebounceTimer; // Debounce timer for search
    
    // State tracking
    bool m_viewerInitialized;
    bool m_pdfLoaded;              // Left panel PDF loaded state
    // Right panel PDF state removed
    bool m_usingFallback;
    bool m_navigationInProgress;  // Flag to track when programmatic navigation is happening
    QString m_currentFilePath;
    QString m_lastSelectedText;  // Track last selected text to detect changes
    // Cross-viewer additions
    QString m_linkedPcbFileName; // display name for context menu labels
    bool m_crossSearchEnabled { true };
    QPoint m_rightPressPos;      // track right button press position
    qint64 m_rightPressTimeMs {0};
    bool m_rightDragging {false};
    QString captureCurrentSelection() const; // helper to fetch current selection
    void showCrossContextMenu(const QPoint &globalPos, const QString &text);
    int m_lastKnownPage = -1;    // Track last page to avoid redundant UI churn
    double m_lastKnownZoom = -1; // Track last zoom level
    QString m_lastSearchTerm;    // Track last executed search term
    static constexpr int SEARCH_DEBOUNCE_MS = 250;
    
    // Constants
    static constexpr int UPDATE_INTERVAL_MS = 16; // ~60 FPS

    // Async scaffolding state (Phase 1)
    int m_currentLoadId = 0;
    bool m_cancelRequested = false;
    QFutureWatcher<bool> *m_loadWatcher = nullptr;
    QFutureWatcher<PDFPreviewResult> *m_previewWatcher = nullptr; // Phase 2 preview
    class LoadingOverlay *m_loadingOverlay = nullptr;
    QString m_pendingFilePath;
    QLabel *m_previewLabel = nullptr; // first page preview
};

#endif // PDFVIEWERWIDGET_H
