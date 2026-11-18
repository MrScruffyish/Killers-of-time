#pragma once

#include <QDialog>

namespace kot::client {

// The in-game help (F1 or Game menu): keys, mouse, what the marks on the map mean,
// and how days, weeks and growth work. Plain text, no game state.
class HelpDialog : public QDialog {
    Q_OBJECT
public:
    explicit HelpDialog(QWidget* parent = nullptr);
    static QString helpHtml();
};

}  // namespace kot::client
