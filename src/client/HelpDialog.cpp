#include "client/HelpDialog.h"

#include <QDialogButtonBox>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace kot::client {

HelpDialog::HelpDialog(QWidget* parent) : QDialog(parent) {
    setObjectName("helpDialog");
    setWindowTitle(tr("Help"));
    resize(640, 620);
    auto* layout = new QVBoxLayout(this);
    auto* text = new QTextBrowser(this);
    text->setObjectName("helpText");
    text->setOpenLinks(false);
    text->setHtml(helpHtml());
    layout->addWidget(text, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString HelpDialog::helpHtml() {
    const auto row = [](const QString& key, const QString& what) {
        return QStringLiteral("<tr><td style='padding-right:14px'><b>%1</b></td><td>%2</td></tr>").arg(key, what);
    };
    QString h;
    h += tr("<h2>Killers of Time</h2>"
            "<p>Take turns with the other players. Each day you move your heroes, build in your towns and buy units. "
            "A player with no towns and no heroes is out; the last team standing wins.</p>");

    h += tr("<h3>Keys on the map</h3><table>");
    h += row(tr("E"), tr("End the turn"));
    h += row(tr("N"), tr("Next hero who can still walk today"));
    h += row(tr("C or Home"), tr("Look at the selected hero"));
    h += row(tr("H"), tr("Open the selected hero"));
    h += row(tr("Enter"), tr("Walk the selected hero along the planned route"));
    h += row(tr("T"), tr("Next town (the map looks there)"));
    h += row(tr("O"), tr("Open the town"));
    h += row(tr("M"), tr("Market: trade resources"));
    h += row(tr("+ and -"), tr("Bigger or smaller map icons (also Ctrl + mouse wheel)"));
    h += row(tr("F1"), tr("This help"));
    h += QStringLiteral("</table>");

    h += tr("<h3>Mouse on the map</h3><table>");
    h += row(tr("Click"), tr("Select your hero, or plan a route for the selected hero"));
    h += row(tr("Click the goal again, or double-click"), tr("Walk there"));
    h += row(tr("Right-click"), tr("What is there (your own town: open it)"));
    h += row(tr("Shift-click"), tr("Open your town"));
    h += row(tr("Click your hero next to the selected one"), tr("The two heroes meet: swap units and artifacts"));
    h += row(tr("Drag"), tr("Move the map; the small map also moves it"));
    h += QStringLiteral("</table>");

    h += tr("<h3>Keys in a town</h3><table>");
    h += row(tr("T, Page Down / Page Up"), tr("Next or previous town"));
    h += row(tr("G"), tr("Give all units to the hero in town"));
    h += row(tr("Esc"), tr("Close"));
    h += QStringLiteral("</table>");

    h += tr("<h3>Keys in battle</h3><table>");
    h += row(tr("D"), tr("Defend: the unit stands and takes less damage"));
    h += row(tr("W"), tr("Wait: the unit acts later this round"));
    h += row(tr("A"), tr("Auto-play on or off"));
    h += QStringLiteral("</table>");

    h += tr("<h3>What the marks on the map mean</h3><ul>"
            "<li><b>Coloured ring on the ground</b>: who owns it (hero, town, mine, dwelling). Neutral things have none.</li>"
            "<li><b>Gold ring</b>: the selected hero.</li>"
            "<li><b>Lighter squares</b>: where the selected hero can still go today.</li>"
            "<li><b>Reddish squares</b>: next to an enemy army; stepping there means a fight (zone of control).</li>"
            "<li><b>White dots</b>: the route walked today. <b>Red dots</b>: the rest, walked on later days. "
            "A ring marks the goal; small faint dots are only a preview under the mouse.</li>"
            "<li><b>Orange cross</b>: on this route the hero would be attacked here.</li>"
            "<li><b>Dim land</b>: seen before, but not watched now; what is there may have changed. Black: never seen.</li>"
            "</ul>");

    h += tr("<h3>Days and weeks</h3><ul>"
            "<li>Towns and mines pay every day, from day 2.</li>"
            "<li>A week has seven days, shown as seven marks. On the first day of a new week (day 8, 15, ...) "
            "every dwelling grows new units, and a week event may change growth or bring resources.</li>"
            "<li>Units not bought wait in the dwelling; buy them in the town or with <i>Buy all units</i>.</li>"
            "<li>A town builds once a day. <i>Build, then buy units</i> picks a building for you, "
            "but never a choice that would shut out another building.</li>"
            "<li>Heroes get their full steps back every morning. Spell points come back a little each day.</li>"
            "<li>Before you end the turn, you are reminded of heroes who can still walk and towns that can still build "
            "(switch it off in the Game menu).</li>"
            "</ul>");

    h += tr("<h3>Battles</h3><ul>"
            "<li>Walk onto an enemy hero, a defended town or monsters to fight.</li>"
            "<li>Units act by speed. Each unit hits back once a round when attacked. Shooters shoot from afar "
            "and have a limited number of shots; with an enemy next to them they cannot shoot.</li>"
            "<li>Retreat saves the hero (it flees to your nearest town with no hero) but loses the army.</li>"
            "<li>Survivors heal after the battle. Units that fight gain stars of experience.</li>"
            "</ul>");
    return h;
}

}  // namespace kot::client
