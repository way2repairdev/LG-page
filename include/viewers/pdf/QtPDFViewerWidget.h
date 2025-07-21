#pragma once

#include <QWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QSpinBox>
#include <memory>

class PDFViewerEmbedder;

/**
 * QtPDFViewerWidget - Qt wrapper for the PDFViewerEmbedder
 * 
 * This widget provides a Qt interface to your existing PDF viewer,
 * embedding it as a native Windows child window and providing
 * Qt-based controls for navigation and search.
 */
class QtPDFViewerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit QtPDFViewerWidget(QWidget *parent = nullptr);
    ~QtPDFViewerWidget();

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
     * Get current page count
     */
    int getPageCount() const;

    /**
     * Get current zoom level
     */
    float getCurrentZoom() const;

    /**
     * Get current page number
     */
    int getCurrentPage() const;

public slots:
    // Navigation slots
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void goToPage(int pageNumber);
    void nextPage();
    void previousPage();
    
    // Search slots
    void findText(const QString& searchTerm);
    void findNext();
    void findPrevious();
    void clearSelection();

signals:
    // Emitted when PDF is loaded
    void pdfLoaded(const QString& filePath, int pageCount);
    
    // Emitted when zoom changes
    void zoomChanged(float zoomLevel);
    
    // Emitted when page changes
    void pageChanged(int currentPage, int totalPages);
    
    // Emitted when text is selected
    void textSelected(const QString& selectedText);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void updateViewer();
    void onZoomSliderChanged(int value);
    void onPageSpinBoxChanged(int value);
    void onSearchTextChanged();
    void onSearchReturnPressed();

private:
    void setupUI();
    void setupViewerArea();
    void setupControlsToolbar();
    void updateControlsState();
    
    // Core PDF viewer
    std::unique_ptr<PDFViewerEmbedder> m_pdfEmbedder;
    
    // Qt UI components
    QVBoxLayout* m_mainLayout;
    QHBoxLayout* m_toolbarLayout;
    QWidget* m_viewerContainer;
    QWidget* m_toolbar;
    
    // Control widgets
    QPushButton* m_prevPageBtn;
    QPushButton* m_nextPageBtn;
    QSpinBox* m_pageSpinBox;
    QLabel* m_pageCountLabel;
    QPushButton* m_zoomInBtn;
    QPushButton* m_zoomOutBtn;
    QPushButton* m_zoomFitBtn;
    QSlider* m_zoomSlider;
    QLabel* m_zoomLabel;
    
    // Search widgets
    QLineEdit* m_searchEdit;
    QPushButton* m_searchPrevBtn;
    QPushButton* m_searchNextBtn;
    QPushButton* m_clearSelectionBtn;
    
    // Update timer
    QTimer* m_updateTimer;
    
    // State tracking
    bool m_viewerInitialized;
    QString m_currentFilePath;
    int m_lastPageCount;
    float m_lastZoomLevel;
    int m_lastCurrentPage;
};
