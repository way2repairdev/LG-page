// Phase 2: Asynchronous lightweight PDF preview loader
#pragma once

#include <QString>
#include <QImage>

struct PDFPreviewResult {
    bool success = false;
    QString filePath;
    QImage firstPage;
    int pageCount = 0;
    QString error;
};

PDFPreviewResult LoadPdfFirstPagePreview(const QString &filePath, int maxDimension = 1024);
