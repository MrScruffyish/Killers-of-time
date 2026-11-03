#pragma once

#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QWidget>

namespace kot::client {

// A town's buildings as a graph: what a building needs stands to its left, with
// lines to what it opens up. Colours tell built / can build now / needs a trade /
// not yet. Hovering a building lights up everything it needs; clicking one that
// can be built asks to build it.
class BuildTree : public QWidget {
    Q_OBJECT
public:
    enum class State { Built, Now, Trade, Blocked };
    struct Node {
        QString id;
        QString name;
        State state = State::Blocked;
        QStringList needs;     // ids of other nodes
        QString tooltip;       // rich text
    };

    explicit BuildTree(QWidget* parent = nullptr);
    void setNodes(const QList<Node>& nodes, bool canBuild);
    QSize sizeHint() const override { return m_size; }
    QSize minimumSizeHint() const override { return m_size; }
    // For tests: where a node is drawn (empty when unknown).
    QRectF nodeRect(const QString& id) const;

signals:
    void buildRequested(const QString& id, bool useTrade);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool event(QEvent* event) override;

private:
    int nodeAt(QPointF p) const;
    void layoutNodes();

    QList<Node> m_nodes;
    QList<QList<int>> m_parents;  // per node: indices of what it needs
    QList<QRectF> m_rects;
    QSize m_size{400, 200};
    bool m_canBuild = false;
    int m_hover = -1;
};

}  // namespace kot::client
