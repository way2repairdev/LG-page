#include "ui/textextraction.h"
#include <QDebug>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

// PDFium includes
#include "fpdfview.h"
#include "fpdf_text.h"

// TextSelection Implementation
TextSelection::TextSelection()
    : m_hasSelection(false)
    , m_isSelecting(false)
    , m_startPageIndex(-1)
    , m_endPageIndex(-1)
{
}

TextSelection::~TextSelection() = default;

void TextSelection::startSelection(int pageIndex, const QPointF& startPoint)
{
    m_isSelecting = true;
    m_hasSelection = false;
    m_startPageIndex = pageIndex;
    m_endPageIndex = pageIndex;
    m_startPoint = startPoint;
    m_endPoint = startPoint;
    
    qDebug() << "TextSelection: Started selection on page" << pageIndex << "at" << startPoint;
}

void TextSelection::updateSelection(int pageIndex, const QPointF& endPoint)
{
    if (!m_isSelecting) {
        return;
    }
    
    m_endPageIndex = pageIndex;
    m_endPoint = endPoint;
    m_hasSelection = true;
    
    qDebug() << "TextSelection: Updated selection to page" << pageIndex << "at" << endPoint;
}

void TextSelection::endSelection()
{
    m_isSelecting = false;
    qDebug() << "TextSelection: Ended selection";
}

void TextSelection::clearSelection()
{
    m_hasSelection = false;
    m_isSelecting = false;
    m_startPageIndex = -1;
    m_endPageIndex = -1;
    m_startPoint = QPointF();
    m_endPoint = QPointF();
    
    qDebug() << "TextSelection: Cleared selection";
}

QRectF TextSelection::getSelectionRect() const
{
    if (!m_hasSelection) {
        return QRectF();
    }
    
    // For single-page selection, return the bounding rectangle
    if (m_startPageIndex == m_endPageIndex) {
        qreal left = qMin(m_startPoint.x(), m_endPoint.x());
        qreal top = qMin(m_startPoint.y(), m_endPoint.y());
        qreal right = qMax(m_startPoint.x(), m_endPoint.x());
        qreal bottom = qMax(m_startPoint.y(), m_endPoint.y());
        
        return QRectF(left, top, right - left, bottom - top);
    }
    
    // For multi-page selection, return empty rect (handled separately)
    return QRectF();
}

std::vector<int> TextSelection::getSelectedPages() const
{
    std::vector<int> pages;
    if (!m_hasSelection) {
        return pages;
    }
    
    int startPage = qMin(m_startPageIndex, m_endPageIndex);
    int endPage = qMax(m_startPageIndex, m_endPageIndex);
    
    for (int i = startPage; i <= endPage; ++i) {
        pages.push_back(i);
    }
    
    return pages;
}

// TextExtractor Implementation
TextExtractor::TextExtractor() = default;
TextExtractor::~TextExtractor() = default;

PageTextContent TextExtractor::extractPageText(PDFDocument document, int pageIndex)
{
    PageTextContent content;
    content.pageIndex = pageIndex;
    
    if (!document) {
        qWarning() << "TextExtractor: Invalid document";
        return content;
    }
    
    // Cast our void pointer to the proper PDFium type
    FPDF_DOCUMENT pdfDoc = static_cast<FPDF_DOCUMENT>(document);
    
    // Load the page
    FPDF_PAGE page = FPDF_LoadPage(pdfDoc, pageIndex);
    if (!page) {
        qWarning() << "TextExtractor: Failed to load page" << pageIndex;
        return content;
    }
    
    // Get page dimensions
    content.pageWidth = static_cast<float>(FPDF_GetPageWidth(page));
    content.pageHeight = static_cast<float>(FPDF_GetPageHeight(page));
    
    qDebug() << "TextExtractor: Extracting text from page" << pageIndex 
             << "size:" << content.pageWidth << "x" << content.pageHeight;
    
    // Load text page
    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
    if (!textPage) {
        qWarning() << "TextExtractor: Failed to load text page" << pageIndex;
        FPDF_ClosePage(page);
        return content;
    }
    
    // Extract characters with coordinates
    extractCharacters(textPage, content);
    
    // Group characters into words and lines
    groupCharactersIntoWords(content);
    groupWordsIntoLines(content);
    
    // Build full text
    for (const auto& line : content.lines) {
        content.fullText += line.text + "\n";
    }
    
    qDebug() << "TextExtractor: Extracted" << content.getCharacterCount() << "characters," 
             << content.getWordCount() << "words," << content.getLineCount() << "lines";
    
    // Cleanup
    FPDFText_ClosePage(textPage);
    FPDF_ClosePage(page);
    
    return content;
}

void TextExtractor::extractCharacters(PDFTextPage textPage, PageTextContent& content)
{
    // Cast our void pointer to the proper PDFium type
    FPDF_TEXTPAGE fpdfTextPage = static_cast<FPDF_TEXTPAGE>(textPage);
    int charCount = FPDFText_CountChars(fpdfTextPage);
    content.characters.reserve(charCount);
    
    for (int i = 0; i < charCount; ++i) {
        // Get character
        unsigned int unicode = FPDFText_GetUnicode(fpdfTextPage, i);
        QChar character(unicode);
        
        // Skip control characters except newlines and tabs
        if (!character.isPrint() && character != '\n' && character != '\t') {
            continue;
        }
        
        // Get character bounds
        double left, top, right, bottom;
        if (FPDFText_GetCharBox(fpdfTextPage, i, &left, &right, &bottom, &top)) {
            // PDFium uses bottom-left origin, we need to convert to top-left origin
            // Also flip the Y coordinate relative to page height
            double flippedTop = content.pageHeight - bottom;
            double flippedBottom = content.pageHeight - top;
            
            // Convert PDFium coordinates to Qt coordinates with proper Y-flip
            QRectF bounds = QRectF(left, flippedTop, right - left, flippedBottom - flippedTop);
            
            // Get font size (approximate)
            double fontSize = FPDFText_GetFontSize(fpdfTextPage, i);
            
            TextChar textChar(character, bounds, static_cast<int>(fontSize));
            content.characters.push_back(textChar);
        }
    }
    
    qDebug() << "TextExtractor: Extracted" << content.characters.size() << "characters";
}

void TextExtractor::groupCharactersIntoWords(PageTextContent& content)
{
    if (content.characters.empty()) {
        return;
    }
    
    content.words.clear();
    TextWord currentWord;
    
    for (size_t i = 0; i < content.characters.size(); ++i) {
        const TextChar& ch = content.characters[i];
        
        // Start new word if this is the first character or if spacing indicates word break
        bool startNewWord = currentWord.characters.empty() ||
                           ch.character.isSpace() ||
                           (i > 0 && !shouldGroupCharacters(content.characters[i-1], ch));
        
        if (startNewWord && !currentWord.characters.empty()) {
            // Finalize current word
            currentWord.text = QString();
            for (const auto& wordChar : currentWord.characters) {
                currentWord.text += wordChar.character;
            }
            
            if (!currentWord.text.trimmed().isEmpty()) {
                content.words.push_back(currentWord);
            }
            currentWord = TextWord();
        }
        
        if (!ch.character.isSpace()) {
            // Add character to current word
            if (currentWord.characters.empty()) {
                currentWord.bounds = ch.bounds;
            } else {
                currentWord.bounds = currentWord.bounds.united(ch.bounds);
            }
            currentWord.characters.push_back(ch);
        }
    }
    
    // Add final word
    if (!currentWord.characters.empty()) {
        currentWord.text = QString();
        for (const auto& wordChar : currentWord.characters) {
            currentWord.text += wordChar.character;
        }
        
        if (!currentWord.text.trimmed().isEmpty()) {
            content.words.push_back(currentWord);
        }
    }
    
    qDebug() << "TextExtractor: Grouped into" << content.words.size() << "words";
}

void TextExtractor::groupWordsIntoLines(PageTextContent& content)
{
    if (content.words.empty()) {
        return;
    }
    
    content.lines.clear();
    TextLine currentLine;
    
    for (size_t i = 0; i < content.words.size(); ++i) {
        const TextWord& word = content.words[i];
        
        // Start new line if this is the first word or if vertical spacing indicates line break
        bool startNewLine = currentLine.words.empty() ||
                           (i > 0 && !shouldGroupWords(content.words[i-1], word));
        
        if (startNewLine && !currentLine.words.empty()) {
            // Finalize current line
            currentLine.text = QString();
            for (size_t j = 0; j < currentLine.words.size(); ++j) {
                if (j > 0) currentLine.text += " ";
                currentLine.text += currentLine.words[j].text;
            }
            
            content.lines.push_back(currentLine);
            currentLine = TextLine();
        }
        
        // Add word to current line
        if (currentLine.words.empty()) {
            currentLine.bounds = word.bounds;
        } else {
            currentLine.bounds = currentLine.bounds.united(word.bounds);
        }
        currentLine.words.push_back(word);
    }
    
    // Add final line
    if (!currentLine.words.empty()) {
        currentLine.text = QString();
        for (size_t j = 0; j < currentLine.words.size(); ++j) {
            if (j > 0) currentLine.text += " ";
            currentLine.text += currentLine.words[j].text;
        }
        content.lines.push_back(currentLine);
    }
    
    qDebug() << "TextExtractor: Grouped into" << content.lines.size() << "lines";
}

bool TextExtractor::shouldGroupCharacters(const TextChar& char1, const TextChar& char2) const
{
    // Check horizontal spacing
    double spacing = char2.bounds.left() - char1.bounds.right();
    return spacing <= CHAR_SPACING_THRESHOLD;
}

bool TextExtractor::shouldGroupWords(const TextWord& word1, const TextWord& word2) const
{
    // Check if words are on the same line (similar vertical position)
    double verticalDistance = qAbs(word1.bounds.center().y() - word2.bounds.center().y());
    if (verticalDistance > LINE_HEIGHT_THRESHOLD) {
        return false;
    }
    
    // Check horizontal spacing
    double spacing = word2.bounds.left() - word1.bounds.right();
    return spacing <= WORD_SPACING_THRESHOLD;
}

QRectF TextExtractor::pdfiumToQRect(double left, double top, double right, double bottom)
{
    // PDFium uses bottom-left origin, Qt uses top-left origin
    return QRectF(left, top, right - left, bottom - top);
}

QPointF TextExtractor::pdfiumToQPoint(double x, double y)
{
    return QPointF(x, y);
}

std::vector<QRectF> TextExtractor::findTextInPage(const PageTextContent& pageContent, 
                                                   const QString& searchText, 
                                                   bool caseSensitive)
{
    std::vector<QRectF> results;
    
    if (searchText.isEmpty() || pageContent.fullText.isEmpty()) {
        return results;
    }
    
    Qt::CaseSensitivity sensitivity = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    QString text = pageContent.fullText;
    
    int startIndex = 0;
    while (true) {
        int index = text.indexOf(searchText, startIndex, sensitivity);
        if (index == -1) {
            break;
        }
        
        // Find corresponding character bounds
        if (index < static_cast<int>(pageContent.characters.size())) {
            QRectF bounds = pageContent.characters[index].bounds;
            
            // Extend bounds to cover the entire search term
            int endIndex = qMin(index + searchText.length() - 1, 
                               static_cast<int>(pageContent.characters.size()) - 1);
            
            for (int i = index + 1; i <= endIndex; ++i) {
                bounds = bounds.united(pageContent.characters[i].bounds);
            }
            
            results.push_back(bounds);
        }
        
        startIndex = index + 1;
    }
    
    return results;
}
