#ifndef PDFVIEWERWIDGET_SIMPLE_H
#define PDFVIEWERWIDGET_SIMPLE_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QLineEdit>
#include <QToolBar>
#include <QTimer>
#include <QCheckBox>
#include <QFrame>
#include <QMenu>
#include <QAction>
#include <QPixmap>
#include <QFileInfo>
#include <QString>
#include <memory>

// Forward declaration for PDF renderer
class PDFRenderer;

class PDFViewerWidget : public QWidget
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

signals:
    void pdfLoaded(const QString &filePath);
    void pdfClosed();
    void pageChanged(int currentPage, int totalPages);
    void zoomChanged(double zoomLevel);
    void searchResultsChanged(int currentResult, int totalResults);
    void errorOccurred(const QString &error);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private slots:
    void onZoomSliderChanged(int value);
    void onPageInputChanged();
    void onSearchTextChanged();
    void onSearchNext();
    void onSearchPrevious();
    void onToggleCaseSensitive(bool enabled);
    void onToggleWholeWords(bool enabled);
    void updateRender();

private:
    // Initialization
    void setupUI();
    void setupToolbar();
    void setupSearchBar();
    void setupPageDisplay();
    void createContextMenu();

    // PDF rendering
    void renderCurrentPage();
    void updatePageDisplay();
    QPixmap renderPageToPixmap(int pageIndex);

    // View management
    void updateZoomUI();
    void updatePageUI();
    void calculateZoomToFit();
    void calculateZoomToWidth();

    // Search helpers
    void performSearch();
    void updateSearchUI();

private:
    // PDF renderer
    std::unique_ptr<PDFRenderer> m_renderer;

    // UI components
    QVBoxLayout *m_mainLayout;
    QWidget *m_toolbarWidget;
    QWidget *m_searchWidget;
    QScrollArea *m_scrollArea;
    QLabel *m_pageLabel;

    // Toolbar controls
    QSlider *m_zoomSlider;
    QLabel *m_zoomLabel;
    QLineEdit *m_pageInput;
    QLabel *m_pageCountLabel;
    QPushButton *m_firstPageBtn;
    QPushButton *m_prevPageBtn;
    QPushButton *m_nextPageBtn;
    QPushButton *m_lastPageBtn;
    QPushButton *m_zoomInBtn;
    QPushButton *m_zoomOutBtn;
    QPushButton *m_zoomFitBtn;
    QPushButton *m_zoomWidthBtn;
    QPushButton *m_searchBtn;

    // Search UI
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

    // State variables
    QString m_filePath;
    bool m_isPDFLoaded;
    int m_currentPage;
    int m_pageCount;
    double m_zoomLevel;
    QTimer *m_renderTimer;

    // Constants
    static constexpr double MIN_ZOOM = 0.1;
    static constexpr double MAX_ZOOM = 5.0;
    static constexpr double DEFAULT_ZOOM = 1.0;
    static constexpr double ZOOM_STEP = 0.1;
    static constexpr int TOOLBAR_HEIGHT = 40;
    static constexpr int SEARCH_BAR_HEIGHT = 35;
};

#endif // PDFVIEWERWIDGET_SIMPLE_H
