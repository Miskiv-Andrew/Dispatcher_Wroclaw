#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

#include "applicationcontroller.h"
#include "modbus_client.h"
#include "localserver.h"
#include "traymanager.h"


// ============================================================================
// logMessage
//
// Writes operational messages both to Qt debug output and to the
// ModBusBridgeService.log file.
// ============================================================================
void logMessage(const QString &message)
{
    qDebug() << message;


    QFile logFile(
        "ModBusBridgeService.log"
        );


    if (logFile.open(
            QIODevice::Append |
            QIODevice::Text
            ))
    {
        QTextStream stream(&logFile);


        const QString timestamp =
            QDateTime::currentDateTime()
                .toString(
                    "yyyy-MM-dd HH:mm:ss"
                    );


        stream
            << timestamp
            << " "
            << message
            << "\n";


        logFile.close();
    }
}


// ============================================================================
// main
//
// main.cpp is now responsible only for application startup and top-level
// configuration.
//
// Business logic is handled by ApplicationController.
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(
        argc,
        argv
        );


    // ========================================================================
    // 1. APPLICATION START
    // ========================================================================

    logMessage(
        "[SYSTEM] Starting ModBusBridgeService"
        );


    // ========================================================================
    // 2. CENTRAL APPLICATION CONTROLLER
    //
    // The controller owns:
    //
    //   - TrayManager;
    //   - ModBusClient;
    //   - LocalServer;
    //   - PLC polling timer;
    //   - Python watchdog timer.
    //
    // It also handles all Python -> PLC commands.
    // ========================================================================

    ApplicationController applicationController;


    // ------------------------------------------------------------------------
    // Perform controlled shutdown before QObject destruction begins.
    // ------------------------------------------------------------------------
    QObject::connect(
        &app,
        &QCoreApplication::aboutToQuit,
        &applicationController,
        &ApplicationController::shutdown
        );


    // ========================================================================
    // 3. MODBUS TCP CLIENT CONFIGURATION
    // ========================================================================

    ModBusClient *client =
        applicationController.modBusClient();


    // ------------------------------------------------------------------------
    // Operational PLC connection logging.
    // ------------------------------------------------------------------------
    QObject::connect(
        client,
        &ModBusClient::connected,
        []()
        {
            logMessage(
                "[SYSTEM] Connection to PLC established"
                );
        }
        );


    QObject::connect(
        client,
        &ModBusClient::disconnected,
        []()
        {
            logMessage(
                "[SYSTEM] Communication with PLC lost"
                );
        }
        );


    QObject::connect(
        client,
        &ModBusClient::errorOccurred,
        [](const QString &error)
        {
            logMessage(
                "[ERROR] " + error
                );
        }
        );


    QObject::connect(
        client,
        &ModBusClient::logMessage,
        [](const QString &message)
        {
            logMessage(
                "[LOG] " + message
                );
        }
        );


    // ------------------------------------------------------------------------
    // Default PLC connection parameters.
    // ------------------------------------------------------------------------
    QString ip =
        "127.0.0.1";


    quint16 port =
        502;


    // ------------------------------------------------------------------------
    // Optional command-line parameters:
    //
    //   ModBusBridgeService.exe <IP> <PORT>
    // ------------------------------------------------------------------------
    if (argc >= 3)
    {
        ip =
            QString::fromLocal8Bit(
                argv[1]
                );


        port =
            QString::fromLocal8Bit(
                argv[2]
                ).toUShort();
    }


    client->setConnectionParams(
        ip,
        port
        );


    client->setTimeout(
        2000
        );


    client->setUnitId(
        1
        );


    client->setWordOrder(
        true
        );


    // ------------------------------------------------------------------------
    // Initial PLC connection attempt.
    //
    // If PLC is unavailable, ModBusClient will continue using its reconnect
    // mechanism.
    // ------------------------------------------------------------------------
    if (!client->connectToPLC())
    {
        logMessage(
            "[SYSTEM] Failed to initiate connection to PLC"
            );
    }


    // ========================================================================
    // 4. LOCAL PYTHON SERVER
    //
    // All Python command handling is already connected internally by
    // ApplicationController.
    //
    // main.cpp only starts the server and connects operational logging.
    // ========================================================================

    LocalServer *server =
        applicationController.localServer();


    QObject::connect(
        server,
        &LocalServer::logMessage,
        &logMessage
        );


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
        "[SYSTEM] Starting event loop"
        );


    const int result =
        app.exec();


    // ========================================================================
    // 7. APPLICATION FINISHED
    // ========================================================================

    logMessage(
        "[SYSTEM] Application stopped"
        );


    return result;
}
