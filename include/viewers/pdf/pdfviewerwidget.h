#ifndef PDFVIEWERWIDGET_H
#define PDFVIEWERWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QSpinBox>
#include <QProgressBar>
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
     * Check if a PDF is currently loaded
     */
    bool isPDFLoaded() const;

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
    // Navigation
    void zoomIn();
    void zoomOut();
    void goToPage(int pageNumber);
    void nextPage();
    void previousPage();
    
    // Search functionality
    void findText(const QString& searchTerm);
    void findNext();
    void findPrevious();
    void clearSelection();
    
    // View controls
    void setFullScreen(bool fullScreen);

signals:
    // Emitted when PDF is successfully loaded
    void pdfLoaded(const QString& filePath);
    
    // Emitted when there's an error loading or rendering
    void errorOccurred(const QString& error);
    
    // Emitted when the current page changes
    void pageChanged(int currentPage, int totalPages);
    
    // Emitted when zoom level changes
    void zoomChanged(double zoomLevel);
    
    // Emitted when text is selected
    void textSelected(const QString& selectedText);
    
    // Emitted when loading progress updates
    void loadingProgress(int percentage);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;

private slots:
    void updateViewer();
    void checkForSelectedText();  // Checks for text selection changes

private:
    void setupUI();
    void setupViewerArea();
    void initializePDFViewer();
    
    // Core PDF viewer component (your existing renderer)
    std::unique_ptr<PDFViewerEmbedder> m_pdfEmbedder;
    
    // UI Components
    QVBoxLayout* m_mainLayout;
    QWidget* m_viewerContainer;
    
    // Update timer for the embedded viewer
    QTimer* m_updateTimer;
    QTimer* m_selectionTimer;  // Timer to check for selected text changes
    
    // State tracking
    bool m_viewerInitialized;
    bool m_pdfLoaded;
    bool m_usingFallback;
    QString m_currentFilePath;
    int m_lastPageCount;
    double m_lastZoomLevel;
    int m_lastCurrentPage;
    QString m_lastSelectedText;  // Track last selected text to detect changes
    
    // Constants
    static constexpr int UPDATE_INTERVAL_MS = 16; // ~60 FPS
};

#endif // PDFVIEWERWIDGET_H
