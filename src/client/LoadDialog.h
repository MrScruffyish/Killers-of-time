#pragma once

#include <QDialog>
#include <QJsonArray>

class QTableWidget;
class QPushButton;
class QLabel;

namespace kot::client {

// The saved games on the server (list_saves `games`), newest first: name, when it
// was saved, day, players, map size, game version and whether it can be loaded.
// Saves that cannot be loaded are greyed out and say why; older ones that will be
// converted say so.
class LoadDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoadDialog(const QJsonArray& games, QWidget* parent = nullptr);
    QString chosen() const { return m_chosen; }

private:
    void updateSelection();

    QJsonArray m_games;
    QTableWidget* m_table;
    QPushButton* m_loadButton;
    QLabel* m_details;
    QString m_chosen;
};

}  // namespace kot::client
