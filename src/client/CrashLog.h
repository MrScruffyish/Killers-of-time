#pragma once

namespace kot::client {

// On a crash (SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS) prints a backtrace to the
// terminal and appends it to ~/.local/share/KillersOfTime/crash.log (Linux), then
// lets the crash go on (core dump as usual). Function names need a Debug build.
void installCrashLog();

}  // namespace kot::client
