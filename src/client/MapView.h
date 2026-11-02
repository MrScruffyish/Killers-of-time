#pragma once

#include <QAbstractScrollArea>
#include <QColor>
#include <QList>

#include "client/ViewModel.h"

namespace kot::core { class ContentRegistry; }

namespace kot::client {

// Draws the player's view of the map with placeholder graphics: terrain colours,
// black for unexplored tiles, darkened for remembered ones, simple shapes for heroes,
// towns, mines and dwellings. Scrolls with the scroll bars, zooms with Ctrl + wheel.
class MapView : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit MapView(const core::ContentRegistry* content, QWidget* parent = nullptr);

    void setView(const GameView& view);
    void setSelectedHero(int heroId);
    void centerOn(core::Pos tile);
    int tileSize() const { return m_tile; }
    void setTileSize(int pixels);
    core::Pos tileAt(const QPoint& viewportPos) const;
    // Where a tile is inside the viewport (for tests and overlays).
    QRect tileRect(core::Pos tile) const;

signals:
    void tileClicked(core::Pos tile);        // left click
    void tileRightClicked(core::Pos tile);   // right click

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateScrollBars();
    QColor ownerColor(int owner) const;

    const core::ContentRegistry* m_content;
    GameView m_view;
    QList<QColor> m_paletteColors;
    int m_selectedHero = 0;
    int m_tile = 14;
    QPoint m_pressPos;
};

}  // namespace kot::client
