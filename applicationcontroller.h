#ifndef APPLICATIONCONTROLLER_H
#define APPLICATIONCONTROLLER_H

#include <QObject>
#include <QDateTime>
#include <QJsonObject>


class TrayManager;
class ModBusClient;
class LocalServer;
class QTimer;


// ============================================================================
// ApplicationController
//
// Central application controller.
//
// Responsibilities:
//   - owns the main application objects;
//   - controls their lifetime;
//   - connects LocalServer, ModBusClient and TrayManager;
//   - handles commands received from Python;
//   - performs periodic PLC polling;
//   - monitors Python activity;
//   - manages controlled application shutdown.
//
// QObject child ownership is used for all managed QObject instances.
// ============================================================================
class ApplicationController : public QObject
{
    Q_OBJECT


public:

    // ------------------------------------------------------------------------
    // Creates all main application components.
    // ------------------------------------------------------------------------
    explicit ApplicationController(QObject *parent = nullptr);

    ~ApplicationController() override;


    // ------------------------------------------------------------------------
    // Access to TrayManager is still required by main.cpp to connect
    // the tray Exit command to QApplication::quit().
    // ------------------------------------------------------------------------
    TrayManager *trayManager() const;


    // ------------------------------------------------------------------------
    // Access to ModBusClient is still required by main.cpp for initial
    // PLC configuration and operational logging.
    //
    // There are no global ModBusClient pointers anymore.
    // ------------------------------------------------------------------------
    ModBusClient *modBusClient() const;


    // ------------------------------------------------------------------------
    // Access to LocalServer is still required by main.cpp to start the
    // TCP server and connect operational logging.
    //
    // There are no global LocalServer pointers anymore.
    // ------------------------------------------------------------------------
    LocalServer *localServer() const;


    // ------------------------------------------------------------------------
    // Performs controlled application shutdown.
    //
    // Shutdown order:
    //
    //   1. stop PLC polling;
    //   2. stop Python watchdog;
    //   3. disconnect from PLC;
    //   4. stop LocalServer.
    //
    // The method is idempotent.
    // ------------------------------------------------------------------------
    void shutdown();


public slots:

    // ------------------------------------------------------------------------
    // Reads tank fullness states from PLC and sends them to Python.
    //
    // Called:
    //   - periodically by m_pollTimer;
    //   - on explicit requestFullness from Python.
    // ------------------------------------------------------------------------
    void readFullnessAndSend();


private slots:

    // ------------------------------------------------------------------------
    // Updates the two-part system tray indicator.
    // ------------------------------------------------------------------------
    void updateTrayStatus();


    // ------------------------------------------------------------------------
    // Checks Python application activity and updates watchdog Coil 9035.
    // ------------------------------------------------------------------------
    void checkWatchdog();


    // ------------------------------------------------------------------------
    // Updates the timestamp of the last working command received from Python.
    // ------------------------------------------------------------------------
    void onPythonDataReceived();


    // ------------------------------------------------------------------------
    // Writes ZB tank data received from Python to PLC.
    // ------------------------------------------------------------------------
    void writeZBToPLC(
        int zbNumber,
        const QJsonObject &data
        );


    // ------------------------------------------------------------------------
    // Writes CZ wall detector data received from Python to PLC.
    // ------------------------------------------------------------------------
    void writeCZToPLC(
        int czNumber,
        const QJsonObject &data
        );


    // ------------------------------------------------------------------------
    // Replaces the serial number of a ZB device in PLC.
    // ------------------------------------------------------------------------
    void replaceDevice(
        int zbNumber,
        int newSN
        );


private:

    // System tray manager.
    TrayManager *m_trayManager;


    // Modbus TCP client used for communication with PLC.
    ModBusClient *m_modBusClient;


    // Local TCP server used for communication with Python.
    LocalServer *m_localServer;


    // Periodic PLC polling timer.
    QTimer *m_pollTimer;


    // Python activity watchdog timer.
    QTimer *m_watchdogTimer;


    // Prevents repeated shutdown execution.
    bool m_shuttingDown;


    // Time of the last working command received from Python.
    QDateTime m_lastPythonDataTime;


    // ------------------------------------------------------------------------
    // Cached state of the Python watchdog coil in PLC.
    //
    // m_watchdogStateKnown:
    //     false - we do not know whether PLC currently contains the required
    //             watchdog value. In this case the next watchdog check must
    //             write the value to PLC.
    //
    // m_lastWatchdogState:
    //     last successfully written state:
    //         true  -> Python is active
    //         false -> Python activity timeout
    //
    // This prevents writing the same value to Coil 9035 every time the
    // watchdog timer fires.
    // ------------------------------------------------------------------------
    bool m_watchdogStateKnown;
    bool m_lastWatchdogState;


};


#endif // APPLICATIONCONTROLLER_H
