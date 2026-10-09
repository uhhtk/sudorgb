#include "system/SystemMonitor.h"

#include <QDir>
#include <QFile>

using namespace Qt::StringLiterals;

namespace {
QString readText(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}
}  // namespace

SystemMonitor::SystemMonitor(QObject* parent) : QObject(parent) {
    m_cpuPath = findTemp({u"k10temp"_s, u"zenpower"_s, u"coretemp"_s}, {u"Tctl"_s, u"Tdie"_s, u"Package id 0"_s}, &m_cpuSensorLabel);
    m_gpuPath = findTemp({u"amdgpu"_s}, {u"edge"_s, u"junction"_s}, &m_gpuSensorLabel);
    for (const QString& line : readText(u"/proc/cpuinfo"_s).split(u'\n')) {
        if (line.startsWith(u"model name"_s)) {
            m_cpuName = line.section(u':', 1).trimmed();
            break;
        }
    }
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &SystemMonitor::sample);
}

QString SystemMonitor::findTemp(const QStringList& chips, const QStringList& labels, QString* label) {
    const QDir root(u"/sys/class/hwmon"_s);
    for (const QString& chip : chips) {
        for (const QString& hw : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString dir = root.absoluteFilePath(hw);
            if (readText(dir + u"/name"_s) != chip) continue;
            const QStringList inputs = QDir(dir).entryList({u"temp*_input"_s}, QDir::Files, QDir::Name);
            for (const QString& want : labels) {
                for (const QString& in : inputs) {
                    QString lab = in;
                    lab.replace(u"_input"_s, u"_label"_s);
                    if (readText(dir + u'/' + lab) == want) {
                        *label = chip + u" · "_s + want;
                        return dir + u'/' + in;
                    }
                }
            }
            if (!inputs.isEmpty()) {
                *label = chip;
                return dir + u'/' + inputs.first();
            }
        }
    }
    return {};
}

void SystemMonitor::setActive(bool on) {
    if (on == m_timer.isActive()) return;
    if (on) {
        sample();
        m_timer.start();
    } else {
        m_timer.stop();
    }
    Q_EMIT activeChanged();
}

void SystemMonitor::sample() {
    auto temp = [](const QString& path) -> QVariant {
        if (path.isEmpty()) return {};
        bool ok = false;
        const double v = readText(path).toDouble(&ok) / 1000.0;
        return ok && v > -40 && v < 150 ? QVariant(v) : QVariant();
    };
    m_cpuTemp = temp(m_cpuPath);
    m_gpuTemp = temp(m_gpuPath);

    const QStringList f = readText(u"/proc/stat"_s).section(u'\n', 0, 0).split(u' ', Qt::SkipEmptyParts);
    if (f.size() >= 8 && f.first() == u"cpu"_s) {
        quint64 total = 0;
        for (int i = 1; i < f.size(); ++i) total += f[i].toULongLong();
        const quint64 idle = f[4].toULongLong() + f[5].toULongLong();  // idle + iowait
        if (m_prevTotal && total > m_prevTotal)
            m_cpuLoad = 100.0 * (1.0 - double(idle - m_prevIdle) / double(total - m_prevTotal));
        m_prevIdle = idle;
        m_prevTotal = total;
    }
    if (m_cpuTemp.isValid()) {
        m_history.append(m_cpuTemp);
        if (m_history.size() > 180) m_history.removeFirst();
    }
    Q_EMIT updated();
}
