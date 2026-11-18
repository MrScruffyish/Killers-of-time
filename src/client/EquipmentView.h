#pragma once

#include <QList>
#include <QRectF>
#include <QStringList>
#include <QWidget>

#include "core/content/ContentIds.h"

namespace kot::core { class ContentRegistry; }

namespace kot::client {

// What a hero wears, as a figure with a box for every place (head, neck, body,
// weapon, shield, feet, two rings, three more), and the backpack below.
// Click a worn artifact to take it off, a carried one to wear it (swapping with
// what is worn there). Pointing at a carried one lights up where it goes.
class EquipmentView : public QWidget {
    Q_OBJECT
public:
    EquipmentView(const core::ContentRegistry* content, QWidget* parent = nullptr);
    void setArtifacts(const QList<core::ArtifactId>& worn, const QList<core::ArtifactId>& backpack, bool canChange);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

signals:
    void equipRequested(core::ArtifactId artifact);
    void unequipRequested(core::ArtifactId artifact);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool event(QEvent* event) override;

private:
    struct Box {
        QRectF rect;
        QString slot;      // the place (also for backpack boxes: the artifact's place)
        core::ArtifactId artifact;  // invalid = nothing there
        bool worn = true;
    };
    void layoutBoxes();
    int boxAt(QPointF p);
    QString tooltipFor(const Box& b) const;

    const core::ContentRegistry* m_content;
    QList<core::ArtifactId> m_worn, m_backpack;
    bool m_canChange = false;
    QList<Box> m_boxes;
    int m_hover = -1;
    int m_layoutWidth = -1;  // the width the boxes were laid out for
};

}  // namespace kot::client
