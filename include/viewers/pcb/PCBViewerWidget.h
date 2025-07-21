#pragma once

#include <QWidget>
#include <QString>
#include <memory>

// Forward declarations
class PCBViewerEmbedder;

/**
 * PCBViewerWidget - Future PCB viewer widget for embedded PCB visualization
 * 
 * This widget will integrate PCB viewing capabilities into the Qt application,
 * supporting various PCB file formats including KiCad, Eagle, and Gerber files.
 * 
 * IMPLEMENTATION STATUS: NOT STARTED - This is a placeholder for future development
 */
class PCBViewerWidget : public QWidget {
    Q_OBJECT

public:
    explicit PCBViewerWidget(QWidget *parent = nullptr);
    ~PCBViewerWidget();

    // File operations (planned)
    bool loadPCB(const QString &filePath);
    void closePCB();
    bool isPCBLoaded() const;

    // Viewer operations (planned)
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void resetView();

    // Layer management (planned)
    void showLayer(int layerId, bool visible);
    void showAllLayers();
    void hideAllLayers();
    QStringList getLayerNames() const;

    // Component operations (planned)
    void highlightComponent(const QString &reference);
    void clearHighlights();
    QStringList getComponentList() const;

signals:
    // File events
    void pcbLoaded(const QString &filePath);
    void pcbClosed();
    void errorOccurred(const QString &error);

    // View events
    void zoomChanged(double zoomLevel);
    void viewChanged();

    // Selection events
    void componentSelected(const QString &reference);
    void netHighlighted(const QString &netName);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void initializePCBViewer();
    void setupUI();

    // Future implementation will include:
    std::unique_ptr<PCBViewerEmbedder> m_pcbEmbedder;
    
    // UI state
    bool m_initialized;
    QString m_currentFilePath;
};
