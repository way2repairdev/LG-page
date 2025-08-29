#ifndef TITLEBARWIDGET_H
#define TITLEBARWIDGET_H

#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QIcon>

class TitleBarWidget : public QWidget {
    Q_OBJECT
public:
    explicit TitleBarWidget(QWidget *parent = nullptr);
    void setTitle(const QString &title);
    void setIcon(const QIcon &icon);
    int preferredHeight() const { return m_height; }

signals:
    void minimizeRequested();
    void maximizeRestoreRequested();
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void updateMaximizeIcon();

    QLabel *m_iconLabel { nullptr };
    QLabel *m_titleLabel { nullptr };
    QToolButton *m_minButton { nullptr };
    QToolButton *m_maxButton { nullptr };
    QToolButton *m_closeButton { nullptr };
    QPoint m_dragStartPos;
    int m_height { 56 }; // Tall title bar
};

#endif // TITLEBARWIDGET_H
