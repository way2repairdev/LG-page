#include "rendering/pdf-render.h"
#include "fpdf_text.h" // Include the header for text manipulation APIs
#include <thread>
#include <iostream>
#include <mutex> // Required for std::lock_guard
#include <cstring>

PDFRenderer::PDFRenderer()
    : document_(nullptr), viewportX_(0), viewportY_(0), viewportWidth_(0), viewportHeight_(0), zoomScale_(1.0) {}

PDFRenderer::~PDFRenderer() {
    if (document_) {
        FPDF_CloseDocument(document_);
    }
    FPDF_DestroyLibrary();
}

void PDFRenderer::Initialize() {
    try {
        FPDF_InitLibrary();
        std::cout << "PDFRenderer: PDFium library initialized successfully" << std::endl;
    } catch (...) {
        std::cerr << "PDFRenderer: Failed to initialize PDFium library" << std::endl;
        std::cerr << "PDFRenderer: This is likely due to missing or incompatible PDFium DLL/library" << std::endl;
        throw std::runtime_error("PDFium initialization failed");
    }
}

bool PDFRenderer::LoadDocument(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(document_mutex_); // Lock before accessing document_
    
    try {
        document_ = FPDF_LoadDocument(filePath.c_str(), nullptr);
        if (!document_) {
            unsigned long error = FPDF_GetLastError();
            std::cerr << "Failed to load PDF document: " << filePath << std::endl;
            std::cerr << "PDFium error code: " << error << std::endl;
            
            switch (error) {
                case FPDF_ERR_SUCCESS:
                    std::cerr << "Error: No error (this shouldn't happen)" << std::endl;
                    break;
                case FPDF_ERR_UNKNOWN:
                    std::cerr << "Error: Unknown error" << std::endl;
                    break;
                case FPDF_ERR_FILE:
                    std::cerr << "Error: File not found or could not be opened" << std::endl;
                    break;
                case FPDF_ERR_FORMAT:
                    std::cerr << "Error: File not in PDF format or corrupted" << std::endl;
                    break;
                case FPDF_ERR_PASSWORD:
                    std::cerr << "Error: Password required" << std::endl;
                    break;
                case FPDF_ERR_SECURITY:
                    std::cerr << "Error: Unsupported security scheme" << std::endl;
                    break;
                case FPDF_ERR_PAGE:
                    std::cerr << "Error: Page not found or content error" << std::endl;
                    break;
                default:
                    std::cerr << "Error: Unknown error code: " << error << std::endl;
                    break;
            }
            return false;
        }
        std::cout << "Successfully loaded PDF: " << filePath << std::endl;
        return true;
    } catch (...) {
        std::cerr << "Exception occurred while loading PDF: " << filePath << std::endl;
        return false;
    }
}

FPDF_BITMAP PDFRenderer::RenderPageToBitmap(int pageIndex, int pixelWidth, int pixelHeight) {
    std::lock_guard<std::mutex> lock(document_mutex_); // Lock before accessing document_
    if (!document_) return nullptr;
    FPDF_PAGE page = FPDF_LoadPage(document_, pageIndex);
    if (!page) return nullptr;
    FPDF_BITMAP bitmap = FPDFBitmap_Create(pixelWidth, pixelHeight, 0);
    FPDFBitmap_FillRect(bitmap, 0, 0, pixelWidth, pixelHeight, 0xFFFFFFFF);
    int flags = FPDF_ANNOT | FPDF_PRINTING | FPDF_RENDER_LIMITEDIMAGECACHE | FPDF_LCD_TEXT;
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, pixelWidth, pixelHeight, 0, flags);
    FPDF_ClosePage(page);
    return bitmap;
}

FPDF_BITMAP PDFRenderer::RenderPageToBitmap(int pageIndex, int& width, int& height, bool highResolution) {
    std::lock_guard<std::mutex> lock(document_mutex_); // Lock before accessing document_
    if (!document_) return nullptr;
    FPDF_PAGE page = FPDF_LoadPage(document_, pageIndex);
    if (!page) return nullptr;
    
    double pageWidth = FPDF_GetPageWidth(page);
    double pageHeight = FPDF_GetPageHeight(page);
    
    // Use higher quality scaling for better rendering
    double scale = highResolution ? 3.0 : 1.5; // Increased from 2.0 to 3.0 for high-res
    width = static_cast<int>(pageWidth * scale);
    height = static_cast<int>(pageHeight * scale);
    
    FPDF_BITMAP bitmap = FPDFBitmap_Create(width, height, 0);
    if (!bitmap) {
        FPDF_ClosePage(page);
        return nullptr;
    }
    
    // Fill with white background
    FPDFBitmap_FillRect(bitmap, 0, 0, width, height, 0xFFFFFFFF);
    
    // Use high-quality rendering flags
    int flags = FPDF_ANNOT | FPDF_PRINTING | FPDF_RENDER_LIMITEDIMAGECACHE | FPDF_LCD_TEXT;
    
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, width, height, 0, flags);
    FPDF_ClosePage(page);
    return bitmap;
}

void PDFRenderer::GetBestFitSize(int pageIndex, int viewportWidth, int viewportHeight, int& outWidth, int& outHeight) {
    std::lock_guard<std::mutex> lock(document_mutex_); // Lock before accessing document_
    if (!document_) { outWidth = outHeight = 0; return; }
    FPDF_PAGE page = FPDF_LoadPage(document_, pageIndex);
    if (!page) { outWidth = outHeight = 0; return; }
    double pageWidth = FPDF_GetPageWidth(page);
    double pageHeight = FPDF_GetPageHeight(page);
    FPDF_ClosePage(page);
    double pageAspect = pageWidth / pageHeight;
    double viewAspect = (double)viewportWidth / (double)viewportHeight;
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

void PDFRenderer::RenderPage(int pageIndex, bool highResolution) {
    std::lock_guard<std::mutex> lock(document_mutex_); // Lock before accessing document_
    if (!document_) return;

    int pixelWidth, pixelHeight;
    double scale = highResolution ? zoomScale_ : 1.0; // Adjust resolution based on zoom scale
    GetBestFitSize(pageIndex, static_cast<int>(viewportWidth_ * scale), static_cast<int>(viewportHeight_ * scale), pixelWidth, pixelHeight);

    FPDF_BITMAP bitmap = RenderPageToBitmap(pageIndex, pixelWidth, pixelHeight);

    // Custom text alignment logic
    // FPDF_PAGE page = FPDF_LoadPage(document_, pageIndex); // Already locked and page loaded if needed by RenderPageToBitmap
    // if (page) {
    //     FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
    //     if (textPage) {
    //         int textCount = FPDFText_CountChars(textPage);
    //         for (int i = 0; i < textCount; ++i) {
    //             // Adjust text positions to float to the left
    //             // Placeholder logic: refine as needed
    //             double leftPos = 0.0; // Example adjustment
    //             double topPos = 0.0; // Example adjustment
    //             // Use additional APIs or custom logic to set positions
    //         }
    //         FPDFText_ClosePage(textPage);
    //     }
    //     FPDF_ClosePage(page); // Should be closed if opened here
    // }

    // Store or display the rendered bitmap as needed

    FPDFBitmap_Destroy(bitmap);
}

void PDFRenderer::SetViewport(int x, int y, int width, int height) {
    viewportX_ = x;
    viewportY_ = y;
    viewportWidth_ = width;
    viewportHeight_ = height;
    RenderVisiblePages();
}

void PDFRenderer::Zoom(double scale) {
    zoomScale_ = scale;

    // Adjust rendering resolution based on the new zoom scale
    RenderVisiblePages();
}

void PDFRenderer::Pan(int offsetX, int offsetY) {
    viewportX_ += offsetX;
    viewportY_ += offsetY;
    RenderVisiblePages();
}

void PDFRenderer::StartBackgroundRendering() {
    std::thread backgroundThread(&PDFRenderer::RenderInBackground, this);
    backgroundThread.detach();
}

void PDFRenderer::RenderVisiblePages() {
    std::lock_guard<std::mutex> lock(document_mutex_);
    if (!document_) return;

    // This function is now primarily used for triggering re-rendering notifications
    // The actual texture regeneration happens in the main rendering loop in Viewer-new.cpp
    // when zoomChanged flag is detected
    
    // We could add more sophisticated visible page detection here if needed
    // For now, just ensure the document is accessible and zoom scale is updated
}

// Render a portion of the page using PDFium matrix into a provided BGRA buffer.
// pageRect is in PDF page units (points). Buffer must be width*height*4 bytes (BGRA).
bool PDFRenderer::RenderPageRegionToBGRA(int pageIndex,
                                         double pageLeft, double pageTop,
                                         double pageRight, double pageBottom,
                                         int outWidth, int outHeight,
                                         void* outBGRA, int outStride) {
    std::lock_guard<std::mutex> lock(document_mutex_);
    if (!document_ || !outBGRA || outWidth <= 0 || outHeight <= 0) return false;

    FPDF_PAGE page = FPDF_LoadPage(document_, pageIndex);
    if (!page) return false;

    // Create bitmap wrapping the output buffer
    FPDF_BITMAP bmp = FPDFBitmap_CreateEx(outWidth, outHeight, FPDFBitmap_BGRA, outBGRA, outStride);
    if (!bmp) { FPDF_ClosePage(page); return false; }

    // Fill white background
    FPDFBitmap_FillRect(bmp, 0, 0, outWidth, outHeight, 0xFFFFFFFF);

    // Build matrix: map page rect to device rect [0..outWidth]x[0..outHeight]
    FS_MATRIX m;
    const double sx = (double)outWidth / (pageRight - pageLeft);
    const double sy = -(double)outHeight / (pageBottom - pageTop); // flip Y
    m.a = sx;  m.b = 0;   m.c = 0;   m.d = sy;
    m.e = -pageLeft * sx;
    m.f = -pageTop  * sy;

    FS_RECTF clip;
    clip.left = (float)pageLeft;
    clip.top = (float)pageTop;
    clip.right = (float)pageRight;
    clip.bottom = (float)pageBottom;

    int flags = FPDF_ANNOT | FPDF_PRINTING | FPDF_RENDER_LIMITEDIMAGECACHE;
    FPDF_RenderPageBitmapWithMatrix(bmp, page, &m, &clip, flags);

    FPDF_ClosePage(page);
    // bmp is destroyed by caller if using CreateEx with external buffer? We used external buffer, so destroy wrapper.
    FPDFBitmap_Destroy(bmp);
    return true;
}

void PDFRenderer::RenderInBackground() {
    std::lock_guard<std::mutex> lock(document_mutex_); // Lock before accessing document_
    if (!document_) return;

    int pageCount = FPDF_GetPageCount(document_);
    for (int i = 10; i < pageCount; ++i) {
        RenderPage(i, false);
    }
}

int PDFRenderer::GetPageCount() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(document_mutex_)); // Lock for const method
    if (!document_) return 0;
    return FPDF_GetPageCount(document_);
}

void PDFRenderer::GetOriginalPageSize(int pageIndex, double& outWidth, double& outHeight) {
    std::lock_guard<std::mutex> lock(document_mutex_);
    if (!document_) {
        outWidth = outHeight = 0.0;
        return;
    }
    
    FPDF_PAGE page = FPDF_LoadPage(document_, pageIndex);
    if (!page) {
        outWidth = outHeight = 0.0;
        return;
    }
    
    outWidth = FPDF_GetPageWidth(page);
    outHeight = FPDF_GetPageHeight(page);
    FPDF_ClosePage(page);
}



PDFRenderer pdfRenderer;