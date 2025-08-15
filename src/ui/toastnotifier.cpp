#include "toastnotifier.h"
#include <QLabel>
#include <QPropertyAnimation>
#include <QTimer>
#include <QGraphicsOpacityEffect>

void ToastNotifier::show(QWidget *parent, const QString &message, int msec) {
    if (!parent) return;
    QLabel *label = new QLabel(parent);
    label->setText(message);
    label->setStyleSheet(
        "QLabel{background:rgba(40,40,40,200);color:#fff;padding:6px 12px;"
        "border-radius:6px;font:10pt 'Segoe UI';}"
    );
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    label->adjustSize();
    QSize ps = label->size();
    int margin = 16;
    QPoint pos(parent->width() - ps.width() - margin, parent->height() - ps.height() - margin);
    label->move(pos);
    label->show();
    auto *eff = new QGraphicsOpacityEffect(label);
    label->setGraphicsEffect(eff);
    eff->setOpacity(0.0);
    auto *fadeIn = new QPropertyAnimation(eff, "opacity", label);
    fadeIn->setDuration(160);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
    QTimer::singleShot(msec, label, [label, eff]() {
        auto *fadeOut = new QPropertyAnimation(eff, "opacity", label);
        fadeOut->setDuration(220);
        fadeOut->setStartValue(1.0);
        fadeOut->setEndValue(0.0);
        QObject::connect(fadeOut, &QPropertyAnimation::finished, label, &QLabel::deleteLater);
        fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
    });
}
