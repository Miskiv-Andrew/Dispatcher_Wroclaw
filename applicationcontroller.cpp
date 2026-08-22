#include "applicationcontroller.h"

#include <QDebug>

#include "traymanager.h"

// ============================================================================
// ApplicationController::ApplicationController
// ============================================================================
ApplicationController::ApplicationController(QObject *parent)
    : QObject(parent)
    , m_trayManager(nullptr)
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
