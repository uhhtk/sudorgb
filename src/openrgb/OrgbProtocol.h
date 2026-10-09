#pragma once
// OpenRGB SDK wire protocol (little-endian, 16-byte "ORGB" header).
// Reference: OpenRGB Documentation/OpenRGBSDK.md. We negotiate up to protocol
// version 4 (brightness + zone segments), which every 0.9+ and 1.0 server speaks
// and whose layout is fully specified; the server encodes controller data in
// the version the client requests.

#include <QByteArray>
#include <QList>
#include <QRgb>
#include <QString>
#include <cstdint>
#include <optional>

namespace orgb {

constexpr uint32_t kClientProtocolVersion = 4;
constexpr int kHeaderSize = 16;
constexpr uint32_t kMaxPacketSize = 64u * 1024u * 1024u;

enum PacketId : uint32_t {
    RequestControllerCount = 0,
    RequestControllerData = 1,
    RequestProtocolVersion = 40,
    SetClientName = 50,
    DeviceListUpdated = 100,
    ResizeZone = 1000,
    UpdateLeds = 1050,
    UpdateZoneLeds = 1051,
    UpdateSingleLed = 1052,
    SetCustomMode = 1100,
    UpdateMode = 1101,
    SaveMode = 1102,
};

enum ModeFlag : uint32_t {
    HasSpeed = 1u << 0,
    HasDirectionLR = 1u << 1,
    HasDirectionUD = 1u << 2,
    HasDirectionHV = 1u << 3,
    HasBrightness = 1u << 4,
    HasPerLedColor = 1u << 5,
    HasModeSpecificColor = 1u << 6,
    HasRandomColor = 1u << 7,
    ManualSave = 1u << 8,
    AutomaticSave = 1u << 9,
};

enum ColorMode : uint32_t { ColorNone = 0, ColorPerLed = 1, ColorModeSpecific = 2, ColorRandom = 3 };

struct Mode {
    QString name;
    int32_t value = 0;
    uint32_t flags = 0;
    uint32_t speedMin = 0, speedMax = 0;
    uint32_t brightnessMin = 0, brightnessMax = 0;
    uint32_t colorsMin = 0, colorsMax = 0;
    uint32_t speed = 0, brightness = 0, direction = 0, colorMode = 0;
    QList<QRgb> colors;

    bool has(ModeFlag f) const { return (flags & f) != 0; }
    bool isPerLed() const { return colorMode == ColorPerLed; }
};

struct Segment {
    QString name;
    int32_t type = 0;
    uint32_t startIdx = 0, ledsCount = 0;
};

struct Zone {
    QString name;
    int32_t type = 0;  // 0 single, 1 linear, 2 matrix
    uint32_t ledsMin = 0, ledsMax = 0, ledsCount = 0;
    uint32_t matrixHeight = 0, matrixWidth = 0;
    QList<uint32_t> matrix;
    QList<Segment> segments;
};

struct Led {
    QString name;
    uint32_t value = 0;
};

struct Controller {
    int32_t type = 0;
    QString name, vendor, description, version, serial, location;
    int32_t activeMode = 0;
    QList<Mode> modes;
    QList<Zone> zones;
    QList<Led> leds;
    QList<QRgb> colors;

    const Mode* currentMode() const {
        return (activeMode >= 0 && activeMode < modes.size()) ? &modes[activeMode] : nullptr;
    }
    // Index of the first LED of zone `z` in the flat LED/colour arrays.
    int zoneStart(int z) const;
};

struct Header {
    uint32_t deviceIndex = 0;
    uint32_t packetId = 0;
    uint32_t size = 0;
};

QString deviceTypeName(int32_t type);
QString deviceTypeIcon(int32_t type);

// Wire colour is 0x00BBGGRR; QRgb is 0xAARRGGBB.
inline uint32_t toWire(QRgb c) { return uint32_t(qRed(c)) | (uint32_t(qGreen(c)) << 8) | (uint32_t(qBlue(c)) << 16); }
inline QRgb fromWire(uint32_t w) { return qRgb(w & 0xFF, (w >> 8) & 0xFF, (w >> 16) & 0xFF); }

// Returns nullopt if `data` doesn't start with a valid header (bad magic / absurd size).
std::optional<Header> parseHeader(const QByteArray& data);
QByteArray packet(uint32_t deviceIndex, uint32_t packetId, const QByteArray& payload = {});

// `payload` is the full REQUEST_CONTROLLER_DATA reply body (starting with data_size).
std::optional<Controller> parseController(const QByteArray& payload, uint32_t protocol);

QByteArray packUint32(uint32_t v);
QByteArray packClientName(const QString& name);
QByteArray packUpdateLeds(const QList<QRgb>& colors);
QByteArray packUpdateZoneLeds(uint32_t zone, const QList<QRgb>& colors);
QByteArray packUpdateMode(int32_t modeIndex, const Mode& mode, uint32_t protocol);
QByteArray serializeController(const Controller& c, uint32_t protocol);  // for tests / fake servers

}  // namespace orgb
