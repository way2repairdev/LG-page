#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QPainter>

// Simple semi-transparent overlay indicating a viewer-specific loading task with cancel.
class LoadingOverlay : public QWidget {
    Q_OBJECT
public:
    explicit LoadingOverlay(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_message(new QLabel("Loading...", this))
        , m_progress(new QProgressBar(this))
        , m_cancel(new QPushButton("Cancel", this))
    {
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setAutoFillBackground(false);
        setVisible(false);
        setObjectName("LoadingOverlay");

        m_message->setAlignment(Qt::AlignCenter);
        m_message->setStyleSheet("QLabel{color:#fff;font:12pt 'Segoe UI';font-weight:600;}");
        m_progress->setRange(0,0); // indeterminate by default
        m_progress->setTextVisible(false);
        m_progress->setFixedHeight(6);
        m_cancel->setStyleSheet("QPushButton{background:#d9534f;color:#fff;border:none;padding:6px 14px;border-radius:4px;}QPushButton:hover{background:#c9302c;}");

        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->setContentsMargins(40,40,40,40);
        layout->addStretch();
        QWidget *panel = new QWidget(this);
        panel->setObjectName("overlayPanel");
        panel->setStyleSheet("#overlayPanel{background:rgba(20,20,20,170);border:1px solid rgba(255,255,255,60);border-radius:12px;}");
        QVBoxLayout *pl = new QVBoxLayout(panel);
        pl->setContentsMargins(24,24,24,24);
        pl->setSpacing(12);
        pl->addWidget(m_message);
        pl->addWidget(m_progress);
        pl->addWidget(m_cancel, 0, Qt::AlignCenter);
        layout->addWidget(panel, 0, Qt::AlignCenter);
        layout->addStretch();

        connect(m_cancel, &QPushButton::clicked, this, &LoadingOverlay::cancelRequested);
    }

    void showOverlay(const QString &msg){ setMessage(msg); resizeToParent(); setVisible(true); raise(); }
    void hideOverlay(){ setVisible(false); }
    void setMessage(const QString &msg){ m_message->setText(msg); }
    void setIndeterminate(){ m_progress->setRange(0,0); }
    void setDeterminate(int value, int maximum){ if (maximum<=0){ setIndeterminate(); return;} if(m_progress->maximum()!=maximum)m_progress->setRange(0,maximum); m_progress->setValue(value);}    
    void resizeToParent(){ if(parentWidget()) setGeometry(parentWidget()->rect()); }
    void setCancellable(bool on){ if (m_cancel) m_cancel->setVisible(on); }

signals:
    void cancelRequested();

protected:
    void paintEvent(QPaintEvent *e) override { Q_UNUSED(e); QPainter p(this); p.fillRect(rect(), QColor(0,0,0,100)); }
    void resizeEvent(QResizeEvent *e) override { QWidget::resizeEvent(e); }

private:
    QLabel *m_message;
    QProgressBar *m_progress;
    QPushButton *m_cancel;
};
