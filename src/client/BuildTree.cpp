#include "client/BuildTree.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QToolTip>
#include <algorithm>
#include <functional>

#include "client/Art.h"

namespace kot::client {

namespace {
constexpr double kNodeW = 172, kNodeH = 40, kColGap = 64, kRowGap = 12, kMargin = 16, kLegend = 34;

struct Look {
    QColor top, bottom, border, text;
};
Look lookFor(BuildTree::State s) {
    switch (s) {
    case BuildTree::State::Built: return {QColor("#4a6a34"), QColor("#2a4020"), QColor("#9acf72"), QColor("#eef8e0")};
    case BuildTree::State::Now: return {QColor("#7a6020"), QColor("#4a3810"), QColor("#ffd84a"), QColor("#fff4c8")};
    case BuildTree::State::Trade: return {QColor("#6a4a22"), QColor("#432c12"), QColor("#e89a40"), QColor("#f8e4c8")};
    case BuildTree::State::Blocked: break;
    }
    return {QColor("#3c342a"), QColor("#29231b"), QColor("#6e6250"), QColor("#b0a288")};
}

void drawLock(QPainter& p, const QRectF& r, const QColor& c) {
    p.save();
    p.setPen(QPen(c, 1.6));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(r.left() + r.width() * 0.22, r.top(), r.width() * 0.56, r.height() * 0.7), 0, 180 * 16);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(QRectF(r.left() + r.width() * 0.1, r.top() + r.height() * 0.38, r.width() * 0.8, r.height() * 0.6), 2, 2);
    p.restore();
}

void drawCheck(QPainter& p, const QRectF& r, const QColor& c) {
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(r);
    p.setPen(QPen(QColor("#1e3010"), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(QPolygonF{QPointF(r.left() + r.width() * 0.25, r.center().y()), QPointF(r.left() + r.width() * 0.45, r.bottom() - r.height() * 0.25),
                             QPointF(r.right() - r.width() * 0.22, r.top() + r.height() * 0.28)});
    p.restore();
}
}  // namespace

BuildTree::BuildTree(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setObjectName("buildTree");
}

void BuildTree::setNodes(const QList<Node>& nodes, bool canBuild) {
    m_nodes = nodes;
    m_canBuild = canBuild;
    m_hover = -1;
    layoutNodes();
    updateGeometry();
    resize(m_size);
    update();
}

void BuildTree::layoutNodes() {
    const int n = int(m_nodes.size());
    QHash<QString, int> index;
    for (int i = 0; i < n; ++i) index.insert(m_nodes[i].id, i);
    m_parents = QList<QList<int>>(n);
    for (int i = 0; i < n; ++i)
        for (const QString& r : m_nodes[i].needs)
            if (index.contains(r) && index.value(r) != i && !m_parents[i].contains(index.value(r))) m_parents[i].append(index.value(r));
    // Column: one more than the deepest thing it needs.
    QList<int> depth(n, -1);
    std::function<int(int, int)> depthOf = [&](int i, int guard) {
        if (depth[i] >= 0) return depth[i];
        if (guard > n) return 0;  // a loop in the data: stop
        int d = 0;
        for (const int p : m_parents[i]) d = std::max(d, depthOf(p, guard + 1) + 1);
        return depth[i] = d;
    };
    int columns = 0;
    for (int i = 0; i < n; ++i) columns = std::max(columns, depthOf(i, 0) + 1);
    // Order inside a column: near the things they need (the mean row of their
    // parents), so lines cross less; the first column by state, then name.
    QList<QList<int>> cols(columns);
    for (int i = 0; i < n; ++i) cols[depth[i]].append(i);
    QList<double> row(n, 0);
    for (int c = 0; c < columns; ++c) {
        QList<int>& col = cols[c];
        QHash<int, double> key;
        for (const int i : col) {
            double sum = 0;
            for (const int p : m_parents[i]) sum += row[p];
            key.insert(i, m_parents[i].isEmpty() ? int(m_nodes[i].state) * 100.0 : sum / m_parents[i].size());
        }
        std::stable_sort(col.begin(), col.end(), [&](int a, int b) {
            if (key[a] != key[b]) return key[a] < key[b];
            return m_nodes[a].name < m_nodes[b].name;
        });
        for (int k = 0; k < col.size(); ++k) row[col[k]] = k;
    }
    // Columns start at the top, so the first ones are in view at once.
    int tallest = 0;
    for (const QList<int>& col : cols) tallest = std::max(tallest, int(col.size()));
    const double fullH = tallest * (kNodeH + kRowGap) - kRowGap;
    m_rects = QList<QRectF>(n);
    for (int c = 0; c < columns; ++c) {
        const double top = kMargin + kLegend;
        for (int k = 0; k < cols[c].size(); ++k)
            m_rects[cols[c][k]] = QRectF(kMargin + c * (kNodeW + kColGap), top + k * (kNodeH + kRowGap), kNodeW, kNodeH);
    }
    m_size = QSize(int(kMargin * 2 + std::max(1, columns) * (kNodeW + kColGap) - kColGap), int(kMargin * 2 + kLegend + std::max(0.0, fullH)));
    m_size = m_size.expandedTo(QSize(560, 120));
}

QRectF BuildTree::nodeRect(const QString& id) const {
    for (int i = 0; i < m_nodes.size(); ++i)
        if (m_nodes[i].id == id) return m_rects.value(i);
    return {};
}

int BuildTree::nodeAt(QPointF p) const {
    for (int i = 0; i < m_rects.size(); ++i)
        if (m_rects[i].contains(p)) return i;
    return -1;
}

void BuildTree::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QLinearGradient bg(0, 0, 0, height());
    bg.setColorAt(0, QColor("#2e2418"));
    bg.setColorAt(1, QColor("#1e1810"));
    p.fillRect(rect(), bg);

    // The legend.
    {
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 0.92);
        p.setFont(f);
        double x = kMargin;
        const std::pair<State, QString> items[] = {{State::Built, tr("built")},
                                                   {State::Now, tr("can build now")},
                                                   {State::Trade, tr("needs a trade")},
                                                   {State::Blocked, tr("not yet")}};
        for (const auto& [s, text] : items) {
            const Look l = lookFor(s);
            const QRectF chip(x, kMargin, 22, 14);
            p.setPen(QPen(l.border, 1.5));
            p.setBrush(l.top);
            p.drawRoundedRect(chip, 4, 4);
            p.setPen(QColor("#d8c8a8"));
            const double tw = p.fontMetrics().horizontalAdvance(text);
            p.drawText(QRectF(chip.right() + 6, chip.top() - 3, tw + 4, 20), Qt::AlignVCenter | Qt::AlignLeft, text);
            x = chip.right() + 6 + tw + 22;
        }
        p.setPen(QColor("#9a8a70"));
        const QString hint = m_canBuild ? tr("Point at a building to see what it needs; click a gold one to build it.")
                                        : tr("Point at a building to see what it needs.");
        p.drawText(QRectF(x, kMargin - 3, width() - x - kMargin, 20), Qt::AlignVCenter | Qt::AlignLeft, hint);
    }

    // What lights up while pointing: the building, all it needs, and what it opens.
    QSet<int> lit;
    QSet<int> opens;
    if (m_hover >= 0) {
        QList<int> todo{m_hover};
        while (!todo.isEmpty()) {
            const int i = todo.takeLast();
            if (lit.contains(i)) continue;
            lit.insert(i);
            todo << m_parents[i];
        }
        for (int i = 0; i < m_nodes.size(); ++i)
            if (m_parents[i].contains(m_hover)) opens.insert(i);
    }
    const bool focus = m_hover >= 0;

    // Lines first, from what is needed to what it opens.
    for (int i = 0; i < m_nodes.size(); ++i)
        for (const int par : m_parents[i]) {
            const QPointF a(m_rects[par].right(), m_rects[par].center().y()), b(m_rects[i].left() - 4, m_rects[i].center().y());
            QPainterPath path(a);
            const double dx = std::max(30.0, (b.x() - a.x()) * 0.5);
            path.cubicTo(a + QPointF(dx, 0), b - QPointF(dx, 0), b);
            const bool chain = focus && lit.contains(i) && lit.contains(par);
            const bool next = focus && par == m_hover && opens.contains(i);
            QColor c = m_nodes[par].state == State::Built ? QColor(154, 207, 114, 190) : QColor(150, 132, 100, 150);
            double w = 1.6;
            if (chain) {
                c = QColor("#ffd84a");
                w = 2.8;
            } else if (next) {
                c = QColor(255, 216, 74, 150);
                w = 2.0;
            } else if (focus) {
                c.setAlpha(45);
            }
            p.setPen(QPen(c, w, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawPolygon(QPolygonF{b + QPointF(4, 0), b + QPointF(-5, -4.5), b + QPointF(-5, 4.5)});
        }

    // The buildings.
    QFont bold = font();
    bold.setBold(true);
    p.setFont(bold);
    for (int i = 0; i < m_nodes.size(); ++i) {
        const Node& nd = m_nodes[i];
        const QRectF r = m_rects[i];
        const Look l = lookFor(nd.state);
        const bool dim = focus && !lit.contains(i) && !opens.contains(i);
        p.setOpacity(dim ? 0.35 : 1.0);
        if (i == m_hover || (nd.state == State::Now && !focus)) {  // a soft glow
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(l.border.red(), l.border.green(), l.border.blue(), i == m_hover ? 90 : 45));
            p.drawRoundedRect(r.adjusted(-4, -4, 4, 4), 11, 11);
        }
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        g.setColorAt(0, l.top);
        g.setColorAt(1, l.bottom);
        p.setPen(QPen(l.border, i == m_hover ? 2.6 : 1.6));
        p.setBrush(g);
        p.drawRoundedRect(r, 8, 8);
        p.setPen(QPen(QColor(255, 255, 255, 40), 1));  // a top highlight
        p.drawLine(QPointF(r.left() + 8, r.top() + 2), QPointF(r.right() - 8, r.top() + 2));
        // The state mark on the left.
        const QRectF mark(r.left() + 8, r.center().y() - 10, 20, 20);
        switch (nd.state) {
        case State::Built: drawCheck(p, mark.adjusted(1, 1, -1, -1), l.border); break;
        case State::Now: p.drawPixmap(mark.toRect(), art::buttonIcon("build").pixmap(40, 40)); break;
        case State::Trade: p.drawPixmap(mark.toRect(), art::buttonIcon("market").pixmap(40, 40)); break;
        case State::Blocked: drawLock(p, mark.adjusted(3, 2, -3, -2), l.border); break;
        }
        p.setPen(l.text);
        const QRectF text = r.adjusted(34, 0, -8, 0);
        p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(nd.name, Qt::ElideRight, int(text.width())));
    }
    p.setOpacity(1.0);
}

void BuildTree::mouseMoveEvent(QMouseEvent* e) {
    const int h = nodeAt(e->position());
    if (h != m_hover) {
        m_hover = h;
        const bool clickable = h >= 0 && m_canBuild && (m_nodes[h].state == State::Now || m_nodes[h].state == State::Trade);
        setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void BuildTree::leaveEvent(QEvent*) {
    m_hover = -1;
    update();
}

void BuildTree::mousePressEvent(QMouseEvent* e) {
    const int i = nodeAt(e->position());
    if (i < 0 || !m_canBuild || e->button() != Qt::LeftButton) return;
    if (m_nodes[i].state == State::Now) emit buildRequested(m_nodes[i].id, false);
    else if (m_nodes[i].state == State::Trade) emit buildRequested(m_nodes[i].id, true);
}

bool BuildTree::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(e);
        const int i = nodeAt(help->pos());
        if (i >= 0) QToolTip::showText(help->globalPos(), m_nodes[i].tooltip, this, m_rects[i].toRect());
        else QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

}  // namespace kot::client
