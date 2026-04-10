#include "ipc/extension_pipe_server.h"
#include "util/logging.h"

#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>

namespace cr {

namespace {
constexpr const char* kPipeName = "creechr-extension";
} // namespace

ExtensionPipeServer::ExtensionPipeServer(QObject* parent)
    : QObject(parent)
{
}

ExtensionPipeServer::~ExtensionPipeServer()
{
    if (m_server) {
        m_server->close();
    }
}

bool ExtensionPipeServer::start()
{
    if (m_server) return true;

    // QLocalServer doesnt clean up stale pipe handles from a previous
    // crashed run. removeServer() is the documented escape hatch.
    QLocalServer::removeServer(QLatin1String(kPipeName));

    m_server = new QLocalServer(this);
    // SocketAccessOption defaults to UserAccessOption which restricts
    // the pipe to the current user. perfect — no other login session
    // gets to talk to creechr.
    m_server->setSocketOptions(QLocalServer::UserAccessOption);

    if (!m_server->listen(QLatin1String(kPipeName))) {
        LOG_WARN(QStringLiteral("ExtensionPipeServer: listen failed: %1")
            .arg(m_server->errorString()));
        delete m_server;
        m_server = nullptr;
        return false;
    }
    connect(m_server, &QLocalServer::newConnection,
            this, &ExtensionPipeServer::onNewConnection);
    LOG_INFO(QStringLiteral("ExtensionPipeServer: listening on %1")
        .arg(QString::fromLatin1(kPipeName)));
    return true;
}

bool ExtensionPipeServer::isConnected() const
{
    return m_client != nullptr && m_client->state() == QLocalSocket::ConnectedState;
}

bool ExtensionPipeServer::send(const QJsonObject& message)
{
    if (!isConnected()) return false;
    QByteArray bytes = QJsonDocument(message).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    const qint64 wrote = m_client->write(bytes);
    if (wrote != bytes.size()) {
        LOG_WARN(QStringLiteral("ExtensionPipeServer: short write %1/%2")
            .arg(wrote).arg(bytes.size()));
        return false;
    }
    m_client->flush();
    return true;
}

void ExtensionPipeServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QLocalSocket* sock = m_server->nextPendingConnection();
        if (!sock) continue;

        if (m_client) {
            // last-bridge-wins. drop the previous one.
            LOG_INFO(QStringLiteral("ExtensionPipeServer: replacing existing client"));
            disconnect(m_client, nullptr, this, nullptr);
            m_client->disconnectFromServer();
            m_client->deleteLater();
            m_client = nullptr;
            m_rxBuffer.clear();
            m_extId.clear();
            emit bridgeDisconnected();
        }

        m_client = sock;
        connect(m_client, &QLocalSocket::readyRead,
                this, &ExtensionPipeServer::onReadyRead);
        connect(m_client, &QLocalSocket::disconnected,
                this, &ExtensionPipeServer::onClientDisconnected);

        LOG_INFO(QStringLiteral("ExtensionPipeServer: bridge connected"));
        emit bridgeConnected();

        // drain anything already buffered
        if (m_client->bytesAvailable() > 0) {
            onReadyRead();
        }
    }
}

void ExtensionPipeServer::onReadyRead()
{
    if (!m_client) return;
    m_rxBuffer.append(m_client->readAll());

    // parse newline-delimited json. multiple messages can arrive
    // in a single read so loop until no more newlines.
    while (true) {
        const int nl = m_rxBuffer.indexOf('\n');
        if (nl < 0) break;
        const QByteArray line = m_rxBuffer.left(nl);
        m_rxBuffer.remove(0, nl + 1);
        if (line.isEmpty()) continue;

        QJsonParseError err;
        const auto doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            LOG_WARN(QStringLiteral("ExtensionPipeServer: bad json: %1")
                .arg(err.errorString()));
            continue;
        }
        const QJsonObject obj = doc.object();
        const QString type = obj.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("hello")) {
            m_extId = obj.value(QStringLiteral("ext")).toString();
            LOG_INFO(QStringLiteral("ExtensionPipeServer: hello from ext '%1'").arg(m_extId));
        }
        emit messageReceived(obj);
    }
}

void ExtensionPipeServer::onClientDisconnected()
{
    LOG_INFO(QStringLiteral("ExtensionPipeServer: bridge disconnected"));
    if (m_client) {
        m_client->deleteLater();
        m_client = nullptr;
    }
    m_rxBuffer.clear();
    m_extId.clear();
    emit bridgeDisconnected();
}

} // namespace cr
