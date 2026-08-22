#include "applicationcontroller.h"

#include <QDebug>

// ============================================================================
// ApplicationController::ApplicationController
// ============================================================================
ApplicationController::ApplicationController(QObject *parent)
    : QObject(parent)
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
