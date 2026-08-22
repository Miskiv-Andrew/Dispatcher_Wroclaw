#include <QCoreApplication>
#include <QApplication>
#include <QTimer>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "modbus_client.h"
#include "localserver.h"
#include "traymanager.h"
#include "applicationcontroller.h"

// ------------------------------------------------------------
// Глобальні об'єкти
// ------------------------------------------------------------
static ModBusClient *g_modbusClient = nullptr;
static LocalServer *g_localServer = nullptr;


// static QTimer *g_pollTimer = nullptr;


// ------------------------------------------------------------
// Логування
// ------------------------------------------------------------
void logMessage(const QString &msg)
{
    qDebug() << msg;

    QFile logFile("ModBusBridgeService.log");
    if (logFile.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&logFile);
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        stream << timestamp << " " << msg << "\n";
        logFile.close();
    }
}

// ------------------------------------------------------------
// Запис даних цистерни в ПЛК
// ------------------------------------------------------------
void writeZBToPLC(int zbNumber, const QJsonObject &data)
{
    if (!g_modbusClient || !g_modbusClient->isConnected()) {
        logMessage("[ERROR] No connection to PLC");
        return;
    }

    // Використовуємо адреси згідно з таблицею
    quint16 baseSN = 9071;
    quint16 baseTemp = 9089;
    quint16 basePaed = 9107;
    quint16 baseIso1Name = 9125;
    quint16 baseIso1Act = 9134;
    quint16 baseIso1Conc = 9311;
    quint16 baseIso2Name = 9152;
    quint16 baseIso2Act = 9161;
    quint16 baseIso2Conc = 9329;
    quint16 baseIso3Name = 9179;
    quint16 baseIso3Act = 9188;
    quint16 baseIso3Conc = 9347;

    // Для ZB3 — додаткові адреси для ізотопів 4,5
    quint16 iso4Name = 9365;
    quint16 iso4Act = 9367;
    quint16 iso4Conc = 9371;
    quint16 iso5Name = 9366;
    quint16 iso5Act = 9369;
    quint16 iso5Conc = 9373;

    int idx = zbNumber - 1;

    // SN
    if (data.contains("sn")) {
        quint32 sn = data.value("sn").toInt();
        g_modbusClient->writeHoldingRegister32Int(baseSN + idx * 2, sn);
    }

    // Температура
    if (data.contains("temperature")) {
        float temp = data.value("temperature").toDouble();
        g_modbusClient->writeHoldingRegister32Float(baseTemp + idx * 2, temp);
    }

    // ПАЕД
    if (data.contains("paed")) {
        float paed = data.value("paed").toDouble();
        g_modbusClient->writeHoldingRegister32Float(basePaed + idx * 2, paed);
    }

    // Ізотопи
    QJsonArray isotopes = data.value("isotopes").toArray();
    for (const QJsonValue &val : isotopes) {
        QJsonObject iso = val.toObject();
        int id = iso.value("id").toInt();
        int name = iso.value("name").toInt();
        float activity = iso.value("activity").toDouble();
        float concentration = iso.value("concentration").toDouble();

        if (zbNumber == 3) {
            // Резервна цистерна — ізотопи 1..5
            if (id == 1) {
                g_modbusClient->writeHoldingRegister(baseIso1Name + idx, name);
                g_modbusClient->writeHoldingRegister32Float(baseIso1Act + idx * 2, activity);
                g_modbusClient->writeHoldingRegister32Float(baseIso1Conc + idx * 2, concentration);
            } else if (id == 2) {
                g_modbusClient->writeHoldingRegister(baseIso2Name + idx, name);
                g_modbusClient->writeHoldingRegister32Float(baseIso2Act + idx * 2, activity);
                g_modbusClient->writeHoldingRegister32Float(baseIso2Conc + idx * 2, concentration);
            } else if (id == 3) {
                g_modbusClient->writeHoldingRegister(baseIso3Name + idx, name);
                g_modbusClient->writeHoldingRegister32Float(baseIso3Act + idx * 2, activity);
                g_modbusClient->writeHoldingRegister32Float(baseIso3Conc + idx * 2, concentration);
            } else if (id == 4) {
                g_modbusClient->writeHoldingRegister(iso4Name, name);
                g_modbusClient->writeHoldingRegister32Float(iso4Act, activity);
                g_modbusClient->writeHoldingRegister32Float(iso4Conc, concentration);
            } else if (id == 5) {
                g_modbusClient->writeHoldingRegister(iso5Name, name);
                g_modbusClient->writeHoldingRegister32Float(iso5Act, activity);
                g_modbusClient->writeHoldingRegister32Float(iso5Conc, concentration);
            }
        } else {
            // Інші цистерни — ізотопи 1..3
            if (id == 1) {
                g_modbusClient->writeHoldingRegister(baseIso1Name + idx, name);
                g_modbusClient->writeHoldingRegister32Float(baseIso1Act + idx * 2, activity);
                g_modbusClient->writeHoldingRegister32Float(baseIso1Conc + idx * 2, concentration);
            } else if (id == 2) {
                g_modbusClient->writeHoldingRegister(baseIso2Name + idx, name);
                g_modbusClient->writeHoldingRegister32Float(baseIso2Act + idx * 2, activity);
                g_modbusClient->writeHoldingRegister32Float(baseIso2Conc + idx * 2, concentration);
            } else if (id == 3) {
                g_modbusClient->writeHoldingRegister(baseIso3Name + idx, name);
                g_modbusClient->writeHoldingRegister32Float(baseIso3Act + idx * 2, activity);
                g_modbusClient->writeHoldingRegister32Float(baseIso3Conc + idx * 2, concentration);
            }
        }
    }

    logMessage(QString("[WRITE] Tanker ZB%1: data written").arg(zbNumber));
}

// ------------------------------------------------------------
// Запис даних настенного детектора в ПЛК
// ------------------------------------------------------------
void writeCZToPLC(int czNumber, const QJsonObject &data)
{
    if (!g_modbusClient || !g_modbusClient->isConnected()) {
        logMessage("[ERROR] No connection to PLC");
        return;
    }

    quint16 baseSN = 9281;
    quint16 baseTemp = 9287;
    quint16 basePaed = 9293;

    int idx = czNumber - 1;

    if (data.contains("sn")) {
        quint32 sn = data.value("sn").toInt();
        g_modbusClient->writeHoldingRegister32Int(baseSN + idx * 2, sn);
    }

    if (data.contains("temperature")) {
        float temp = data.value("temperature").toDouble();
        g_modbusClient->writeHoldingRegister32Float(baseTemp + idx * 2, temp);
    }

    if (data.contains("paed")) {
        float paed = data.value("paed").toDouble();
        g_modbusClient->writeHoldingRegister32Float(basePaed + idx * 2, paed);
    }

    logMessage(QString("[RECORD] Wall detector CZ%1: data recorded").arg(czNumber));
}

// ------------------------------------------------------------
// Заміна приладу
// ------------------------------------------------------------
void replaceDevice(int zbNumber, int newSN)
{
    if (!g_modbusClient || !g_modbusClient->isConnected()) {
        logMessage("[ERROR] No connection to PLC");
        return;
    }

    quint16 baseSN = 9044;
    int idx = zbNumber - 1;
    g_modbusClient->writeHoldingRegister32Int(baseSN + idx * 2, static_cast<quint32>(newSN));

    logMessage(QString("[REPLACE] Tank ZB%1: new SN = %2").arg(zbNumber).arg(newSN));
}








// ------------------------------------------------------------
// ГОЛОВНА ФУНКЦІЯ
// ------------------------------------------------------------
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);


    // ========================================================================
    // 1. ЛОГИРОВАНИЕ СТАРТА
    // ========================================================================

    logMessage("[СИСТЕМА] Запуск ModBusBridgeService");


    // ========================================================================
    // 2. ЦЕНТРАЛЬНЫЙ КОНТРОЛЛЕР
    // ========================================================================

    ApplicationController applicationController;


    // ------------------------------------------------------------------------
    // Контролируемое завершение приложения.
    // ------------------------------------------------------------------------
    QObject::connect(
        &app,
        &QCoreApplication::aboutToQuit,
        &applicationController,
        &ApplicationController::shutdown
        );


    // ========================================================================
    // 3. MODBUS TCP CLIENT
    //
    // g_modbusClient пока временно остаётся, потому что некоторые
    // обработчики Python-команд всё ещё находятся в main.cpp.
    // ========================================================================

    g_modbusClient = applicationController.modBusClient();

    ModBusClient *client = g_modbusClient;


    // ------------------------------------------------------------------------
    // Логирование состояния соединения с ПЛК.
    // ------------------------------------------------------------------------
    QObject::connect(
        client,
        &ModBusClient::connected,
        []()
        {
            logMessage("[SYSTEM] Connection to PLC established");
        }
        );


    QObject::connect(
        client,
        &ModBusClient::disconnected,
        []()
        {
            logMessage("[SYSTEM] Communication with PLC lost");
        }
        );


    QObject::connect(
        client,
        &ModBusClient::errorOccurred,
        [](const QString &error)
        {
            logMessage("[ERROR] " + error);
        }
        );


    QObject::connect(
        client,
        &ModBusClient::logMessage,
        [](const QString &msg)
        {
            logMessage("[LOG] " + msg);
        }
        );


    // ------------------------------------------------------------------------
    // Настройки подключения к ПЛК.
    // ------------------------------------------------------------------------
    QString ip = "127.0.0.1";
    quint16 port = 502;


    if (argc >= 3)
    {
        ip = QString::fromLocal8Bit(argv[1]);

        port =
            QString::fromLocal8Bit(argv[2]).toUShort();
    }


    client->setConnectionParams(ip, port);

    client->setTimeout(2000);

    client->setUnitId(1);

    client->setWordOrder(true);


    // ------------------------------------------------------------------------
    // Первоначальная попытка подключения.
    //
    // При неудаче ModBusClient запустит reconnect самостоятельно.
    // ------------------------------------------------------------------------
    if (!client->connectToPLC())
    {
        logMessage(
            "[SYSTEM] Failed to initiate connection to PLC"
            );
    }


    // ========================================================================
    // 4. LOCAL SERVER
    //
    // g_localServer пока временно остаётся.
    // ========================================================================

    g_localServer =
        applicationController.localServer();

    LocalServer *server =
        g_localServer;


    // ------------------------------------------------------------------------
    // Python -> PLC.
    //
    // Эти обработчики пока ещё находятся в main.cpp.
    // ------------------------------------------------------------------------
    QObject::connect(
        server,
        &LocalServer::writeZB,
        &writeZBToPLC
        );


    QObject::connect(
        server,
        &LocalServer::writeCZ,
        &writeCZToPLC
        );


    QObject::connect(
        server,
        &LocalServer::replaceDevice,
        &replaceDevice
        );


    // ------------------------------------------------------------------------
    // Запрос состояния баков.
    //
    // Теперь обработчик находится внутри ApplicationController.
    // ------------------------------------------------------------------------
    QObject::connect(
        server,
        &LocalServer::requestFullness,
        &applicationController,
        &ApplicationController::readFullnessAndSend
        );


    QObject::connect(
        server,
        &LocalServer::logMessage,
        &logMessage
        );


    // ------------------------------------------------------------------------
    // Запуск локального TCP-сервера.
    // ------------------------------------------------------------------------
    if (!server->start(12345))
    {
        logMessage(
            "[SYSTEM] Failed to start local server"
            );
    }


    // ========================================================================
    // 5. SYSTEM TRAY
    // ========================================================================

    QObject::connect(
        applicationController.trayManager(),
        &TrayManager::exitRequested,
        &app,
        &QCoreApplication::quit
        );

    // ========================================================================
    // 6. EVENT LOOP
    // ========================================================================

    logMessage(
        "[SYSTEM] Starting the event processing loop"
        );


    const int result =
        app.exec();


    // ========================================================================
    // 7. ЗАВЕРШЕНИЕ
    // ========================================================================

    logMessage(
        "[SYSTEM] Shutting down"
        );


    return result;
}
