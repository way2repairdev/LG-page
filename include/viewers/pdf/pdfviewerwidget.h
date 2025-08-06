#ifndef PDFVIEWERWIDGET_H
#define PDFVIEWERWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QSplitter>
#include <QToolBar>
#include <QAction>
#include <QLineEdit>
#include <QLabel>
#include <QEvent>
#include <memory>

class PDFViewerEmbedder;

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

    /**
     * Load a PDF file into the right panel (for split view)
     * @param filePath Path to the PDF file
     * @return true if loaded successfully
     */
    bool loadRightPanelPDF(const QString& filePath);

    /**
     * Clear/unload the PDF from the right panel
     */
    void clearRightPanelPDF();

    /**
     * Check if a PDF is currently loaded
     */
    bool isPDFLoaded() const;

    /**
     * Check if a PDF is currently loaded in the right panel
     */
    bool isRightPanelPDFLoaded() const;

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

signals:
    // Emitted when PDF is successfully loaded
    void pdfLoaded(const QString& filePath);
    
    // Emitted when there's an error loading or rendering
    void errorOccurred(const QString& error);
    
    // Emitted when the current page changes
    void pageChanged(int currentPage, int totalPages);
    
    // Emitted when zoom level changes
    void zoomChanged(double zoomLevel);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;

private slots:
    void updateViewer();
    void onPageInputChanged();
    void onSearchInputChanged();
    void checkForSelectedText();
    void onSlipTabClicked();

protected:
    // Event handling to clear page input focus when clicking elsewhere
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUI();
    void setupToolbar();
    void setupViewerArea();
    void setupIndividualToolbar(QToolBar* toolbar, bool isLeftPanel);
    void syncToolbarStates();
    void initializePDFViewer();
    
    // Core PDF viewer component (your existing renderer)
    std::unique_ptr<PDFViewerEmbedder> m_pdfEmbedder;
    
    // UI Components
    QVBoxLayout* m_mainLayout;
    QToolBar* m_toolbar;
    QWidget* m_viewerContainer;
    
    // Split view components
    QSplitter* m_splitter;
    QWidget* m_leftViewerContainer;
    QWidget* m_rightViewerContainer;
    QLabel* m_rightPlaceholderLabel;
    bool m_isSplitView;
    
    // Dual toolbar support
    QToolBar* m_leftToolbar;
    QToolBar* m_rightToolbar;
    QWidget* m_leftPanel;    // Contains left toolbar + left viewer
    QWidget* m_rightPanel;   // Contains right toolbar + right viewer
    
    // Toolbar actions (shared/main toolbar)
    QAction* m_actionSlipTab;
    QAction* m_actionRotateLeft;
    QAction* m_actionRotateRight;
    QAction* m_actionPreviousPage;
    QAction* m_actionNextPage;
    QAction* m_actionZoomIn;
    QAction* m_actionZoomOut;
    QAction* m_actionFindPrevious;
    QAction* m_actionFindNext;
    
    // Left panel toolbar actions (for split view)
    QAction* m_leftActionRotateLeft;
    QAction* m_leftActionRotateRight;
    QAction* m_leftActionPreviousPage;
    QAction* m_leftActionNextPage;
    QAction* m_leftActionZoomIn;
    QAction* m_leftActionZoomOut;
    QAction* m_leftActionFindPrevious;
    QAction* m_leftActionFindNext;
    
    // Right panel toolbar actions (for split view)
    QAction* m_rightActionRotateLeft;
    QAction* m_rightActionRotateRight;
    QAction* m_rightActionPreviousPage;
    QAction* m_rightActionNextPage;
    QAction* m_rightActionZoomIn;
    QAction* m_rightActionZoomOut;
    QAction* m_rightActionFindPrevious;
    QAction* m_rightActionFindNext;
    
    // Page navigation widgets (shared/main toolbar)
    QLabel* m_pageLabel;
    QLineEdit* m_pageInput;
    QLabel* m_totalPagesLabel;
    
    // Search widgets (shared/main toolbar)
    QLabel* m_searchLabel;
    QLineEdit* m_searchInput;
    
    // Left panel navigation widgets
    QLabel* m_leftPageLabel;
    QLineEdit* m_leftPageInput;
    QLabel* m_leftTotalPagesLabel;
    QLabel* m_leftSearchLabel;
    QLineEdit* m_leftSearchInput;
    
    // Right panel navigation widgets  
    QLabel* m_rightPageLabel;
    QLineEdit* m_rightPageInput;
    QLabel* m_rightTotalPagesLabel;
    QLabel* m_rightSearchLabel;
    QLineEdit* m_rightSearchInput;
    
    // Update timer for the embedded viewer
    QTimer* m_updateTimer;
    QTimer* m_navigationTimer;  // Timer to reset navigation flag
    
    // State tracking
    bool m_viewerInitialized;
    bool m_pdfLoaded;              // Left panel PDF loaded state
    bool m_rightPdfLoaded;         // Right panel PDF loaded state
    bool m_usingFallback;
    bool m_navigationInProgress;  // Flag to track when programmatic navigation is happening
    QString m_currentFilePath;
    QString m_lastSelectedText;  // Track last selected text to detect changes
    
    // Constants
    static constexpr int UPDATE_INTERVAL_MS = 16; // ~60 FPS
};

#endif // PDFVIEWERWIDGET_H
