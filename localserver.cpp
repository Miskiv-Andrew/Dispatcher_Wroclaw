#include "localserver.h"
#include <QDebug>
#include <QJsonParseError>

LocalServer::LocalServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
    , m_port(12345)
    , m_running(false)
{
    // Підключаємо сигнал нового підключення
    connect(m_server, &QTcpServer::newConnection, this, &LocalServer::onNewConnection);
}

LocalServer::~LocalServer()
{
    stop();
}

bool LocalServer::start(quint16 port)
{
    if (m_running) {
        emit logMessage("Server is already running.");
        return true;
    }

    m_port = port;
    if (!m_server->listen(QHostAddress::LocalHost, m_port)) {
        emit logMessage(QString("Failed to start server on port %1: %2")
                            .arg(m_port).arg(m_server->errorString()));
        return false;
    }

    m_running = true;
    emit logMessage(QString("Server is running on port %1").arg(m_port));
    return true;
}


void LocalServer::stop()
{
    if (!m_running)
    {
        return;
    }

    // Stop accepting new connections first.
    m_server->close();

    // Work with a copy because socket disconnection may eventually
    // modify m_clients through onClientDisconnected().
    const QList<QTcpSocket*> clients = m_clients;

    // Clear internal client state immediately.
    m_clients.clear();
    m_receiveBuffers.clear();

    // Close all currently connected client sockets.
    for (QTcpSocket *client : clients)
    {
        if (!client)
        {
            continue;
        }

        // We are performing controlled server shutdown.
        // Disconnect these signals so that shutdown does not trigger
        // normal client-disconnection processing.
        disconnect(
            client,
            &QTcpSocket::disconnected,
            this,
            &LocalServer::onClientDisconnected
            );

        disconnect(
            client,
            &QTcpSocket::readyRead,
            this,
            &LocalServer::onClientReadyRead
            );

        // Abort closes the socket immediately.
        // During application shutdown there is no reason to wait
        // for a graceful TCP disconnect handshake.
        client->abort();

        // QObject will be safely deleted by the Qt event loop.
        client->deleteLater();
    }

    m_running = false;

    emit logMessage("Server stopped");
}


void LocalServer::broadcast(const QJsonObject &json)
{
    if (m_clients.isEmpty()) {
        emit logMessage("No connected clients to send");
        return;
    }

    QJsonDocument doc(json);
    QByteArray data = doc.toJson(QJsonDocument::Compact) + "\n";

    for (QTcpSocket *client : m_clients) {
        if (client->state() == QTcpSocket::ConnectedState) {
            client->write(data);
        }
    }

    emit logMessage("Broadcast sent to all clients");
}

// ------------------------------------------------------------
// СЛОТИ ДЛЯ РОБОТИ З КЛІЄНТАМИ
// ------------------------------------------------------------

void LocalServer::onNewConnection()
{
    while (m_server->hasPendingConnections())
    {
        QTcpSocket *client = m_server->nextPendingConnection();

        if (!client)
        {
            continue;
        }

        // Store the connected client.
        m_clients.append(client);

        // Create an empty receive buffer for this specific client.
        //
        // TCP does not preserve message boundaries, therefore incoming
        // bytes must be accumulated until a complete '\n'-terminated
        // JSON message is available.
        m_receiveBuffers.insert(client, QByteArray());

        // Receive notification when the client disconnects.
        connect(
            client,
            &QTcpSocket::disconnected,
            this,
            &LocalServer::onClientDisconnected
            );

        // Receive notification when new TCP data is available.
        connect(
            client,
            &QTcpSocket::readyRead,
            this,
            &LocalServer::onClientReadyRead
            );

        emit logMessage(
            QString("Client connected: %1:%2")
                .arg(client->peerAddress().toString())
                .arg(client->peerPort())
            );

        emit clientConnected();
    }
}


void LocalServer::onClientDisconnected()
{
    QTcpSocket *client = qobject_cast<QTcpSocket*>(sender());

    if (!client)
    {
        return;
    }

    // Remove the client from the list of active connections.
    m_clients.removeAll(client);

    // Remove all unprocessed TCP data associated with this client.
    //
    // If the client disconnected in the middle of a JSON message,
    // that incomplete message must never be reused for another
    // connection.
    m_receiveBuffers.remove(client);

    // The socket is already in the disconnected state because this
    // method is called from QTcpSocket::disconnected.
    // Schedule the QObject for safe deletion by the Qt event loop.
    client->deleteLater();

    emit logMessage("Client disconnected");

    emit clientDisconnected();
}





void LocalServer::onClientReadyRead()
{
    QTcpSocket *client = qobject_cast<QTcpSocket*>(sender());

    if (!client)
    {
        return;
    }

    // Read all bytes that are currently available from the TCP socket.
    const QByteArray receivedData = client->readAll();

    if (receivedData.isEmpty())
    {
        return;
    }

    emit logMessage(
        QString("Received %1 bytes")
            .arg(receivedData.size())
        );

    // Get the individual receive buffer associated with this client.
    //
    // The reference is important: all new bytes are appended directly
    // to the buffer stored inside m_receiveBuffers.
    QByteArray &buffer = m_receiveBuffers[client];

    // Append newly received TCP data to data that may have remained
    // from a previous readyRead() call.
    buffer.append(receivedData);

    // One JSON message in our protocol is terminated by '\n'.
    //
    // There may already be several complete messages in the buffer,
    // therefore continue processing until no complete line remains.
    while (true)
    {
        const qsizetype newlinePosition = buffer.indexOf('\n');

        // No '\n' means that the last JSON message is still incomplete.
        //
        // Leave it in the buffer. The next readyRead() call will append
        // more bytes and processing will continue from there.
        if (newlinePosition < 0)
        {
            break;
        }

        // Extract exactly one complete message.
        QByteArray message = buffer.left(newlinePosition);

        // Remove the processed message together with its '\n' delimiter.
        buffer.remove(
            0,
            newlinePosition + 1
            );

        // Ignore empty lines.
        message = message.trimmed();

        if (message.isEmpty())
        {
            continue;
        }

        // Parse one complete JSON message.
        QJsonParseError parseError;

        const QJsonDocument document =
            QJsonDocument::fromJson(
                message,
                &parseError
                );

        if (parseError.error != QJsonParseError::NoError)
        {
            emit logMessage(
                QString("JSON parse error: %1")
                    .arg(parseError.errorString())
                );

            // This message is invalid, but it must not prevent
            // processing of the following messages already present
            // in the TCP buffer.
            continue;
        }

        if (!document.isObject())
        {
            emit logMessage(
                "Received JSON message is not an object"
                );

            continue;
        }

        // Pass one valid and complete JSON object for command processing.
        processJson(document.object());
    }
}


// ------------------------------------------------------------
// ОБРОБКА JSON
// ------------------------------------------------------------
void LocalServer::processJson(const QJsonObject &json)
{
    QString type = json.value("type").toString();

    if (type == "write")
    {
        QJsonObject data = json.value("data").toObject();

        // Process ZB data.
        QJsonArray zbArray = data.value("zb").toArray();

        for (const QJsonValue &val : zbArray)
        {
            QJsonObject zb = val.toObject();
            int number = zb.value("number").toInt();

            if (number >= 1 && number <= 9)
            {
                emit writeZB(number, zb);
            }
        }

        // Process CZ data.
        QJsonArray czArray = data.value("cz").toArray();

        for (const QJsonValue &val : czArray)
        {
            QJsonObject cz = val.toObject();
            int number = cz.value("number").toInt();

            if (number >= 1 && number <= 3)
            {
                emit writeCZ(number, cz);
            }
        }

        // Process device replacement.
        if (data.contains("replacement"))
        {
            QJsonObject replacement =
                data.value("replacement").toObject();

            int zbNumber =
                replacement.value("zb_number").toInt();

            int newSN =
                replacement.value("new_sn").toInt();

            if (zbNumber >= 1 &&
                zbNumber <= 9 &&
                newSN > 0)
            {
                emit replaceDevice(
                    zbNumber,
                    newSN
                    );
            }
        }

        // Request current tank states after writing data.
        emit requestFullness();
    }
    else if (type == "read")
    {
        // Request current tank states.
        emit requestFullness();
    }
    else
    {
        emit logMessage(
            QString("Unknown command type: %1")
                .arg(type)
            );
    }
}
