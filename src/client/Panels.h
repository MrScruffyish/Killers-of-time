#pragma once

#include <QWidget>

#include "client/ViewModel.h"

namespace kot::core { class ContentRegistry; }

namespace kot::client {

// Your resources as icons with the amount and, smaller, what comes in each day.
class ResourceBar : public QWidget {
    Q_OBJECT
public:
    explicit ResourceBar(const core::ContentRegistry* content, QWidget* parent = nullptr);
    void setView(const GameView& view);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    bool event(QEvent* e) override;

private:
    struct Item {
        QString id, name;
        int amount = 0, income = 0;
        QRect rect;
    };
    const core::ContentRegistry* m_content;
    QList<Item> m_items;
};

// The week across the whole width: seven days, the days gone by filled, today a
// bright diamond, and the rest hollow. The ends say which week it is and when the
// towns and dwellings grow.
class WeekBar : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int day READ day)
public:
    explicit WeekBar(QWidget* parent = nullptr);
    // `day` is the game day (1-based); `goal` is shown in the tooltip.
    void setDay(int day, const QString& goal = QString());
    int day() const { return m_day; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    int m_day = 1;
    QString m_goal;
};

// The selected hero at a glance: name and class, bars for movement, spell points
// and experience, the main traits, and the army in its slots (unit, count, stars;
// tooltips with the unit's stats).
class HeroPanel : public QWidget {
    Q_OBJECT
public:
    explicit HeroPanel(const core::ContentRegistry* content, QWidget* parent = nullptr);
    void setHero(const GameView& view, int heroId);
    QSize sizeHint() const override;

signals:
    void clicked();  // opens the hero screen

protected:
    void paintEvent(QPaintEvent*) override;
    bool event(QEvent* e) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    QRect slotRect(int index) const;
    QString slotTip(int index) const;

    const core::ContentRegistry* m_content;
    GameView m_view;
    int m_heroId = 0;
};

}  // namespace kot::client
