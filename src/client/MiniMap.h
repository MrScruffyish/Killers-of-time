#pragma once

#include <QList>
#include <QWidget>

#include "client/ViewModel.h"

namespace kot::core { class ContentRegistry; }

namespace kot::client {

// The whole map, small: terrain, fog, towns and heroes as dots in their owner's
// colour, and a frame around the part the map view shows. Click to jump there.
class MiniMap : public QWidget {
    Q_OBJECT
public:
    explicit MiniMap(const core::ContentRegistry* content, QWidget* parent = nullptr);

    void setView(const GameView& view);
    void setVisibleTiles(const QRect& tiles);
    QSize sizeHint() const override { return {250, 170}; }

signals:
    void centerRequested(core::Pos tile);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QRectF mapRect() const;
    core::Pos tileAt(QPointF p) const;

    const core::ContentRegistry* m_content;
    GameView m_view;
    QList<QColor> m_colors;
    QRect m_visible;
};

}  // namespace kot::client
