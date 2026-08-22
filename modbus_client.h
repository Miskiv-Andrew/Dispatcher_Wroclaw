#ifndef MODBUS_CLIENT_H
#define MODBUS_CLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

/**
 * @brief Клас для роботи з ModBus TCP клієнтом (синхронний режим).
 */
class ModBusClient : public QObject
{
    Q_OBJECT

public:
    explicit ModBusClient(QObject *parent = nullptr);
    ~ModBusClient();

    // Налаштування
    void setConnectionParams(const QString &ipAddress, quint16 port);
    void setTimeout(int ms);
    void setWordOrder(bool lswFirst);
    void setUnitId(quint8 unitId);
    void setAddressOffset(int offset);

    // Підключення
    bool connectToPLC();
    void disconnectFromPLC();
    bool isConnected() const;
    bool isConnecting() const;

    // 16-бітні Holding Registers
    bool readHoldingRegister(quint16 address, quint16 &result);
    bool writeHoldingRegister(quint16 address, quint16 value);

    // 32-бітні Holding Registers (окремі методи для int32 та float)
    bool readHoldingRegister32Int(quint16 address, quint32 &result, bool lswFirst = true);
    bool readHoldingRegister32Float(quint16 address, float &result, bool lswFirst = true);
    bool writeHoldingRegister32Int(quint16 address, quint32 value, bool lswFirst = true);
    bool writeHoldingRegister32Float(quint16 address, float value, bool lswFirst = true);

    // Coils
    bool readCoil(quint16 address, bool &result);
    bool writeCoil(quint16 address, bool value);

    // Discrete Inputs
    bool readDiscreteInput(quint16 address, bool &result);

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &errorText);
    void logMessage(const QString &message);

// private slots:
//     void onSocketConnected();
//     void onSocketDisconnected();
//     void onSocketError(QAbstractSocket::SocketError socketError);

private slots:
    // Вызывается QTcpSocket после успешного подключения к ПЛК.
    void onSocketConnected();

    // Вызывается QTcpSocket после потери соединения.
    void onSocketDisconnected();

    // Обработка сетевых ошибок QTcpSocket.
    void onSocketError(QAbstractSocket::SocketError socketError);

    // Периодическая попытка восстановить соединение с ПЛК.
    // Слот вызывается таймером reconnect.
    void tryReconnect();

private:
    // Методи формування запитів (PDU)
    QByteArray buildReadHoldingRegisters(quint16 address, quint16 count);
    QByteArray buildWriteSingleRegister(quint16 address, quint16 value);
    QByteArray buildWriteMultipleRegisters(quint16 address, quint16 count, const QByteArray &data);
    QByteArray buildReadCoils(quint16 address, quint16 count);
    QByteArray buildWriteSingleCoil(quint16 address, bool value);
    QByteArray buildReadDiscreteInputs(quint16 address, quint16 count);

    // Відправка запиту та отримання відповіді
    bool sendRequestAndWaitForResponse(const QByteArray &request, QByteArray &response);
    bool checkException(const QByteArray &response);

    // Допоміжні методи
    quint16 adjustAddress(quint16 address) const;

    // Запускает таймер автоматического восстановления соединения.
    //
    // Метод сам проверяет:
    //   - разрешён ли автоматический reconnect;
    //   - не запущен ли таймер уже;
    //   - действительно ли сокет отключён.
    //
    // Благодаря этому несколько одинаковых ошибок сокета
    // не создадут несколько параллельных попыток подключения.
    void startReconnectTimer();

    QTcpSocket *m_socket;
    QString m_ipAddress;
    quint16 m_port;
    quint8 m_unitId;
    int m_timeoutMs;
    bool m_connected;
    quint16 m_transactionId;
    bool m_lswFirst;
    int m_addressOffset;   // зсув адреси (за замовчуванням 0)

    // ------------------------------------------------------------------------
    // Таймер автоматического восстановления соединения с ПЛК.
    //
    // Таймер принадлежит ModBusClient и работает в том же потоке,
    // что и сам ModBusClient/QTcpSocket.
    // ------------------------------------------------------------------------
    QTimer *m_reconnectTimer;

    // Интервал между попытками повторного подключения.
    // Пока используем фиксированные 3 секунды.
    int m_reconnectIntervalMs;

    // ------------------------------------------------------------------------
    // Флаг намеренного отключения.
    //
    // false:
    //     потеря соединения считается аварийной и разрешён reconnect.
    //
    // true:
    //     disconnectFromPLC() был вызван намеренно, например при завершении
    //     приложения, поэтому автоматически подключаться снова нельзя.
    // ------------------------------------------------------------------------
    bool m_manualDisconnect;


    // ------------------------------------------------------------------------
    // Защита от одновременного выполнения нескольких Modbus-запросов.
    //
    // false:
    //     ModBusClient готов принять новый запрос.
    //
    // true:
    //     один Modbus-запрос уже выполняется и ожидает ответ от ПЛК.
    //
    // ModBusClient сейчас работает в одном Qt-потоке, поэтому mutex
    // для этой задачи не требуется.
    // ------------------------------------------------------------------------
    bool m_requestInProgress;

};

#endif // MODBUS_CLIENT_H
