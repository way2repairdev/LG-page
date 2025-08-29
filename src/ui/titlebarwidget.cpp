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

    // Controls cluster container for a compact, premium look
    m_controlsContainer = new QWidget(this);
    auto *controlsLayout = new QHBoxLayout(m_controlsContainer);
    controlsLayout->setContentsMargins(6, 2, 6, 2);
    controlsLayout->setSpacing(4);
    m_controlsContainer->setLayout(controlsLayout);
    m_controlsContainer->setObjectName("controlsCluster");

    m_minButton = new QToolButton(m_controlsContainer);
    m_minButton->setAutoRaise(true);
    m_minButton->setIcon(makeMinIcon(Qt::white));
    m_minButton->setIconSize(QSize(16,16));
    m_minButton->setFixedSize(QSize(32,32)); // tighter, less bulky
    m_minButton->setToolTip("Minimize");
    m_minButton->setAccessibleName("Minimize window");
    connect(m_minButton, &QToolButton::clicked, this, &TitleBarWidget::minimizeRequested);

    m_maxButton = new QToolButton(m_controlsContainer);
    m_maxButton->setAutoRaise(true);
    m_maxButton->setIcon(makeMaxIcon(Qt::white));
    m_maxButton->setIconSize(QSize(16,16));
    m_maxButton->setFixedSize(QSize(32,32));
    m_maxButton->setToolTip("Maximize");
    m_maxButton->setAccessibleName("Maximize or restore window");
    connect(m_maxButton, &QToolButton::clicked, this, &TitleBarWidget::maximizeRestoreRequested);

    m_closeButton = new QToolButton(m_controlsContainer);
    m_closeButton->setAutoRaise(true);
    m_closeButton->setIcon(makeCloseIcon(Qt::white));
    m_closeButton->setIconSize(QSize(16,16));
    m_closeButton->setFixedSize(QSize(32,32));
    m_closeButton->setToolTip("Close");
    m_closeButton->setAccessibleName("Close window");
    connect(m_closeButton, &QToolButton::clicked, this, &TitleBarWidget::closeRequested);

    controlsLayout->addWidget(m_minButton);
    controlsLayout->addWidget(m_maxButton);
    controlsLayout->addWidget(m_closeButton);
    layout->addWidget(m_controlsContainer);

    setLayout(layout);

    setAttribute(Qt::WA_StyledBackground, true);
    // Material-like controls with clear affordances and accessibility
    setStyleSheet(
        "TitleBarWidget {"
        "  color: #0F172A;"
        "  border: none;"
        "}"
        "TitleBarWidget QLabel { color: #0F172A; }"
    "#controlsCluster {"
    "  background: rgba(255,255,255,0.14);" /* subtle pill over blue */
    "  border-radius: 18px;"
    "}"
    "QToolButton {"
    "  padding: 6px;"                     /* 32px buttons */
    "  border-radius: 16px;"              /* circular for 32px */
        "  background: transparent;"
        "  color: #0F172A;"
        "  outline: none;"
        "}"
        "QToolButton:hover {"
    "  background: rgba(255,255,255,0.18);" /* neutral hover tuned for blue bg */
        "}"
        "QToolButton:pressed {"
    "  background: rgba(255,255,255,0.26);" /* pressed */
        "}"
    "QToolButton:focus {"
    "  box-shadow: 0 0 0 2px rgba(255,255,255,0.6);" /* focus ring visible on blue */
        "}"
        /* Close button with caution color on hover/press */
    "QToolButton#closeBtn:hover { background: rgba(244, 67, 54, 0.18); }"
    "QToolButton#closeBtn:pressed { background: rgba(244, 67, 54, 0.26); }"
    );

    // Give the close button an object name for targeted styling
    m_closeButton->setObjectName("closeBtn");

    // Keep maximize icon in sync with window state
    if (auto *w = window()) {
        w->installEventFilter(this);
    updateMaximizeIcon();
    }
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

bool TitleBarWidget::eventFilter(QObject *watched, QEvent *event) {
    if (watched == window()) {
        switch (event->type()) {
            case QEvent::WindowStateChange:
            case QEvent::Show:
            case QEvent::Resize:
                updateMaximizeIcon();
                break;
            default:
                break;
        }
    }
    return QWidget::eventFilter(watched, event);
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

void TitleBarWidget::updateMaximizeIcon() {
    auto *w = window();
    if (!w || !m_maxButton) return;
    bool maximized = w->isMaximized();
    if (!maximized) {
        // Heuristic: treat as maximized if the frame fills the available screen area
        const QRect frame = w->frameGeometry();
        QScreen *scr = QGuiApplication::screenAt(frame.center());
        if (!scr) scr = QGuiApplication::primaryScreen();
        if (scr) {
            const QRect avail = scr->availableGeometry();
            // Allow small off-by-ones due to DPI and borders
            const int tol = 2;
            maximized = std::abs(frame.left() - avail.left()) <= tol &&
                        std::abs(frame.top() - avail.top()) <= tol &&
                        std::abs(frame.right() - avail.right()) <= tol &&
                        std::abs(frame.bottom() - avail.bottom()) <= tol;
        }
    }
    m_maxButton->setIcon(maximized ? makeRestoreIcon(Qt::white) : makeMaxIcon(Qt::white));
    m_maxButton->setToolTip(maximized ? "Restore" : "Maximize");
}

// --- Simple crisp white glyph icons ---
static QIcon makeGlyph(const QSize &size, std::function<void(QPainter&, const QRectF&)> draw) {
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    QRectF box(0, 0, size.width(), size.height());
    draw(p, box.adjusted(2, 2, -2, -2));
    p.end();
    return QIcon(QPixmap::fromImage(img));
}

QIcon TitleBarWidget::makeMinIcon(const QColor &color, const QSize &size) {
    return makeGlyph(size, [color](QPainter &p, const QRectF &r){
        QPen pen(color, 2.0, Qt::SolidLine, Qt::SquareCap);
        p.setPen(pen);
        const qreal y = r.center().y();
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    });
}

QIcon TitleBarWidget::makeMaxIcon(const QColor &color, const QSize &size) {
    return makeGlyph(size, [color](QPainter &p, const QRectF &r){
        QPen pen(color, 2.0);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        QRectF box = r.adjusted(1, 1, -1, -1);
        p.drawRect(box);
    });
}

QIcon TitleBarWidget::makeRestoreIcon(const QColor &color, const QSize &size) {
    return makeGlyph(size, [color](QPainter &p, const QRectF &r){
        QPen pen(color, 2.0);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        // Draw two overlapping squares
        QRectF back = QRectF(r.left()+3, r.top(), r.width()-3, r.height()-3);
        QRectF front = QRectF(r.left(), r.top()+3, r.width()-3, r.height()-3);
        p.drawRect(back);
        p.drawRect(front);
    });
}

QIcon TitleBarWidget::makeCloseIcon(const QColor &color, const QSize &size) {
    return makeGlyph(size, [color](QPainter &p, const QRectF &r){
        QPen pen(color, 2.0, Qt::SolidLine, Qt::RoundCap);
        p.setPen(pen);
        p.drawLine(r.topLeft(), r.bottomRight());
        p.drawLine(r.topRight(), r.bottomLeft());
    });
}
