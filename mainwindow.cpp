#include "mainwindow.h"
#include "./ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setupLoginConnections();
    
    // Set window properties
    setWindowTitle("Login Page");
    setFixedSize(800, 600);
    
    // Set focus to username field
    ui->usernameLineEdit->setFocus();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupLoginConnections()
{
    // Connect login button to slot
    connect(ui->loginButton, &QPushButton::clicked, this, &MainWindow::onLoginButtonClicked);
    
    // Connect Enter key press in password field to login
    connect(ui->passwordLineEdit, &QLineEdit::returnPressed, this, &MainWindow::onLoginButtonClicked);
    
    // Connect text change signals for validation
    connect(ui->usernameLineEdit, &QLineEdit::textChanged, this, &MainWindow::onUsernameChanged);
    connect(ui->passwordLineEdit, &QLineEdit::textChanged, this, &MainWindow::onPasswordChanged);
}

void MainWindow::onLoginButtonClicked()
{
    if (!validateInput()) {
        return;
    }
    
    QString username = ui->usernameLineEdit->text().trimmed();
    QString password = ui->passwordLineEdit->text();
    
    performLogin(username, password);
}

void MainWindow::onUsernameChanged()
{
    // Enable/disable login button based on input
    bool hasInput = !ui->usernameLineEdit->text().trimmed().isEmpty() && 
                    !ui->passwordLineEdit->text().isEmpty();
    ui->loginButton->setEnabled(hasInput);
}

void MainWindow::onPasswordChanged()
{
    // Enable/disable login button based on input
    bool hasInput = !ui->usernameLineEdit->text().trimmed().isEmpty() && 
                    !ui->passwordLineEdit->text().isEmpty();
    ui->loginButton->setEnabled(hasInput);
}

bool MainWindow::validateInput()
{
    QString username = ui->usernameLineEdit->text().trimmed();
    QString password = ui->passwordLineEdit->text();
    
    if (username.isEmpty()) {
        QMessageBox::warning(this, "Invalid Input", "Please enter a username.");
        ui->usernameLineEdit->setFocus();
        return false;
    }
    
    if (password.isEmpty()) {
        QMessageBox::warning(this, "Invalid Input", "Please enter a password.");
        ui->passwordLineEdit->setFocus();
        return false;
    }
    
    if (username.length() < 3) {
        QMessageBox::warning(this, "Invalid Input", "Username must be at least 3 characters long.");
        ui->usernameLineEdit->setFocus();
        return false;
    }
    
    if (password.length() < 4) {
        QMessageBox::warning(this, "Invalid Input", "Password must be at least 4 characters long.");
        ui->passwordLineEdit->setFocus();
        return false;
    }
    
    return true;
}

void MainWindow::performLogin(const QString &username, const QString &password)
{
    // For demonstration purposes, using hardcoded credentials
    // In a real application, you would validate against a database or authentication service
    
    if (username == "admin" && password == "password") {
        QMessageBox::information(this, "Login Successful", 
                                QString("Welcome, %1!").arg(username));
        
        // Clear the form after successful login
        ui->usernameLineEdit->clear();
        ui->passwordLineEdit->clear();
        ui->usernameLineEdit->setFocus();
        
        // Here you could navigate to the main application window
        ui->statusbar->showMessage("Login successful!", 3000);
        
    } else if (username == "user" && password == "1234") {
        QMessageBox::information(this, "Login Successful", 
                                QString("Welcome, %1!").arg(username));
        
        ui->usernameLineEdit->clear();
        ui->passwordLineEdit->clear();
        ui->usernameLineEdit->setFocus();
        ui->statusbar->showMessage("Login successful!", 3000);
        
    } else {
        QMessageBox::critical(this, "Login Failed", 
                             "Invalid username or password.\n\nTry:\n- admin / password\n- user / 1234");
        
        ui->passwordLineEdit->clear();
        ui->usernameLineEdit->setFocus();
        ui->statusbar->showMessage("Login failed!", 3000);
    }
}
