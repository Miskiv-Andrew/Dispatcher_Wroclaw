/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "modbus_client.h"
#include <QDataStream>
// #include <QThread>
#include <QDebug>
#include <QElapsedTimer>
#include <cstring>


ModBusClient::ModBusClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_port(502)
    , m_unitId(1)
    , m_timeoutMs(1000)
    , m_connected(false)
    , m_transactionId(0)
    , m_lswFirst(true)
    , m_addressOffset(-1)
    , m_reconnectTimer(new QTimer(this))
    , m_reconnectIntervalMs(3000)    // Повторяем попытку подключения раз в 3 секунды.
    , m_manualDisconnect(false)      // При обычном запуске автоматическое подключение разрешено.
    , m_requestInProgress(false)
{
    connect(m_socket, &QTcpSocket::connected, this, &ModBusClient::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &ModBusClient::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &ModBusClient::onSocketError);

    // ------------------------------------------------------------------------
    // Настраиваем таймер автоматического reconnect.
    //
    // Он НЕ запускается постоянно.
    // Таймер будет запущен только после потери соединения
    // или неудачной попытки подключения.
    // ------------------------------------------------------------------------
    m_reconnectTimer->setInterval(m_reconnectIntervalMs);

    // Обычный повторяющийся таймер.
    // Он будет каждые 3 секунды пытаться восстановить соединение,
    // пока подключение не станет успешным.
    m_reconnectTimer->setSingleShot(false);

    connect(
        m_reconnectTimer,
        &QTimer::timeout,
        this,
        &ModBusClient::tryReconnect
    );
}

ModBusClient::~ModBusClient()
{
    // QTcpSocket and QTimer are QObject children of ModBusClient.
    //
    // Controlled shutdown is performed earlier by
    // ApplicationController::shutdown().
    //
    // Calling disconnectFromPLC() again here would only produce duplicate
    // shutdown messages and unnecessary socket operations during QObject
    // destruction.
}

// ------------------------------------------------------------
// НАЛАШТУВАННЯ
// ------------------------------------------------------------
void ModBusClient::setConnectionParams(const QString &ipAddress, quint16 port)
{
    m_ipAddress = ipAddress;
    m_port = port;
    emit logMessage(QString("Parameters set: IP=%1, port=%2").arg(ipAddress).arg(port));
}

void ModBusClient::setTimeout(int ms)
{
    if (ms > 0) m_timeoutMs = ms;
}

void ModBusClient::setWordOrder(bool lswFirst)
{
    m_lswFirst = lswFirst;
}

void ModBusClient::setUnitId(quint8 unitId)
{
    m_unitId = unitId;
}

void ModBusClient::setAddressOffset(int offset)
{
    m_addressOffset = offset;
}

// ------------------------------------------------------------
// ПІДКЛЮЧЕННЯ
// ------------------------------------------------------------
bool ModBusClient::connectToPLC()
{
    // ------------------------------------------------------------------------
    // Публичный вызов connectToPLC() означает, что соединение с ПЛК
    // требуется. Поэтому разрешаем автоматическое восстановление связи.
    // ------------------------------------------------------------------------
    m_manualDisconnect = false;


    // ------------------------------------------------------------------------
    // Если соединение уже установлено, ничего делать не нужно.
    // ------------------------------------------------------------------------
    if (isConnected()) {
        return true;
    }


    // ------------------------------------------------------------------------
    // Если предыдущая попытка подключения ещё выполняется,
    // не запускаем вторую connectToHost().
    // ------------------------------------------------------------------------
    if (m_socket->state() == QAbstractSocket::ConnectingState) {
        emit logMessage("Already connecting...");
        return false;
    }


    // ------------------------------------------------------------------------
    // Без IP-адреса подключение невозможно.
    // ------------------------------------------------------------------------
    if (m_ipAddress.isEmpty()) {
        emit errorOccurred("IP address is empty");
        return false;
    }


    // ------------------------------------------------------------------------
    // Если сокет по какой-либо причине остался
    // в промежуточном состоянии, сбрасываем его.
    // ------------------------------------------------------------------------
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }


    // До получения сигнала connected() считаем связь отсутствующей.
    m_connected = false;

    emit logMessage("Attempting to connect to PLC...");


    // ------------------------------------------------------------------------
    // Подключение выполняется асинхронно.
    //
    // Результат придёт через:
    //   onSocketConnected()
    //   onSocketError()
    //   onSocketDisconnected()
    // ------------------------------------------------------------------------
    m_socket->connectToHost(m_ipAddress, m_port);

    return true;
}


void ModBusClient::disconnectFromPLC()
{
    // ========================================================================
    // 1. MARK THIS AS AN INTENTIONAL DISCONNECT
    //
    // Reconnect must not start after this point.
    // ========================================================================

    m_manualDisconnect = true;


    // ========================================================================
    // 2. STOP RECONNECT TIMER
    // ========================================================================

    if (m_reconnectTimer &&
        m_reconnectTimer->isActive())
    {
        m_reconnectTimer->stop();
    }


    // ========================================================================
    // 3. CLEAR REQUEST STATE
    //
    // During controlled application shutdown no Modbus request is allowed
    // to remain logically active.
    // ========================================================================

    m_requestInProgress = false;


    // ========================================================================
    // 4. CLOSE SOCKET
    // ========================================================================

    if (m_socket &&
        m_socket->state() != QAbstractSocket::UnconnectedState)
    {
        m_socket->disconnectFromHost();


        // If Qt cannot complete graceful disconnect immediately, abort the
        // socket so application shutdown cannot remain dependent on network
        // state.
        if (m_socket->state() != QAbstractSocket::UnconnectedState)
        {
            m_socket->abort();
        }
    }


    // ========================================================================
    // 5. RESET LOCAL STATE
    // ========================================================================

    m_connected = false;


    emit logMessage(
        "Disconnected from PLC"
        );
}


bool ModBusClient::isConnected() const
{
    return m_connected && (m_socket->state() == QAbstractSocket::ConnectedState);
}

bool ModBusClient::isConnecting() const
{
    return (m_socket->state() == QAbstractSocket::ConnectingState);
}

// ------------------------------------------------------------
// СЛОТИ СОКЕТА
// ------------------------------------------------------------
void ModBusClient::onSocketConnected()
{
    // ------------------------------------------------------------------------
    // Соединение успешно восстановлено.
    // ------------------------------------------------------------------------
    m_connected = true;


    // ------------------------------------------------------------------------
    // Если работал таймер reconnect — он больше не нужен.
    // ------------------------------------------------------------------------
    if (m_reconnectTimer->isActive()) {
        m_reconnectTimer->stop();

        emit logMessage("PLC reconnect timer stopped");
    }


    emit logMessage("Connection to PLC established");

    // Сообщаем остальному приложению об успешном подключении.
    // В частности, это обновит левую половину tray-индикатора.
    emit connected();
}

void ModBusClient::onSocketDisconnected()
{
    // ------------------------------------------------------------------------
    // Запоминаем предыдущее состояние.
    //
    // Это позволяет не посылать несколько одинаковых disconnected()
    // при последовательности нескольких сетевых ошибок.
    // ------------------------------------------------------------------------
    const bool wasConnected = m_connected;

    m_connected = false;


    // ------------------------------------------------------------------------
    // Если соединение действительно было установлено,
    // сообщаем приложению о его потере.
    // ------------------------------------------------------------------------
    if (wasConnected) {
        emit logMessage("Connection to PLC lost");
        emit disconnected();
    }


    // ------------------------------------------------------------------------
    // Если отключение произошло НЕ по нашей команде,
    // запускаем автоматическое восстановление.
    // ------------------------------------------------------------------------
    if (!m_manualDisconnect) {
        startReconnectTimer();
    }
}





void ModBusClient::onSocketError(
    QAbstractSocket::SocketError socketError
    )
{
    Q_UNUSED(socketError);


    const QString errorText =
        QString(
            "Socket error: %1"
            ).arg(
                m_socket->errorString()
                );


    // ------------------------------------------------------------------------
    // Do not call handleCommunicationFailure() here unconditionally.
    //
    // During a synchronous Modbus operation the request method itself will
    // detect the failed socket operation and perform controlled recovery.
    //
    // Here we primarily report the asynchronous socket error.
    // ------------------------------------------------------------------------
    emit errorOccurred(
        errorText
        );


    emit logMessage(
        errorText
        );


    // ------------------------------------------------------------------------
    // If Qt already reports the socket as fully disconnected, update local
    // state and start reconnect immediately.
    // ------------------------------------------------------------------------
    if (m_socket->state() ==
        QAbstractSocket::UnconnectedState)
    {
        const bool wasConnected =
            m_connected;


        m_connected = false;


        if (wasConnected)
        {
            emit disconnected();
        }


        if (!m_manualDisconnect)
        {
            startReconnectTimer();
        }
    }
}














void ModBusClient::startReconnectTimer()
{
    // ------------------------------------------------------------------------
    // Если отключение было намеренным,
    // никаких попыток восстановления делать нельзя.
    // ------------------------------------------------------------------------
    if (m_manualDisconnect) {
        return;
    }


    // ------------------------------------------------------------------------
    // Если соединение уже восстановилось,
    // таймер тоже не нужен.
    // ------------------------------------------------------------------------
    if (isConnected()) {
        return;
    }


    // ------------------------------------------------------------------------
    // Не запускаем второй экземпляр того же таймера.
    //
    // Например, при обрыве могут последовательно прийти:
    //   errorOccurred()
    //   disconnected()
    //
    // Оба обработчика вызывают этот метод, но таймер
    // должен быть только один.
    // ------------------------------------------------------------------------
    if (m_reconnectTimer->isActive()) {
        return;
    }


    emit logMessage(
        QString("PLC reconnect will be attempted every %1 ms")
            .arg(m_reconnectIntervalMs)
        );


    // Запускаем периодические попытки подключения.
    m_reconnectTimer->start();
}


void ModBusClient::tryReconnect()
{
    // ------------------------------------------------------------------------
    // Если за время ожидания приложение запросило обычное отключение,
    // прекращаем reconnect.
    // ------------------------------------------------------------------------
    if (m_manualDisconnect) {
        m_reconnectTimer->stop();
        return;
    }


    // ------------------------------------------------------------------------
    // Если соединение уже восстановлено,
    // дальнейшие попытки не нужны.
    // ------------------------------------------------------------------------
    if (isConnected()) {
        m_reconnectTimer->stop();
        return;
    }


    // ------------------------------------------------------------------------
    // Если предыдущая попытка подключения ещё выполняется,
    // не запускаем параллельную connectToHost().
    // ------------------------------------------------------------------------
    if (m_socket->state() == QAbstractSocket::ConnectingState) {
        return;
    }


    // ------------------------------------------------------------------------
    // Повторную попытку выполняем только из UnconnectedState.
    // ------------------------------------------------------------------------
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        return;
    }


    emit logMessage("Trying to reconnect to PLC...");


    // ------------------------------------------------------------------------
    // Запускаем новую попытку подключения напрямую.
    //
    // Здесь специально используем connectToHost(), а не connectToPLC(),
    // чтобы внутренний периодический reconnect не изменял управляющие
    // флаги публичного интерфейса.
    // ------------------------------------------------------------------------
    m_socket->connectToHost(
        m_ipAddress,
        m_port
        );
}




// ------------------------------------------------------------
// ПОБУДОВА ЗАПИТІВ (PDU)
// ------------------------------------------------------------
QByteArray ModBusClient::buildReadHoldingRegisters(quint16 address, quint16 count)
{
    QByteArray pdu;
    QDataStream stream(&pdu, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint8(0x03);
    stream << address;
    stream << count;
    return pdu;
}

QByteArray ModBusClient::buildWriteSingleRegister(quint16 address, quint16 value)
{
    QByteArray pdu;
    QDataStream stream(&pdu, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint8(0x06);
    stream << address;
    stream << value;
    return pdu;
}

QByteArray ModBusClient::buildWriteMultipleRegisters(quint16 address, quint16 count, const QByteArray &data)
{
    QByteArray pdu;
    QDataStream stream(&pdu, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint8(0x10);
    stream << address;
    stream << count;
    stream << quint8(data.size());
    stream.writeRawData(data.constData(), data.size());
    return pdu;
}

QByteArray ModBusClient::buildReadCoils(quint16 address, quint16 count)
{
    QByteArray pdu;
    QDataStream stream(&pdu, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint8(0x01);
    stream << address;
    stream << count;
    return pdu;
}

QByteArray ModBusClient::buildWriteSingleCoil(quint16 address, bool value)
{
    QByteArray pdu;
    QDataStream stream(&pdu, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint8(0x05);
    stream << address;
    stream << quint16(value ? 0xFF00 : 0x0000);
    return pdu;
}

QByteArray ModBusClient::buildReadDiscreteInputs(quint16 address, quint16 count)
{
    QByteArray pdu;
    QDataStream stream(&pdu, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint8(0x02);
    stream << address;
    stream << count;
    return pdu;
}




void ModBusClient::clearStaleInputBuffer()
{
    // ------------------------------------------------------------------------
    // In the current synchronous model there must be no unread response
    // before sending the next request.
    //
    // Such bytes can remain after:
    //   - a previous timeout;
    //   - a delayed PLC response;
    //   - an interrupted transaction.
    // ------------------------------------------------------------------------
    if (!m_socket)
    {
        return;
    }


    const qint64 available =
        m_socket->bytesAvailable();


    if (available <= 0)
    {
        return;
    }


    const QByteArray staleData =
        m_socket->readAll();


    emit logMessage(
        QString(
            "Discarded %1 stale Modbus response bytes"
            ).arg(
                staleData.size()
                )
        );
}



void ModBusClient::handleCommunicationFailure(
    const QString &errorText
    )
{
    // ========================================================================
    // 1. REPORT FAILURE
    // ========================================================================

    emit errorOccurred(
        errorText
        );


    emit logMessage(
        errorText
        );


    // ========================================================================
    // 2. REMEMBER PREVIOUS CONNECTION STATE
    // ========================================================================

    const bool wasConnected =
        m_connected;


    m_connected = false;


    // ========================================================================
    // 3. RESET SOCKET
    //
    // After a timeout or incomplete Modbus frame we cannot safely assume
    // that the TCP stream is still synchronized with our request/response
    // sequence.
    //
    // The safest recovery for this bridge is to reset the connection.
    // ========================================================================

    if (m_socket &&
        m_socket->state() != QAbstractSocket::UnconnectedState)
    {
        m_socket->abort();
    }


    // ========================================================================
    // 4. NOTIFY APPLICATION
    // ========================================================================

    if (wasConnected)
    {
        emit disconnected();
    }


    // ========================================================================
    // 5. START RECONNECT
    // ========================================================================

    if (!m_manualDisconnect)
    {
        startReconnectTimer();
    }
}



// ------------------------------------------------------------
// ВІДПРАВКА ТА ОТРИМАННЯ ВІДПОВІДІ
// ------------------------------------------------------------
bool ModBusClient::sendRequestAndWaitForResponse(
    const QByteArray &request,
    QByteArray &response
    )
{
    // ========================================================================
    // 1. VALIDATE REQUEST
    // ========================================================================

    response.clear();


    if (request.isEmpty())
    {
        emit errorOccurred(
            "Cannot send empty Modbus request"
            );

        return false;
    }


    const quint8 requestedFunctionCode =
        static_cast<quint8>(
            request.at(0)
            );


    // ========================================================================
    // 2. CHECK CONNECTION
    // ========================================================================

    if (!isConnected())
    {
        emit errorOccurred(
            "No connection to PLC"
            );

        return false;
    }


    // ========================================================================
    // 3. PREVENT OVERLAPPING REQUESTS
    // ========================================================================

    if (m_requestInProgress)
    {
        emit errorOccurred(
            "Modbus request rejected: another request is already in progress"
            );

        return false;
    }


    m_requestInProgress = true;


    // ========================================================================
    // 4. RAII REQUEST GUARD
    // ========================================================================

    struct RequestGuard
    {
        bool &flag;


        explicit RequestGuard(bool &value)
            : flag(value)
        {
        }


        ~RequestGuard()
        {
            flag = false;
        }
    };


    RequestGuard requestGuard(
        m_requestInProgress
        );


    // ========================================================================
    // 5. REMOVE STALE DATA FROM PREVIOUS FAILED TRANSACTION
    // ========================================================================

    clearStaleInputBuffer();


    // ========================================================================
    // 6. CREATE TRANSACTION ID
    // ========================================================================

    ++m_transactionId;


    if (m_transactionId == 0)
    {
        m_transactionId = 1;
    }


    const quint16 currentTransactionId =
        m_transactionId;


    // ========================================================================
    // 7. BUILD MODBUS TCP FRAME
    // ========================================================================

    QByteArray frame;


    QDataStream stream(
        &frame,
        QIODevice::WriteOnly
        );


    stream.setByteOrder(
        QDataStream::BigEndian
        );


    stream << currentTransactionId;

    // Protocol ID.
    stream << quint16(0);

    // Unit ID + PDU.
    stream << quint16(
        request.size() + 1
        );

    stream << m_unitId;


    frame.append(
        request
        );


    // ========================================================================
    // 8. START GLOBAL TRANSACTION TIMEOUT
    // ========================================================================

    QElapsedTimer timer;

    timer.start();


    // ========================================================================
    // 9. WRITE COMPLETE REQUEST
    // ========================================================================

    const qint64 written =
        m_socket->write(
            frame
            );


    if (written < 0)
    {
        handleCommunicationFailure(
            QString(
                "Failed to write Modbus request: %1"
                ).arg(
                    m_socket->errorString()
                    )
            );

        return false;
    }


    if (written != frame.size())
    {
        handleCommunicationFailure(
            QString(
                "Incomplete Modbus request write: %1 of %2 bytes"
                )
                .arg(written)
                .arg(frame.size())
            );

        return false;
    }


    // ========================================================================
    // 10. WAIT UNTIL REQUEST LEAVES QT WRITE BUFFER
    // ========================================================================

    while (m_socket->bytesToWrite() > 0)
    {
        const qint64 remainingTime =
            m_timeoutMs -
            timer.elapsed();


        if (remainingTime <= 0)
        {
            handleCommunicationFailure(
                "Timeout while sending Modbus request"
                );

            return false;
        }


        if (!m_socket->waitForBytesWritten(
                static_cast<int>(
                    remainingTime
                    )
                ))
        {
            handleCommunicationFailure(
                QString(
                    "Failed while sending Modbus request: %1"
                    ).arg(
                        m_socket->errorString()
                        )
                );

            return false;
        }
    }


    emit logMessage(
        QString(
            "Request sent, transaction %1, function 0x%2, %3 bytes"
            )
            .arg(
                currentTransactionId
                )
            .arg(
                requestedFunctionCode,
                2,
                16,
                QChar('0')
                )
            .arg(
                frame.size()
                )
        );


    // ========================================================================
    // 11. RECEIVE MODBUS TCP FRAMES
    //
    // A delayed response from a previous timed-out transaction can arrive
    // after the new request was sent.
    //
    // Therefore we keep reading complete frames until:
    //
    //   - the expected Transaction ID is found;
    //   - the common timeout expires.
    // ========================================================================

    constexpr qint64 mbapHeaderSize = 6;


    while (true)
    {
        // ====================================================================
        // 11.1 WAIT FOR MBAP HEADER
        // ====================================================================

        while (m_socket->bytesAvailable() <
               mbapHeaderSize)
        {
            const qint64 remainingTime =
                m_timeoutMs -
                timer.elapsed();


            if (remainingTime <= 0)
            {
                handleCommunicationFailure(
                    "Timeout waiting for Modbus response"
                    );

                return false;
            }


            if (!m_socket->waitForReadyRead(
                    static_cast<int>(
                        remainingTime
                        )
                    ))
            {
                handleCommunicationFailure(
                    QString(
                        "Failed waiting for Modbus response: %1"
                        ).arg(
                            m_socket->errorString()
                            )
                    );

                return false;
            }
        }


        // ====================================================================
        // 11.2 READ MBAP HEADER
        // ====================================================================

        const QByteArray header =
            m_socket->read(
                mbapHeaderSize
                );


        if (header.size() !=
            mbapHeaderSize)
        {
            handleCommunicationFailure(
                "Incomplete Modbus MBAP header"
                );

            return false;
        }


        QDataStream headerStream(
            header
            );


        headerStream.setByteOrder(
            QDataStream::BigEndian
            );


        quint16 responseTransactionId = 0;
        quint16 responseProtocolId = 0;
        quint16 responseLength = 0;


        headerStream
            >> responseTransactionId
            >> responseProtocolId
            >> responseLength;


        // ====================================================================
        // 11.3 VALIDATE MBAP STRUCTURE
        // ====================================================================

        if (responseProtocolId != 0)
        {
            handleCommunicationFailure(
                QString(
                    "Invalid Modbus Protocol ID: %1"
                    ).arg(
                        responseProtocolId
                        )
                );

            return false;
        }


        if (responseLength < 2 ||
            responseLength > 254)
        {
            handleCommunicationFailure(
                QString(
                    "Invalid Modbus response length: %1"
                    ).arg(
                        responseLength
                        )
                );

            return false;
        }


        const qint64 expectedBodySize =
            responseLength;


        // ====================================================================
        // 11.4 WAIT FOR COMPLETE BODY
        // ====================================================================

        while (m_socket->bytesAvailable() <
               expectedBodySize)
        {
            const qint64 remainingTime =
                m_timeoutMs -
                timer.elapsed();


            if (remainingTime <= 0)
            {
                handleCommunicationFailure(
                    "Timeout waiting for complete Modbus response"
                    );

                return false;
            }


            if (!m_socket->waitForReadyRead(
                    static_cast<int>(
                        remainingTime
                        )
                    ))
            {
                handleCommunicationFailure(
                    QString(
                        "Failed while reading Modbus response: %1"
                        ).arg(
                            m_socket->errorString()
                            )
                    );

                return false;
            }
        }


        const QByteArray raw =
            m_socket->read(
                expectedBodySize
                );


        if (raw.size() !=
            expectedBodySize)
        {
            handleCommunicationFailure(
                QString(
                    "Incomplete Modbus response: received %1 of %2 bytes"
                    )
                    .arg(
                        raw.size()
                        )
                    .arg(
                        expectedBodySize
                        )
                );

            return false;
        }


        // ====================================================================
        // 11.5 DISCARD STALE TRANSACTION
        //
        // This can happen when a previous request timed out but its response
        // arrived late.
        //
        // We discard the complete old frame instead of treating it as the
        // response to the current request.
        // ====================================================================

        if (responseTransactionId !=
            currentTransactionId)
        {
            emit logMessage(
                QString(
                    "Discarded stale Modbus response: transaction %1, expected %2"
                    )
                    .arg(
                        responseTransactionId
                        )
                    .arg(
                        currentTransactionId
                        )
                );


            // Continue waiting within the SAME overall timeout.
            continue;
        }


        // ====================================================================
        // 11.6 VALIDATE UNIT ID
        // ====================================================================

        const quint8 receivedUnitId =
            static_cast<quint8>(
                raw.at(0)
                );


        if (receivedUnitId !=
            m_unitId)
        {
            emit errorOccurred(
                QString(
                    "Unit ID mismatch: received %1, expected %2"
                    )
                    .arg(
                        receivedUnitId
                        )
                    .arg(
                        m_unitId
                        )
                );

            return false;
        }


        // ====================================================================
        // 11.7 EXTRACT PDU
        // ====================================================================

        response =
            raw.mid(1);


        if (response.isEmpty())
        {
            handleCommunicationFailure(
                "Empty Modbus PDU received"
                );

            return false;
        }


        // ====================================================================
        // 11.8 VALIDATE FUNCTION CODE
        // ====================================================================

        const quint8 receivedFunctionCode =
            static_cast<quint8>(
                response.at(0)
                );


        const quint8 exceptionFunctionCode =
            static_cast<quint8>(
                requestedFunctionCode |
                0x80
                );


        const bool normalResponse =
            receivedFunctionCode ==
            requestedFunctionCode;


        const bool exceptionResponse =
            receivedFunctionCode ==
            exceptionFunctionCode;


        if (!normalResponse &&
            !exceptionResponse)
        {
            emit errorOccurred(
                QString(
                    "Function Code mismatch: received 0x%1, expected 0x%2"
                    )
                    .arg(
                        receivedFunctionCode,
                        2,
                        16,
                        QChar('0')
                        )
                    .arg(
                        requestedFunctionCode,
                        2,
                        16,
                        QChar('0')
                        )
                );

            return false;
        }


        // ====================================================================
        // 11.9 VALIDATE EXCEPTION FRAME SIZE
        // ====================================================================

        if (exceptionResponse &&
            response.size() < 2)
        {
            handleCommunicationFailure(
                "Incomplete Modbus exception response"
                );

            return false;
        }


        // ====================================================================
        // 11.10 RESPONSE ACCEPTED
        // ====================================================================

        emit logMessage(
            QString(
                "Response received, transaction %1, function 0x%2, %3 bytes PDU"
                )
                .arg(
                    currentTransactionId
                    )
                .arg(
                    receivedFunctionCode,
                    2,
                    16,
                    QChar('0')
                    )
                .arg(
                    response.size()
                    )
            );


        return true;
    }
}





bool ModBusClient::checkException(const QByteArray &response)
{
    // ========================================================================
    // 1. BASIC VALIDATION
    //
    // A Modbus exception response must contain at least:
    //
    //   Function Code
    //   Exception Code
    // ========================================================================

    if (response.size() < 2)
    {
        return false;
    }


    // ========================================================================
    // 2. READ FUNCTION CODE
    // ========================================================================

    const quint8 functionCode =
        static_cast<quint8>(
            response.at(0)
            );


    // ------------------------------------------------------------------------
    // Normal Modbus response:
    //
    //   bit 7 = 0
    //
    // Exception response:
    //
    //   bit 7 = 1
    // ------------------------------------------------------------------------
    if ((functionCode & 0x80) == 0)
    {
        return false;
    }


    // ========================================================================
    // 3. READ EXCEPTION CODE
    // ========================================================================

    const quint8 exceptionCode =
        static_cast<quint8>(
            response.at(1)
            );


    // ========================================================================
    // 4. CONVERT EXCEPTION CODE TO HUMAN-READABLE TEXT
    // ========================================================================

    QString exceptionText;


    switch (exceptionCode)
    {
    case 0x01:
        exceptionText = "Illegal Function";
        break;

    case 0x02:
        exceptionText = "Illegal Data Address";
        break;

    case 0x03:
        exceptionText = "Illegal Data Value";
        break;

    case 0x04:
        exceptionText = "Server Device Failure";
        break;

    case 0x05:
        exceptionText = "Acknowledge";
        break;

    case 0x06:
        exceptionText = "Server Device Busy";
        break;

    case 0x08:
        exceptionText = "Memory Parity Error";
        break;

    case 0x0A:
        exceptionText = "Gateway Path Unavailable";
        break;

    case 0x0B:
        exceptionText = "Gateway Target Device Failed to Respond";
        break;

    default:
        exceptionText = "Unknown Modbus Exception";
        break;
    }


    // ========================================================================
    // 5. REPORT ERROR
    // ========================================================================

    const QString errorText =
        QString(
            "Modbus Exception 0x%1: %2"
            )
            .arg(
                exceptionCode,
                2,
                16,
                QChar('0')
                )
            .arg(
                exceptionText
                );


    emit errorOccurred(
        errorText
        );


    emit logMessage(
        errorText
        );


    return true;
}



// ------------------------------------------------------------
// ДОПОМІЖНІ МЕТОДИ
// ------------------------------------------------------------
quint16 ModBusClient::adjustAddress(quint16 address) const
{
    return address + m_addressOffset;
}

// ------------------------------------------------------------
// 16-бітні HOLDING REGISTERS
// ------------------------------------------------------------
bool ModBusClient::readHoldingRegister(quint16 address, quint16 &result)
{
    quint16 adjustedAddr = adjustAddress(address);
    QByteArray request = buildReadHoldingRegisters(adjustedAddr, 1);
    QByteArray response;

    if (!sendRequestAndWaitForResponse(request, response))
        return false;

    if (checkException(response))
        return false;

    if (response.size() < 4) {
        emit errorOccurred("Incomplete read response");
        return false;
    }

    quint8 funcCode = static_cast<quint8>(response[0]);
    if (funcCode != 0x03) {
        emit errorOccurred(QString("Invalid Function Code: 0x%1").arg(funcCode, 2, 16, QChar('0')));
        return false;
    }

    quint8 byteCount = static_cast<quint8>(response[1]);
    if (byteCount != 2) {
        emit errorOccurred(QString("Invalid Byte Count: %1, expected 2").arg(byteCount));
        return false;
    }

    QDataStream stream(response.mid(2, 2));
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> result;

    emit logMessage(QString("Reading 0x%1 = %2").arg(address, 4, 16, QChar('0')).arg(result));
    return true;
}

bool ModBusClient::writeHoldingRegister(quint16 address, quint16 value)
{
    quint16 adjustedAddr = adjustAddress(address);
    QByteArray request = buildWriteSingleRegister(adjustedAddr, value);
    QByteArray response;

    if (!sendRequestAndWaitForResponse(request, response))
        return false;

    if (checkException(response))
        return false;

    if (response.size() < 5) {
        emit errorOccurred("Incomplete write response");
        return false;
    }

    quint8 funcCode = static_cast<quint8>(response[0]);
    if (funcCode != 0x06) {
        emit errorOccurred(QString("Invalid Function Code: 0x%1").arg(funcCode, 2, 16, QChar('0')));
        return false;
    }

    quint16 respAddress, respValue;
    QDataStream stream(response.mid(1, 4));
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> respAddress >> respValue;

    if (respAddress != adjustedAddr) {
        emit errorOccurred(QString("Address mismatch in echo: %1 instead of %2").arg(respAddress).arg(adjustedAddr));
        return false;
    }
    if (respValue != value) {
        emit errorOccurred(QString("Value mismatch in echo: %1 instead of %2").arg(respValue).arg(value));
        return false;
    }

    emit logMessage(QString("Recording 0x%1 = %2").arg(address, 4, 16, QChar('0')).arg(value));
    return true;
}

// ------------------------------------------------------------
// 32-бітні HOLDING REGISTERS (int32)
// ------------------------------------------------------------
bool ModBusClient::readHoldingRegister32Int(quint16 address, quint32 &result, bool lswFirst)
{
    quint16 adjustedAddr = adjustAddress(address);
    QByteArray request = buildReadHoldingRegisters(adjustedAddr, 2);
    QByteArray response;

    if (!sendRequestAndWaitForResponse(request, response))
        return false;

    if (checkException(response))
        return false;

    if (response.size() < 6) {
        emit errorOccurred("Incomplete 32-bit read response");
        return false;
    }

    quint8 funcCode = static_cast<quint8>(response[0]);
    if (funcCode != 0x03) {
        emit errorOccurred(QString("Invalid Function Code: 0x%1").arg(funcCode, 2, 16, QChar('0')));
        return false;
    }

    quint8 byteCount = static_cast<quint8>(response[1]);
    if (byteCount != 4) {
        emit errorOccurred(QString("Invalid Byte Count: %1, expected 4").arg(byteCount));
        return false;
    }

    QDataStream stream(response.mid(2, 4));
    stream.setByteOrder(QDataStream::BigEndian);

    quint16 word1, word2;
    stream >> word1 >> word2;

    if (lswFirst) {
        result = (static_cast<quint32>(word2) << 16) | word1;
    } else {
        result = (static_cast<quint32>(word1) << 16) | word2;
    }

    emit logMessage(QString("Reading 32-bit 0x%1 = %2").arg(address, 4, 16, QChar('0')).arg(result));
    return true;
}

bool ModBusClient::readHoldingRegister32Float(quint16 address, float &result, bool lswFirst)
{
    quint32 temp;
    if (!readHoldingRegister32Int(address, temp, lswFirst))
        return false;
    memcpy(&result, &temp, sizeof(float));
    return true;
}

bool ModBusClient::writeHoldingRegister32Int(quint16 address, quint32 value, bool lswFirst)
{
    quint16 adjustedAddr = adjustAddress(address);
    quint16 word1, word2;
    if (lswFirst) {
        word1 = static_cast<quint16>(value & 0xFFFF);
        word2 = static_cast<quint16>(value >> 16);
    } else {
        word1 = static_cast<quint16>(value >> 16);
        word2 = static_cast<quint16>(value & 0xFFFF);
    }

    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << word1 << word2;

    QByteArray request = buildWriteMultipleRegisters(adjustedAddr, 2, data);
    QByteArray response;

    if (!sendRequestAndWaitForResponse(request, response))
        return false;

    if (checkException(response))
        return false;

    if (response.size() < 5) {
        emit errorOccurred("Incomplete 32-bit write response");
        return false;
    }

    quint8 funcCode = static_cast<quint8>(response[0]);
    if (funcCode != 0x10) {
        emit errorOccurred(QString("Invalid Function Code: 0x%1").arg(funcCode, 2, 16, QChar('0')));
        return false;
    }

    quint16 respAddress, respCount;
    QDataStream respStream(response.mid(1, 4));
    respStream.setByteOrder(QDataStream::BigEndian);
    respStream >> respAddress >> respCount;

    if (respAddress != adjustedAddr) {
        emit errorOccurred(QString("Address mismatch in echo: %1 instead of %2").arg(respAddress).arg(adjustedAddr));
        return false;
    }
    if (respCount != 2) {
        emit errorOccurred(QString("Register count mismatch: %1, expected 2").arg(respCount));
        return false;
    }

    emit logMessage(QString("Writing 32-bit 0x%1 = %2").arg(address, 4, 16, QChar('0')).arg(value));
    return true;
}

bool ModBusClient::writeHoldingRegister32Float(quint16 address, float value, bool lswFirst)
{
    quint32 temp;
    memcpy(&temp, &value, sizeof(float));
    return writeHoldingRegister32Int(address, temp, lswFirst);
}




// ------------------------------------------------------------
// COILS
// ------------------------------------------------------------
bool ModBusClient::readCoil(quint16 address, bool &result)
{
    quint16 adjustedAddr = adjustAddress(address);
    QByteArray request = buildReadCoils(adjustedAddr, 1);
    QByteArray response;

    if (!sendRequestAndWaitForResponse(request, response))
        return false;

    if (checkException(response))
        return false;

    if (response.size() < 3) {
        emit errorOccurred("Incomplete Coil read response");
        return false;
    }

    quint8 funcCode = static_cast<quint8>(response[0]);
    if (funcCode != 0x01) {
        emit errorOccurred(QString("Invalid Function Code: 0x%1").arg(funcCode, 2, 16, QChar('0')));
        return false;
    }

    quint8 byteCount = static_cast<quint8>(response[1]);
    if (byteCount != 1) {
        emit errorOccurred(QString("Invalid Byte Count: %1, expected 1").arg(byteCount));
        return false;
    }

    quint8 value = static_cast<quint8>(response[2]);
    result = (value & 0x01) != 0;

    emit logMessage(QString("Reading Coil 0x%1 = %2").arg(address, 4, 16, QChar('0')).arg(result ? "ON" : "OFF"));
    return true;
}











bool ModBusClient::writeCoil(quint16 address, bool value)
{
    quint16 adjustedAddr = adjustAddress(address);
    QByteArray request = buildWriteSingleCoil(adjustedAddr, value);
    QByteArray response;

    if (!sendRequestAndWaitForResponse(request, response))
        return false;

    if (checkException(response))
        return false;

    if (response.size() < 5) {
        emit errorOccurred("Incomplete Coil write response");
        return false;
    }

    quint8 funcCode = static_cast<quint8>(response[0]);
    if (funcCode != 0x05) {
        emit errorOccurred(QString("Invalid Function Code: 0x%1").arg(funcCode, 2, 16, QChar('0')));
        return false;
    }

    quint16 respAddress, respValue;
    QDataStream stream(response.mid(1, 4));
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> respAddress >> respValue;

    if (respAddress != adjustedAddr) {
        emit errorOccurred(QString("Address mismatch in echo: %1 instead of %2").arg(respAddress).arg(adjustedAddr));
        return false;
    }

    quint16 expectedValue = value ? 0xFF00 : 0x0000;
    if (respValue != expectedValue) {
        emit errorOccurred(QString("Value mismatch in echo: 0x%1 instead of 0x%2")
                               .arg(respValue, 4, 16, QChar('0')).arg(expectedValue, 4, 16, QChar('0')));
        return false;
    }

    emit logMessage(QString("Recording Coil 0x%1 = %2").arg(address, 4, 16, QChar('0')).arg(value ? "ON" : "OFF"));
    return true;
}

// ------------------------------------------------------------
// DISCRETE INPUTS
// ------------------------------------------------------------
bool ModBusClient::readDiscreteInput(quint16 address, bool &result)
{
    quint16 adjustedAddr = adjustAddress(address);
    QByteArray request = buildReadDiscreteInputs(adjustedAddr, 1);
    QByteArray response;

    if (!sendRequestAndWaitForResponse(request, response))
        return false;

    if (checkException(response))
        return false;

    if (response.size() < 3) {
        emit errorOccurred("Incomplete Discrete Input read response");
        return false;
    }

    quint8 funcCode = static_cast<quint8>(response[0]);
    if (funcCode != 0x02) {
        emit errorOccurred(QString("Invalid Function Code: 0x%1").arg(funcCode, 2, 16, QChar('0')));
        return false;
    }

    quint8 byteCount = static_cast<quint8>(response[1]);
    if (byteCount != 1) {
        emit errorOccurred(QString("Invalid Byte Count: %1, expected 1").arg(byteCount));
        return false;
    }

    quint8 value = static_cast<quint8>(response[2]);
    result = (value & 0x01) != 0;

    emit logMessage(QString("Reading Discrete Input 0x%1 = %2").arg(address, 4, 16, QChar('0')).arg(result ? "TRUE" : "FALSE"));
    return true;
}




