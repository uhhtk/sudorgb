#include "openrgb/OpenRgbClient.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcOrgb, "orkc.openrgb")

namespace {
constexpr int kReplyTimeoutMs = 5000;
constexpr int kVersionTimeoutMs = 1000;  // pre-v1 servers silently ignore the request
constexpr int kMaxBackoffMs = 10000;
constexpr int kLedFlushMs = 33;
}  // namespace

OpenRgbClient::OpenRgbClient(QObject* parent) : QObject(parent) {
    m_socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(&m_socket, &QTcpSocket::connected, this, &OpenRgbClient::onConnected);
    connect(&m_socket, &QTcpSocket::readyRead, this, &OpenRgbClient::onReadyRead);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &OpenRgbClient::onSocketError);
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] {
        if (m_state != State::Disconnected) fail(QStringLiteral("OpenRGB server closed the connection"));
    });

    m_replyTimer.setSingleShot(true);
    connect(&m_replyTimer, &QTimer::timeout, this, [this] {
        if (m_pending.empty()) return;
        if (m_pending.front().kind == Expect::Version) {
            // Protocol 0 server: it ignores the version request entirely.
            m_pending.pop_front();
            m_protocol = 0;
            qCInfo(lcOrgb) << "server did not answer version request; assuming protocol 0";
            send(0, orgb::SetClientName, orgb::packClientName(QStringLiteral("ORKC")));
            startFullSync();
            return;
        }
        fail(QStringLiteral("OpenRGB server stopped responding"));
    });

    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this] {
        if (m_wantConnected) connectToServer(m_host, m_port);
    });

    m_resyncDebounce.setSingleShot(true);
    m_resyncDebounce.setInterval(300);
    connect(&m_resyncDebounce, &QTimer::timeout, this, &OpenRgbClient::startFullSync);

    m_ledFlush.setSingleShot(true);
    m_ledFlush.setInterval(kLedFlushMs);
    connect(&m_ledFlush, &QTimer::timeout, this, &OpenRgbClient::flushLedWrites);

    m_refreshDebounce.setSingleShot(true);
    m_refreshDebounce.setInterval(250);
    connect(&m_refreshDebounce, &QTimer::timeout, this, [this] {
        const auto devs = m_refreshQueue;
        m_refreshQueue.clear();
        for (int d : devs) {
            if (m_state != State::Connected || d >= m_controllers.size()) continue;
            send(uint32_t(d), orgb::RequestControllerData, orgb::packUint32(m_protocol));
            expect(Expect::Data, orgb::RequestControllerData, uint32_t(d), true);
        }
    });
}

OpenRgbClient::~OpenRgbClient() {
    // m_socket is destroyed after every other member; its destructor emits
    // disconnected()/errorOccurred(), whose handlers would touch members that
    // are already gone. Detach first.
    m_socket.disconnect(this);
    m_socket.abort();
}

void OpenRgbClient::setState(State s) {
    if (m_state == s) return;
    m_state = s;
    Q_EMIT stateChanged(s);
}

void OpenRgbClient::connectToServer(const QString& host, quint16 port) {
    m_host = host;
    m_port = port;
    m_wantConnected = true;
    m_reconnectTimer.stop();
    if (m_socket.state() != QAbstractSocket::UnconnectedState) m_socket.abort();
    m_buffer.clear();
    m_pending.clear();
    setState(State::Connecting);
    m_socket.connectToHost(host, port);
}

void OpenRgbClient::disconnectFromServer() {
    m_wantConnected = false;
    m_reconnectTimer.stop();
    m_replyTimer.stop();
    m_pending.clear();
    setState(State::Disconnected);
    m_socket.abort();
}

void OpenRgbClient::resync() {
    if (m_state == State::Connected) startFullSync();
    else if (m_wantConnected) connectToServer(m_host, m_port);
}

void OpenRgbClient::onConnected() {
    m_backoffMs = 1000;
    m_lastError.clear();
    setState(State::Syncing);
    send(0, orgb::RequestProtocolVersion, orgb::packUint32(orgb::kClientProtocolVersion));
    expect(Expect::Version, orgb::RequestProtocolVersion, 0);
    m_replyTimer.start(kVersionTimeoutMs);
}

void OpenRgbClient::onSocketError() {
    if (m_state == State::Disconnected) return;
    fail(m_socket.errorString());
}

void OpenRgbClient::fail(const QString& why) {
    m_lastError = why;
    qCInfo(lcOrgb) << "connection lost:" << why;
    m_replyTimer.stop();
    m_pending.clear();
    m_buffer.clear();
    m_socket.abort();
    const bool hadDevices = !m_controllers.isEmpty();
    m_controllers.clear();
    if (hadDevices) Q_EMIT controllersReset();
    setState(State::Disconnected);
    Q_EMIT errorOccurred(why);
    scheduleReconnect();
}

void OpenRgbClient::scheduleReconnect() {
    if (!m_wantConnected) return;
    m_reconnectTimer.start(m_backoffMs);
    m_backoffMs = qMin(m_backoffMs * 2, kMaxBackoffMs);
}

void OpenRgbClient::send(uint32_t dev, uint32_t id, const QByteArray& payload) {
    if (m_socket.state() != QAbstractSocket::ConnectedState) return;
    m_socket.write(orgb::packet(dev, id, payload));
}

void OpenRgbClient::expect(Expect kind, uint32_t packetId, uint32_t dev, bool refreshOnly) {
    m_pending.push_back({kind, packetId, dev, refreshOnly});
    if (!m_replyTimer.isActive()) m_replyTimer.start(kReplyTimeoutMs);
}

void OpenRgbClient::startFullSync() {
    if (m_socket.state() != QAbstractSocket::ConnectedState) return;
    // Drop any queued single-device refreshes; the full sync supersedes them.
    std::erase_if(m_pending, [](const Pending& p) { return p.refreshOnly; });
    setState(State::Syncing);
    m_staging.clear();
    send(0, orgb::RequestControllerCount);
    expect(Expect::Count, orgb::RequestControllerCount, 0);
}

void OpenRgbClient::onReadyRead() {
    m_buffer.append(m_socket.readAll());
    while (m_buffer.size() >= orgb::kHeaderSize) {
        const auto header = orgb::parseHeader(m_buffer);
        if (!header) {
            fail(QStringLiteral("Protocol error: bad packet header from server"));
            return;
        }
        const qsizetype total = orgb::kHeaderSize + qsizetype(header->size);
        if (m_buffer.size() < total) return;  // wait for the rest
        const QByteArray payload = m_buffer.mid(orgb::kHeaderSize, header->size);
        m_buffer.remove(0, total);
        handlePacket(*header, payload);
        if (m_state == State::Disconnected) return;
    }
}

void OpenRgbClient::handlePacket(const orgb::Header& h, const QByteArray& payload) {
    if (h.packetId == orgb::DeviceListUpdated) {
        qCInfo(lcOrgb) << "server reports device list change; resyncing";
        m_resyncDebounce.start();
        return;
    }
    if (m_pending.empty() || m_pending.front().packetId != h.packetId) {
        qCDebug(lcOrgb) << "ignoring unsolicited packet" << h.packetId;
        return;
    }
    const Pending p = m_pending.front();
    m_pending.pop_front();
    m_replyTimer.stop();
    if (!m_pending.empty()) m_replyTimer.start(kReplyTimeoutMs);

    switch (p.kind) {
    case Expect::Version: {
        const uint32_t server = payload.size() >= 4 ? uint32_t(uchar(payload[0])) | uint32_t(uchar(payload[1])) << 8 |
                                                          uint32_t(uchar(payload[2])) << 16 | uint32_t(uchar(payload[3])) << 24
                                                    : 0;
        m_protocol = qMin(server, orgb::kClientProtocolVersion);
        qCInfo(lcOrgb) << "server protocol" << server << "-> using" << m_protocol;
        send(0, orgb::SetClientName, orgb::packClientName(QStringLiteral("ORKC")));
        startFullSync();
        break;
    }
    case Expect::Count: {
        m_expectedCount = payload.size() >= 4 ? uint32_t(uchar(payload[0])) | uint32_t(uchar(payload[1])) << 8 |
                                                     uint32_t(uchar(payload[2])) << 16 | uint32_t(uchar(payload[3])) << 24
                                               : 0;
        if (m_expectedCount > 512) {
            fail(QStringLiteral("Protocol error: absurd controller count"));
            return;
        }
        if (m_expectedCount == 0) {
            m_controllers.clear();
            setState(State::Connected);
            Q_EMIT controllersReset();
            break;
        }
        for (uint32_t i = 0; i < m_expectedCount; ++i) {
            send(i, orgb::RequestControllerData, orgb::packUint32(m_protocol));
            expect(Expect::Data, orgb::RequestControllerData, i);
        }
        break;
    }
    case Expect::Data: {
        auto c = orgb::parseController(payload, m_protocol);
        if (!c) {
            fail(QStringLiteral("Protocol error: malformed controller data (device %1)").arg(h.deviceIndex));
            return;
        }
        if (p.refreshOnly) {
            if (int(h.deviceIndex) < m_controllers.size()) {
                m_controllers[int(h.deviceIndex)] = *c;
                Q_EMIT controllerChanged(int(h.deviceIndex));
            }
        } else {
            m_staging.append(*c);
            if (uint32_t(m_staging.size()) == m_expectedCount) {
                m_controllers = std::move(m_staging);
                m_staging.clear();
                setState(State::Connected);
                Q_EMIT controllersReset();
            }
        }
        break;
    }
    }
}

bool OpenRgbClient::updateLeds(int dev, const QList<QRgb>& colors) {
    if (m_state != State::Connected || dev < 0 || dev >= m_controllers.size()) return false;
    m_controllers[dev].colors = colors;
    m_pendingLeds.insert(dev, colors);
    if (!m_ledFlush.isActive()) m_ledFlush.start();
    Q_EMIT controllerChanged(dev);
    return true;
}

void OpenRgbClient::flushLedWrites() {
    for (auto it = m_pendingLeds.cbegin(); it != m_pendingLeds.cend(); ++it)
        send(uint32_t(it.key()), orgb::UpdateLeds, orgb::packUpdateLeds(it.value()));
    m_pendingLeds.clear();
}

bool OpenRgbClient::updateZoneLeds(int dev, int zone, const QList<QRgb>& colors) {
    if (m_state != State::Connected || dev < 0 || dev >= m_controllers.size()) return false;
    auto& c = m_controllers[dev];
    if (zone < 0 || zone >= c.zones.size()) return false;
    const int start = c.zoneStart(zone);
    for (int i = 0; i < colors.size() && start + i < c.colors.size(); ++i) c.colors[start + i] = colors[i];
    // A pending whole-device write would overwrite this zone with stale data.
    if (m_pendingLeds.contains(dev)) m_pendingLeds[dev] = c.colors;
    else send(uint32_t(dev), orgb::UpdateZoneLeds, orgb::packUpdateZoneLeds(uint32_t(zone), colors));
    Q_EMIT controllerChanged(dev);
    return true;
}

bool OpenRgbClient::updateMode(int dev, int modeIndex, const orgb::Mode& mode) {
    if (m_state != State::Connected || dev < 0 || dev >= m_controllers.size()) return false;
    auto& c = m_controllers[dev];
    if (modeIndex < 0 || modeIndex >= c.modes.size()) return false;
    flushLedWrites();  // keep ordering: earlier colour writes land before the mode change
    c.modes[modeIndex] = mode;
    c.activeMode = modeIndex;
    send(uint32_t(dev), orgb::UpdateMode, orgb::packUpdateMode(modeIndex, mode, m_protocol));
    Q_EMIT controllerChanged(dev);
    requestRefresh(dev);
    return true;
}

bool OpenRgbClient::saveMode(int dev, int modeIndex, const orgb::Mode& mode) {
    if (m_state != State::Connected || m_protocol < 3 || dev < 0 || dev >= m_controllers.size()) return false;
    send(uint32_t(dev), orgb::SaveMode, orgb::packUpdateMode(modeIndex, mode, m_protocol));
    return true;
}

bool OpenRgbClient::setCustomMode(int dev) {
    if (m_state != State::Connected || dev < 0 || dev >= m_controllers.size()) return false;
    flushLedWrites();
    send(uint32_t(dev), orgb::SetCustomMode);
    requestRefresh(dev);
    return true;
}

void OpenRgbClient::requestRefresh(int dev) {
    m_refreshQueue.insert(dev);
    m_refreshDebounce.start();
}
