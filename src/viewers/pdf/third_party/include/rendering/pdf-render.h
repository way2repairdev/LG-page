#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex> // Added for std::mutex
#include <condition_variable>
#include "fpdfview.h"

class PDFRenderer {
public:
    PDFRenderer();
    ~PDFRenderer();

    // Initialize PDFium library
    void Initialize();

    // Load a PDF document
    bool LoadDocument(const std::string& filePath);

    // Render a specific page
    void RenderPage(int pageIndex, bool highResolution);

    // Render a page and return the bitmap (for OpenGL texture)
    FPDF_BITMAP RenderPageToBitmap(int pageIndex, int& width, int& height, bool highResolution);

    // Render the given page to a bitmap at the specified pixel size
    FPDF_BITMAP RenderPageToBitmap(int pageIndex, int pixelWidth, int pixelHeight);

    // Compute the best-fit pixel size for a PDF page in the current viewport, preserving aspect ratio
    void GetBestFitSize(int pageIndex, int viewportWidth, int viewportHeight, int& outWidth, int& outHeight);

    // Set the viewport dimensions
    void SetViewport(int x, int y, int width, int height);

    // Handle zooming and panning
    void Zoom(double scale);
    void Pan(int offsetX, int offsetY);

    // Start background rendering for non-visible pages
    void StartBackgroundRendering();

    // New: Get rendered bitmap for a page (thread-safe)
    FPDF_BITMAP GetRenderedBitmap(int pageIndex);

    // Helper: Render a page and store in buffer
    void RenderAndStore(int pageIndex, std::vector<FPDF_BITMAP>& buffer, bool highResolution);

    void SwapBuffers();    // New: Get the number of pages in the document
    int GetPageCount() const;

    // New: Get original PDF page dimensions (in PDF coordinate units)
    void GetOriginalPageSize(int pageIndex, double& outWidth, double& outHeight);

    // New: Get the loaded document (thread-safe)
    FPDF_DOCUMENT GetDocument() const { 
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(document_mutex_)); 
        return document_; 
    }

    // Helper to render visible pages
    void RenderVisiblePages();

    // Render a portion of a page into a caller-provided BGRA buffer using PDFium matrix.
    // page rectangle in page units (points). Returns false on failure.
    bool RenderPageRegionToBGRA(int pageIndex,
                                double pageLeft, double pageTop,
                                double pageRight, double pageBottom,
                                int outWidth, int outHeight,
                                void* outBGRA, int outStride);

private:
    FPDF_DOCUMENT document_;
    std::mutex document_mutex_; // Added for thread safety
    std::vector<FPDF_BITMAP> frontBuffer_; // Double buffer: front
    std::vector<FPDF_BITMAP> backBuffer_;  // Double buffer: back
    std::mutex bufferMutex_;
    std::condition_variable bufferCV_;
    int pageCount_ = 0;
    int viewportX_, viewportY_, viewportWidth_, viewportHeight_;
    double zoomScale_;

    // Helper to render pages in the background
    void RenderInBackground();
};