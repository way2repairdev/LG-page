#ifndef TEXTEXTRACTION_H
#define TEXTEXTRACTION_H

#include <QString>
#include <QRectF>
#include <QPointF>
#include <vector>
#include <memory>

// Use void pointers to avoid PDFium type conflicts - cast in implementation
using PDFDocument = void*;
using PDFPage = void*;
using PDFTextPage = void*;

// Structure to represent a single character with its coordinates
struct TextChar {
    QChar character;
    QRectF bounds;          // Character bounding box in PDF coordinates
    int fontSize;
    QString fontName;
    
    TextChar() : character('\0'), fontSize(0) {}
    TextChar(QChar ch, const QRectF& rect, int size = 0, const QString& font = "")
        : character(ch), bounds(rect), fontSize(size), fontName(font) {}
};

// Structure to represent a word (collection of characters)
struct TextWord {
    QString text;
    QRectF bounds;          // Word bounding box in PDF coordinates
    std::vector<TextChar> characters;
    
    TextWord() {}
    TextWord(const QString& txt, const QRectF& rect) : text(txt), bounds(rect) {}
};

// Structure to represent a text line (collection of words)
struct TextLine {
    QString text;
    QRectF bounds;          // Line bounding box in PDF coordinates
    std::vector<TextWord> words;
    
    TextLine() {}
    TextLine(const QString& txt, const QRectF& rect) : text(txt), bounds(rect) {}
};

// Structure to represent all text content on a page
struct PageTextContent {
    int pageIndex;
    float pageWidth;
    float pageHeight;
    std::vector<TextLine> lines;
    std::vector<TextWord> words;
    std::vector<TextChar> characters;
    QString fullText;       // Complete text content of the page
    
    PageTextContent() : pageIndex(-1), pageWidth(0), pageHeight(0) {}
    
    // Helper methods
    bool isEmpty() const { return characters.empty(); }
    int getCharacterCount() const { return static_cast<int>(characters.size()); }
    int getWordCount() const { return static_cast<int>(words.size()); }
    int getLineCount() const { return static_cast<int>(lines.size()); }
};

// Text selection state management
class TextSelection {
public:
    TextSelection();
    ~TextSelection();
    
    // Selection management
    void startSelection(int pageIndex, const QPointF& startPoint);
    void updateSelection(int pageIndex, const QPointF& endPoint);
    void endSelection();
    void clearSelection();
    
    // Selection state queries
    bool hasSelection() const { return m_hasSelection; }
    bool isSelecting() const { return m_isSelecting; }
    
    // Selection bounds
    int getStartPage() const { return m_startPageIndex; }
    int getEndPage() const { return m_endPageIndex; }
    QPointF getStartPoint() const { return m_startPoint; }
    QPointF getEndPoint() const { return m_endPoint; }
    QRectF getSelectionRect() const;
    
    // Multi-page selection support
    bool isMultiPageSelection() const { return m_startPageIndex != m_endPageIndex; }
    std::vector<int> getSelectedPages() const;

private:
    bool m_hasSelection;
    bool m_isSelecting;
    int m_startPageIndex;
    int m_endPageIndex;
    QPointF m_startPoint;
    QPointF m_endPoint;
};

// Main text extraction class using PDFium
class TextExtractor {
public:
    TextExtractor();
    ~TextExtractor();
    
    // Main extraction method
    PageTextContent extractPageText(PDFDocument document, int pageIndex);
    
    // Helper methods for coordinate conversion
    static QRectF pdfiumToQRect(double left, double top, double right, double bottom);
    static QPointF pdfiumToQPoint(double x, double y);
    
    // Text search in extracted content
    std::vector<QRectF> findTextInPage(const PageTextContent& pageContent, 
                                       const QString& searchText, 
                                       bool caseSensitive = false);

private:
    // Internal extraction helpers
    void extractCharacters(PDFTextPage textPage, PageTextContent& content);
    void groupCharactersIntoWords(PageTextContent& content);
    void groupWordsIntoLines(PageTextContent& content);
    
    // Character grouping logic
    bool shouldGroupCharacters(const TextChar& char1, const TextChar& char2) const;
    bool shouldGroupWords(const TextWord& word1, const TextWord& word2) const;
    
    // Constants for text grouping
    static constexpr double CHAR_SPACING_THRESHOLD = 2.0;  // Maximum space between chars in a word
    static constexpr double WORD_SPACING_THRESHOLD = 8.0;  // Maximum space between words
    static constexpr double LINE_HEIGHT_THRESHOLD = 5.0;   // Maximum vertical distance for same line
};

#endif // TEXTEXTRACTION_H
