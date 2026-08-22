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
{
    qDebug() << "[APP] ApplicationController created";


    // ========================================================================
    // 1. СОЗДАНИЕ ОСНОВНЫХ ОБЪЕКТОВ
    //
    // Сначала обязательно создаём ВСЕ объекты.
    // Только после этого ниже выполняем connect().
    // ========================================================================

    // ------------------------------------------------------------------------
    // TrayManager
    // ------------------------------------------------------------------------
    m_trayManager = new TrayManager(this);

    qDebug() << "[APP] TrayManager created";


    // ------------------------------------------------------------------------
    // ModBusClient
    // ------------------------------------------------------------------------
    m_modBusClient = new ModBusClient(this);

    qDebug() << "[APP] ModBusClient created";


    // ------------------------------------------------------------------------
    // LocalServer
    // ------------------------------------------------------------------------
    m_localServer = new LocalServer(this);

    qDebug() << "[APP] LocalServer created";


    // ------------------------------------------------------------------------
    // Рабочие таймеры приложения.
    // ------------------------------------------------------------------------
    m_pollTimer = new QTimer(this);
    m_watchdogTimer = new QTimer(this);

    qDebug() << "[APP] Poll timer created";
    qDebug() << "[APP] Watchdog timer created";


    // ========================================================================
    // 2. ПОДКЛЮЧЕНИЕ СИГНАЛОВ
    //
    // К этому моменту m_trayManager, m_modBusClient и m_localServer
    // гарантированно существуют.
    // ========================================================================

    // ------------------------------------------------------------------------
    // Изменение состояния ПЛК.
    // ------------------------------------------------------------------------
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


    // ------------------------------------------------------------------------
    // Подключение Python-клиента.
    //
    // Lambda пока оставлена специально для диагностики.
    // По сообщению в консоли сразу увидим приход сигнала.
    // ------------------------------------------------------------------------
    connect(
        m_localServer,
        &LocalServer::clientConnected,
        this,
        [this]()
        {
            qDebug()
            << "[APP] LocalServer signal: clientConnected";

            updateTrayStatus();
        }
        );


    // ------------------------------------------------------------------------
    // Отключение Python-клиента.
    // ------------------------------------------------------------------------
    connect(
        m_localServer,
        &LocalServer::clientDisconnected,
        this,
        [this]()
        {
            qDebug()
            << "[APP] LocalServer signal: clientDisconnected";

            updateTrayStatus();
        }
        );


    // ========================================================================
    // 3. НАЧАЛЬНОЕ СОСТОЯНИЕ TRAY
    //
    // Вызываем только ПОСЛЕ создания всех объектов и connect().
    // ========================================================================

    updateTrayStatus();
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
    // Проверяем, что все необходимые объекты уже созданы.
    //
    // В нормальной работе они всегда существуют, но такая проверка защищает
    // от случайного вызова метода на этапе инициализации или завершения.
    // ------------------------------------------------------------------------
    if (!m_trayManager ||
        !m_modBusClient ||
        !m_localServer)
    {
        qDebug()
        << "[APP] updateTrayStatus skipped:"
        << "TrayManager =" << (m_trayManager != nullptr)
        << "ModBusClient =" << (m_modBusClient != nullptr)
        << "LocalServer =" << (m_localServer != nullptr);

        return;
    }


    // ------------------------------------------------------------------------
    // Получаем текущее состояние соединения с ПЛК.
    // ------------------------------------------------------------------------
    const bool plcConnected =
        m_modBusClient->isConnected();


    // ------------------------------------------------------------------------
    // Получаем количество подключённых Python-клиентов.
    //
    // Пока используем существующую логику проекта:
    // если клиентов больше нуля — считаем Python подключённым.
    // ------------------------------------------------------------------------
    const int pythonClientsCount =
        m_localServer->clientsCount();


    const bool pythonConnected =
        (pythonClientsCount > 0);


    // ------------------------------------------------------------------------
    // ВРЕМЕННАЯ ДИАГНОСТИКА.
    //
    // По этой строке мы увидим:
    //   1. был ли вообще вызван updateTrayStatus();
    //   2. какое состояние ПЛК он видит;
    //   3. сколько Python-клиентов видит LocalServer;
    //   4. какое состояние Python будет передано в TrayManager.
    // ------------------------------------------------------------------------
    qDebug()
        << "[APP] updateTrayStatus:"
        << "PLC =" << plcConnected
        << "Python =" << pythonConnected
        << "clientsCount =" << pythonClientsCount;


    // ------------------------------------------------------------------------
    // Передаём два независимых состояния в TrayManager:
    //
    //   левая половина  -> ПЛК;
    //   правая половина -> Python.
    // ------------------------------------------------------------------------
    m_trayManager->updateStatus(
        plcConnected,
        pythonConnected
        );
}
