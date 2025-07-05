#include <QtSql/QSqlDatabase>
#include <QDebug>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    qDebug() << "Available SQL Drivers:";
    QStringList drivers = QSqlDatabase::drivers();
    for (const QString &driver : drivers) {
        qDebug() << " -" << driver;
    }
    
    qDebug() << "\nMySQL Driver Available:" << drivers.contains("QMYSQL");
    
    return 0;
}
