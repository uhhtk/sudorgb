#pragma once
// RGB devices ORKC drives itself (no OpenRGB driver exists for them).
//
// Currently: the PixArt-based Glorious Model O 2 / O 2 Mini / I 2 family
// (VID 0x093a). Lighting is a write-only vendor feature report (ID 0x03, three
// 64-byte fragments, command 02 FB) on the receiver's vendor interface (usage
// page 0xFF00). Protocol facts per zeppybabe/gloriousctl-linux and the
// OpenMouse-Project notes; this is an independent implementation.
//
// Devices are presented as orgb::Controller objects so the rest of the app
// (device list, colour wheel, modes, profiles, session restore) treats them
// exactly like OpenRGB devices.

#include "openrgb/OrgbProtocol.h"

#include <QHash>
#include <QObject>
#include <QTimer>

namespace glorious {

// Effect ids (payload byte 5).
enum Effect : uint8_t {
    Off = 0x00, Rainbow = 0x01, SeamlessBreathing = 0x02, BreathingCycle = 0x03,
    Static = 0x04, Breathing = 0x05, Tail = 0x06, Rave = 0x07, Wave = 0x08,
};
constexpr uint8_t kReportId = 0x03;
constexpr int kPacketLength = 64;
constexpr int kPaletteSize = 7;

uint8_t quantize(uint32_t value, std::initializer_list<uint8_t> levels);
// The three feature-report fragments (each 64 bytes, byte 0 = report id).
QList<QByteArray> lightingPayload(uint8_t effect, uint32_t brightness, uint32_t speed, const QList<QRgb>& colors);
QList<orgb::Mode> modes();

}  // namespace glorious

class NativeDevices : public QObject {
    Q_OBJECT
public:
    explicit NativeDevices(QObject* parent = nullptr);

    void start();
    void rescan();
    const QList<orgb::Controller>& controllers() const { return m_controllers; }

    bool updateMode(int index, int modeIndex, const orgb::Mode& mode);
    QString lastError() const { return m_lastError; }

Q_SIGNALS:
    void controllersReset();
    void controllerChanged(int index);
    void error(const QString& message);

private:
    struct Hw {
        QString node;     // /dev/hidrawN (vendor interface)
        uint16_t pid = 0;
    };
    void flush();
    void scheduleFlush();
    void sendNextFragment();
    bool write(const Hw& hw, const QList<QByteArray>& fragments, QString* err) const;
    void loadState();
    void saveState() const;

    QList<orgb::Controller> m_controllers;
    QList<Hw> m_hw;
    QHash<int, orgb::Mode> m_pending;  // device index -> newest mode to write (latest wins)
    QHash<int, QByteArray> m_lastSent; // device index -> last payload actually written
    qint64 m_lastWriteMs = 0;
    QList<QPair<int, QByteArray>> m_outbox;  // fragments still to send, 120 ms apart
    QTimer m_fragTimer;
    QTimer m_flush;
    QTimer m_scan;
    QString m_lastError;
};
