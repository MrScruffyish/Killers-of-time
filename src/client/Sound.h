#pragma once

class QByteArray;

namespace kot::client::sound {

// Sound effects, generated in code at start (no sound files needed) and played with
// Qt Multimedia when it is available. Off without it, on the offscreen platform
// (tests) and when KOT_NO_SOUND is set.
enum class Effect { Click, Step, Clash, Spell, Coins, Bell, Drums, Fanfare, Defeat };

void play(Effect effect);
void setMuted(bool muted);
bool muted();
bool available();
// The generated sound as a WAV file (16-bit mono, 22050 Hz), for tests.
QByteArray effectWav(Effect effect);

}  // namespace kot::client::sound
