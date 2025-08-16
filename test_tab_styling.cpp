// Quick test file to verify QTabWidget styling works
// Compile and run this to test styling without your full app

#include <QApplication>
#include <QTabWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
#include <QStyleFactory>
#include <QPushButton>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    // Same style as your main app
    app.setStyle(QStyleFactory::create("Fusion"));
    
    QWidget window;
    window.setWindowTitle("Tab Style Test - With Fusion Style");
    window.resize(600, 400);
    
    QVBoxLayout *layout = new QVBoxLayout(&window);
    
    // Test button
    QPushButton *testBtn = new QPushButton("Apply Obvious Test Style");
    
    // Create test tab widget
    QTabWidget *tabWidget = new QTabWidget();
    tabWidget->setTabsClosable(true);
    
    // Add some test tabs
    for (int i = 1; i <= 4; i++) {
        QLabel *label = new QLabel(QString("Content for Tab %1").arg(i));
        label->setAlignment(Qt::AlignCenter);
        tabWidget->addTab(label, QString("Tab %1").arg(i));
    }
    
    // Your modern style with !important
    QString modernStyle = R"(
        QTabWidget {
            background: #ffffff !important;
            border: none !important;
        }
        QTabWidget::pane {
            border: 1px solid #ccc !important;
            background: #ffffff !important;
            border-radius: 0px !important;
            margin-top: -1px !important;
        }
        QTabBar::tab {
            background: #f3f3f3 !important;
            border: 1px solid #ccc !important;
            border-bottom: none !important;
            border-radius: 6px 6px 0px 0px !important;
            padding: 8px 16px 8px 12px !important;
            margin-right: 2px !important;
            color: #333 !important;
            font-size: 12px !important;
            min-width: 60px !important;
        }
        QTabBar::tab:hover {
            background: #e8e8e8 !important;
        }
        QTabBar::tab:selected {
            background: #ffffff !important;
            border: 1px solid #999 !important;
            color: #000 !important;
            font-weight: bold !important;
            margin-bottom: -1px !important;
        }
    )";
    
    // Obvious test style
    QString testStyle = R"(
        QTabWidget::pane {
            border: 5px solid red !important;
            background-color: yellow !important;
        }
        QTabBar::tab {
            background-color: red !important;
            color: white !important;
            padding: 15px !important;
            margin: 3px !important;
            border: 3px solid blue !important;
            font-weight: bold !important;
            font-size: 16px !important;
        }
        QTabBar::tab:selected {
            background-color: green !important;
            color: yellow !important;
        }
        QTabBar::tab:hover {
            background-color: purple !important;
            color: white !important;
        }
    )";
    
    // Apply modern style initially
    tabWidget->setStyleSheet(modernStyle);
    
    // Connect test button
    QObject::connect(testBtn, &QPushButton::clicked, [tabWidget, testStyle]() {
        tabWidget->setStyleSheet(testStyle);
        qDebug() << "Applied OBVIOUS test style - should see red/yellow/green!";
    });
    
    layout->addWidget(testBtn);
    layout->addWidget(tabWidget);
    
    window.show();
    
    qDebug() << "Modern style applied. Click button to test with obvious colors.";
    qDebug() << "If modern style doesn't show, there's a Fusion style conflict.";
    
    return app.exec();
}
