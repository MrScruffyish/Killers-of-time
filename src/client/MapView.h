#pragma once

#include <QAbstractScrollArea>
#include <QColor>
#include <QList>

#include "client/MovePlanner.h"
#include "client/ViewModel.h"

namespace kot::core { class ContentRegistry; }

namespace kot::client {

// Draws the player's view of the map with placeholder graphics: terrain colours,
// black for unexplored tiles, darkened for remembered ones, simple shapes for heroes,
// towns, mines and dwellings. Scrolls with the scroll bars, zooms with Ctrl + wheel.
// For the selected hero it tints the tiles he can still reach today and, under the
// mouse, shows the planned path (white dots this turn, red dots later). Tooltips
// describe the tile under the mouse.
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

    // A one-line description of a tile (terrain and what is on it), for tooltips.
    QString describeTile(core::Pos tile) const;
    // The visible part of the map, in tiles (for the minimap).
    QRect visibleTiles() const;
    const MovePlan& plan() const { return m_plan; }

signals:
    void tileClicked(core::Pos tile);        // left click
    void tileRightClicked(core::Pos tile);   // right click
    void scrolled();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool viewportEvent(QEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    void updateScrollBars();
    QColor ownerColor(int owner) const;

    const core::ContentRegistry* m_content;
    GameView m_view;
    QList<QColor> m_paletteColors;
    int m_selectedHero = 0;
    int m_tile = 28;
    QPoint m_pressPos;
    MovePlanner m_planner;
    MovePlan m_plan;
    core::Pos m_hover{-1, -1};
};

}  // namespace kot::client
