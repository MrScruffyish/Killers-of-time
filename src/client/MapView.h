#pragma once

#include <QAbstractScrollArea>
#include <QColor>
#include <QList>
#include <QHash>
#include <QElapsedTimer>
#include <QPointF>
#include <optional>

class QPainter;

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
    void setTileSize(int pixels);  // snaps to a zoom step
    void zoomStep(int direction);  // +1 bigger, -1 smaller
    core::Pos tileAt(const QPoint& viewportPos) const;
    // Where a tile is inside the viewport (for tests and overlays).
    QRect tileRect(core::Pos tile) const;

    // A one-line description of a tile (terrain and what is on it), for tooltips.
    QString describeTile(core::Pos tile) const;
    // The visible part of the map, in tiles (for the minimap).
    QRect visibleTiles() const;
    const MovePlan& plan() const { return m_plan; }
    // A big title that fades out over the map ("Day 5", "Week 2").
    void showBanner(const QString& title, const QString& subtitle);
    // The plotted route of the selected hero (single click sets it; shown until
    // cleared). Without one the path under the mouse is previewed.
    void setRoute(std::optional<core::Pos> target);
    std::optional<core::Pos> route() const { return m_route; }
    const MovePlan& routePlan() const { return m_routePlan; }
    // Can the selected hero still take a step today?
    bool canMoveToday() const { return m_planner.ready() && !m_planner.reachableToday().isEmpty(); }

signals:
    void tileClicked(core::Pos tile);        // left click
    void tileRightClicked(core::Pos tile);   // right click
    void tileDoubleClicked(core::Pos tile);  // left double click
    void tileShiftClicked(core::Pos tile);   // left click with Shift
    void zoomChanged(int tileSize);
    void hoverInfo(const QString& text);     // what is under the mouse, one line
    void scrolled();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool viewportEvent(QEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void showEvent(QShowEvent* event) override;

private:
    void updateScrollBars();
    void replan();
    void updatePointer();
    void applyCentre(QPointF world);
    void edgeScroll();
    void drawPlan(QPainter& p, const MovePlan& plan, bool pinned) const;
    QColor ownerColor(int owner) const;

    const core::ContentRegistry* m_content;
    GameView m_view;
    QList<QColor> m_paletteColors;
    int m_selectedHero = 0;
    int m_tile = 48;  // one of art::kZoomSteps
    QPoint m_pressPos;
    MovePlanner m_planner;
    MovePlan m_plan;       // under the mouse
    MovePlan m_routePlan;  // to the plotted route's target
    std::optional<core::Pos> m_route;
    bool m_doubleClicked = false;
    bool m_dragging = false;  // the left button is held and the map is being dragged
    QPoint m_dragLast;
    std::optional<QPointF> m_wantCentre;  // tile coordinates to keep in the middle (centerOn)
    class QTimer* m_edgeTimer;
    // Heroes walking from their old tile to the new one (animation only).
    struct Walk {
        QList<core::Pos> steps;
        double progress = 0;  // steps done, with the fraction of the current one
        QList<ViewObject> pickups;  // treasures taken at the end, drawn until he arrives
    };
    QHash<int, Walk> m_walks;
    class QTimer* m_walkTimer;
    QElapsedTimer m_walkClock;
    QString m_bannerTitle, m_bannerSub;
    QElapsedTimer m_bannerClock;
    class QTimer* m_bannerTimer = nullptr;
    core::Pos m_hover{-1, -1};
};

}  // namespace kot::client
