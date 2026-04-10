#include "targets/extension_target_provider.h"
#include "ipc/extension_pipe_server.h"
#include "util/logging.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QScreen>
#include <QTimer>

namespace cr {

ExtensionTargetProvider::ExtensionTargetProvider(ExtensionPipeServer* server,
                                                  QObject* parent)
    : QObject(parent)
    , m_server(server)
{
    if (!m_server) return;
    connect(m_server, &ExtensionPipeServer::bridgeConnected,
            this, &ExtensionTargetProvider::onBridgeConnected);
    connect(m_server, &ExtensionPipeServer::bridgeDisconnected,
            this, &ExtensionTargetProvider::onBridgeDisconnected);
    connect(m_server, &ExtensionPipeServer::messageReceived,
            this, &ExtensionTargetProvider::onPipeMessage);

    m_autoScanTimer = new QTimer(this);
    m_autoScanTimer->setInterval(3000);
    connect(m_autoScanTimer, &QTimer::timeout,
            this, &ExtensionTargetProvider::onAutoScanTimer);
}

bool ExtensionTargetProvider::isReady() const
{
    return m_server && m_server->isConnected() && !m_cached.isEmpty();
}

std::optional<HeistTarget> ExtensionTargetProvider::pickRandom(const QRect& virtualDesktop) const
{
    if (m_cached.isEmpty()) return std::nullopt;
    // require the cache to be no more than 8 seconds stale. otherwise
    // we might pick an element thats already been scrolled offscreen.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastScanMs == 0 || now - m_lastScanMs > 8000) return std::nullopt;

    // random pick, but require the rect to actually intersect the desktop
    for (int tries = 0; tries < 8; ++tries) {
        const int idx = QRandomGenerator::global()->bounded(m_cached.size());
        const DomTarget& d = m_cached[idx];
        if (!virtualDesktop.intersects(d.screenRect)) continue;
        if (d.screenRect.width() < 16 || d.screenRect.height() < 12) continue;

        HeistTarget t;
        t.kind = TargetKind::DomElement;
        t.screenRect = d.screenRect;
        t.opaqueId = d.id;
        t.label = QStringLiteral("dom:") + d.tag + QStringLiteral(":") + d.label;
        return t;
    }
    return std::nullopt;
}

void ExtensionTargetProvider::requestSteal(const QString& targetId)
{
    if (!m_server || !m_server->isConnected()) return;
    QJsonObject msg;
    msg[QStringLiteral("type")]     = QStringLiteral("steal");
    msg[QStringLiteral("targetId")] = targetId;
    m_server->send(msg);
    LOG_INFO(QStringLiteral("ext: steal request -> %1").arg(targetId));
}

void ExtensionTargetProvider::requestRestore(const QString& targetId)
{
    if (!m_server || !m_server->isConnected()) return;
    QJsonObject msg;
    msg[QStringLiteral("type")]     = QStringLiteral("restore");
    msg[QStringLiteral("targetId")] = targetId;
    m_server->send(msg);
    LOG_INFO(QStringLiteral("ext: restore request -> %1").arg(targetId));
}

void ExtensionTargetProvider::requestScan()
{
    if (!m_server || !m_server->isConnected()) return;
    QJsonObject msg;
    msg[QStringLiteral("type")] = QStringLiteral("scan_targets");
    m_server->send(msg);
}

void ExtensionTargetProvider::onBridgeConnected()
{
    LOG_INFO(QStringLiteral("ext provider: bridge connected, starting scans"));
    m_cached.clear();
    m_lastScanMs = 0;
    requestScan(); // initial
    m_autoScanTimer->start();
}

void ExtensionTargetProvider::onBridgeDisconnected()
{
    LOG_INFO(QStringLiteral("ext provider: bridge gone"));
    m_autoScanTimer->stop();
    m_cached.clear();
    m_lastScanMs = 0;
}

void ExtensionTargetProvider::onAutoScanTimer()
{
    requestScan();
}

void ExtensionTargetProvider::onPipeMessage(const QJsonObject& msg)
{
    const QString type = msg.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("scan_result")) {
        m_cached.clear();
        const QJsonArray items = msg.value(QStringLiteral("items")).toArray();
        QScreen* primary = QGuiApplication::primaryScreen();
        const qreal sysDpr = primary ? primary->devicePixelRatio() : 1.0;

        for (const QJsonValue& v : items) {
            const QJsonObject o = v.toObject();
            DomTarget d;
            d.id    = o.value(QStringLiteral("id")).toString();
            d.tag   = o.value(QStringLiteral("tag")).toString();
            d.label = o.value(QStringLiteral("label")).toString();

            // the extension reports screenX/Y in CSS pixels relative to
            // the OS desktop top-left, plus rectCss.{x,y,w,h} relative
            // to the document. devicePixelRatio is the page's own dpr
            // which may differ from the system dpr. CSS px → physical
            // px → logical (qt) px:
            //
            //   physical = css * page_dpr
            //   logical  = physical / sys_dpr
            //
            // for most setups page_dpr == sys_dpr and they cancel out.
            // we still do the math so it's right when they don't.
            const QJsonObject rc = o.value(QStringLiteral("rectCss")).toObject();
            const double pageDpr = o.value(QStringLiteral("dpr")).toDouble(1.0);
            const double cssX = o.value(QStringLiteral("screenX")).toDouble(0);
            const double cssY = o.value(QStringLiteral("screenY")).toDouble(0);
            const double w = rc.value(QStringLiteral("w")).toDouble(0);
            const double h = rc.value(QStringLiteral("h")).toDouble(0);
            const double scale = pageDpr / (sysDpr <= 0 ? 1.0 : sysDpr);
            d.screenRect = QRect(
                static_cast<int>(cssX * scale),
                static_cast<int>(cssY * scale),
                static_cast<int>(w    * scale),
                static_cast<int>(h    * scale)
            );

            if (!d.id.isEmpty() && !d.screenRect.isEmpty()) {
                m_cached.push_back(std::move(d));
            }
        }
        m_lastScanMs = QDateTime::currentMSecsSinceEpoch();
        LOG_INFO(QStringLiteral("ext provider: scan_result, %1 items cached")
            .arg(m_cached.size()));
    }
    else if (type == QLatin1String("steal_ack")) {
        const QString id = msg.value(QStringLiteral("targetId")).toString();
        m_stealAcks.insert(id, QDateTime::currentMSecsSinceEpoch());
        LOG_INFO(QStringLiteral("ext: steal_ack %1").arg(id));
        emit stealAcked(id);
    }
    else if (type == QLatin1String("restore_ack")) {
        const QString id = msg.value(QStringLiteral("targetId")).toString();
        m_restoreAcks.insert(id, QDateTime::currentMSecsSinceEpoch());
        LOG_INFO(QStringLiteral("ext: restore_ack %1").arg(id));
        emit restoreAcked(id);
    }
}

bool ExtensionTargetProvider::hasStealAck(const QString& id) const
{
    return m_stealAcks.contains(id);
}

void ExtensionTargetProvider::consumeStealAck(const QString& id)
{
    m_stealAcks.remove(id);
}

bool ExtensionTargetProvider::hasRestoreAck(const QString& id) const
{
    return m_restoreAcks.contains(id);
}

void ExtensionTargetProvider::consumeRestoreAck(const QString& id)
{
    m_restoreAcks.remove(id);
}

} // namespace cr
