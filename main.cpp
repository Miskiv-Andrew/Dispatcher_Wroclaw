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
static TrayManager *g_trayManager = nullptr;
static QTimer *g_pollTimer = nullptr;
static QTimer *g_watchdogTimer = nullptr;
static QDateTime g_lastDataTime;

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

void updateTrayStatus()
{
    if (!g_trayManager || !g_modbusClient || !g_localServer) return;
    bool plcConnected = g_modbusClient->isConnected();
    bool hasClient = (g_localServer->clientsCount() > 0);
    g_trayManager->updateStatus(plcConnected, hasClient);
}

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
// Watchdog: перевірка активності Python-клієнта
// ------------------------------------------------------------
void checkWatchdog()
{
    if (!g_modbusClient || !g_modbusClient->isConnected()) {
        return;
    }

    qint64 secondsSinceLastData = g_lastDataTime.secsTo(QDateTime::currentDateTime());

    if (secondsSinceLastData > 10) {
        // Якщо немає даних більше 10 секунд — вимикаємо Coil 9035
        g_modbusClient->writeCoil(9035, false);
        logMessage("[WATCHDOG] Зв'язок з Python втрачено, Coil 9035 = 0");
    } else {
        // Якщо дані є — вмикаємо Coil 9035
        g_modbusClient->writeCoil(9035, true);
    }
}

// ------------------------------------------------------------
// Слот для оновлення часу останнього отримання даних
// ------------------------------------------------------------
void onDataReceived()
{
    g_lastDataTime = QDateTime::currentDateTime();
}

// ------------------------------------------------------------
// ГОЛОВНА ФУНКЦІЯ
// ------------------------------------------------------------
int main(int argc, char *argv[])
{
    // QCoreApplication app(argc, argv);
    QApplication app(argc, argv);

    // ------------------------------------------------------------
    // 1. Логування старту
    // ------------------------------------------------------------
    logMessage("[СИСТЕМА] Запуск ModBusBridgeService");

    // ------------------------------------------------------------------------
    // Центральный управляющий объект приложения.
    //
    // Пока ApplicationController ещё не управляет другими объектами.
    // На этом шаге мы только проверяем его корректный жизненный цикл.
    //
    // Объект создаётся в стеке main(), поэтому:
    //   - существует всё время работы event loop;
    //   - автоматически уничтожится после выхода из app.exec();
    // ------------------------------------------------------------------------
    ApplicationController applicationController;

    // ------------------------------------------------------------
    // 2. Створення клієнта ModBus
    // ------------------------------------------------------------
    ModBusClient client;
    g_modbusClient = &client;

    // Підключення сигналів клієнта
    QObject::connect(&client, &ModBusClient::connected, [](){
        logMessage("[СИСТЕМА] Підключення до ПЛК встановлено");
    });
    QObject::connect(&client, &ModBusClient::disconnected, [](){
        logMessage("[СИСТЕМА] Зв'язок з ПЛК втрачено");
    });
    QObject::connect(&client, &ModBusClient::errorOccurred, [](const QString &error){
        logMessage("[ПОМИЛКА] " + error);
    });
    QObject::connect(&client, &ModBusClient::logMessage, [](const QString &msg){
        logMessage("[ЛОГ] " + msg);
    });

    // Налаштування підключення
    QString ip = "127.0.0.1";
    quint16 port = 502;
    if (argc >= 3) {
        ip = QString::fromLocal8Bit(argv[1]);
        port = QString::fromLocal8Bit(argv[2]).toUShort();
    }

    client.setConnectionParams(ip, port);
    client.setTimeout(2000);
    client.setUnitId(1);
    client.setWordOrder(true);

    if (!client.connectToPLC()) {
        logMessage("[СИСТЕМА] Не вдалося ініціювати підключення до ПЛК");
    }

    // ------------------------------------------------------------
    // 3. Створення локального сервера
    // ------------------------------------------------------------
    LocalServer server;
    g_localServer = &server;

    // Підключення сигналів сервера до обробників
    QObject::connect(&server, &LocalServer::writeZB, &writeZBToPLC);
    QObject::connect(&server, &LocalServer::writeCZ, &writeCZToPLC);
    QObject::connect(&server, &LocalServer::replaceDevice, &replaceDevice);
    QObject::connect(&server, &LocalServer::requestFullness, &readFullnessAndSend);
    QObject::connect(&server, &LocalServer::logMessage, &logMessage);

    // При отриманні будь-яких даних від Python — оновлюємо час
    QObject::connect(&server, &LocalServer::writeZB, &onDataReceived);
    QObject::connect(&server, &LocalServer::writeCZ, &onDataReceived);
    QObject::connect(&server, &LocalServer::replaceDevice, &onDataReceived);

    if (!server.start(12345)) {
        logMessage("[СИСТЕМА] Не вдалося запустити локальний сервер");
    }

    // // Создаём трей-менеджер
    // TrayManager trayManager;
    // g_trayManager = &trayManager;

    // // Закрытие приложения через пункт меню "Выход"
    // QObject::connect(
    //     &trayManager,
    //     &TrayManager::exitRequested,
    //     &app,
    //     &QCoreApplication::quit
    // );

    // ------------------------------------------------------------------------
    // TrayManager теперь создаётся и принадлежит ApplicationController.
    //
    // Пока сохраняем глобальный указатель, потому что updateTrayStatus()
    // ещё остаётся глобальной функцией.
    // Это временное решение на период пошагового рефакторинга.
    // ------------------------------------------------------------------------
    g_trayManager = applicationController.trayManager();

    // Закрытие приложения через пункт меню "Выход"
    QObject::connect(
        g_trayManager,
        &TrayManager::exitRequested,
        &app,
        &QCoreApplication::quit
        );

    // Обновляем статус при изменении состояния подключения к ПЛК
    QObject::connect(&client, &ModBusClient::connected, &updateTrayStatus);
    QObject::connect(&client, &ModBusClient::disconnected, &updateTrayStatus);

    // Обновляем статус при изменении количества клиентов
    QObject::connect(&server, &LocalServer::clientConnected, &updateTrayStatus);
    QObject::connect(&server, &LocalServer::clientDisconnected, &updateTrayStatus);


    // Начальное обновление статуса
    updateTrayStatus();



    // ------------------------------------------------------------
    // 4. Таймер опитування ПЛК (кожні 5 секунд)
    // ------------------------------------------------------------
    g_pollTimer = new QTimer();
    QObject::connect(g_pollTimer, &QTimer::timeout, &readFullnessAndSend);
    g_pollTimer->start(10000);

    // ------------------------------------------------------------
    // 5. Watchdog (кожні 2 секунди)
    // ------------------------------------------------------------
    g_watchdogTimer = new QTimer();
    QObject::connect(g_watchdogTimer, &QTimer::timeout, &checkWatchdog);
    g_watchdogTimer->start(2000);

    // Ініціалізація часу останнього отримання даних
    g_lastDataTime = QDateTime::currentDateTime();

    // ------------------------------------------------------------
    // 6. Запуск циклу обробки подій
    // ------------------------------------------------------------
    logMessage("[СИСТЕМА] Запуск циклу обробки подій");
    int result = app.exec();

    // ------------------------------------------------------------
    // 7. Завершення
    // ------------------------------------------------------------
    logMessage("[СИСТЕМА] Завершення роботи");
    return result;
}
