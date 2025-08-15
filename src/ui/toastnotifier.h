#pragma once
#include <QWidget>
#include <QString>

class ToastNotifier {
public:
    static void show(QWidget *parent, const QString &message, int msec = 2000);
};
