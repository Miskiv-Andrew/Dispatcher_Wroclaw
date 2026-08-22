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
    // Пока ApplicationController ничего не делает.
    //
    // На следующих шагах сюда постепенно будут перенесены:
    //   - ModBusClient;
    //   - LocalServer;
    //   - TrayManager;
    //   - polling timer;
    //   - watchdog timer;
    //   - управляющая логика приложения.
    //
    // Сейчас важно только убедиться, что новый класс корректно
    // подключён к проекту и не влияет на существующее поведение.
    qDebug() << "[APP] ApplicationController created";

    // ------------------------------------------------------------------------
    // Создаём TrayManager.
    //
    // Передаём this как parent, поэтому TrayManager принадлежит
    // ApplicationController и будет автоматически уничтожен вместе с ним.
    // ------------------------------------------------------------------------
    m_trayManager = new TrayManager(this);

    qDebug() << "[APP] TrayManager created";


    // ------------------------------------------------------------------------
    // Создаём ModBusClient.
    //
    // Теперь его жизненный цикл контролируется ApplicationController.
    // На этом этапе настройки подключения и сама логика соединения
    // всё ещё остаются в main.cpp.
    // ------------------------------------------------------------------------
    m_modBusClient = new ModBusClient(this);

    qDebug() << "[APP] ModBusClient created";

    // ------------------------------------------------------------------------
    // Создаём LocalServer.
    //
    // Теперь его жизненный цикл также контролируется ApplicationController.
    //
    // На текущем шаге порт, сигналы и обработка данных всё ещё настраиваются
    // в main.cpp — мы меняем только владение объектом.
    // ------------------------------------------------------------------------
    m_localServer = new LocalServer(this);

    qDebug() << "[APP] LocalServer created";

    // ------------------------------------------------------------------------
    // Создаём оба таймера как дочерние объекты ApplicationController.
    //
    // Теперь у них есть однозначный владелец и понятное время жизни.
    // Пока интервалы и подключения timeout оставляем в main.cpp.
    // ------------------------------------------------------------------------
    m_pollTimer = new QTimer(this);
    m_watchdogTimer = new QTimer(this);

    qDebug() << "[APP] Poll timer created";
    qDebug() << "[APP] Watchdog timer created";


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
