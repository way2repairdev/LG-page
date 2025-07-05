#include <QApplication>
#include <QMainWindow>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QMessageBox>
#include <QDebug>
#include <QTimer>
#include <QFileDialog>
#include "ui/pdfviewerwidget.h"

class TestMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    TestMainWindow(QWidget *parent = nullptr)
        : QMainWindow(parent)
        , m_pdfViewer(nullptr)
    {
        setupUI();
    }

private slots:
    void onOpenPDFClicked()
    {
        qDebug() << "Opening PDF test file...";
        QString filePath = "c:/Users/Rathe/OneDrive/Documents/QT/LoginPage/test.pdf";
        
        if (m_pdfViewer) {
            delete m_pdfViewer;
        }
        
        m_pdfViewer = new PDFViewerWidget();
        setCentralWidget(m_pdfViewer);
        
        // Connect signals
        connect(m_pdfViewer, &PDFViewerWidget::pdfLoaded, this, [this](const QString &path) {
            qDebug() << "PDF loaded successfully:" << path;
            QMessageBox::information(this, "Success", "PDF loaded successfully!");
        });
        
        connect(m_pdfViewer, &PDFViewerWidget::errorOccurred, this, [this](const QString &error) {
            qDebug() << "PDF error:" << error;
            QMessageBox::warning(this, "Error", "PDF Error: " + error);
        });
        
        // Try to load the PDF with a small delay
        QTimer::singleShot(100, this, [this, filePath]() {
            qDebug() << "Attempting to load PDF...";
            if (!m_pdfViewer->loadPDF(filePath)) {
                qDebug() << "Failed to load PDF";
                QMessageBox::warning(this, "Error", "Failed to load PDF file!");
            }
        });
    }

private:
    void setupUI()
    {
        QWidget *centralWidget = new QWidget(this);
        setCentralWidget(centralWidget);
        
        QVBoxLayout *layout = new QVBoxLayout(centralWidget);
        
        QPushButton *openPDFButton = new QPushButton("Open Test PDF", this);
        layout->addWidget(openPDFButton);
        
        connect(openPDFButton, &QPushButton::clicked, this, &TestMainWindow::onOpenPDFClicked);
        
        setWindowTitle("PDF Viewer Test");
        resize(800, 600);
    }

private:
    PDFViewerWidget *m_pdfViewer;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    TestMainWindow window;
    window.show();
    
    return app.exec();
}

#include "debug_test.moc"
