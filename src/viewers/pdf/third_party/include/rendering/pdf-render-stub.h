#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>

// Simplified PDF renderer stub - can be enhanced with actual PDFium integration later
class PDFRenderer {
public:
    PDFRenderer();
    ~PDFRenderer();

    // Initialize PDFium library
    void Initialize();

    // Load a PDF document
    bool LoadDocument(const std::string& filePath);

    // Render a page and return a basic bitmap (simplified for now)
    struct SimpleBitmap {
        int width;
        int height;
        std::vector<unsigned char> data; // RGBA data
    };

    // Render a page to a bitmap
    SimpleBitmap RenderPageToBitmap(int pageIndex, int& width, int& height, bool highResolution);

    // Get the number of pages in the document
    int GetPageCount() const;

    // Set the viewport dimensions
    void SetViewport(int x, int y, int width, int height);

    // Get original PDF page dimensions
    void GetOriginalPageSize(int pageIndex, double& outWidth, double& outHeight);

    // Get best fit size for a page
    void GetBestFitSize(int pageIndex, int viewportWidth, int viewportHeight, int& outWidth, int& outHeight);

private:
    std::string m_filePath;
    int m_pageCount;
    std::vector<std::pair<double, double>> m_pageSizes; // width, height pairs
    std::mutex m_mutex;
    
    // Generate a placeholder bitmap for testing
    SimpleBitmap createPlaceholderBitmap(int width, int height, int pageIndex);
};
