#include "ui/titlebarwidget.h"

#include <QStyle>
#include <QMouseEvent>
#include <QApplication>
#include <QPainter>
#include <QWindow>
#include <QScreen>
#include <QStyleHints>

TitleBarWidget::TitleBarWidget(QWidget *parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(m_height);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(8);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(44, 44); // larger logo
    m_iconLabel->setScaledContents(true);
    layout->addWidget(m_iconLabel, 0, Qt::AlignVCenter);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setText("Way2Solutions");
    m_titleLabel->setStyleSheet("font-size: 16px; font-weight: 700; color: #0F172A;");
    layout->addWidget(m_titleLabel, 1, Qt::AlignVCenter);

    m_minButton = new QToolButton(this);
    m_minButton->setAutoRaise(true);
    m_minButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarMinButton));
    m_minButton->setIconSize(QSize(16,16));
    m_minButton->setFixedSize(QSize(36,36)); // Material circular hit area
    connect(m_minButton, &QToolButton::clicked, this, &TitleBarWidget::minimizeRequested);

    m_maxButton = new QToolButton(this);
    m_maxButton->setAutoRaise(true);
    m_maxButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    m_maxButton->setIconSize(QSize(16,16));
    m_maxButton->setFixedSize(QSize(36,36)); // Material circular hit area
    connect(m_maxButton, &QToolButton::clicked, this, &TitleBarWidget::maximizeRestoreRequested);

    m_closeButton = new QToolButton(this);
    m_closeButton->setAutoRaise(true);
    m_closeButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    m_closeButton->setIconSize(QSize(16,16));
    m_closeButton->setFixedSize(QSize(36,36)); // Material circular hit area
    connect(m_closeButton, &QToolButton::clicked, this, &TitleBarWidget::closeRequested);

    layout->addWidget(m_minButton);
    layout->addWidget(m_maxButton);
    layout->addWidget(m_closeButton);

    setLayout(layout);

    setAttribute(Qt::WA_StyledBackground, true);
    // Material Light Surface + subtle tint: dark content on near-white surface
            setStyleSheet(
                "TitleBarWidget {"
                "  color: #0F172A;"
                "  border: none;"
                "}"
                "TitleBarWidget QLabel { color: #0F172A; }"
                "QToolButton {"
                "  padding: 6px;"
                "  border-radius: 18px;"  /* circular for 36px buttons */
                "  background: transparent;"
                "  color: #0F172A;"
                "}"
                "QToolButton:hover { background: rgba(0,0,0,0.08); }"  /* Material hover */
                "QToolButton:pressed { background: rgba(0,0,0,0.12); }" /* Material pressed */
            );
}

void TitleBarWidget::setTitle(const QString &title) {
    if (m_titleLabel) m_titleLabel->setText(title);
}

void TitleBarWidget::setIcon(const QIcon &icon) {
    if (m_iconLabel) {
        const int sz = qMin(m_iconLabel->width(), m_iconLabel->height());
        m_iconLabel->setPixmap(icon.pixmap(sz, sz));
    }
}

void TitleBarWidget::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        m_dragStartPos = e->globalPosition().toPoint() - parentWidget()->window()->frameGeometry().topLeft();
        e->accept();
    }
}

void TitleBarWidget::mouseMoveEvent(QMouseEvent *e) {
    if (!(e->buttons() & Qt::LeftButton)) return;
    auto *w = parentWidget()->window();
    if (!w) return;
    w->move(e->globalPosition().toPoint() - m_dragStartPos);
}

void TitleBarWidget::mouseDoubleClickEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) emit maximizeRestoreRequested();
}

void TitleBarWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    // Custom paint: Material light surface with subtle horizontal tint + bottom divider
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QRect r = rect();
    // Horizontal, symmetric "sea" gradient (left -> right): blue edges + soft white near sides
    QLinearGradient g(r.topLeft(), r.topRight());
    g.setColorAt(0.00, QColor("#5D8FEA"));   // deeper blue edge
    g.setColorAt(0.10, QColor("#EAF2FF"));   // foam highlight
    g.setColorAt(0.30, QColor("#AFCBFF"));   // light blue
    g.setColorAt(0.50, QColor("#7FA6F5"));   // gentle mid blue
    g.setColorAt(0.70, QColor("#AFCBFF"));   // light blue
    g.setColorAt(0.90, QColor("#EAF2FF"));   // foam highlight
    g.setColorAt(1.00, QColor("#5D8FEA"));   // deeper blue edge
    p.fillRect(r, g);

    // Top inner highlight for a soft premium edge
    p.fillRect(QRect(0, 0, r.width(), 1), QColor(255, 255, 255, 90));

    // Bottom separator line (very light)
    p.fillRect(QRect(0, r.height() - 1, r.width(), 1), QColor("#E6ECF5"));
}
