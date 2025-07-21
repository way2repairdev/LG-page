#pragma once

#include <QWidget>
#include <QString>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QTimer>
#include <QComboBox>
#include <QProgressBar>
#include <memory>

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
    void closePCB();
    bool isPCBLoaded() const;
    QString getCurrentFilePath() const;

    // Viewer operations
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void resetView();
    void setZoomLevel(double zoom);
    double getZoomLevel() const;

    // Layer management
    void showLayer(const QString &layerName, bool visible);
    void showAllLayers();
    void hideAllLayers();
    QStringList getLayerNames() const;

    // Component operations
    void highlightComponent(const QString &reference);
    void highlightNet(const QString &netName);
    void clearHighlights();
    void clearSelection();
    QStringList getComponentList() const;
    QString getSelectedPinInfo() const;

    // UI state
    void setToolbarVisible(bool visible);
    bool isToolbarVisible() const;
    void setStatusMessage(const QString &message);
    
    // ImGui UI control (for debugging/advanced features)
    void setImGuiUIEnabled(bool enabled);
    bool isImGuiUIEnabled() const;

signals:
    // File events
    void pcbLoaded(const QString &filePath);
    void pcbClosed();
    void errorOccurred(const QString &error);
    void statusMessage(const QString &message);

    // View events
    void zoomChanged(double zoomLevel);
    void viewChanged();

    // Selection events
    void componentSelected(const QString &reference);
    void pinSelected(const QString &pinName, const QString &netName);
    void netHighlighted(const QString &netName);

public slots:
    // Toolbar actions
    void onZoomInClicked();
    void onZoomOutClicked();
    void onZoomToFitClicked();
    void onResetViewClicked();
    void onZoomSliderChanged(int value);
    void onLayerComboChanged(const QString &layerName);
    
    // Update functions
    void updateViewer();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private slots:
    void onPCBViewerError(const QString &error);
    void onPCBViewerStatus(const QString &status);
    void onPinSelected(const QString &pinName, const QString &netName);
    void onZoomLevelChanged(double zoom);

private:
    void initializePCBViewer();
    void setupUI();
    void setupToolbar();
    void connectSignals();
    void updateToolbarState();
    void updateZoomSlider();
    void handleViewerError(const QString &error);
    void handleViewerStatus(const QString &status);

    // PCB viewer core
    std::unique_ptr<PCBViewerEmbedder> m_pcbEmbedder;
    
    // UI components
    QVBoxLayout *m_mainLayout;
    QToolBar *m_toolbar;
    QHBoxLayout *m_toolbarLayout;
    QWidget *m_viewerContainer;
    QTimer *m_updateTimer;
    
    // Toolbar controls
    QAction *m_zoomInAction;
    QAction *m_zoomOutAction;
    QAction *m_zoomToFitAction;
    QAction *m_resetViewAction;
    QSlider *m_zoomSlider;
    QLabel *m_zoomLabel;
    QComboBox *m_layerCombo;
    QLabel *m_statusLabel;
    QProgressBar *m_progressBar;

    // State variables
    bool m_viewerInitialized;
    bool m_pcbLoaded;
    bool m_usingFallback;
    bool m_toolbarVisible;
    QString m_currentFilePath;
    double m_lastZoomLevel;
    QString m_lastStatusMessage;
    
    // Update management
    bool m_needsUpdate;
    bool m_isUpdating;
};
