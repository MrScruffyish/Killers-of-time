#include "client/Sound.h"

#include <QSettings>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QUrl>
#include <cmath>
#include <cstdint>

#ifdef KOT_HAVE_SOUND
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QMutex>
#endif

namespace kot::client::sound {

namespace {

constexpr int kRate = 22050;
// Muted or not is remembered in the settings; read on first use.
bool& mutedFlag() {
    static bool flag = QSettings().value(QStringLiteral("sound/muted"), false).toBool();
    return flag;
}

// Mono 16-bit samples -> a WAV file.
QByteArray wav(const QList<float>& samples) {
    QByteArray data;
    data.reserve(samples.size() * 2);
    for (const float s : samples) {
        const qint16 v = static_cast<qint16>(std::clamp(s, -1.0f, 1.0f) * 30000);
        data.append(char(v & 0xff));
        data.append(char((v >> 8) & 0xff));
    }
    QByteArray out;
    const auto u32 = [&](quint32 v) { for (int i = 0; i < 4; ++i) out.append(char((v >> (8 * i)) & 0xff)); };
    const auto u16 = [&](quint16 v) { out.append(char(v & 0xff)); out.append(char(v >> 8)); };
    out.append("RIFF");
    u32(36 + data.size());
    out.append("WAVEfmt ");
    u32(16);
    u16(1);
    u16(1);
    u32(kRate);
    u32(kRate * 2);
    u16(2);
    u16(16);
    out.append("data");
    u32(data.size());
    out.append(data);
    return out;
}

// A tone with a quick attack and an exponential fade.
void tone(QList<float>& out, double start, double length, double freq, double volume, double decay = 6.0,
          double slide = 0.0) {
    const int from = int(start * kRate), n = int(length * kRate);
    if (out.size() < from + n) out.resize(from + n);
    double phase = 0;
    for (int i = 0; i < n; ++i) {
        const double t = double(i) / kRate;
        const double f = freq * (1.0 + slide * t / length);
        phase += 2 * M_PI * f / kRate;
        const double env = std::min(1.0, t * 200) * std::exp(-decay * t / length);
        out[from + i] += float(volume * env * (std::sin(phase) + 0.3 * std::sin(2 * phase)));
    }
}

void noise(QList<float>& out, double start, double length, double volume, double decay, quint32 seed) {
    const int from = int(start * kRate), n = int(length * kRate);
    if (out.size() < from + n) out.resize(from + n);
    quint32 s = seed;
    float last = 0;
    for (int i = 0; i < n; ++i) {
        s = s * 1664525u + 1013904223u;
        const float white = float((s >> 9) & 0xffff) / 32768.0f - 1.0f;
        last = 0.6f * last + 0.4f * white;  // a little softer than white noise
        out[from + i] += float(volume * std::exp(-decay * double(i) / n)) * last;
    }
}

QList<float> make(Effect e) {
    QList<float> s;
    switch (e) {
    case Effect::Click: tone(s, 0, 0.05, 900, 0.3, 8); break;
    case Effect::Step: noise(s, 0, 0.08, 0.25, 6, 7); break;
    case Effect::Clash:
        noise(s, 0, 0.25, 0.6, 5, 11);
        tone(s, 0, 0.3, 1800, 0.25, 7);
        tone(s, 0.01, 0.3, 2650, 0.15, 8);
        break;
    case Effect::Spell:
        tone(s, 0, 0.5, 500, 0.3, 3, 1.5);
        tone(s, 0.05, 0.5, 750, 0.2, 3, 1.5);
        break;
    case Effect::Coins:
        tone(s, 0, 0.12, 2000, 0.25, 5);
        tone(s, 0.08, 0.2, 2600, 0.25, 5);
        break;
    case Effect::Bell:
        tone(s, 0, 1.2, 660, 0.35, 4);
        tone(s, 0, 1.2, 1650, 0.12, 5);
        break;
    case Effect::Drums:
        for (int i = 0; i < 3; ++i) {
            tone(s, i * 0.18, 0.2, 90, 0.6, 6, -0.4);
            noise(s, i * 0.18, 0.1, 0.2, 8, 3 + i);
        }
        break;
    case Effect::Fanfare:
        tone(s, 0.00, 0.25, 523, 0.3, 2);
        tone(s, 0.20, 0.25, 659, 0.3, 2);
        tone(s, 0.40, 0.25, 784, 0.3, 2);
        tone(s, 0.60, 0.70, 1046, 0.35, 3);
        break;
    case Effect::Defeat:
        tone(s, 0.0, 0.5, 392, 0.3, 2);
        tone(s, 0.4, 0.5, 349, 0.3, 2);
        tone(s, 0.8, 0.9, 262, 0.3, 3);
        break;
    }
    return s;
}

bool wanted() {
    if (!qEnvironmentVariableIsEmpty("KOT_NO_SOUND")) return false;
    return QGuiApplication::platformName() != QLatin1String("offscreen");
}

}  // namespace

QByteArray effectWav(Effect e) { return wav(make(e)); }

static bool s_disabled = false;  // --no-sound

bool available() {
#ifdef KOT_HAVE_SOUND
    return !s_disabled && wanted();
#else
    return false;
#endif
}

void closeOutputIfOpen();
void setMuted(bool m) {
    mutedFlag() = m;
    QSettings().setValue(QStringLiteral("sound/muted"), m);
    if (m) closeOutputIfOpen();
}
bool muted() { return mutedFlag(); }

#ifdef KOT_HAVE_SOUND
namespace {

// All effects go through one audio output that stays open, fed by this mixer:
// overlapping effects are added together. (Qt 6.4's QSoundEffect opened a
// PulseAudio stream per effect and could abort in pa_stream_writable_size when
// two played right after each other.)
class Mixer : public QIODevice {
public:
    Mixer() { open(QIODevice::ReadOnly); }
    void add(const QList<float>& samples, float volume) {
        QMutexLocker lock(&m_mutex);
        if (m_voices.size() >= 8) m_voices.removeFirst();  // never pile up
        Voice v;
        v.samples.reserve(samples.size());
        for (const float x : samples) v.samples.append(x * volume);
        m_voices.append(v);
    }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return 4096 + QIODevice::bytesAvailable(); }

protected:
    qint64 readData(char* data, qint64 maxSize) override {
        QMutexLocker lock(&m_mutex);
        const qint64 frames = maxSize / 2;
        auto* out = reinterpret_cast<qint16*>(data);
        for (qint64 i = 0; i < frames; ++i) {
            float mix = 0;
            for (Voice& v : m_voices)
                if (v.pos < v.samples.size()) mix += v.samples[v.pos++];
            out[i] = static_cast<qint16>(std::clamp(mix, -1.0f, 1.0f) * 30000);
        }
        m_voices.removeIf([](const Voice& v) { return v.pos >= v.samples.size(); });
        return frames * 2;
    }
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    struct Voice {
        QList<float> samples;
        qsizetype pos = 0;
    };
    QMutex m_mutex;
    QList<Voice> m_voices;
};

Mixer* g_mixer = nullptr;
QAudioSink* g_sink = nullptr;
bool g_tried = false;

// Sound off closes the audio output altogether (nothing is opened while off).
void closeOutput() {
    if (g_sink) {
        g_sink->stop();
        delete g_sink;
        g_sink = nullptr;
    }
    delete g_mixer;
    g_mixer = nullptr;
    g_tried = false;
}

Mixer* mixer() {
    Mixer*& m = g_mixer;
    if (g_tried) return m;
    g_tried = true;
    QAudioFormat format;
    format.setSampleRate(kRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull() || !device.isFormatSupported(format)) return nullptr;
    m = new Mixer();
    g_sink = new QAudioSink(device, format, qApp);
    g_sink->setBufferSize(kRate / 10 * 2);  // about 0.1 s: short delay
    g_sink->start(m);
    return m;
}

}  // namespace
#endif

void closeOutputIfOpen() {
#ifdef KOT_HAVE_SOUND
    closeOutput();
#endif
}

void disableForThisRun() {
    s_disabled = true;
    closeOutputIfOpen();
}

void play(Effect e) {
#ifdef KOT_HAVE_SOUND
    if (mutedFlag() || !available()) return;
    static QHash<int, QList<float>> cache;
    auto it = cache.find(int(e));
    if (it == cache.end()) it = cache.insert(int(e), make(e));
    if (Mixer* m = mixer()) m->add(it.value(), 0.6f);
#else
    (void)e;
#endif
}

}  // namespace kot::client::sound
