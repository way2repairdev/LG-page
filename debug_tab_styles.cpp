// Debug file to test QTabWidget styling issues
// This will help you understand why your styles aren't applying

#include <QApplication>
#include <QTabWidget>
#include <QTabBar>
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QDebug>
#include <QPushButton>
#include <QTextEdit>
#include <QStyleFactory>

class StyleDebugger : public QMainWindow
{
    Q_OBJECT

public:
    StyleDebugger(QWidget *parent = nullptr) : QMainWindow(parent)
    {
        setupUI();
        setWindowTitle("QTabWidget Style Debugger");
        resize(800, 600);
    }

private slots:
    void applyTestStyle()
    {
        // Test with OBVIOUS red background to confirm styling works
        QString testStyle = R"(
            QTabWidget::pane {
                border: 3px solid red;
                background-color: yellow;
            }
            QTabBar::tab {
                background-color: red;
                color: white;
                padding: 10px;
                margin: 2px;
                border: 2px solid blue;
                font-weight: bold;
                font-size: 14px;
            }
            QTabBar::tab:selected {
                background-color: green;
                color: yellow;
            }
            QTabBar::tab:hover {
                background-color: purple;
                color: white;
            }
        )";
        
        m_tabWidget->setStyleSheet(testStyle);
        m_debugOutput->append("Applied OBVIOUS test style (red/yellow/green)");
        m_debugOutput->append("If you don't see these colors, there's a style conflict!");
    }

    void clearStyles()
    {
        m_tabWidget->setStyleSheet("");
        m_debugOutput->append("Cleared all styles - should show default Qt style");
    }

    void applyModernStyle()
    {
        // Your modern style with fixes
        QString modernStyle = R"(
            QTabWidget::pane {
                border: 1px solid #cccccc;
                background-color: white;
                border-radius: 6px;
                margin-top: -1px;
            }
            
            QTabBar::tab {
                background-color: #f3f3f3;
                border: 1px solid #cccccc;
                color: #333333;
                padding: 8px 16px;
                margin-right: 2px;
                border-top-left-radius: 6px;
                border-top-right-radius: 6px;
                font-weight: normal;
                min-width: 80px;
            }
            
            QTabBar::tab:selected {
                background-color: #ffffff;
                color: #333333;
                border-color: #999999;
                font-weight: bold;
                margin-bottom: -1px;
            }
            
            QTabBar::tab:hover:!selected {
                background-color: #e8e8e8;
            }
            
            QTabBar::close-button {
                image: url(data:image/svg+xml;base64,PHN2ZyB3aWR0aD0iMTIiIGhlaWdodD0iMTIiIHZpZXdCb3g9IjAgMCAxMiAxMiIgZmlsbD0ibm9uZSIgeG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIj4KPHBhdGggZD0iTTkgM0w2IDZMMy. gOSIgZmlsbD0iIzY2NjY2NiIvPgo8L3N2Zz4K);
                subcontrol-position: right;
                subcontrol-origin: padding;
                width: 12px;
                height: 12px;
                margin: 2px;
                border-radius: 6px;
            }
            
            QTabBar::close-button:hover {
                background-color: #ff6b6b;
                border-radius: 6px;
            }
            
            QTabBar::scroller {
                width: 20px;
            }
            
            QTabBar QToolButton {
                background-color: #f0f0f0;
                border: 1px solid #cccccc;
                border-radius: 3px;
                margin: 2px;
            }
            
            QTabBar QToolButton:hover {
                background-color: #e0e0e0;
            }
        )";
        
        m_tabWidget->setStyleSheet(modernStyle);
        m_debugOutput->append("Applied modern style");
    }

    void checkStyleConflicts()
    {
        m_debugOutput->append("=== STYLE DEBUGGING INFO ===");
        
        // Check application style
        QString appStyle = QApplication::style()->objectName();
        m_debugOutput->append(QString("Application Style: %1").arg(appStyle));
        
        // Check if there's a global stylesheet
        QString globalSheet = qApp->styleSheet();
        if (globalSheet.isEmpty()) {
            m_debugOutput->append("Global Stylesheet: NONE");
        } else {
            m_debugOutput->append(QString("Global Stylesheet: %1 characters").arg(globalSheet.length()));
            m_debugOutput->append("First 200 chars: " + globalSheet.left(200));
        }
        
        // Check parent stylesheets
        QWidget *parent = m_tabWidget->parentWidget();
        int level = 0;
        while (parent && level < 5) {
            QString parentSheet = parent->styleSheet();
            if (!parentSheet.isEmpty()) {
                m_debugOutput->append(QString("Parent Level %1 (%2): %3 characters")
                    .arg(level).arg(parent->metaObject()->className()).arg(parentSheet.length()));
            }
            parent = parent->parentWidget();
            level++;
        }
        
        // Check current stylesheet
        QString currentSheet = m_tabWidget->styleSheet();
        m_debugOutput->append(QString("Current QTabWidget stylesheet: %1 characters").arg(currentSheet.length()));
        
        m_debugOutput->append("=== END DEBUG INFO ===");
    }

    void forceStyleApplication()
    {
        // Force style refresh
        m_tabWidget->style()->unpolish(m_tabWidget);
        m_tabWidget->style()->polish(m_tabWidget);
        
        // Also polish the tab bar
        QTabBar *tabBar = m_tabWidget->tabBar();
        tabBar->style()->unpolish(tabBar);
        tabBar->style()->polish(tabBar);
        
        m_debugOutput->append("Forced style refresh (unpolish + polish)");
    }

private:
    void setupUI()
    {
        QWidget *central = new QWidget(this);
        setCentralWidget(central);
        
        QVBoxLayout *layout = new QVBoxLayout(central);
        
        // Create test tab widget
        m_tabWidget = new QTabWidget();
        m_tabWidget->setTabsClosable(true);
        m_tabWidget->setMovable(true);
        
        // Enable scrollable tabs
        m_tabWidget->tabBar()->setUsesScrollButtons(true);
        m_tabWidget->tabBar()->setElideMode(Qt::ElideRight);
        m_tabWidget->tabBar()->setExpanding(false);
        
        // Add some test tabs
        for (int i = 1; i <= 5; i++) {
            QLabel *label = new QLabel(QString("Content for Tab %1").arg(i));
            label->setAlignment(Qt::AlignCenter);
            m_tabWidget->addTab(label, QString("Tab %1").arg(i));
        }
        
        layout->addWidget(m_tabWidget);
        
        // Control buttons
        QWidget *controls = new QWidget();
        QVBoxLayout *controlLayout = new QVBoxLayout(controls);
        
        QPushButton *testBtn = new QPushButton("Apply OBVIOUS Test Style (Red/Yellow)");
        connect(testBtn, &QPushButton::clicked, this, &StyleDebugger::applyTestStyle);
        
        QPushButton *modernBtn = new QPushButton("Apply Modern Style");
        connect(modernBtn, &QPushButton::clicked, this, &StyleDebugger::applyModernStyle);
        
        QPushButton *clearBtn = new QPushButton("Clear All Styles");
        connect(clearBtn, &QPushButton::clicked, this, &StyleDebugger::clearStyles);
        
        QPushButton *debugBtn = new QPushButton("Check Style Conflicts");
        connect(debugBtn, &QPushButton::clicked, this, &StyleDebugger::checkStyleConflicts);
        
        QPushButton *forceBtn = new QPushButton("Force Style Refresh");
        connect(forceBtn, &QPushButton::clicked, this, &StyleDebugger::forceStyleApplication);
        
        controlLayout->addWidget(testBtn);
        controlLayout->addWidget(modernBtn);
        controlLayout->addWidget(clearBtn);
        controlLayout->addWidget(debugBtn);
        controlLayout->addWidget(forceBtn);
        
        layout->addWidget(controls);
        
        // Debug output
        m_debugOutput = new QTextEdit();
        m_debugOutput->setMaximumHeight(200);
        layout->addWidget(m_debugOutput);
        
        // Initial debug check
        checkStyleConflicts();
    }
    
    QTabWidget *m_tabWidget;
    QTextEdit *m_debugOutput;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    // Test with different styles
    qDebug() << "Available styles:" << QStyleFactory::keys();
    
    // This matches your main app
    app.setStyle(QStyleFactory::create("Fusion"));
    
    StyleDebugger debugger;
    debugger.show();
    
    return app.exec();
}

#include "debug_tab_styles.moc"
