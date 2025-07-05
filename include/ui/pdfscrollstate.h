#ifndef PDFSCROLLSTATE_H
#define PDFSCROLLSTATE_H

#include <QPointF>
#include <QSizeF>
#include <QRect>
#include <vector>

/**
 * @brief Manages scroll state and viewport calculations for PDF viewing
 */
struct PDFScrollState {
    // Current scroll position
    QPointF scrollPosition;
    
    // Viewport dimensions
    QSizeF viewportSize;
    
    // Document dimensions
    QSizeF documentSize;
    
    // Page layouts (position and size of each page)
    std::vector<QRect> pageRects;
    
    // Current visible page range
    int firstVisiblePage;
    int lastVisiblePage;
    
    // Zoom level
    double zoomLevel;
    
    // Constructor
    PDFScrollState() 
        : scrollPosition(0, 0)
        , viewportSize(0, 0)
        , documentSize(0, 0)
        , firstVisiblePage(0)
        , lastVisiblePage(0)
        , zoomLevel(1.0)
    {
    }
    
    // Calculate visible page range
    void updateVisiblePages() {
        firstVisiblePage = 0;
        lastVisiblePage = static_cast<int>(pageRects.size()) - 1;
        
        // TODO: Calculate actual visible range based on scroll position
        // For now, assume all pages are visible
    }
    
    // Check if a page is visible
    bool isPageVisible(int pageIndex) const {
        return pageIndex >= firstVisiblePage && pageIndex <= lastVisiblePage;
    }
    
    // Get page rect in viewport coordinates
    QRect getPageViewportRect(int pageIndex) const {
        if (pageIndex < 0 || pageIndex >= static_cast<int>(pageRects.size())) {
            return QRect();
        }
        
        QRect pageRect = pageRects[pageIndex];
        pageRect.translate(-scrollPosition.x(), -scrollPosition.y());
        return pageRect;
    }
    
    // Update layout for all pages
    void updatePageLayouts(const std::vector<QSizeF> &pageSizes, double zoom, int pageMargin) {
        pageRects.clear();
        
        double currentY = 0;
        double maxWidth = 0;
        
        for (size_t i = 0; i < pageSizes.size(); ++i) {
            QSizeF scaledSize = pageSizes[i] * zoom;
            
            // Center horizontally
            double x = (viewportSize.width() - scaledSize.width()) / 2.0;
            if (x < 0) x = 0;
            
            QRect pageRect(x, currentY, scaledSize.width(), scaledSize.height());
            pageRects.push_back(pageRect);
            
            currentY += scaledSize.height() + pageMargin;
            maxWidth = std::max(maxWidth, scaledSize.width());
        }
        
        documentSize = QSizeF(maxWidth, currentY);
        zoomLevel = zoom;
    }
    
    // Get scroll limits
    QPointF getMaxScrollPosition() const {
        return QPointF(
            std::max(0.0, documentSize.width() - viewportSize.width()),
            std::max(0.0, documentSize.height() - viewportSize.height())
        );
    }
    
    // Clamp scroll position to valid range
    void clampScrollPosition() {
        QPointF maxScroll = getMaxScrollPosition();
        scrollPosition.setX(std::clamp(scrollPosition.x(), 0.0, maxScroll.x()));
        scrollPosition.setY(std::clamp(scrollPosition.y(), 0.0, maxScroll.y()));
    }
};

#endif // PDFSCROLLSTATE_H
