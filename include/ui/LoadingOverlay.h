#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QCoreApplication>
#include <QEventLoop>

// Simple semi-transparent overlay indicating a viewer-specific loading task with cancel.
class LoadingOverlay : public QWidget {
    Q_OBJECT
public:
    explicit LoadingOverlay(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_panel(new QWidget(this))
        , m_progress(new QProgressBar(this))
        , m_percent(new QLabel(this))
        , m_cancel(new QPushButton("Cancel", this))
    {
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setAutoFillBackground(false);
        setVisible(false);
        setObjectName("LoadingOverlay");

        // Modern, minimal indeterminate bar (bar-only, no percent/text)
        m_progress->setRange(0, 0); // indeterminate by default
        m_progress->setTextVisible(false);
        m_progress->setFixedHeight(8);
        m_progress->setMinimumWidth(260);
        m_progress->setStyleSheet(
            "QProgressBar {"
            "  background: rgba(0,0,0,0.08);"
            "  border: 1px solid rgba(0,0,0,0.12);"
            "  border-radius: 6px;"
            "  height: 8px;"
            "}"
            "QProgressBar::chunk {"
            "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #4FC3F7, stop:1 #1E88E5);"
            "  border-radius: 6px;"
            "}"
        );

        // Centered panel containing only the bar (no text), with subtle light background
        m_panel->setObjectName("overlayPanel");
        m_panel->setStyleSheet(
            "#overlayPanel {"
            "  background: rgba(255,255,255,0.92);"
            "  border: 1px solid rgba(0,0,0,0.06);"
            "  border-radius: 12px;"
            "}"
        );
    QVBoxLayout *panelLayout = new QVBoxLayout(m_panel);
        panelLayout->setContentsMargins(20, 20, 20, 20);
        panelLayout->setSpacing(12);
    // Percent label is not used for bar-only design; keep hidden
    m_percent->setVisible(false);
    m_percent->setAlignment(Qt::AlignCenter);
    m_percent->setStyleSheet("QLabel{color:#1a1a1a;font:10pt 'Segoe UI';}");
    panelLayout->addWidget(m_progress);

        // Cancel button is supported but hidden by default (no text UI requested)
        m_cancel->setVisible(false);
        m_cancel->setCursor(Qt::PointingHandCursor);
        m_cancel->setStyleSheet(
            "QPushButton {"
            "  background: #e53935;"
            "  color: white;"
            "  border: none;"
            "  padding: 6px 12px;"
            "  border-radius: 6px;"
            "  font: 10pt 'Segoe UI';"
            "}"
            "QPushButton:hover { background: #d32f2f; }"
        );
        panelLayout->addWidget(m_cancel, 0, Qt::AlignCenter);

        QVBoxLayout *root = new QVBoxLayout(this);
        root->setContentsMargins(40, 40, 40, 40);
        root->addStretch();
        root->addWidget(m_panel, 0, Qt::AlignCenter);
        root->addStretch();

        connect(m_cancel, &QPushButton::clicked, this, &LoadingOverlay::cancelRequested);
    }

    void showOverlay(const QString &msg){
        Q_UNUSED(msg);
        resizeToParent();
        setVisible(true);
        raise();
        // Ensure first frame is painted before heavy work starts
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    void hideOverlay(){ setVisible(false); }
    void setMessage(const QString &msg){ Q_UNUSED(msg); /* textless overlay; no-op */ }
    void setIndeterminate(){
        m_progress->setRange(0,0);
        m_progress->setFormat(QLatin1String(""));
        m_progress->setTextVisible(false);
        m_percent->setVisible(false);
    }
    void setDeterminate(int /*value*/, int /*maximum*/){
        // Keep the bar indeterminate for the old bar-only design
        setIndeterminate();
    }
    void setProgressPercent(int /*percent*/){
        // Ignore numeric progress; keep the bar in indeterminate mode
        setIndeterminate();
    }
    void resizeToParent(){ if(parentWidget()) setGeometry(parentWidget()->rect()); }
    void setCancellable(bool on){ if (m_cancel) m_cancel->setVisible(on); }

signals:
    void cancelRequested();

protected:
    void paintEvent(QPaintEvent *e) override { Q_UNUSED(e); QPainter p(this); p.fillRect(rect(), QColor(0,0,0,100)); }
    void resizeEvent(QResizeEvent *e) override { QWidget::resizeEvent(e); }

private:
    QWidget *m_panel;
    QProgressBar *m_progress;
    QLabel *m_percent;
    QPushButton *m_cancel;
};
