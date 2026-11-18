#pragma once

#include <QPixmap>
#include <QString>
#include <QWidget>

class QLabel;

namespace kot::client {

// A short note over a window that fades in, stays a few seconds and fades out
// by itself (nothing to click; clicks go through it). Several stack downwards.
class Toast : public QWidget {
    Q_OBJECT
public:
    static Toast* show(QWidget* host, const QPixmap& icon, const QString& html, int milliseconds = 4500);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Toast(QWidget* host, const QPixmap& icon, const QString& html);
    void place();

    QPixmap m_icon;
    QLabel* m_text;
};

}  // namespace kot::client
