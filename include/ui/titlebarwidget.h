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
        void updateMaximizeIcon(); // Expose updateMaximizeIcon to public

signals:
    void minimizeRequested();
    void maximizeRestoreRequested();
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Custom glyph icons for a consistent, professional look
    QIcon makeMinIcon(const QColor &color, const QSize &size = QSize(18,18));
    QIcon makeMaxIcon(const QColor &color, const QSize &size = QSize(18,18));
    QIcon makeRestoreIcon(const QColor &color, const QSize &size = QSize(18,18));
    QIcon makeCloseIcon(const QColor &color, const QSize &size = QSize(18,18));

    QLabel *m_iconLabel { nullptr };
    QLabel *m_titleLabel { nullptr };
    QToolButton *m_minButton { nullptr };
    QToolButton *m_maxButton { nullptr };
    QToolButton *m_closeButton { nullptr };
    QWidget *m_controlsContainer { nullptr };
    QPoint m_dragStartPos;
    int m_height { 56 }; // Tall title bar
};

#endif // TITLEBARWIDGET_H
