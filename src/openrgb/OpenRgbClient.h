#pragma once
// Asynchronous OpenRGB SDK client (single QTcpSocket, never blocks the GUI).
//
// * Handshake: protocol-version negotiation (servers < v1 never reply, so a
//   short timeout falls back to v0), client name, controller count, then each
//   controller's data block.
// * Server push DEVICE_LIST_UPDATED triggers a debounced resync.
// * Replies are matched against a FIFO of expectations (the SDK has no request
//   ids); any reply that doesn't arrive within the timeout drops the
//   connection, which then reconnects with exponential backoff.
// * LED writes are coalesced per device at <= 30 Hz, so dragging a colour wheel
//   never floods the server or USB bus.

#include "openrgb/OrgbProtocol.h"

#include <QHash>
#include <QSet>
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <deque>

class OpenRgbClient : public QObject {
    Q_OBJECT
public:
    enum class State { Disconnected, Connecting, Syncing, Connected };
    Q_ENUM(State)

    explicit OpenRgbClient(QObject* parent = nullptr);
    ~OpenRgbClient() override;

    void connectToServer(const QString& host, quint16 port);
    void disconnectFromServer();
    void resync();

    State state() const { return m_state; }
    uint32_t protocol() const { return m_protocol; }
    QString lastError() const { return m_lastError; }
    const QList<orgb::Controller>& controllers() const { return m_controllers; }

    // Writes. Indices refer to controllers(). All return false if not connected.
    bool updateLeds(int dev, const QList<QRgb>& colors);  // coalesced
    bool updateZoneLeds(int dev, int zone, const QList<QRgb>& colors);
    bool updateMode(int dev, int modeIndex, const orgb::Mode& mode);
    bool saveMode(int dev, int modeIndex, const orgb::Mode& mode);
    bool setCustomMode(int dev);

Q_SIGNALS:
    void stateChanged(OpenRgbClient::State state);
    void controllersReset();         // whole list replaced
    void controllerChanged(int dev); // one controller's data refreshed or written
    void errorOccurred(const QString& message);

private:
    enum class Expect { Version, Count, Data };
    struct Pending {
        Expect kind;
        uint32_t packetId;
        uint32_t device;
        bool refreshOnly;  // single-device refresh vs. part of a full sync
    };

    void setState(State s);
    void onConnected();
    void onReadyRead();
    void onSocketError();
    void handlePacket(const orgb::Header& h, const QByteArray& payload);
    void send(uint32_t dev, uint32_t id, const QByteArray& payload = {});
    void expect(Expect kind, uint32_t packetId, uint32_t dev, bool refreshOnly = false);
    void startFullSync();
    void requestRefresh(int dev);
    void flushLedWrites();
    void scheduleReconnect();
    void fail(const QString& why);

    QTcpSocket m_socket;
    QByteArray m_buffer;
    State m_state = State::Disconnected;
    QString m_host;
    quint16 m_port = 6742;
    bool m_wantConnected = false;
    uint32_t m_protocol = 0;
    QString m_lastError;

    std::deque<Pending> m_pending;
    QTimer m_replyTimer;      // fires if the oldest expectation isn't satisfied
    QTimer m_reconnectTimer;
    QTimer m_resyncDebounce;
    QTimer m_ledFlush;
    QTimer m_refreshDebounce;
    int m_backoffMs = 1000;

    QList<orgb::Controller> m_controllers;
    QList<orgb::Controller> m_staging;
    uint32_t m_expectedCount = 0;
    QHash<int, QList<QRgb>> m_pendingLeds;
    QSet<int> m_refreshQueue;
};
