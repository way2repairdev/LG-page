/*
 * Example integration of HybridPDFViewer into your main application
 * This shows how to use both Qt's native PDF viewer and your custom OpenGL viewer
 */

#include "ui/hybridpdfviewer.h"
#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QLabel>
#include <QKeySequence>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr)
        : QMainWindow(parent)
        , m_pdfViewer(new HybridPDFViewer(this))
        , m_statusLabel(new QLabel("Ready", this))
    {
        setupUI();
        setupMenus();
        setupConnections();
        
        // Set window properties
        setWindowTitle("Hybrid PDF Viewer Demo");
        setMinimumSize(1200, 800);
        resize(1400, 900);
        
        // Add status bar
        statusBar()->addWidget(m_statusLabel);
        statusBar()->showMessage("Ready");
    }

private slots:
    void openPDF()
    {
        QString filePath = QFileDialog::getOpenFileName(
            this, 
            "Open PDF File", 
            "", 
            "PDF Files (*.pdf);;All Files (*)"
        );
        
        if (!filePath.isEmpty()) {
            if (m_pdfViewer->loadPDF(filePath)) {
                statusBar()->showMessage(QString("Loaded: %1").arg(filePath));
                setWindowTitle(QString("Hybrid PDF Viewer - %1").arg(QFileInfo(filePath).fileName()));
            } else {
                QMessageBox::warning(this, "Error", "Failed to load PDF file");
            }
        }
    }
    
    void closePDF()
    {
        m_pdfViewer->closePDF();
        statusBar()->showMessage("PDF closed");
        setWindowTitle("Hybrid PDF Viewer Demo");
    }
    
    void switchViewer()
    {
        auto currentMode = m_pdfViewer->getViewerMode();
        auto newMode = (currentMode == HybridPDFViewer::QtNativeViewer) ? 
                       HybridPDFViewer::CustomOpenGLViewer : 
                       HybridPDFViewer::QtNativeViewer;
        
        m_pdfViewer->setViewerMode(newMode);
        
        QString modeText = (newMode == HybridPDFViewer::QtNativeViewer) ? 
                          "Qt Native" : "OpenGL High Performance";
        statusBar()->showMessage(QString("Switched to %1 viewer").arg(modeText));
    }
    
    void showAbout()
    {
        QMessageBox::about(this, "About Hybrid PDF Viewer",
            "<h3>Hybrid PDF Viewer</h3>"
            "<p>This application demonstrates the integration of:</p>"
            "<ul>"
            "<li><b>Qt's Native PDF Viewer</b> - Standard Qt PDF rendering</li>"
            "<li><b>Custom OpenGL PDF Viewer</b> - High-performance rendering with PDFium</li>"
            "</ul>"
            "<p>Features:</p>"
            "<ul>"
            "<li>Hardware-accelerated OpenGL rendering</li>"
            "<li>Advanced search capabilities</li>"
            "<li>Cursor-based zooming</li>"
            "<li>Background rendering optimization</li>"
            "<li>Seamless switching between viewers</li>"
            "</ul>"
        );
    }
    
    void onPdfLoaded(const QString &filePath)
    {
        statusBar()->showMessage(QString("PDF loaded: %1").arg(QFileInfo(filePath).fileName()));
    }
    
    void onViewerModeChanged(HybridPDFViewer::ViewerMode mode)
    {
        QString modeText = (mode == HybridPDFViewer::QtNativeViewer) ? 
                          "Qt Native" : "OpenGL High Performance";
        m_statusLabel->setText(QString("Mode: %1").arg(modeText));
    }
    
    void onPageChanged(int currentPage, int totalPages)
    {
        statusBar()->showMessage(QString("Page %1 of %2").arg(currentPage).arg(totalPages));
    }

private:
    void setupUI()
    {
        setCentralWidget(m_pdfViewer);
    }
    
    void setupMenus()
    {
        // File menu
        QMenu *fileMenu = menuBar()->addMenu("&File");
        
        QAction *openAction = fileMenu->addAction("&Open PDF...");
        openAction->setShortcut(QKeySequence::Open);
        connect(openAction, &QAction::triggered, this, &MainWindow::openPDF);
        
        QAction *closeAction = fileMenu->addAction("&Close PDF");
        closeAction->setShortcut(QKeySequence::Close);
        connect(closeAction, &QAction::triggered, this, &MainWindow::closePDF);
        
        fileMenu->addSeparator();
        
        QAction *exitAction = fileMenu->addAction("E&xit");
        exitAction->setShortcut(QKeySequence::Quit);
        connect(exitAction, &QAction::triggered, this, &QWidget::close);
        
        // View menu
        QMenu *viewMenu = menuBar()->addMenu("&View");
        
        QAction *switchAction = viewMenu->addAction("&Switch Viewer");
        switchAction->setShortcut(QKeySequence("Ctrl+T"));
        connect(switchAction, &QAction::triggered, this, &MainWindow::switchViewer);
        
        viewMenu->addSeparator();
        
        QAction *zoomInAction = viewMenu->addAction("Zoom &In");
        zoomInAction->setShortcut(QKeySequence::ZoomIn);
        connect(zoomInAction, &QAction::triggered, m_pdfViewer, &HybridPDFViewer::zoomIn);
        
        QAction *zoomOutAction = viewMenu->addAction("Zoom &Out");
        zoomOutAction->setShortcut(QKeySequence::ZoomOut);
        connect(zoomOutAction, &QAction::triggered, m_pdfViewer, &HybridPDFViewer::zoomOut);
        
        QAction *zoomFitAction = viewMenu->addAction("&Fit to Page");
        zoomFitAction->setShortcut(QKeySequence("Ctrl+0"));
        connect(zoomFitAction, &QAction::triggered, m_pdfViewer, &HybridPDFViewer::zoomToFit);
        
        // Help menu
        QMenu *helpMenu = menuBar()->addMenu("&Help");
        
        QAction *aboutAction = helpMenu->addAction("&About");
        connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    }
    
    void setupConnections()
    {
        connect(m_pdfViewer, &HybridPDFViewer::pdfLoaded, this, &MainWindow::onPdfLoaded);
        connect(m_pdfViewer, &HybridPDFViewer::viewerModeChanged, this, &MainWindow::onViewerModeChanged);
        connect(m_pdfViewer, &HybridPDFViewer::pageChanged, this, &MainWindow::onPageChanged);
    }

private:
    HybridPDFViewer *m_pdfViewer;
    QLabel *m_statusLabel;
};

// Example usage in main application
/*
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    // Set application properties
    app.setApplicationName("Hybrid PDF Viewer Demo");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("Your Company");
    
    MainWindow window;
    window.show();
    
    return app.exec();
}
*/

#include "hybrid_pdf_demo.moc"
