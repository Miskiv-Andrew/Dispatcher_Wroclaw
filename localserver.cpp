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
        emit logMessage("Сервер вже запущено");
        return true;
    }

    m_port = port;
    if (!m_server->listen(QHostAddress::LocalHost, m_port)) {
        emit logMessage(QString("Не вдалося запустити сервер на порту %1: %2")
                            .arg(m_port).arg(m_server->errorString()));
        return false;
    }

    m_running = true;
    emit logMessage(QString("Сервер запущено на порту %1").arg(m_port));
    return true;
}

void LocalServer::stop()
{
    if (!m_running) return;

    // Закриваємо всі клієнтські сокети
    for (QTcpSocket *client : m_clients) {
        client->disconnectFromHost();
        client->deleteLater();
    }
    m_clients.clear();

    m_server->close();
    m_running = false;
    emit logMessage("Сервер зупинено");
}

void LocalServer::broadcast(const QJsonObject &json)
{
    if (m_clients.isEmpty()) {
        emit logMessage("Немає підключених клієнтів для відправки");
        return;
    }

    QJsonDocument doc(json);
    QByteArray data = doc.toJson(QJsonDocument::Compact) + "\n";

    for (QTcpSocket *client : m_clients) {
        if (client->state() == QTcpSocket::ConnectedState) {
            client->write(data);
        }
    }

    emit logMessage("Відправлено broadcast всім клієнтам");
}

// ------------------------------------------------------------
// СЛОТИ ДЛЯ РОБОТИ З КЛІЄНТАМИ
// ------------------------------------------------------------

void LocalServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *client = m_server->nextPendingConnection();
        m_clients.append(client);

        connect(client, &QTcpSocket::disconnected, this, &LocalServer::onClientDisconnected);
        connect(client, &QTcpSocket::readyRead, this, &LocalServer::onClientReadyRead);

        emit logMessage(QString("Підключено клієнт: %1:%2")
                            .arg(client->peerAddress().toString())
                            .arg(client->peerPort()));

        emit clientConnected();
    }
}

void LocalServer::onClientDisconnected()
{
    QTcpSocket *client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;

    // Удаляем сокет из списка
    m_clients.removeAll(client);

    // Закрываем и удаляем
    client->disconnectFromHost();
    client->deleteLater();

    emit logMessage("Клієнт відключено");

    emit clientDisconnected();
}


void LocalServer::onClientReadyRead()
{
    QTcpSocket *client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;

    QByteArray data = client->readAll();
    emit logMessage(QString("Отримано %1 байт").arg(data.size()));

    // Парсимо JSON
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        emit logMessage(QString("Помилка парсингу JSON: %1").arg(parseError.errorString()));
        return;
    }

    if (!doc.isObject()) {
        emit logMessage("Отримано не JSON-об'єкт");
        return;
    }

    processJson(doc.object(), client);
}

// ------------------------------------------------------------
// ОБРОБКА JSON
// ------------------------------------------------------------

void LocalServer::processJson(const QJsonObject &json, QTcpSocket *client)
{
    QString type = json.value("type").toString();

    if (type == "write") {
        QJsonObject data = json.value("data").toObject();

        // Обробка цистерн
        QJsonArray zbArray = data.value("zb").toArray();
        for (const QJsonValue &val : zbArray) {
            QJsonObject zb = val.toObject();
            int number = zb.value("number").toInt();
            if (number >= 1 && number <= 9) {
                emit writeZB(number, zb);
            }
        }

        // Обробка настінних детекторів
        QJsonArray czArray = data.value("cz").toArray();
        for (const QJsonValue &val : czArray) {
            QJsonObject cz = val.toObject();
            int number = cz.value("number").toInt();
            if (number >= 1 && number <= 3) {
                emit writeCZ(number, cz);
            }
        }

        // Обробка заміни приладу
        if (data.contains("replacement")) {
            QJsonObject replacement = data.value("replacement").toObject();
            int zbNumber = replacement.value("zb_number").toInt();
            int newSN = replacement.value("new_sn").toInt();
            if (zbNumber >= 1 && zbNumber <= 9 && newSN > 0) {
                emit replaceDevice(zbNumber, newSN);
            }
        }

        // Після запису запитуємо стан баків
        emit requestFullness();

    } else if (type == "read") {
        // Запит стану баків
        emit requestFullness();

    } else {
        emit logMessage(QString("Невідомий тип команди: %1").arg(type));
    }
}
