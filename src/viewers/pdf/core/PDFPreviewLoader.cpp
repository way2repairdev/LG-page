// Phase 2 async preview implementation
#include "viewers/pdf/PDFPreviewLoader.h"

#include <fpdfview.h>
#include <fpdf_edit.h>
#include <QFileInfo>
#include <cstring>
#include <mutex>

PDFPreviewResult LoadPdfFirstPagePreview(const QString &filePath, int maxDimension) {
    PDFPreviewResult result;
    result.filePath = filePath;
    if (!QFileInfo::exists(filePath)) {
        result.error = QString("File not found: %1").arg(filePath);
        return result;
    }

    static std::once_flag s_pdfiumOnce;
    std::call_once(s_pdfiumOnce, [](){
        FPDF_LIBRARY_CONFIG config{}; config.version = 3; FPDF_InitLibraryWithConfig(&config);
    });
    FPDF_DOCUMENT doc = FPDF_LoadDocument(filePath.toUtf8().constData(), nullptr);
    if (!doc) {
        result.error = QString("Failed to open PDF: %1").arg(filePath);
        return result;
    }

    int pageCount = FPDF_GetPageCount(doc);
    if (pageCount <= 0) {
        FPDF_CloseDocument(doc);
        result.error = QString("PDF has no pages: %1").arg(filePath);
        return result;
    }

    FPDF_PAGE page = FPDF_LoadPage(doc, 0);
    if (!page) {
        FPDF_CloseDocument(doc);
        result.error = QString("Failed to load first page: %1").arg(filePath);
        return result;
    }

    double pw = FPDF_GetPageWidth(page);
    double ph = FPDF_GetPageHeight(page);
    if (pw <= 0 || ph <= 0) {
        FPDF_ClosePage(page);
        FPDF_CloseDocument(doc);
        result.error = QString("Invalid page size: %1").arg(filePath);
        return result;
    }

    double scale = (pw > ph) ? (maxDimension / pw) : (maxDimension / ph);
    if (scale <= 0) scale = 1.0;
    int targetW = static_cast<int>(pw * scale);
    int targetH = static_cast<int>(ph * scale);
    if (targetW < 1) targetW = 1;
    if (targetH < 1) targetH = 1;

    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(targetW, targetH, FPDFBitmap_BGRA, nullptr, 0);
    if (!bitmap) {
        FPDF_ClosePage(page);
        FPDF_CloseDocument(doc);
        result.error = QString("Failed to create bitmap %1").arg(filePath);
        return result;
    }
    FPDFBitmap_FillRect(bitmap, 0, 0, targetW, targetH, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, targetW, targetH, 0, FPDF_ANNOT);

    unsigned char *buffer = static_cast<unsigned char*>(FPDFBitmap_GetBuffer(bitmap));
    int stride = FPDFBitmap_GetStride(bitmap);
    QImage img(targetW, targetH, QImage::Format_RGBA8888);
    for (int y = 0; y < targetH; ++y) {
        int copyBytes = stride < img.bytesPerLine() ? stride : (int)img.bytesPerLine();
        memcpy(img.scanLine(y), buffer + y * stride, copyBytes);
    }

    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(doc);

    result.firstPage = std::move(img);
    result.pageCount = pageCount;
    result.success = true;
    return result;
}
