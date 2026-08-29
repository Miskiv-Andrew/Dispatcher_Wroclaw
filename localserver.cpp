#include "localserver.h"
#include <QDebug>
#include <QJsonParseError>
#include <cmath>

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
    // ------------------------------------------------------------
    // 1. Validate command type.
    // ------------------------------------------------------------
    const QJsonValue typeValue = json.value("type");

    if (!typeValue.isString())
    {
        emit logMessage(
            "Invalid JSON command: missing or invalid 'type'"
            );

        return;
    }

    const QString type = typeValue.toString();

    if (type.isEmpty())
    {
        emit logMessage(
            "Invalid JSON command: 'type' is empty"
            );

        return;
    }

    // ------------------------------------------------------------
    // 2. WRITE command.
    // ------------------------------------------------------------
    if (type == "write")
    {
        const QJsonValue dataValue = json.value("data");

        if (!dataValue.isObject())
        {
            emit logMessage(
                "Invalid write command: missing or invalid 'data' object"
                );

            return;
        }

        const QJsonObject data = dataValue.toObject();

        bool containsSupportedData = false;

        // --------------------------------------------------------
        // 2.1. Process ZB data.
        // --------------------------------------------------------
        if (data.contains("zb"))
        {
            containsSupportedData = true;

            const QJsonValue zbValue = data.value("zb");

            if (!zbValue.isArray())
            {
                emit logMessage(
                    "Invalid write command: 'zb' must be an array"
                    );
            }
            else
            {
                const QJsonArray zbArray = zbValue.toArray();

                for (const QJsonValue &value : zbArray)
                {
                    if (!value.isObject())
                    {
                        emit logMessage(
                            "Invalid ZB entry: expected JSON object"
                            );

                        continue;
                    }

                    const QJsonObject zb = value.toObject();

                    const QJsonValue numberValue =
                        zb.value("number");

                    if (!numberValue.isDouble())
                    {
                        emit logMessage(
                            "Invalid ZB entry: missing or invalid 'number'"
                            );

                        continue;
                    }

                    const double rawNumber =
                        numberValue.toDouble();

                    // A tank number must be an integer.
                    //
                    // Values such as 2.5 must not silently become 2.
                    if (std::floor(rawNumber) != rawNumber)
                    {
                        emit logMessage(
                            "Invalid ZB entry: 'number' must be an integer"
                            );

                        continue;
                    }

                    const int number =
                        static_cast<int>(rawNumber);

                    if (number < 1 || number > 9)
                    {
                        emit logMessage(
                            QString(
                                "Invalid ZB number: %1, expected range 1..9"
                                )
                                .arg(number)
                            );

                        continue;
                    }

                    emit writeZB(
                        number,
                        zb
                        );
                }
            }
        }

        // --------------------------------------------------------
        // 2.2. Process CZ data.
        // --------------------------------------------------------
        if (data.contains("cz"))
        {
            containsSupportedData = true;

            const QJsonValue czValue = data.value("cz");

            if (!czValue.isArray())
            {
                emit logMessage(
                    "Invalid write command: 'cz' must be an array"
                    );
            }
            else
            {
                const QJsonArray czArray = czValue.toArray();

                for (const QJsonValue &value : czArray)
                {
                    if (!value.isObject())
                    {
                        emit logMessage(
                            "Invalid CZ entry: expected JSON object"
                            );

                        continue;
                    }

                    const QJsonObject cz = value.toObject();

                    const QJsonValue numberValue =
                        cz.value("number");

                    if (!numberValue.isDouble())
                    {
                        emit logMessage(
                            "Invalid CZ entry: missing or invalid 'number'"
                            );

                        continue;
                    }

                    const double rawNumber =
                        numberValue.toDouble();

                    // A detector number must be an integer.
                    if (std::floor(rawNumber) != rawNumber)
                    {
                        emit logMessage(
                            "Invalid CZ entry: 'number' must be an integer"
                            );

                        continue;
                    }

                    const int number =
                        static_cast<int>(rawNumber);

                    if (number < 1 || number > 3)
                    {
                        emit logMessage(
                            QString(
                                "Invalid CZ number: %1, expected range 1..3"
                                )
                                .arg(number)
                            );

                        continue;
                    }

                    emit writeCZ(
                        number,
                        cz
                        );
                }
            }
        }

        // --------------------------------------------------------
        // 2.3. Process device replacement.
        // --------------------------------------------------------
        if (data.contains("replacement"))
        {
            containsSupportedData = true;

            const QJsonValue replacementValue =
                data.value("replacement");

            if (!replacementValue.isObject())
            {
                emit logMessage(
                    "Invalid write command: 'replacement' must be an object"
                    );
            }
            else
            {
                const QJsonObject replacement =
                    replacementValue.toObject();

                const QJsonValue zbNumberValue =
                    replacement.value("zb_number");

                const QJsonValue newSnValue =
                    replacement.value("new_sn");

                if (!zbNumberValue.isDouble())
                {
                    emit logMessage(
                        "Invalid replacement: missing or invalid 'zb_number'"
                        );
                }
                else if (!newSnValue.isDouble())
                {
                    emit logMessage(
                        "Invalid replacement: missing or invalid 'new_sn'"
                        );
                }
                else
                {
                    const double rawZbNumber =
                        zbNumberValue.toDouble();

                    const double rawNewSn =
                        newSnValue.toDouble();

                    if (std::floor(rawZbNumber) != rawZbNumber)
                    {
                        emit logMessage(
                            "Invalid replacement: 'zb_number' must be an integer"
                            );
                    }
                    else if (std::floor(rawNewSn) != rawNewSn)
                    {
                        emit logMessage(
                            "Invalid replacement: 'new_sn' must be an integer"
                            );
                    }
                    else
                    {
                        const int zbNumber =
                            static_cast<int>(rawZbNumber);

                        const int newSN =
                            static_cast<int>(rawNewSn);

                        if (zbNumber < 1 || zbNumber > 9)
                        {
                            emit logMessage(
                                QString(
                                    "Invalid replacement ZB number: %1, expected range 1..9"
                                    )
                                    .arg(zbNumber)
                                );
                        }
                        else if (newSN <= 0)
                        {
                            emit logMessage(
                                QString(
                                    "Invalid replacement serial number: %1"
                                    )
                                    .arg(newSN)
                                );
                        }
                        else
                        {
                            emit replaceDevice(
                                zbNumber,
                                newSN
                                );
                        }
                    }
                }
            }
        }

        // --------------------------------------------------------
        // 2.4. Reject an empty/unsupported write command.
        // --------------------------------------------------------
        if (!containsSupportedData)
        {
            emit logMessage(
                "Invalid write command: no supported data sections found"
                );

            return;
        }

        // Request current tank states after processing a write command.
        emit requestFullness();

        return;
    }

    // ------------------------------------------------------------
    // 3. READ command.
    // ------------------------------------------------------------
    if (type == "read")
    {
        emit requestFullness();

        return;
    }

    // ------------------------------------------------------------
    // 4. Unknown command.
    // ------------------------------------------------------------
    emit logMessage(
        QString("Unknown command type: %1")
            .arg(type)
        );
}
