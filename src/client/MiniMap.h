#pragma once

#include <QImage>
#include <QList>
#include <QWidget>

#include "client/ViewModel.h"

namespace kot::core { class ContentRegistry; }

namespace kot::client {

// The map, small: terrain, fog, towns and heroes as dots in their owner's colour,
// and a frame around the part the map view shows. Click to jump there. It shows
// the explored part (at least 24 tiles across), growing as the map is explored.
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
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QRectF mapRect() const;
    core::Pos tileAt(QPointF p) const;
    void rebuildImage();  // the terrain picture, made when the view changes

    const core::ContentRegistry* m_content;
    GameView m_view;
    QList<QColor> m_colors;
    QList<QString> m_kinds;  // terrain id per palette index
    QList<bool> m_water;
    QImage m_image;          // kPixels per tile
    QRect m_area;            // the tiles shown: what is explored (with a margin), at least kMinArea across
    QRect m_visible;
};

}  // namespace kot::client
