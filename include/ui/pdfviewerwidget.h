#ifndef PDFVIEWERWIDGET_H
#define PDFVIEWERWIDGET_H

#include <QObject>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QTimer>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFileInfo>
#include <QString>
#include <QMenu>
#include <QAction>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QSplitter>
#include <QScrollBar>
#include <memory>
#include <vector>

// Constants for PDF viewing - matching standalone viewer limits
static constexpr double MIN_ZOOM = 0.35;   // Match standalone viewer (35%)
static constexpr double MAX_ZOOM = 5.0;    // Match standalone viewer (500%)
static constexpr double DEFAULT_ZOOM = 0.8;  // Fit to screen better
static constexpr double ZOOM_STEP = 0.1;
static constexpr float PAGE_MARGIN = 10.0f;
static constexpr int TOOLBAR_HEIGHT = 40;
static constexpr int SEARCH_BAR_HEIGHT = 35;

// Forward declarations for PDF viewer components
class PDFRenderer;
struct PDFScrollState;
struct TextSearch;

// Custom deleter for PDFRenderer to avoid incomplete type issues
struct PDFRendererDeleter {
    void operator()(PDFRenderer* ptr) const;
};

QT_BEGIN_NAMESPACE
QT_END_NAMESPACE

class PDFViewerWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit PDFViewerWidget(QWidget *parent = nullptr);
    ~PDFViewerWidget();

    // PDF loading and management
    bool loadPDF(const QString &filePath);
    void closePDF();
    bool isPDFLoaded() const;
    QString getCurrentFilePath() const;

    // View control
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void zoomToWidth();
    void resetZoom();
    void setZoomLevel(double zoom);
    double getZoomLevel() const;

    // Navigation
    void goToPage(int pageNumber);
    void nextPage();
    void previousPage();
    void goToFirstPage();
    void goToLastPage();
    int getCurrentPage() const;
    int getPageCount() const;

    // Search functionality
    void startSearch();
    void searchNext();
    void searchPrevious();
    void setSearchTerm(const QString &term);
    void clearSearch();
    
    // Zoom fit functionality
    void fitToWidth();
    void fitToPage();
    double calculateFitToWidthZoom() const;
    double calculateFitToPageZoom() const;
    
    // Cursor-based zoom functionality
    void performCursorBasedZoom(const QPoint &cursorPos, bool zoomIn);
    
    // Auto-centering functionality
    void autoCenter();
    
    // Coordinate conversion
    QPointF screenToDocumentCoordinates(const QPoint &screenPos) const;
    QPoint documentToScreenCoordinates(const QPointF &docPos) const;

signals:
    void pdfLoaded(const QString &filePath);
    void pdfClosed();
    void pageChanged(int currentPage, int totalPages);
    void zoomChanged(double zoomLevel);
    void searchResultsChanged(int currentResult, int totalResults);
    void errorOccurred(const QString &error);

protected:
    // QOpenGLWidget overrides
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    // Event handling
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onZoomSliderChanged(int value);
    void onPageInputChanged();
    void onSearchTextChanged(const QString &text);
    void onSearchNext();
    void onSearchPrevious();
    void onToggleCaseSensitive(bool enabled);
    void onToggleWholeWords(bool enabled);
    void onVerticalScrollBarChanged(int value);
    void updateRender();

private:
    // Initialization
    void initializePDFRenderer();
    void setupUI();
    void setupToolbar();
    void setupSearchBar();
    void createContextMenu();

    // OpenGL rendering
    void renderPDF();
    void updateTextures();
    void updateVisibleTextures(); // Fast update for visible pages only
    void createQuadGeometry();
    GLuint createTextureFromPDFBitmap(void* bitmap, int width, int height);

    // View management
    void updateScrollState();
    void updateViewport();
    void updateScrollBar();
    void calculatePageLayout();
    void handlePanning(const QPoint &delta);
    void handleZooming(double factor, const QPoint &center);

    // Search helpers
    void performSearch();
    void highlightSearchResults();
    void updateSearchUI();

    // Background texture loading for high zoom performance
    void loadTexturesInBackground(const std::vector<int>& pageIndices);
    void scheduleTextureUpdate(int pageIndex);
    
    // Wheel event throttling and acceleration
    void processThrottledWheelEvent();
    void resetWheelAcceleration();
    void handleWheelEventBatch(double totalDelta, const QPoint& cursorPos);
    
    // Panning throttling and optimization
    void processThrottledPanEvent();
    void handlePanEventBatch(const QPoint& totalDelta);
    void resetPanThrottling();
    
    // Progressive rendering for smooth high zoom experience
    void startProgressiveRender();
    void processProgressiveRender();
    void cancelProgressiveRender();
    
    // Texture memory management
    void cleanupUnusedTextures();
    size_t calculateTextureMemoryUsage() const;
    static constexpr size_t MAX_TEXTURE_MEMORY = 512 * 1024 * 1024; // 512MB limit

    // Utility functions
    QPoint mapToViewport(const QPoint &point);
    int getPageAtPoint(const QPoint &point);
    double calculateZoomToFit();
    double calculateZoomToWidth();
    QPointF screenToDocument(const QPoint &screenPoint) const;
    QPointF documentToScreen(const QPointF &docPoint) const;

private:
    // PDF renderer components
    std::unique_ptr<PDFRenderer, PDFRendererDeleter> m_renderer;
    std::unique_ptr<PDFScrollState> m_scrollState;
    std::unique_ptr<TextSearch> m_textSearch;

    // OpenGL resources
    QOpenGLShaderProgram *m_shaderProgram;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vertexBuffer;
    std::vector<GLuint> m_pageTextures;
    std::vector<int> m_pageWidths;
    std::vector<int> m_pageHeights;

    // UI components
    QWidget *m_toolbarWidget;
    QToolBar *m_toolbar;
    QSlider *m_zoomSlider;
    QLabel *m_zoomLabel;
    QLineEdit *m_pageInput;
    QLabel *m_pageCountLabel;
    
    // Scroll bar
    QScrollBar *m_verticalScrollBar;
    
    // Search UI
    QWidget *m_searchWidget;
    QLineEdit *m_searchInput;
    QPushButton *m_searchNextButton;
    QPushButton *m_searchPrevButton;
    QCheckBox *m_caseSensitiveCheck;
    QCheckBox *m_wholeWordsCheck;
    QLabel *m_searchResultsLabel;
    QPushButton *m_closeSearchButton;

    // Context menu
    QMenu *m_contextMenu;
    QAction *m_zoomInAction;
    QAction *m_zoomOutAction;
    QAction *m_zoomFitAction;
    QAction *m_zoomWidthAction;
    QAction *m_searchAction;
    QAction *m_zoomModeAction;

    // State variables
    QString m_filePath;
    bool m_isPDFLoaded;
    int m_currentPage;
    int m_pageCount;
    double m_zoomLevel;
    double m_lastRenderedZoom;  // Track last zoom level for texture updates
    bool m_zoomChanged;         // Flag for zoom change detection
    bool m_immediateRenderRequired; // Flag for immediate visible page updates
    bool m_isDragging;
    QPoint m_lastPanPoint;
    QTimer *m_renderTimer;

    // View parameters
    int m_viewportWidth;
    int m_viewportHeight;
    float m_scrollOffsetY;
    float m_scrollOffsetX;
    float m_maxScrollY;
    float m_maxScrollX;
    float m_minScrollX;  // Minimum scroll X position (can be negative for centering)
    
    // Performance optimization flags
    bool m_useBackgroundLoading;
    bool m_highZoomMode;           // Enables optimizations at high zoom levels
    
    // Wheel event throttling and acceleration for high zoom performance
    QTimer* m_wheelThrottleTimer;
    QTimer* m_wheelAccelTimer;
    double m_pendingZoomDelta;
    QPoint m_pendingWheelCursor;
    int m_wheelEventCount;
    qint64 m_lastWheelTime;
    bool m_wheelThrottleActive;
    static constexpr int WHEEL_THROTTLE_MS = 5; // 5ms throttle at high zoom
    static constexpr int WHEEL_ACCEL_RESET_MS = 150; // Reset acceleration after 150ms
    static constexpr int MAX_WHEEL_EVENTS_PER_BATCH = 3; // Max events per batch

    // Panning throttling and optimization for high zoom performance
    QTimer* m_panThrottleTimer;
    QPoint m_pendingPanDelta;
    bool m_panThrottleActive;
    qint64 m_lastPanTime;
    int m_panEventCount;
    static constexpr int PAN_THROTTLE_MS = 8; // 8ms throttle at high zoom (slightly slower than wheel)
    static constexpr int MAX_PAN_EVENTS_PER_BATCH = 5; // Max pan events per batch
    
    // Progressive rendering for smooth experience
    QTimer* m_progressiveRenderTimer;
    std::vector<int> m_pendingTextureUpdates;
    bool m_progressiveRenderActive;
    
    // Loading indicator
    QLabel *m_loadingLabel;
    bool m_isLoadingTextures;
};

#endif // PDFVIEWERWIDGET_H
