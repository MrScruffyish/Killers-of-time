#pragma once

#include <QString>

namespace kot::net {

// Where the time of an action goes, click to screen (client side): sending the
// command, the server (rules, views, network), decoding the answer, the client's work
// on the new view (phases), and painting it. On when the environment variable
// KOT_TIMING is set: one line per action to standard error; when its value is a file
// name (not "1"), also appended to that file. Cheap and silent when off.
namespace timing {

bool enabled();
// Nanoseconds on the timing clock.
qint64 now();
// The command `seq` of `type` is sent now.
void sent(int seq, const QString& type);
// A message arrived (taken from the socket at `arrivedNs`, decoded in `decodeNs`).
void received(const QString& type, qint64 arrivedNs, qint64 decodeNs);
// The client's work on a new view: start, named phases (time since the last mark), end.
void viewStart();
void phase(const char* name);
void viewEnd();
// The map or battle starts painting (the time before: the rest of the window).
void paintStarted();
// A view was painted (after viewEnd: the action is done, its line is written).
void painted();
// Time spent on something inside the action (e.g. "chunks": map ground painted), added up
// and shown as "name count x total".
void add(const char* name, qint64 ns);
void add(const QString& name, qint64 ns);
// A note line (e.g. the screen's pixel ratio), written as is.
void note(const QString& line);
// Server side: a command was handled in `totalNs`; writes "server <type>: " and the
// times added (rules, battle AI, AI turns, views, sending, autosave) since the last one.
void serverDone(const QString& type, qint64 totalNs);
// Times a block: add(name, time from here to the end of the scope).
class Scope {
public:
    explicit Scope(const char* name) : m_name(name), m_start(enabled() ? now() : -1) {}
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    const char* m_name;
    qint64 m_start;
};

}  // namespace timing
}  // namespace kot::net
