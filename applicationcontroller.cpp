#include "applicationcontroller.h"

#include <QDebug>

#include <QTimer>

#include "traymanager.h"

#include "modbus_client.h"

#include "localserver.h"






// ============================================================================
// ApplicationController::ApplicationController
// ============================================================================
ApplicationController::ApplicationController(QObject *parent)
    : QObject(parent)
    , m_trayManager(nullptr)
    , m_modBusClient(nullptr)
    , m_localServer(nullptr)
    , m_pollTimer(nullptr)
    , m_watchdogTimer(nullptr)
    , m_shuttingDown(false)
    , m_lastPythonDataTime(QDateTime::currentDateTime())
{
    qDebug() << "[APP] ApplicationController created";


    // ========================================================================
    // 1. СОЗДАНИЕ ОСНОВНЫХ ОБЪЕКТОВ
    // ========================================================================

    // Менеджер системного трея.
    m_trayManager = new TrayManager(this);

    qDebug() << "[APP] TrayManager created";


    // Modbus TCP клиент для связи с ПЛК.
    m_modBusClient = new ModBusClient(this);

    qDebug() << "[APP] ModBusClient created";


    // Локальный TCP-сервер для связи с Python.
    m_localServer = new LocalServer(this);

    qDebug() << "[APP] LocalServer created";


    // Таймер периодического опроса.
    m_pollTimer = new QTimer(this);

    qDebug() << "[APP] Poll timer created";


    // Watchdog-таймер Python-приложения.
    m_watchdogTimer = new QTimer(this);

    qDebug() << "[APP] Watchdog timer created";


    // ========================================================================
    // 2. СОСТОЯНИЕ ПЛК -> TRAY
    // ========================================================================

    connect(
        m_modBusClient,
        &ModBusClient::connected,
        this,
        &ApplicationController::updateTrayStatus
        );

    connect(
        m_modBusClient,
        &ModBusClient::disconnected,
        this,
        &ApplicationController::updateTrayStatus
        );


    // ========================================================================
    // 3. СОСТОЯНИЕ PYTHON -> TRAY
    // ========================================================================

    connect(
        m_localServer,
        &LocalServer::clientConnected,
        this,
        &ApplicationController::updateTrayStatus
        );

    connect(
        m_localServer,
        &LocalServer::clientDisconnected,
        this,
        &ApplicationController::updateTrayStatus
        );


    // ========================================================================
    // 4. КОНТРОЛЬ АКТИВНОСТИ PYTHON
    //
    // Пока сохраняем существующую семантику проекта:
    // получение любой из этих команд означает, что Python работает.
    // ========================================================================

    connect(
        m_localServer,
        &LocalServer::writeZB,
        this,
        &ApplicationController::onPythonDataReceived
        );

    connect(
        m_localServer,
        &LocalServer::writeCZ,
        this,
        &ApplicationController::onPythonDataReceived
        );

    connect(
        m_localServer,
        &LocalServer::replaceDevice,
        this,
        &ApplicationController::onPythonDataReceived
        );


    // ========================================================================
    // 5. WATCHDOG
    //
    // Проверяем активность Python каждые 2 секунды.
    // ========================================================================

    m_watchdogTimer->setInterval(2000);

    connect(
        m_watchdogTimer,
        &QTimer::timeout,
        this,
        &ApplicationController::checkWatchdog
        );

    m_watchdogTimer->start();


    // ========================================================================
    // 6. НАЧАЛЬНОЕ СОСТОЯНИЕ TRAY
    // ========================================================================

    updateTrayStatus();
}









void ApplicationController::onPythonDataReceived()
{
    // ------------------------------------------------------------------------
    // Любая рабочая команда от Python означает, что приложение активно.
    //
    // Запоминаем момент получения последних данных.
    // ------------------------------------------------------------------------
    m_lastPythonDataTime = QDateTime::currentDateTime();
}




void ApplicationController::checkWatchdog()
{
    // ------------------------------------------------------------------------
    // Во время завершения приложения watchdog работать не должен.
    // ------------------------------------------------------------------------
    if (m_shuttingDown) {
        return;
    }


    // ------------------------------------------------------------------------
    // Без соединения с ПЛК записать watchdog Coil невозможно.
    //
    // Просто ждём восстановления Modbus-соединения.
    // ------------------------------------------------------------------------
    if (!m_modBusClient ||
        !m_modBusClient->isConnected())
    {
        return;
    }


    // ------------------------------------------------------------------------
    // Определяем, сколько секунд прошло с момента последней рабочей
    // команды от Python.
    // ------------------------------------------------------------------------
    const qint64 secondsSinceLastData =
        m_lastPythonDataTime.secsTo(
            QDateTime::currentDateTime()
            );


    // ------------------------------------------------------------------------
    // Если Python не присылал рабочих данных более 10 секунд,
    // считаем связь с основным Python-приложением потерянной.
    // ------------------------------------------------------------------------
    if (secondsSinceLastData > 10)
    {
        m_modBusClient->writeCoil(
            9035,
            false
            );

        qDebug()
            << "[WATCHDOG] Python activity timeout,"
            << "Coil 9035 = 0";
    }
    else
    {
        // Python недавно передавал данные — watchdog считается нормальным.
        m_modBusClient->writeCoil(
            9035,
            true
            );
    }
}












// ============================================================================
// ApplicationController::~ApplicationController
// ============================================================================
ApplicationController::~ApplicationController()
{
    // На текущем этапе внутри класса ещё нет принадлежащих ему объектов.
    //
    // Позже здесь, скорее всего, вообще не понадобится ручное удаление
    // QObject-объектов, поскольку они будут иметь ApplicationController
    // своим parent.
    qDebug() << "[APP] ApplicationController destroyed";
}


// ============================================================================
// ApplicationController::trayManager
// ============================================================================
TrayManager *ApplicationController::trayManager() const
{
    return m_trayManager;
}

// ============================================================================
// ApplicationController::modBusClient
// ============================================================================
ModBusClient *ApplicationController::modBusClient() const
{
    return m_modBusClient;
}


// ============================================================================
// ApplicationController::localServer
// ============================================================================
LocalServer *ApplicationController::localServer() const
{
    return m_localServer;
}


// ============================================================================
// ApplicationController::pollTimer
// ============================================================================
QTimer *ApplicationController::pollTimer() const
{
    return m_pollTimer;
}


// ============================================================================
// ApplicationController::watchdogTimer
// ============================================================================
QTimer *ApplicationController::watchdogTimer() const
{
    return m_watchdogTimer;
}

// ============================================================================
// ApplicationController::shutdown
//
// Выполняет контролируемую остановку рабочих компонентов приложения.
//
// ВАЖНО:
// этот метод вызывается ещё при работающем Qt event loop через aboutToQuit().
// Поэтому мы не ждём, пока QObject начнут разрушаться сами, а заранее
// переводим приложение в безопасное остановленное состояние.
// ============================================================================
void ApplicationController::shutdown()
{
    // ------------------------------------------------------------------------
    // Защита от повторного shutdown.
    //
    // Если метод уже выполнялся, повторно ничего не делаем.
    // ------------------------------------------------------------------------
    if (m_shuttingDown) {
        return;
    }

    m_shuttingDown = true;

    qDebug() << "[APP] Shutdown started";


    // ------------------------------------------------------------------------
    // 1. Останавливаем периодический polling.
    //
    // После начала shutdown новые операции с ПЛК запускаться не должны.
    // ------------------------------------------------------------------------
    if (m_pollTimer && m_pollTimer->isActive()) {
        m_pollTimer->stop();

        qDebug() << "[APP] Poll timer stopped";
    }


    // ------------------------------------------------------------------------
    // 2. Останавливаем watchdog.
    //
    // Во время завершения приложения больше не нужно контролировать
    // состояние Python-клиента.
    // ------------------------------------------------------------------------
    if (m_watchdogTimer && m_watchdogTimer->isActive()) {
        m_watchdogTimer->stop();

        qDebug() << "[APP] Watchdog timer stopped";
    }


    // ------------------------------------------------------------------------
    // 3. Намеренно отключаемся от ПЛК.
    //
    // disconnectFromPLC() теперь устанавливает m_manualDisconnect = true
    // внутри ModBusClient и останавливает его reconnect timer.
    //
    // Поэтому после этой точки ModBusClient НЕ должен пытаться
    // подключиться к ПЛК повторно.
    // ------------------------------------------------------------------------
    if (m_modBusClient) {
        m_modBusClient->disconnectFromPLC();

        qDebug() << "[APP] PLC disconnected";
    }


    // ------------------------------------------------------------------------
    // 4. Останавливаем локальный TCP-сервер.
    //
    // После этого новые Python-клиенты подключаться уже не смогут.
    // ------------------------------------------------------------------------
    if (m_localServer) {
        m_localServer->stop();

        qDebug() << "[APP] LocalServer stopped";
    }


    qDebug() << "[APP] Shutdown completed";
}


// ============================================================================
// ApplicationController::updateTrayStatus
//
// Централизованно формирует состояние tray-индикатора.
//
// ApplicationController является единственным местом,
// которое знает одновременно о:
//   - ModBusClient;
//   - LocalServer;
//   - TrayManager.
//
// Поэтому глобальная функция updateTrayStatus() в main.cpp
// больше не требуется.
// ============================================================================

void ApplicationController::updateTrayStatus()
{
    // ------------------------------------------------------------------------
    // Защита от вызова в момент, когда один из компонентов ещё не создан
    // или уже находится в процессе завершения.
    // ------------------------------------------------------------------------
    if (!m_trayManager ||
        !m_modBusClient ||
        !m_localServer)
    {
        return;
    }


    // ------------------------------------------------------------------------
    // Левая половина tray-индикатора:
    // состояние соединения с ПЛК.
    // ------------------------------------------------------------------------
    const bool plcConnected =
        m_modBusClient->isConnected();


    // ------------------------------------------------------------------------
    // Правая половина tray-индикатора:
    // наличие подключённого Python-клиента.
    //
    // Пока считаем Python подключённым, если LocalServer видит
    // хотя бы одного активного клиента.
    //
    // Позже, когда добавим heartbeat/watchdog-состояние,
    // здесь будет учитываться уже не только TCP-соединение,
    // но и фактическое состояние Python-приложения.
    // ------------------------------------------------------------------------
    const bool pythonConnected =
        (m_localServer->clientsCount() > 0);


    // ------------------------------------------------------------------------
    // TrayManager ничего не вычисляет сам.
    // Он только отображает переданные ему состояния.
    // ------------------------------------------------------------------------
    m_trayManager->updateStatus(
        plcConnected,
        pythonConnected
        );
}
