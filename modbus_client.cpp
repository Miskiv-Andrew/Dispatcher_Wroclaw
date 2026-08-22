/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "modbus_client.h"
#include <QDataStream>
#include <QThread>
#include <QDebug>
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
    disconnectFromPLC();
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
    // ------------------------------------------------------------------------
    // Это НАМЕРЕННОЕ отключение.
    //
    // Например:
    //   - завершение программы;
    //   - в будущем ручная остановка связи.
    //
    // Поэтому автоматический reconnect необходимо запретить.
    // ------------------------------------------------------------------------
    m_manualDisconnect = true;


    // ------------------------------------------------------------------------
    // Если таймер повторного подключения уже работает,
    // обязательно останавливаем его.
    // ------------------------------------------------------------------------
    if (m_reconnectTimer->isActive()) {
        m_reconnectTimer->stop();
    }


    // ------------------------------------------------------------------------
    // Просим QTcpSocket корректно закрыть соединение.
    // ------------------------------------------------------------------------
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }


    // Локальное состояние соединения сбрасываем.
    m_connected = false;

    emit logMessage("Disconnected from PLC");
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

void ModBusClient::onSocketError(QAbstractSocket::SocketError socketError)
{
    // Формируем единое диагностическое сообщение.
    const QString errorText =
        QString("Socket error: %1").arg(m_socket->errorString());

    emit errorOccurred(errorText);
    emit logMessage(errorText);


    // ------------------------------------------------------------------------
    // Для reconnect нам не принципиально, был ли это:
    //   ConnectionRefusedError,
    //   RemoteHostClosedError,
    //   NetworkError
    // или другая ошибка связи.
    //
    // Если сокет после ошибки оказался отключён,
    // считаем соединение потерянным.
    // ------------------------------------------------------------------------
    if (m_socket->state() == QAbstractSocket::UnconnectedState) {

        const bool wasConnected = m_connected;

        m_connected = false;


        // Если раньше соединение действительно существовало,
        // уведомляем остальные части приложения.
        if (wasConnected) {
            emit disconnected();
        }


        // Если пользователь сам не запросил отключение,
        // начинаем восстановление.
        if (!m_manualDisconnect) {
            startReconnectTimer();
        }
    }


    // socketError пока оставляем в сигнатуре,
    // так как этого требует сигнал QTcpSocket::errorOccurred().
    Q_UNUSED(socketError);
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

// ------------------------------------------------------------
// ВІДПРАВКА ТА ОТРИМАННЯ ВІДПОВІДІ
// ------------------------------------------------------------
bool ModBusClient::sendRequestAndWaitForResponse(const QByteArray &request, QByteArray &response)
{
    if (!isConnected()) {
        emit errorOccurred("No connection to PLC");
        return false;
    }

    m_transactionId++;
    if (m_transactionId == 0) m_transactionId = 1;

    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << m_transactionId;
    stream << quint16(0);
    stream << quint16(request.size() + 1);
    stream << m_unitId;
    frame.append(request);

    qint64 written = m_socket->write(frame);
    if (written == -1) {
        emit errorOccurred("Write error");
        return false;
    }
    if (written != frame.size()) {
        emit logMessage(QString("Warning: written %1 of %2 bytes").arg(written).arg(frame.size()));
    }

    emit logMessage(QString("Request sent, transaction %1, %2 bytes").arg(m_transactionId).arg(frame.size()));

    if (!m_socket->waitForReadyRead(m_timeoutMs)) {
        emit errorOccurred("Timeout waiting for response");
        return false;
    }

    if (m_socket->bytesAvailable() < 6) {
        if (!m_socket->waitForReadyRead(m_timeoutMs) || m_socket->bytesAvailable() < 6) {
            emit errorOccurred("Incomplete MBAP header");
            return false;
        }
    }

    QByteArray header = m_socket->read(6);
    QDataStream headerStream(header);
    headerStream.setByteOrder(QDataStream::BigEndian);

    quint16 respTransId, respProtocolId, respLength;
    headerStream >> respTransId >> respProtocolId >> respLength;

    if (respTransId != m_transactionId) {
        emit errorOccurred("Transaction ID mismatch");
        return false;
    }
    if (respProtocolId != 0) {
        emit errorOccurred("Invalid Protocol ID");
        return false;
    }
    if (respLength < 1 || respLength > 255) {
        emit errorOccurred(QString("Invalid response length: %1").arg(respLength));
        return false;
    }

    int remaining = respLength;
    while (m_socket->bytesAvailable() < remaining) {
        if (!m_socket->waitForReadyRead(m_timeoutMs)) {
            emit errorOccurred("Timeout reading data");
            return false;
        }
    }

    QByteArray raw = m_socket->read(remaining);
    if (raw.size() < 1) {
        emit errorOccurred("Not enough data for PDU");
        return false;
    }

    quint8 receivedUnitId = static_cast<quint8>(raw[0]);
    if (receivedUnitId != m_unitId) {
        emit errorOccurred(QString("Unit ID mismatch: received %1, expected %2").arg(receivedUnitId).arg(m_unitId));
        return false;
    }

    response = raw.mid(1);
    emit logMessage(QString("Response received, %1 bytes PDU").arg(response.size()));
    return true;
}

bool ModBusClient::checkException(const QByteArray &response)
{
    if (response.size() < 2) return false;
    quint8 functionCode = static_cast<quint8>(response[0]);
    if ((functionCode & 0x80) != 0) {
        quint8 exceptionCode = static_cast<quint8>(response[1]);
        emit errorOccurred(QString("ModBus error: code 0x%1").arg(exceptionCode, 2, 16, QChar('0')));
        return true;
    }
    return false;
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
