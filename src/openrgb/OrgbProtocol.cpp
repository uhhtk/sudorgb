#include "openrgb/OrgbProtocol.h"

#include <cstring>

namespace orgb {
namespace {

class Reader {
public:
    explicit Reader(const QByteArray& d) : m_data(d) {}

    bool ok() const { return m_ok; }
    qsizetype remaining() const { return m_data.size() - m_pos; }

    uint16_t u16() { return readLE<uint16_t>(); }
    uint32_t u32() { return readLE<uint32_t>(); }
    int32_t i32() { return static_cast<int32_t>(readLE<uint32_t>()); }

    QString str() {
        const uint16_t len = u16();
        if (!need(len)) return {};
        QByteArray raw = m_data.mid(m_pos, len);
        m_pos += len;
        if (!raw.isEmpty() && raw.back() == '\0') raw.chop(1);
        return QString::fromUtf8(raw);
    }

    QList<QRgb> colors(uint32_t count) {
        QList<QRgb> out;
        if (!need(qsizetype(count) * 4)) return out;
        out.reserve(count);
        for (uint32_t i = 0; i < count; ++i) out.append(fromWire(u32()));
        return out;
    }

    void skip(qsizetype n) {
        if (need(n)) m_pos += n;
    }

private:
    bool need(qsizetype n) {
        if (n < 0 || m_pos + n > m_data.size()) m_ok = false;
        return m_ok;
    }

    template <typename T>
    T readLE() {
        if (!need(sizeof(T))) return 0;
        T v = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            v |= T(uint8_t(m_data[m_pos + qsizetype(i)])) << (8 * i);
        m_pos += sizeof(T);
        return v;
    }

    const QByteArray& m_data;
    qsizetype m_pos = 0;
    bool m_ok = true;
};

class Writer {
public:
    void u16(uint16_t v) { le(v); }
    void u32(uint32_t v) { le(v); }
    void i32(int32_t v) { le(static_cast<uint32_t>(v)); }
    void str(const QString& s) {
        const QByteArray b = s.toUtf8();
        u16(uint16_t(b.size() + 1));
        buf.append(b);
        buf.append('\0');
    }
    void colors(const QList<QRgb>& cs) {
        for (QRgb c : cs) u32(toWire(c));
    }
    QByteArray buf;

private:
    template <typename T>
    void le(T v) {
        for (size_t i = 0; i < sizeof(T); ++i) buf.append(char((v >> (8 * i)) & 0xFF));
    }
};

Mode readMode(Reader& r, uint32_t protocol) {
    Mode m;
    m.name = r.str();
    m.value = r.i32();
    m.flags = r.u32();
    m.speedMin = r.u32();
    m.speedMax = r.u32();
    if (protocol >= 3) {
        m.brightnessMin = r.u32();
        m.brightnessMax = r.u32();
    }
    m.colorsMin = r.u32();
    m.colorsMax = r.u32();
    m.speed = r.u32();
    if (protocol >= 3) m.brightness = r.u32();
    m.direction = r.u32();
    m.colorMode = r.u32();
    m.colors = r.colors(r.u16());
    return m;
}

void writeMode(Writer& w, const Mode& m, uint32_t protocol) {
    w.str(m.name);
    w.i32(m.value);
    w.u32(m.flags);
    w.u32(m.speedMin);
    w.u32(m.speedMax);
    if (protocol >= 3) {
        w.u32(m.brightnessMin);
        w.u32(m.brightnessMax);
    }
    w.u32(m.colorsMin);
    w.u32(m.colorsMax);
    w.u32(m.speed);
    if (protocol >= 3) w.u32(m.brightness);
    w.u32(m.direction);
    w.u32(m.colorMode);
    w.u16(uint16_t(m.colors.size()));
    w.colors(m.colors);
}

}  // namespace

int Controller::zoneStart(int z) const {
    int start = 0;
    for (int i = 0; i < z && i < zones.size(); ++i) start += int(zones[i].ledsCount);
    return start;
}

QString deviceTypeName(int32_t type) {
    static const char* names[] = {"Motherboard", "Memory",     "GPU",       "Cooler",     "LED Strip",
                                  "Keyboard",    "Mouse",      "Mousemat",  "Headset",    "Headset Stand",
                                  "Gamepad",     "Light",      "Speaker",   "Virtual",    "Storage",
                                  "Case",        "Microphone", "Accessory", "Keypad",     "Laptop",
                                  "Monitor"};
    if (type >= 0 && type < int32_t(std::size(names))) return QString::fromLatin1(names[type]);
    return QStringLiteral("Device");
}

QString deviceTypeIcon(int32_t type) {
    switch (type) {
    case 0: return QStringLiteral("motherboard");
    case 1: return QStringLiteral("memory");
    case 2: return QStringLiteral("gpu");
    case 3: return QStringLiteral("fan");
    case 4: return QStringLiteral("strip");
    case 5: case 18: return QStringLiteral("keyboard");
    case 6: return QStringLiteral("mouse");
    case 8: case 9: return QStringLiteral("headset");
    default: return QStringLiteral("chip");
    }
}

std::optional<Header> parseHeader(const QByteArray& data) {
    if (data.size() < kHeaderSize || std::memcmp(data.constData(), "ORGB", 4) != 0) return std::nullopt;
    Reader r(data);
    r.skip(4);
    Header h;
    h.deviceIndex = r.u32();
    h.packetId = r.u32();
    h.size = r.u32();
    if (h.size > kMaxPacketSize) return std::nullopt;
    return h;
}

QByteArray packet(uint32_t deviceIndex, uint32_t packetId, const QByteArray& payload) {
    Writer w;
    w.buf.append("ORGB", 4);
    w.u32(deviceIndex);
    w.u32(packetId);
    w.u32(uint32_t(payload.size()));
    w.buf.append(payload);
    return w.buf;
}

std::optional<Controller> parseController(const QByteArray& payload, uint32_t protocol) {
    Reader r(payload);
    const uint32_t dataSize = r.u32();
    if (!r.ok() || dataSize > uint32_t(payload.size())) return std::nullopt;

    Controller c;
    c.type = r.i32();
    c.name = r.str();
    if (protocol >= 1) c.vendor = r.str();
    c.description = r.str();
    c.version = r.str();
    c.serial = r.str();
    c.location = r.str();

    const uint16_t numModes = r.u16();
    c.activeMode = r.i32();
    for (uint16_t i = 0; i < numModes && r.ok(); ++i) c.modes.append(readMode(r, protocol));

    const uint16_t numZones = r.u16();
    for (uint16_t i = 0; i < numZones && r.ok(); ++i) {
        Zone z;
        z.name = r.str();
        z.type = r.i32();
        z.ledsMin = r.u32();
        z.ledsMax = r.u32();
        z.ledsCount = r.u32();
        const uint16_t matrixLen = r.u16();
        if (matrixLen > 0) {
            z.matrixHeight = r.u32();
            z.matrixWidth = r.u32();
            const qsizetype cells = qsizetype(z.matrixHeight) * z.matrixWidth;
            if (cells * 4 + 8 != matrixLen) return std::nullopt;
            for (qsizetype k = 0; k < cells && r.ok(); ++k) z.matrix.append(r.u32());
        }
        if (protocol >= 4) {
            const uint16_t numSegments = r.u16();
            for (uint16_t s = 0; s < numSegments && r.ok(); ++s) {
                Segment seg;
                seg.name = r.str();
                seg.type = r.i32();
                seg.startIdx = r.u32();
                seg.ledsCount = r.u32();
                z.segments.append(seg);
            }
        }
        c.zones.append(z);
    }

    const uint16_t numLeds = r.u16();
    for (uint16_t i = 0; i < numLeds && r.ok(); ++i) {
        Led l;
        l.name = r.str();
        l.value = r.u32();
        c.leds.append(l);
    }
    c.colors = r.colors(r.u16());

    if (!r.ok()) return std::nullopt;
    if (c.activeMode >= c.modes.size()) c.activeMode = c.modes.isEmpty() ? -1 : 0;
    return c;
}

QByteArray packUint32(uint32_t v) {
    Writer w;
    w.u32(v);
    return w.buf;
}

QByteArray packClientName(const QString& name) {
    QByteArray b = name.toUtf8();
    b.append('\0');
    return b;
}

QByteArray packUpdateLeds(const QList<QRgb>& colors) {
    Writer body;
    body.u16(uint16_t(colors.size()));
    body.colors(colors);
    Writer w;
    w.u32(uint32_t(body.buf.size() + 4));
    w.buf.append(body.buf);
    return w.buf;
}

QByteArray packUpdateZoneLeds(uint32_t zone, const QList<QRgb>& colors) {
    Writer body;
    body.u32(zone);
    body.u16(uint16_t(colors.size()));
    body.colors(colors);
    Writer w;
    w.u32(uint32_t(body.buf.size() + 4));
    w.buf.append(body.buf);
    return w.buf;
}

QByteArray packUpdateMode(int32_t modeIndex, const Mode& mode, uint32_t protocol) {
    Writer body;
    body.i32(modeIndex);
    writeMode(body, mode, protocol);
    Writer w;
    w.u32(uint32_t(body.buf.size() + 4));
    w.buf.append(body.buf);
    return w.buf;
}

QByteArray serializeController(const Controller& c, uint32_t protocol) {
    Writer w;
    w.i32(c.type);
    w.str(c.name);
    if (protocol >= 1) w.str(c.vendor);
    w.str(c.description);
    w.str(c.version);
    w.str(c.serial);
    w.str(c.location);
    w.u16(uint16_t(c.modes.size()));
    w.i32(c.activeMode);
    for (const auto& m : c.modes) writeMode(w, m, protocol);
    w.u16(uint16_t(c.zones.size()));
    for (const auto& z : c.zones) {
        w.str(z.name);
        w.i32(z.type);
        w.u32(z.ledsMin);
        w.u32(z.ledsMax);
        w.u32(z.ledsCount);
        if (z.matrix.isEmpty()) {
            w.u16(0);
        } else {
            w.u16(uint16_t(8 + 4 * z.matrix.size()));
            w.u32(z.matrixHeight);
            w.u32(z.matrixWidth);
            for (uint32_t v : z.matrix) w.u32(v);
        }
        if (protocol >= 4) {
            w.u16(uint16_t(z.segments.size()));
            for (const auto& s : z.segments) {
                w.str(s.name);
                w.i32(s.type);
                w.u32(s.startIdx);
                w.u32(s.ledsCount);
            }
        }
    }
    w.u16(uint16_t(c.leds.size()));
    for (const auto& l : c.leds) {
        w.str(l.name);
        w.u32(l.value);
    }
    w.u16(uint16_t(c.colors.size()));
    w.colors(c.colors);
    Writer out;
    out.u32(uint32_t(w.buf.size() + 4));
    out.buf.append(w.buf);
    return out.buf;
}

}  // namespace orgb
