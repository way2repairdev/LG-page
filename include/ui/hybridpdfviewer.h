#ifndef HYBRIDPDFVIEWER_H
#define HYBRIDPDFVIEWER_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QLabel>
#include <QSlider>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QTabWidget>
#include <QSplitter>
#include <QPdfDocument>
#include <QPdfView>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QTimer>
#include <memory>

// Forward declarations
class PDFRenderer;
class PDFViewerWidget;
struct PDFScrollState;
struct TextSearch;

/**
 * @brief Hybrid PDF viewer combining Qt PDF module with custom OpenGL rendering
 * 
 * This class provides a tabbed interface where users can choose between:
 * - Qt's native PDF viewer (QPdfView) - simple and standard
 * - Custom OpenGL PDF viewer - high performance with advanced features
 */
class HybridPDFViewer : public QWidget
{
    Q_OBJECT

public:
    enum ViewerMode {
        QtNativeViewer,     // Use Qt's built-in PDF viewer
        CustomOpenGLViewer  // Use custom OpenGL PDF viewer
    };

    explicit HybridPDFViewer(QWidget *parent = nullptr);
    ~HybridPDFViewer();

    // Document management
    bool loadPDF(const QString &filePath);
    void closePDF();
    bool isPDFLoaded() const;
    QString getCurrentFilePath() const;

    // Viewer mode switching
    void setViewerMode(ViewerMode mode);
    ViewerMode getViewerMode() const;

    // Common controls
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void resetZoom();
    void goToPage(int pageNumber);
    void nextPage();
    void previousPage();
    int getCurrentPage() const;
    int getPageCount() const;

    // Search functionality
    void startSearch(const QString &searchTerm);
    void searchNext();
    void searchPrevious();
    void clearSearch();

signals:
    void pdfLoaded(const QString &filePath);
    void pdfClosed();
    void pageChanged(int currentPage, int totalPages);
    void zoomChanged(double zoomLevel);
    void viewerModeChanged(ViewerMode mode);
    void searchResultsChanged(int currentResult, int totalResults);
    void errorOccurred(const QString &error);

private slots:
    void onTabChanged(int index);
    void onSwitchViewer();
    void onZoomSliderChanged(int value);
    void onPageInputChanged();
    void onSearchTextChanged();
    void onPerformanceToggled(bool enabled);
    
    // Qt PDF viewer slots
    void onQtPdfLoaded();
    void onQtPdfError(QPdfDocument::Error error);
    void onQtPdfPageChanged(int page);
    
    // Custom PDF viewer slots
    void onCustomPdfLoaded(const QString &filePath);
    void onCustomPdfError(const QString &error);
    void onCustomPageChanged(int currentPage, int totalPages);
    void onCustomZoomChanged(double zoomLevel);

private:
    void setupUI();
    void setupToolbar();
    void setupTabWidget();
    void setupSearchControls();
    void createViewerTabs();
    void syncViewerStates();
    void updateToolbarFromActiveViewer();
    void enableControls(bool enabled);

private:
    // Main layout
    QVBoxLayout *m_mainLayout;
    QHBoxLayout *m_toolbarLayout;
    QTabWidget *m_tabWidget;
    
    // Toolbar controls
    QToolBar *m_toolbar;
    QLabel *m_modeLabel;
    QPushButton *m_switchButton;
    QSlider *m_zoomSlider;
    QLabel *m_zoomLabel;
    QLineEdit *m_pageInput;
    QLabel *m_pageCountLabel;
    QPushButton *m_prevPageButton;
    QPushButton *m_nextPageButton;
    QCheckBox *m_performanceMode;
    
    // Search controls
    QWidget *m_searchWidget;
    QLineEdit *m_searchInput;
    QPushButton *m_searchButton;
    QPushButton *m_searchNextButton;
    QPushButton *m_searchPrevButton;
    QCheckBox *m_caseSensitiveCheck;
    QLabel *m_searchResultsLabel;
    
    // Qt PDF viewer components
    QWidget *m_qtViewerWidget;
    QPdfDocument *m_qtPdfDocument;
    QPdfView *m_qtPdfView;
    QVBoxLayout *m_qtViewerLayout;
    
    // Custom OpenGL PDF viewer
    PDFViewerWidget *m_customPdfViewer;
    
    // State management
    ViewerMode m_currentMode;
    QString m_currentFilePath;
    bool m_isPDFLoaded;
    int m_currentPage;
    int m_pageCount;
    double m_zoomLevel;
    QString m_currentSearchTerm;
    
    // Performance tracking
    QTimer *m_performanceTimer;
    int m_frameCount;
    qint64 m_lastFrameTime;
};

#endif // HYBRIDPDFVIEWER_H
