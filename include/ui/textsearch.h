#ifndef TEXTSEARCH_H
#define TEXTSEARCH_H

#include <QString>
#include <QStringList>
#include <QRect>
#include <vector>

/**
 * @brief Represents a single search result
 */
struct SearchResult {
    int pageIndex;
    QRect boundingRect;
    QString matchedText;
    
    SearchResult(int page, const QRect &rect, const QString &text)
        : pageIndex(page), boundingRect(rect), matchedText(text) {}
};

/**
 * @brief Manages text search functionality for PDF documents
 */
struct TextSearch {
    // Search parameters
    QString searchTerm;
    bool caseSensitive;
    bool wholeWords;
    
    // Search results
    std::vector<SearchResult> results;
    int currentResultIndex;
    
    // Search state
    bool isSearchActive;
    bool isSearching;
    
    // Constructor
    TextSearch()
        : caseSensitive(false)
        , wholeWords(false)
        , currentResultIndex(-1)
        , isSearchActive(false)
        , isSearching(false)
    {
    }
    
    // Start a new search
    void startSearch(const QString &term, bool caseSensitive = false, bool wholeWords = false) {
        this->searchTerm = term;
        this->caseSensitive = caseSensitive;
        this->wholeWords = wholeWords;
        this->currentResultIndex = -1;
        this->isSearchActive = true;
        this->isSearching = true;
        
        // Clear previous results
        results.clear();
    }
    
    // Add a search result
    void addResult(int pageIndex, const QRect &boundingRect, const QString &matchedText) {
        results.emplace_back(pageIndex, boundingRect, matchedText);
    }
    
    // Finish search
    void finishSearch() {
        isSearching = false;
        if (!results.empty() && currentResultIndex < 0) {
            currentResultIndex = 0;
        }
    }
    
    // Clear search
    void clearSearch() {
        searchTerm.clear();
        results.clear();
        currentResultIndex = -1;
        isSearchActive = false;
        isSearching = false;
    }
    
    // Navigate to next result
    bool nextResult() {
        if (results.empty()) return false;
        
        currentResultIndex = (currentResultIndex + 1) % static_cast<int>(results.size());
        return true;
    }
    
    // Navigate to previous result
    bool previousResult() {
        if (results.empty()) return false;
        
        currentResultIndex = (currentResultIndex - 1 + static_cast<int>(results.size())) % static_cast<int>(results.size());
        return true;
    }
    
    // Get current result
    const SearchResult* getCurrentResult() const {
        if (currentResultIndex < 0 || currentResultIndex >= static_cast<int>(results.size())) {
            return nullptr;
        }
        return &results[currentResultIndex];
    }
    
    // Get result count
    int getResultCount() const {
        return static_cast<int>(results.size());
    }
    
    // Get current result index (1-based)
    int getCurrentResultIndex() const {
        return currentResultIndex >= 0 ? currentResultIndex + 1 : 0;
    }
    
    // Check if search has results
    bool hasResults() const {
        return !results.empty();
    }
    
    // Check if search is active
    bool isActive() const {
        return isSearchActive;
    }
    
    // Check if currently searching
    bool isInProgress() const {
        return isSearching;
    }
    
    // Get search summary string
    QString getSummary() const {
        if (isSearching) {
            return "Searching...";
        }
        
        if (results.empty()) {
            return "No results";
        }
        
        return QString("%1 of %2").arg(getCurrentResultIndex()).arg(getResultCount());
    }
    
    // Get results for a specific page
    std::vector<SearchResult> getResultsForPage(int pageIndex) const {
        std::vector<SearchResult> pageResults;
        
        for (const auto &result : results) {
            if (result.pageIndex == pageIndex) {
                pageResults.push_back(result);
            }
        }
        
        return pageResults;
    }
    
    // Check if a page has search results
    bool hasResultsOnPage(int pageIndex) const {
        for (const auto &result : results) {
            if (result.pageIndex == pageIndex) {
                return true;
            }
        }
        return false;
    }
};

#endif // TEXTSEARCH_H
