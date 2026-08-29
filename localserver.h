#ifndef LOCALSERVER_H
#define LOCALSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>

/**
 * @brief Local TCP server for receiving JSON commands from Python.
 *
 * Responsibilities:
 * - accepts connections from Python clients;
 * - receives JSON messages;
 * - buffers TCP data until a complete JSON message is received;
 * - parses commands (write, read, replacement);
 * - emits signals for further processing by ApplicationController;
 * - sends JSON responses back to connected Python clients.
 *
 * TCP message format:
 *
 *     JSON + '\n'
 *
 * The newline character is used as the message delimiter.
 */
class LocalServer : public QObject
{
    Q_OBJECT

public:
    explicit LocalServer(QObject *parent = nullptr);
    ~LocalServer();

    /**
     * @brief Starts the local TCP server.
     * @param port TCP port to listen on.
     * @return true if the server was started successfully.
     */
    bool start(quint16 port = 12345);

    /**
     * @brief Stops the server and closes all client connections.
     */
    void stop();

    /**
     * @brief Sends a JSON message to all connected clients.
     * @param json JSON object to send.
     */
    void broadcast(const QJsonObject &json);

    /**
     * @brief Returns the number of currently connected clients.
     */
    int clientsCount() const
    {
        return m_clients.size();
    }

signals:

    /**
     * @brief Requests writing ZB data to PLC.
     * @param zbNumber ZB number (1..9).
     * @param data JSON object containing ZB data.
     */
    void writeZB(int zbNumber, const QJsonObject &data);

    /**
     * @brief Requests writing CZ data to PLC.
     * @param czNumber CZ number (1..3).
     * @param data JSON object containing CZ data.
     */
    void writeCZ(int czNumber, const QJsonObject &data);

    /**
     * @brief Requests device replacement.
     * @param zbNumber ZB number.
     * @param newSN New serial number.
     */
    void replaceDevice(int zbNumber, int newSN);

    /**
     * @brief Requests current tank fullness states.
     */
    void requestFullness();

    /**
     * @brief Sends LocalServer log messages.
     */
    void logMessage(const QString &msg);

    /**
     * @brief Emitted when a Python client connects.
     */
    void clientConnected();

    /**
     * @brief Emitted when a Python client disconnects.
     */
    void clientDisconnected();

private slots:

    /**
     * @brief Handles new TCP connections.
     */
    void onNewConnection();

    /**
     * @brief Handles client disconnection.
     */
    void onClientDisconnected();

    /**
     * @brief Receives and buffers TCP data from a client.
     */
    void onClientReadyRead();

private:

    /**
     * @brief Processes one complete JSON message.
     * @param json Parsed JSON object.
     */
    void processJson(const QJsonObject &json);
    QTcpServer *m_server;

    /**
     * @brief List of currently connected Python clients.
     */
    QList<QTcpSocket*> m_clients;

    /**
     * @brief Individual receive buffer for each connected client.
     *
     * TCP is a byte stream and does not preserve message boundaries.
     * Therefore one readyRead() call may contain:
     *
     * - only part of one JSON message;
     * - exactly one JSON message;
     * - several JSON messages.
     *
     * Each client therefore requires its own accumulation buffer.
     */
    QHash<QTcpSocket*, QByteArray> m_receiveBuffers;

    quint16 m_port;
    bool m_running;
};

#endif // LOCALSERVER_H
