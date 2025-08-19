#pragma once

#include <QWidget>
#include <QString>
#include <QVBoxLayout>
#include <QToolBar>
#include <QTimer>
#include <QComboBox>
#include <QPushButton>
#include <memory>
#include <QFutureWatcher>

// Forward declarations
class PCBViewerEmbedder;

/**
 * PCBViewerWidget - Qt widget for embedded PCB visualization
 * 
 * This widget integrates PCB viewing capabilities into the Qt application,
 * supporting XZZPCB file format and providing interactive PCB viewing.
 * 
 * Features:
 * - Interactive PCB viewing with zoom/pan
 * - Component and pin selection
 * - Layer visibility control
 * - Net highlighting
 * - Toolbar integration
 */
class PCBViewerWidget : public QWidget {
    Q_OBJECT

public:
    explicit PCBViewerWidget(QWidget *parent = nullptr);
    ~PCBViewerWidget();

    // File operations
    bool loadPCB(const QString &filePath);
    void requestLoad(const QString &filePath); // Phase 1 async wrapper
    void cancelLoad();
    void closePCB();
    bool isPCBLoaded() const;
    QString getCurrentFilePath() const;

    // UI state
    void setToolbarVisible(bool visible);
    bool isToolbarVisible() const;
    QToolBar* getToolbar() const;
    
    // Split view functionality removed

signals:
    // File events
    void pcbLoaded(const QString &filePath);
    void pcbClosed();
    void errorOccurred(const QString &error);
    void loadCancelled();
    // Cross-search request (PCB -> PDF)
    void crossSearchRequest(const QString &term, bool isNet, bool targetIsPdf);
    
    // Split view signals removed

public slots:
    // Update functions
    void updateViewer();
    
    // Split view slot removed

    // Ensure viewport and camera are synced after activation/tab switch
    void ensureViewportSync();
    // Rotation controls
    void rotateLeft();
    void rotateRight();
    void flipHorizontal();
    void flipVertical();
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void toggleDiodeReadings();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onPCBViewerError(const QString &error);

private:
    void initializePCBViewer();
    void setupUI();
    void setupToolbar();
    void connectSignals();

    // PCB viewer core
    std::unique_ptr<PCBViewerEmbedder> m_pcbEmbedder;
    
    // UI components
    QVBoxLayout *m_mainLayout;
    QToolBar *m_toolbar;
    QAction *m_actionRotateLeft{nullptr};
    QAction *m_actionRotateRight{nullptr};
    QAction *m_actionFlipH{nullptr};
    QAction *m_actionFlipV{nullptr};
    QAction *m_actionZoomIn{nullptr};
    QAction *m_actionZoomOut{nullptr};
    QAction *m_actionZoomFit{nullptr};
    QAction *m_actionToggleDiode{nullptr};
    // Net navigation UI
    QComboBox *m_netCombo{nullptr};
    QPushButton *m_netSearchButton{nullptr};
    QWidget *m_viewerContainer;
    QTimer *m_updateTimer;
    
    // Split view components removed

    // State variables
    bool m_viewerInitialized;
    bool m_pcbLoaded;
    bool m_usingFallback;
    bool m_toolbarVisible;
    // Split view state removed
    QString m_currentFilePath;
    // Async scaffolding
    int m_currentLoadId = 0;
    bool m_cancelRequested = false;
    QFutureWatcher<bool> *m_loadWatcher = nullptr;
    class LoadingOverlay *m_loadingOverlay = nullptr;
    QString m_pendingFilePath;
    
    // Update management
    bool m_needsUpdate;
    bool m_isUpdating;
    // Cross-viewer context menu state
    QString m_linkedPdfFileName; // for menu label
    bool m_crossSearchEnabled { true };
    QPoint m_rightPressPos; qint64 m_rightPressTimeMs {0}; bool m_rightDragging {false};
    void showCrossContextMenu(const QPoint &globalPos, const QString &candidate);
    // Guards to ensure single context menu and suppress duplicate triggers
    bool m_contextMenuActive { false };            // true while a context menu is open (exec loop active)
    bool m_suppressNextEmbedderQuickMenu { false }; // one-shot flag to cancel the next embedder quick-right-click callback
    void suppressNextEmbedderMenuOnce() { m_suppressNextEmbedderQuickMenu = true; }
    // Deferred reopen when user right-clicks outside current menu
    bool m_pendingReopenRequested { false };
    QPoint m_pendingReopenGlobalPos;
public:
    void setLinkedPdfFileName(const QString &name) { m_linkedPdfFileName = name; }
    void setCrossSearchEnabled(bool en) { m_crossSearchEnabled = en; }
    // External searches invoked from PDF viewer
    bool externalSearchNet(const QString &net);
    bool externalSearchComponent(const QString &comp);

    // Internal helpers for net navigation
    void populateNetList();
    void highlightCurrentNet();
    void populateNetAndComponentList();

private slots:
    void onPinSelectedFromViewer(const std::string &pinName, const std::string &netName);
    void onPartSelectedFromViewer(const std::string &partName);
    void onNetSearchClicked();
    void onNetComboActivated(int index);
};
