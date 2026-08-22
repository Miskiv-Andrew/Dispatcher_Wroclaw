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
static QTimer *g_pollTimer = nullptr;


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

// void updateTrayStatus()
// {
//     if (!g_trayManager || !g_modbusClient || !g_localServer) return;
//     bool plcConnected = g_modbusClient->isConnected();
//     bool hasClient = (g_localServer->clientsCount() > 0);
//     g_trayManager->updateStatus(plcConnected, hasClient);
// }

// ------------------------------------------------------------
// Запис даних цистерни в ПЛК
// ------------------------------------------------------------
void writeZBToPLC(int zbNumber, const QJsonObject &data)
{
    if (!g_modbusClient || !g_modbusClient->isConnected()) {
        logMessage("[ПОМИЛКА] Немає з'єднання з ПЛК");
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

    logMessage(QString("[ЗАПИС] Цистерна ZB%1: дані записано").arg(zbNumber));
}

// ------------------------------------------------------------
// Запис даних настенного детектора в ПЛК
// ------------------------------------------------------------
void writeCZToPLC(int czNumber, const QJsonObject &data)
{
    if (!g_modbusClient || !g_modbusClient->isConnected()) {
        logMessage("[ПОМИЛКА] Немає з'єднання з ПЛК");
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

    logMessage(QString("[ЗАПИС] Настенний детектор CZ%1: дані записано").arg(czNumber));
}

// ------------------------------------------------------------
// Заміна приладу
// ------------------------------------------------------------
void replaceDevice(int zbNumber, int newSN)
{
    if (!g_modbusClient || !g_modbusClient->isConnected()) {
        logMessage("[ПОМИЛКА] Немає з'єднання з ПЛК");
        return;
    }

    quint16 baseSN = 9044;
    int idx = zbNumber - 1;
    g_modbusClient->writeHoldingRegister32Int(baseSN + idx * 2, static_cast<quint32>(newSN));

    logMessage(QString("[ЗАМІНА] Цистерна ZB%1: новий SN = %2").arg(zbNumber).arg(newSN));
}

// ------------------------------------------------------------
// Читання стану баків із ПЛК
// ------------------------------------------------------------
void readFullnessAndSend()
{
    if (!g_modbusClient || !g_modbusClient->isConnected()) {
        logMessage("[ПОМИЛКА] Немає з'єднання з ПЛК для читання стану баків");
        return;
    }

    QJsonArray zbArray;

    for (int i = 1; i <= 9; ++i) {
        quint16 address = 10260 + (i - 1);  // тимчасові Coil для тесту
        bool full = false;
        if (g_modbusClient->readCoil(address, full)) {
            QJsonObject zb;
            zb["number"] = i;
            zb["fullness"] = full ? 1 : 0;
            zbArray.append(zb);
        } else {
            logMessage(QString("[ПОМИЛКА] Не вдалося прочитати стан бака ZB%1").arg(i));
        }
    }

    QJsonObject response;
    response["type"] = "read";
    QJsonObject data;
    data["zb"] = zbArray;
    response["data"] = data;

    if (g_localServer) {
        g_localServer->broadcast(response);
        logMessage("[ЧИТАННЯ] Стан баків прочитано та відправлено");
    }
}

















// ------------------------------------------------------------
// ГОЛОВНА ФУНКЦІЯ
// ------------------------------------------------------------
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);


    // ========================================================================
    // 1. ЛОГИРОВАНИЕ СТАРТА ПРИЛОЖЕНИЯ
    // ========================================================================

    logMessage("[СИСТЕМА] Запуск ModBusBridgeService");


    // ========================================================================
    // 2. ЦЕНТРАЛЬНЫЙ КОНТРОЛЛЕР ПРИЛОЖЕНИЯ
    //
    // ApplicationController уже является владельцем:
    //
    //   - TrayManager;
    //   - ModBusClient;
    //   - LocalServer;
    //   - poll timer;
    //   - watchdog timer.
    //
    // Сам объект находится в стеке main(), поэтому существует всё время
    // работы event loop и автоматически уничтожается после app.exec().
    // ========================================================================

    ApplicationController applicationController;


    // ------------------------------------------------------------------------
    // Перед завершением event loop выполняем контролируемый shutdown.
    //
    // ApplicationController остановит таймеры, отключит ПЛК и остановит
    // LocalServer до начала уничтожения QObject.
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
    // Сам ModBusClient уже создаётся внутри ApplicationController.
    //
    // g_modbusClient пока сохраняем, потому что функции:
    //
    //   writeZBToPLC()
    //   writeCZToPLC()
    //   replaceDevice()
    //   readFullnessAndSend()
    //
    // всё ещё находятся в main.cpp и используют глобальный указатель.
    //
    // Это будет убрано на следующих шагах.
    // ========================================================================

    g_modbusClient = applicationController.modBusClient();

    ModBusClient *client = g_modbusClient;


    // ------------------------------------------------------------------------
    // Рабочее логирование состояния Modbus.
    // ------------------------------------------------------------------------
    QObject::connect(
        client,
        &ModBusClient::connected,
        []()
        {
            logMessage("[СИСТЕМА] Підключення до ПЛК встановлено");
        }
        );

    QObject::connect(
        client,
        &ModBusClient::disconnected,
        []()
        {
            logMessage("[СИСТЕМА] Зв'язок з ПЛК втрачено");
        }
        );

    QObject::connect(
        client,
        &ModBusClient::errorOccurred,
        [](const QString &error)
        {
            logMessage("[ПОМИЛКА] " + error);
        }
        );

    QObject::connect(
        client,
        &ModBusClient::logMessage,
        [](const QString &msg)
        {
            logMessage("[ЛОГ] " + msg);
        }
        );


    // ------------------------------------------------------------------------
    // Параметры подключения к ПЛК.
    // ------------------------------------------------------------------------
    QString ip = "127.0.0.1";
    quint16 port = 502;

    if (argc >= 3)
    {
        ip = QString::fromLocal8Bit(argv[1]);
        port = QString::fromLocal8Bit(argv[2]).toUShort();
    }


    client->setConnectionParams(ip, port);
    client->setTimeout(2000);
    client->setUnitId(1);
    client->setWordOrder(true);


    // ------------------------------------------------------------------------
    // Запускаем первоначальное подключение.
    //
    // Если ПЛК сейчас недоступен, ModBusClient самостоятельно запустит
    // механизм reconnect, который мы реализовали ранее.
    // ------------------------------------------------------------------------
    if (!client->connectToPLC())
    {
        logMessage(
            "[СИСТЕМА] Не вдалося ініціювати підключення до ПЛК"
            );
    }


    // ========================================================================
    // 4. LOCAL SERVER
    //
    // LocalServer также уже принадлежит ApplicationController.
    //
    // g_localServer пока оставляем, потому что readFullnessAndSend()
    // использует его для broadcast данных Python-клиенту.
    // ========================================================================

    g_localServer = applicationController.localServer();

    LocalServer *server = g_localServer;


    // ------------------------------------------------------------------------
    // Рабочие команды Python -> PLC.
    //
    // Эти обработчики пока остаются глобальными функциями main.cpp.
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

    QObject::connect(
        server,
        &LocalServer::requestFullness,
        &readFullnessAndSend
        );

    QObject::connect(
        server,
        &LocalServer::logMessage,
        &logMessage
        );


    // ------------------------------------------------------------------------
    // ВАЖНО:
    //
    // Здесь больше НЕТ подключений:
    //
    //   writeZB       -> onDataReceived
    //   writeCZ       -> onDataReceived
    //   replaceDevice -> onDataReceived
    //
    // Контроль активности Python теперь полностью находится
    // внутри ApplicationController.
    // ------------------------------------------------------------------------


    // ------------------------------------------------------------------------
    // Запускаем локальный TCP-сервер для Python.
    // ------------------------------------------------------------------------
    if (!server->start(12345))
    {
        logMessage(
            "[СИСТЕМА] Не вдалося запустити локальний сервер"
            );
    }


    // ========================================================================
    // 5. SYSTEM TRAY
    // ========================================================================

    // ------------------------------------------------------------------------
    // Команда "Выход" из tray завершает QApplication.
    //
    // После quit() будет вызван aboutToQuit(), а затем
    // ApplicationController::shutdown().
    // ------------------------------------------------------------------------
    QObject::connect(
        applicationController.trayManager(),
        &TrayManager::exitRequested,
        &app,
        &QCoreApplication::quit
        );


    // ========================================================================
    // 6. ПЕРИОДИЧЕСКИЙ ОПРОС ПЛК
    //
    // Poll timer уже принадлежит ApplicationController.
    //
    // Сам readFullnessAndSend() пока остаётся в main.cpp.
    // Его перенос выполним отдельно.
    // ========================================================================

    g_pollTimer = applicationController.pollTimer();

    QObject::connect(
        g_pollTimer,
        &QTimer::timeout,
        &readFullnessAndSend
        );

    g_pollTimer->start(10000);


    // ========================================================================
    // WATCHDOG ЗДЕСЬ БОЛЬШЕ НЕ НАСТРАИВАЕТСЯ.
    //
    // Его:
    //
    //   - таймер;
    //   - интервал;
    //   - timeout;
    //   - время последней активности Python;
    //   - запись Coil 9035
    //
    // теперь полностью обслуживает ApplicationController.
    // ========================================================================


    // ========================================================================
    // 7. ЗАПУСК EVENT LOOP
    // ========================================================================

    logMessage("[СИСТЕМА] Запуск циклу обробки подій");

    const int result = app.exec();


    // ========================================================================
    // 8. ЗАВЕРШЕНИЕ
    // ========================================================================

    logMessage("[СИСТЕМА] Завершення роботи");

    return result;
}
