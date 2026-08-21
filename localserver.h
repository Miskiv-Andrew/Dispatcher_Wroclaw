#ifndef LOCALSERVER_H
#define LOCALSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>

/**
 * @brief Клас локального TCP-сервера для прийому JSON-команд від Python
 *
 * Відповідає за:
 * - прийом підключень від Python-клієнтів
 * - читання JSON-повідомлень
 * - парсинг команд (write, read, replacement)
 * - виклик відповідних методів ModBusClient
 * - відправку відповідей (read)
 */
class LocalServer : public QObject
{
    Q_OBJECT

public:
    explicit LocalServer(QObject *parent = nullptr);
    ~LocalServer();

    /**
     * @brief Запускає TCP-сервер на вказаному порту
     * @param port Порт для прослуховування
     * @return true, якщо сервер успішно запущено
     */
    bool start(quint16 port = 12345);

    /**
     * @brief Зупиняє сервер та закриває всі з'єднання
     */
    void stop();

    /**
     * @brief Відправляє JSON-повідомлення всім підключеним клієнтам
     * @param json JSON-об'єкт для відправки
     */
    void broadcast(const QJsonObject &json);

    int clientsCount() const { return m_clients.size(); }

signals:
    /**
     * @brief Сигнал для запису даних цистерни в ПЛК
     * @param zbNumber Номер цистерни (1..9)
     * @param data JSON-об'єкт з даними
     */
    void writeZB(int zbNumber, const QJsonObject &data);

    /**
     * @brief Сигнал для запису даних настенного детектора в ПЛК
     * @param czNumber Номер детектора (1..3)
     * @param data JSON-об'єкт з даними
     */
    void writeCZ(int czNumber, const QJsonObject &data);

    /**
     * @brief Сигнал для заміни приладу
     * @param zbNumber Номер цистерни
     * @param newSN Новий серійний номер
     */
    void replaceDevice(int zbNumber, int newSN);

    /**
     * @brief Сигнал для запиту стану баків
     */
    void requestFullness();

    /**
     * @brief Сигнал для логування
     */
    void logMessage(const QString &msg);

    void clientConnected();
    void clientDisconnected();


private slots:
    void onNewConnection();
    void onClientDisconnected();
    void onClientReadyRead();

private:
    /**
     * @brief Обробляє отриманий JSON-пакет
     * @param json JSON-об'єкт
     */
    void processJson(const QJsonObject &json, QTcpSocket *client);

    QTcpServer *m_server;               ///< TCP-сервер
    QList<QTcpSocket*> m_clients;       ///< Список підключених клієнтів
    quint16 m_port;                     ///< Порт сервера
    bool m_running;                     ///< Стан сервера
};

#endif // LOCALSERVER_H
