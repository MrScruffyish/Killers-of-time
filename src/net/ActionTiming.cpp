#include "net/ActionTiming.h"

#include <QElapsedTimer>
#include <QFile>
#include <QList>
#include <QMap>
#include <QTextStream>
#include <cstdio>
#include <utility>

namespace kot::net::timing {

namespace {

struct State {
    bool checked = false, on = false;
    QString file;
    QElapsedTimer clock;
    // The action being followed.
    int seq = 0;
    QString type;
    qint64 sentNs = -1, firstReplyNs = -1, decodeNs = 0, viewStartNs = -1, lastMarkNs = -1, viewEndNs = -1, paintStartNs = -1;
    QList<std::pair<QString, qint64>> phases;
    QMap<QString, std::pair<int, qint64>> added;
};

State& state() {
    static State s;
    if (!s.checked) {
        s.checked = true;
        const QByteArray v = qgetenv("KOT_TIMING");
        s.on = !v.isEmpty();
        if (s.on && v != "1") s.file = QString::fromLocal8Bit(v);
        s.clock.start();
    }
    return s;
}

QString ms(qint64 ns) { return QString::number(ns / 1e6, 'f', 1); }

void reset(State& s) {
    s.seq = 0;
    s.type.clear();
    s.sentNs = s.firstReplyNs = s.viewStartNs = s.lastMarkNs = s.viewEndNs = s.paintStartNs = -1;
    s.decodeNs = 0;
    s.phases.clear();
    s.added.clear();
}

void write(const QString& line) {
    State& s = state();
    std::fprintf(stderr, "%s\n", qPrintable(line));
    if (s.file.isEmpty()) return;
    QFile f(s.file);
    if (f.open(QIODevice::Append | QIODevice::Text)) QTextStream(&f) << line << '\n';
}

}  // namespace

bool enabled() { return state().on; }
qint64 now() { return state().clock.nsecsElapsed(); }

void sent(int seq, const QString& type) {
    State& s = state();
    if (!s.on) return;
    reset(s);
    s.seq = seq;
    s.type = type;
    s.sentNs = now();
}

void received(const QString& type, qint64 arrivedNs, qint64 decodeNs) {
    State& s = state();
    if (!s.on) return;
    if (type != QLatin1String("command_result") && type != QLatin1String("game_state") && type != QLatin1String("game_started")) return;
    if (s.sentNs >= 0 && s.firstReplyNs < 0) s.firstReplyNs = arrivedNs;
    s.decodeNs += decodeNs;
}

void note(const QString& line) {
    if (state().on) write(line);
}

void add(const char* name, qint64 ns) { add(QString::fromLatin1(name), ns); }

void add(const QString& name, qint64 ns) {
    State& s = state();
    if (!s.on) return;
    auto& v = s.added[name];
    ++v.first;
    v.second += ns;
}

Scope::~Scope() {
    if (m_start >= 0) add(m_name, now() - m_start);
}

void serverDone(const QString& type, qint64 totalNs) {
    State& s = state();
    if (!s.on) return;
    QStringList parts;
    qint64 counted = 0;
    for (auto it = s.added.cbegin(); it != s.added.cend(); ++it) {
        parts << QStringLiteral("%1 %2x %3").arg(it.key()).arg(it.value().first).arg(ms(it.value().second));
        counted += it.value().second;
    }
    parts << QStringLiteral("rest %1").arg(ms(totalNs - counted));
    write(QStringLiteral("server %1: %2 | total %3 ms").arg(type, parts.join(QStringLiteral(" | ")), ms(totalNs)));
    s.added.clear();
}

void viewStart() {
    State& s = state();
    if (!s.on) return;
    if (s.sentNs < 0) s.type.clear();  // a view nobody here asked for (another player, the AI)
    s.viewStartNs = s.lastMarkNs = now();
    s.phases.clear();
    s.viewEndNs = -1;
}

void phase(const char* name) {
    State& s = state();
    if (!s.on || s.viewStartNs < 0) return;
    const qint64 t = now();
    s.phases.append({QString::fromLatin1(name), t - s.lastMarkNs});
    s.lastMarkNs = t;
}

void viewEnd() {
    State& s = state();
    if (!s.on || s.viewStartNs < 0) return;
    phase("other");
    s.viewEndNs = now();
}

void paintStarted() {
    State& s = state();
    if (s.on && s.viewEndNs >= 0 && s.paintStartNs < 0) s.paintStartNs = now();
}

void painted() {
    State& s = state();
    if (!s.on || s.viewEndNs < 0) return;
    const qint64 t = now();
    QStringList parts;
    const qint64 start = s.sentNs >= 0 ? s.sentNs : s.viewStartNs;
    if (s.sentNs >= 0 && s.firstReplyNs >= 0) parts << QStringLiteral("server+net %1").arg(ms(s.firstReplyNs - s.sentNs));
    if (s.sentNs >= 0) parts << QStringLiteral("waiting %1").arg(ms(s.viewStartNs - std::max(s.firstReplyNs, s.sentNs)));
    parts << QStringLiteral("decode %1").arg(ms(s.decodeNs));
    for (const auto& [name, ns] : s.phases) parts << QStringLiteral("%1 %2").arg(name, ms(ns));
    if (s.paintStartNs >= 0) parts << QStringLiteral("until map paint %1").arg(ms(s.paintStartNs - s.viewEndNs));
    parts << QStringLiteral("paint %1").arg(ms(t - (s.paintStartNs >= 0 ? s.paintStartNs : s.viewEndNs)));
    for (auto it = s.added.cbegin(); it != s.added.cend(); ++it)
        parts << QStringLiteral("(%1 %2x %3)").arg(it.key()).arg(it.value().first).arg(ms(it.value().second));
    write(QStringLiteral("timing %1: %2 | total %3 ms")
              .arg(s.type.isEmpty() ? QStringLiteral("(view)") : s.type, parts.join(QStringLiteral(" | ")), ms(t - start)));
    reset(s);
}

}  // namespace kot::net::timing
