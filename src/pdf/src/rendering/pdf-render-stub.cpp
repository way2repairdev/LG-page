#include "rendering/pdf-render-stub.h"
#include <iostream>
#include <algorithm>
#include <cmath>

PDFRenderer::PDFRenderer() 
    : m_pageCount(0)
{
}

PDFRenderer::~PDFRenderer() 
{
}

void PDFRenderer::Initialize()
{
    // Placeholder initialization
    std::cout << "PDF Renderer initialized (stub)" << std::endl;
}

bool PDFRenderer::LoadDocument(const std::string& filePath)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_filePath = filePath;
    
    // For now, simulate loading a PDF with some pages
    // In a real implementation, this would use PDFium
    m_pageCount = 3; // Simulate 3 pages
    
    // Set up some default page sizes (A4 roughly)
    m_pageSizes.clear();
    m_pageSizes.push_back({595.0, 842.0}); // A4 in points
    m_pageSizes.push_back({595.0, 842.0});
    m_pageSizes.push_back({595.0, 842.0});
    
    std::cout << "Loaded PDF (stub): " << filePath << " with " << m_pageCount << " pages" << std::endl;
    return true;
}

PDFRenderer::SimpleBitmap PDFRenderer::RenderPageToBitmap(int pageIndex, int& width, int& height, bool highResolution)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (pageIndex < 0 || pageIndex >= m_pageCount) {
        width = height = 0;
        return SimpleBitmap{0, 0, {}};
    }
    
    // Default page size
    double pageWidth = m_pageSizes[pageIndex].first;
    double pageHeight = m_pageSizes[pageIndex].second;
    
    // Scale based on resolution
    double scale = highResolution ? 2.0 : 1.0;
    width = static_cast<int>(pageWidth * scale);
    height = static_cast<int>(pageHeight * scale);
    
    return createPlaceholderBitmap(width, height, pageIndex);
}

int PDFRenderer::GetPageCount() const
{
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
    return m_pageCount;
}

void PDFRenderer::SetViewport(int x, int y, int width, int height)
{
    // Store viewport information
    // In a real implementation, this would be used for optimized rendering
    std::cout << "Viewport set: " << x << "," << y << " " << width << "x" << height << std::endl;
}

void PDFRenderer::GetOriginalPageSize(int pageIndex, double& outWidth, double& outHeight)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (pageIndex < 0 || pageIndex >= m_pageCount) {
        outWidth = outHeight = 0.0;
        return;
    }
    
    outWidth = m_pageSizes[pageIndex].first;
    outHeight = m_pageSizes[pageIndex].second;
}

void PDFRenderer::GetBestFitSize(int pageIndex, int viewportWidth, int viewportHeight, int& outWidth, int& outHeight)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (pageIndex < 0 || pageIndex >= m_pageCount) {
        outWidth = outHeight = 0;
        return;
    }
    
    double pageWidth = m_pageSizes[pageIndex].first;
    double pageHeight = m_pageSizes[pageIndex].second;
    
    double pageAspect = pageWidth / pageHeight;
    double viewAspect = static_cast<double>(viewportWidth) / static_cast<double>(viewportHeight);
    
    if (viewAspect > pageAspect) {
        // Window is wider than page: fit by height
        outHeight = viewportHeight;
        outWidth = static_cast<int>(viewportHeight * pageAspect);
    } else {
        // Window is taller than page: fit by width
        outWidth = viewportWidth;
        outHeight = static_cast<int>(viewportWidth / pageAspect);
    }
}

PDFRenderer::SimpleBitmap PDFRenderer::createPlaceholderBitmap(int width, int height, int pageIndex)
{
    SimpleBitmap bitmap;
    bitmap.width = width;
    bitmap.height = height;
    bitmap.data.resize(width * height * 4); // RGBA
    
    // Create a simple placeholder pattern
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int index = (y * width + x) * 4;
            
            // Create a simple pattern based on page index
            unsigned char r = 255; // White background
            unsigned char g = 255;
            unsigned char b = 255;
            unsigned char a = 255;
            
            // Add some pattern to distinguish pages
            if (pageIndex == 0) {
                // Page 1 - light blue tint
                r = 240;
                g = 248;
                b = 255;
            } else if (pageIndex == 1) {
                // Page 2 - light green tint
                r = 240;
                g = 255;
                b = 240;
            } else {
                // Page 3 - light yellow tint
                r = 255;
                g = 255;
                b = 240;
            }
            
            // Add some text-like patterns
            if ((x % 50 < 40) && (y % 20 < 2)) {
                r = g = b = 0; // Black "text" lines
            }
            
            // Add page number
            if (x > width - 100 && x < width - 50 && y > height - 50 && y < height - 20) {
                r = g = b = 100; // Dark gray for page number area
            }
            
            bitmap.data[index] = r;
            bitmap.data[index + 1] = g;
            bitmap.data[index + 2] = b;
            bitmap.data[index + 3] = a;
        }
    }
    
    return bitmap;
}
