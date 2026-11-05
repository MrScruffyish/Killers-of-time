#pragma once

#include <QAbstractScrollArea>
#include <QColor>
#include <QList>
#include <QHash>
#include <QPixmap>
#include <QSet>
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
    // The faint square grid at the bigger zoom steps (remembered in the settings).
    bool showGrid() const { return m_showGrid; }
    void setShowGrid(bool on);
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
    void paintGround(QPainter& p);   // terrain, edges, coasts, trees and fog (cached)
    void paintObjects(QPainter& p);  // everything on top: towns, armies, heroes, routes, banner
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
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
    QStringList m_terrainKeys;      // per terrain id
    int m_selectedHero = 0;
    int m_tile = 48;  // one of art::kZoomSteps
    // The ground (terrain, edges, coasts, trees, shade) is painted in square chunks of the
    // map, kept until their part of the map changes, the zoom or the grid: scrolling only
    // puts chunks on the screen (paintEvent, setView).
    static constexpr int kChunk = 256;  // pixels (logical)
    QHash<quint32, QPixmap> m_chunks;   // key: chunk y << 16 | x
    QList<quint32> m_chunkUse;          // least recently used first (at most kMaxChunks kept)
    QPixmap m_screenGround;             // fog + chunks for the current scroll (walking heroes only redraw what is on top)
    QString m_screenKey;
    int m_groundVersion = 0;            // +1 when chunks were dropped
    // Arrow keys held: the map glides (faster the longer they are held), not a tile per key repeat.
    class QTimer* m_keyScroll;
    QElapsedTimer m_keyClock, m_keyHeld;
    int m_keyDirections = 0;  // bits: 1 left, 2 right, 4 up, 8 down
    QPointF m_keyRest;        // the part of a pixel not scrolled yet
    class QTimer* m_prefetch;           // paints the chunks around the screen when idle
    void prefetchChunk();
    QString m_chunkKey;                 // zoom, grid, pixel ratio of the chunks
    std::optional<QRect> m_groundArea;  // while a chunk is painted: its part of the map (map pixels)
    void dropChunksAround(const QSet<core::Pos>& tiles);
    const QPixmap& chunk(int cx, int cy, qreal dpr);
    QPoint mapOrigin() const;  // the map pixel at the paint area's top left
    QSize paintSize() const;
    void paintFog(QPainter& p);
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
        double slowness = 1.0;      // from the speed setting: 2 slow, 1 normal, 0.5 fast
    };

public:
    // How heroes walk on the map, kept in QSettings: your own and everybody else's.
    enum class WalkSpeed { Slow = 0, Normal = 1, Fast = 2, Instant = 3 };
    static WalkSpeed walkSpeed(bool own);
    static void setWalkSpeed(bool own, WalkSpeed speed);

private:
    QHash<int, Walk> m_walks;
    class QTimer* m_walkTimer;
    QElapsedTimer m_walkClock;
    QString m_bannerTitle, m_bannerSub;
    QElapsedTimer m_bannerClock;
    class QTimer* m_bannerTimer = nullptr;
    core::Pos m_hover{-1, -1};
    bool m_showGrid = true;
};

}  // namespace kot::client
