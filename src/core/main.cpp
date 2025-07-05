#include "ui/mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    // Set application properties
    app.setApplicationName("Way2Repair Login System");
    app.setApplicationVersion("4.75");
    app.setOrganizationName("Way2Repair Systems");
    app.setOrganizationDomain("way2repair.com");
    
    // Set application style
    app.setStyle(QStyleFactory::create("Fusion"));
    
    // Create and show main window
    MainWindow window;
    window.show();
    
    return app.exec();
}
