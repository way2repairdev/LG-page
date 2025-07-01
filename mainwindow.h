#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMessageBox>
#include <QLineEdit>
#include <QPushButton>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onLoginButtonClicked();
    void onUsernameChanged();
    void onPasswordChanged();

private:
    Ui::MainWindow *ui;
    
    void setupLoginConnections();
    bool validateInput();
    void performLogin(const QString &username, const QString &password);
};
#endif // MAINWINDOW_H
