#include "client/Settings.h"

#include <QEvent>
#include <QSettings>
#include <QWidget>

namespace kot::client {

QString settingsFile() {
    return QSettings().fileName();
}

namespace {

class GeometryKeeper : public QObject {
public:
    GeometryKeeper(QWidget* w, const QString& key) : QObject(w), m_window(w), m_key(QStringLiteral("geometry/") + key) {}

protected:
    bool eventFilter(QObject* watched, QEvent* e) override {
        if (watched != m_window) return false;
        if (e->type() == QEvent::Show && !m_restored) {
            m_restored = true;
            const QByteArray g = QSettings().value(m_key).toByteArray();
            if (!g.isEmpty()) m_window->restoreGeometry(g);
        } else if (e->type() == QEvent::Hide || e->type() == QEvent::Close) {
            QSettings().setValue(m_key, m_window->saveGeometry());
        }
        return false;
    }

private:
    QWidget* m_window;
    QString m_key;
    bool m_restored = false;
};

}  // namespace

void rememberGeometry(QWidget* window, const QString& key) {
    window->installEventFilter(new GeometryKeeper(window, key));
}

}  // namespace kot::client
