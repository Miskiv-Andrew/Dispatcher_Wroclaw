#include "applicationcontroller.h"

#include <QDebug>
#include <QTimer>
#include <QJsonArray>
#include <QJsonObject>

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
    , m_watchdogStateKnown(false)
    , m_lastWatchdogState(false)
{
    qDebug() << "[APP] ApplicationController created";


    // ========================================================================
    // 1. CREATE APPLICATION COMPONENTS
    //
    // All objects are created before any signal connections are made.
    // This prevents connect() from receiving nullptr objects.
    // ========================================================================

    m_trayManager = new TrayManager(this);

    qDebug() << "[APP] TrayManager created";


    m_modBusClient = new ModBusClient(this);

    qDebug() << "[APP] ModBusClient created";


    m_localServer = new LocalServer(this);

    qDebug() << "[APP] LocalServer created";


    m_pollTimer = new QTimer(this);

    qDebug() << "[APP] Poll timer created";


    m_watchdogTimer = new QTimer(this);

    qDebug() << "[APP] Watchdog timer created";


    // ========================================================================
    // 2. PLC STATE -> SYSTEM TRAY
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
    // 3. PLC CONNECTION STATE -> WATCHDOG SYNCHRONIZATION
    //
    // The watchdog state stored in ApplicationController is only a cache of
    // the last value successfully written to PLC.
    //
    // After PLC disconnect/reconnect we cannot assume that Coil 9035 still
    // contains the previously written value. The PLC may have restarted or
    // its internal state may have changed.
    //
    // Therefore the cached watchdog state is marked as unknown whenever the
    // PLC connection changes.
    //
    // On the next watchdog timer cycle checkWatchdog() will write the current
    // required state to Coil 9035 again.
    // ========================================================================

    connect(
        m_modBusClient,
        &ModBusClient::connected,
        this,
        [this]()
        {
            m_watchdogStateKnown = false;
        }
        );


    connect(
        m_modBusClient,
        &ModBusClient::disconnected,
        this,
        [this]()
        {
            m_watchdogStateKnown = false;
        }
        );


    // ========================================================================
    // 4. PYTHON CONNECTION STATE -> SYSTEM TRAY
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
    // 5. PYTHON COMMANDS -> PLC
    //
    // The complete Python -> PLC command path belongs to
    // ApplicationController.
    //
    // main.cpp does not handle these commands.
    // ========================================================================

    connect(
        m_localServer,
        &LocalServer::writeZB,
        this,
        &ApplicationController::writeZBToPLC
        );


    connect(
        m_localServer,
        &LocalServer::writeCZ,
        this,
        &ApplicationController::writeCZToPLC
        );


    connect(
        m_localServer,
        &LocalServer::replaceDevice,
        this,
        &ApplicationController::replaceDevice
        );


    // ========================================================================
    // 6. EXPLICIT FULLNESS REQUEST FROM PYTHON
    // ========================================================================

    connect(
        m_localServer,
        &LocalServer::requestFullness,
        this,
        &ApplicationController::readFullnessAndSend
        );


    // ========================================================================
    // 7. PYTHON ACTIVITY MONITORING
    //
    // Receiving a working command means that the Python application is
    // active.
    //
    // onPythonDataReceived() updates m_lastPythonDataTime.
    // checkWatchdog() later uses this timestamp to determine whether Python
    // is still active.
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
    // 8. PERIODIC PLC POLLING
    //
    // Fullness states of ZB tanks are read every 20 seconds.
    //
    // The physical process is very slow, so more frequent polling would
    // generate unnecessary Modbus traffic without providing useful
    // additional information.
    // ========================================================================

    m_pollTimer->setInterval(20000);


    connect(
        m_pollTimer,
        &QTimer::timeout,
        this,
        &ApplicationController::readFullnessAndSend
        );


    m_pollTimer->start();


    // ========================================================================
    // 9. PYTHON WATCHDOG
    //
    // The watchdog CHECK runs every 10 seconds.
    //
    // This timer does not define the Python inactivity timeout itself.
    // It only determines how often checkWatchdog() checks Python activity.
    //
    // The inactivity timeout inside checkWatchdog() is:
    //
    //     300000 ms = 5 minutes
    //
    // Coil 9035 is written only when:
    //
    //     - watchdog state changes ON -> OFF;
    //     - watchdog state changes OFF -> ON;
    //     - PLC reconnects and the cached state becomes unknown.
    //
    // Therefore the timer can run every 10 seconds without generating a
    // Modbus write every 10 seconds.
    // ========================================================================

    m_watchdogTimer->setInterval(10000);


    connect(
        m_watchdogTimer,
        &QTimer::timeout,
        this,
        &ApplicationController::checkWatchdog
        );


    m_watchdogTimer->start();


    // ========================================================================
    // 10. INITIAL SYSTEM TRAY STATE
    // ========================================================================

    updateTrayStatus();
}


// ============================================================================
// ApplicationController::~ApplicationController
// ============================================================================
ApplicationController::~ApplicationController()
{
    // All managed QObject instances have this controller as their parent.
    // Qt destroys them automatically.

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
// ApplicationController::writeZBToPLC
//
// Writes tank data received from Python to PLC.
//
// The PLC register map is intentionally preserved exactly as it existed
// in main.cpp. Register-map refactoring must be done separately after
// functional verification.
// ============================================================================
void ApplicationController::writeZBToPLC(
    int zbNumber,
    const QJsonObject &data
    )
{
    // ------------------------------------------------------------------------
    // Do not start new PLC operations during application shutdown.
    // ------------------------------------------------------------------------
    if (m_shuttingDown) {
        return;
    }


    // ------------------------------------------------------------------------
    // PLC must be connected before any write operation.
    // ------------------------------------------------------------------------
    if (!m_modBusClient ||
        !m_modBusClient->isConnected())
    {
        qWarning() << "[ERROR] No connection to PLC";

        return;
    }


    // ------------------------------------------------------------------------
    // Base addresses for ZB1..ZB9.
    // ------------------------------------------------------------------------
    const quint16 baseSN       = 9071;
    const quint16 baseTemp     = 9089;
    const quint16 basePaed     = 9107;

    const quint16 baseIso1Name = 9125;
    const quint16 baseIso1Act  = 9134;
    const quint16 baseIso1Conc = 9311;

    const quint16 baseIso2Name = 9152;
    const quint16 baseIso2Act  = 9161;
    const quint16 baseIso2Conc = 9329;

    const quint16 baseIso3Name = 9179;
    const quint16 baseIso3Act  = 9188;
    const quint16 baseIso3Conc = 9347;


    // ------------------------------------------------------------------------
    // Additional isotope addresses used only by ZB3.
    // ------------------------------------------------------------------------
    const quint16 iso4Name = 9365;
    const quint16 iso4Act  = 9367;
    const quint16 iso4Conc = 9371;

    const quint16 iso5Name = 9366;
    const quint16 iso5Act  = 9369;
    const quint16 iso5Conc = 9373;


    const int idx = zbNumber - 1;


    // ========================================================================
    // SERIAL NUMBER
    // ========================================================================

    if (data.contains("sn"))
    {
        const quint32 sn =
            static_cast<quint32>(
                data.value("sn").toInt()
                );


        m_modBusClient->writeHoldingRegister32Int(
            baseSN + idx * 2,
            sn
            );
    }


    // ========================================================================
    // TEMPERATURE
    // ========================================================================

    if (data.contains("temperature"))
    {
        const float temperature =
            static_cast<float>(
                data.value("temperature").toDouble()
                );


        m_modBusClient->writeHoldingRegister32Float(
            baseTemp + idx * 2,
            temperature
            );
    }


    // ========================================================================
    // PAED
    // ========================================================================

    if (data.contains("paed"))
    {
        const float paed =
            static_cast<float>(
                data.value("paed").toDouble()
                );


        m_modBusClient->writeHoldingRegister32Float(
            basePaed + idx * 2,
            paed
            );
    }


    // ========================================================================
    // ISOTOPES
    // ========================================================================

    const QJsonArray isotopes =
        data.value("isotopes").toArray();


    for (const QJsonValue &value : isotopes)
    {
        const QJsonObject isotope =
            value.toObject();


        const int id =
            isotope.value("id").toInt();


        const int name =
            isotope.value("name").toInt();


        const float activity =
            static_cast<float>(
                isotope.value("activity").toDouble()
                );


        const float concentration =
            static_cast<float>(
                isotope.value("concentration").toDouble()
                );


        // --------------------------------------------------------------------
        // ZB3 supports isotopes 1..5.
        // --------------------------------------------------------------------
        if (zbNumber == 3)
        {
            if (id == 1)
            {
                m_modBusClient->writeHoldingRegister(
                    baseIso1Name + idx,
                    name
                    );

                m_modBusClient->writeHoldingRegister32Float(
                    baseIso1Act + idx * 2,
                    activity
                    );

                m_modBusClient->writeHoldingRegister32Float(
                    baseIso1Conc + idx * 2,
                    concentration
                    );
            }
            else if (id == 2)
            {
                m_modBusClient->writeHoldingRegister(
                    baseIso2Name + idx,
                    name
                    );

                m_modBusClient->writeHoldingRegister32Float(
                    baseIso2Act + idx * 2,
                    activity
                    );

                m_modBusClient->writeHoldingRegister32Float(
                    baseIso2Conc + idx * 2,
                    concentration
                    );
            }
            else if (id == 3)
            {
                m_modBusClient->writeHoldingRegister(
                    baseIso3Name + idx,
                    name
                    );

                m_modBusClient->writeHoldingRegister32Float(
                    baseIso3Act + idx * 2,
                    activity
                    );

                m_modBusClient->writeHoldingRegister32Float(
                    baseIso3Conc + idx * 2,
                    concentration
                    );
            }
            else if (id == 4)
            {
                m_modBusClient->writeHoldingRegister(
                    iso4Name,
                    name
                    );

                m_modBusClient->writeHoldingRegister32Float(
                    iso4Act,
                    activity
                    );

                m_modBusClient->writeHoldingRegister32Float(
                    iso4Conc,
                    concentration
                    );
            }
            else if (id == 5)
            {
                m_modBusClient->writeHoldingRegister(
                    iso5Name,
                    name
                    );

                m_modBusClient->writeHoldingRegister32Float(
                    iso5Act,
                    activity
                    );

                m_modBusClient->writeHoldingRegister32Float(
                    iso5Conc,
                    concentration
                    );
            }
        }
        else
        {
            // ----------------------------------------------------------------
            // All other tanks support isotopes 1..3.
            // ----------------------------------------------------------------

            if (id == 1)
            {
                m_modBusClient->writeHoldingRegister(
                    baseIso1Name + idx,
                    name
                    );

                m_modBusClient->writeHoldingRegister32Float(
                    baseIso1Act + idx * 2,
                    activity
                    );

                m_modBusClient->writeHoldingRegister32Float(
                    baseIso1Conc + idx * 2,
                    concentration
                    );
            }
            else if (id == 2)
            {
                m_modBusClient->writeHoldingRegister(
                    baseIso2Name + idx,
                    name
                    );

                m_modBusClient->writeHoldingRegister32Float(
                    baseIso2Act + idx * 2,
                    activity
                    );

                m_modBusClient->writeHoldingRegister32Float(
                    baseIso2Conc + idx * 2,
                    concentration
                    );
            }
            else if (id == 3)
            {
                m_modBusClient->writeHoldingRegister(
                    baseIso3Name + idx,
                    name
                    );

                m_modBusClient->writeHoldingRegister32Float(
                    baseIso3Act + idx * 2,
                    activity
                    );

                m_modBusClient->writeHoldingRegister32Float(
                    baseIso3Conc + idx * 2,
                    concentration
                    );
            }
        }
    }


    qDebug()
        << QString(
               "[WRITE] Tank ZB%1: data written"
               ).arg(zbNumber);
}


// ============================================================================
// ApplicationController::writeCZToPLC
//
// Writes wall detector data received from Python to PLC.
// ============================================================================
void ApplicationController::writeCZToPLC(
    int czNumber,
    const QJsonObject &data
    )
{
    if (m_shuttingDown) {
        return;
    }


    if (!m_modBusClient ||
        !m_modBusClient->isConnected())
    {
        qWarning() << "[ERROR] No connection to PLC";

        return;
    }


    // ------------------------------------------------------------------------
    // Base PLC addresses for CZ devices.
    // ------------------------------------------------------------------------
    const quint16 baseSN   = 9281;
    const quint16 baseTemp = 9287;
    const quint16 basePaed = 9293;


    const int idx =
        czNumber - 1;


    // ========================================================================
    // SERIAL NUMBER
    // ========================================================================

    if (data.contains("sn"))
    {
        const quint32 sn =
            static_cast<quint32>(
                data.value("sn").toInt()
                );


        m_modBusClient->writeHoldingRegister32Int(
            baseSN + idx * 2,
            sn
            );
    }


    // ========================================================================
    // TEMPERATURE
    // ========================================================================

    if (data.contains("temperature"))
    {
        const float temperature =
            static_cast<float>(
                data.value("temperature").toDouble()
                );


        m_modBusClient->writeHoldingRegister32Float(
            baseTemp + idx * 2,
            temperature
            );
    }


    // ========================================================================
    // PAED
    // ========================================================================

    if (data.contains("paed"))
    {
        const float paed =
            static_cast<float>(
                data.value("paed").toDouble()
                );


        m_modBusClient->writeHoldingRegister32Float(
            basePaed + idx * 2,
            paed
            );
    }


    qDebug()
        << QString(
               "[WRITE] Wall detector CZ%1: data written"
               ).arg(czNumber);
}


// ============================================================================
// ApplicationController::replaceDevice
//
// Writes a new ZB device serial number to PLC.
// ============================================================================
void ApplicationController::replaceDevice(
    int zbNumber,
    int newSN
    )
{
    if (m_shuttingDown) {
        return;
    }


    if (!m_modBusClient ||
        !m_modBusClient->isConnected())
    {
        qWarning() << "[ERROR] No connection to PLC";

        return;
    }


    const quint16 baseSN = 9044;

    const int idx =
        zbNumber - 1;


    m_modBusClient->writeHoldingRegister32Int(
        baseSN + idx * 2,
        static_cast<quint32>(newSN)
        );


    qDebug()
        << QString(
               "[REPLACE] Tank ZB%1: new SN = %2"
               )
               .arg(zbNumber)
               .arg(newSN);
}


// ============================================================================
// ApplicationController::onPythonDataReceived
// ============================================================================
void ApplicationController::onPythonDataReceived()
{
    // Any working command received from Python means that the Python
    // application is active.

    m_lastPythonDataTime =
        QDateTime::currentDateTime();
}


// ============================================================================
// ApplicationController::checkWatchdog
//
// Checks Python activity and controls PLC watchdog Coil 9035.
//
// The method is called every 10 seconds by m_watchdogTimer.
//
// Python is considered inactive if no working command has been received
// for more than 5 minutes.
//
// IMPORTANT:
// Coil 9035 is NOT written on every watchdog timer cycle.
// A Modbus write is performed only when:
//     1. the watchdog state changes;
//     2. the watchdog state is unknown, for example after PLC reconnect.
// ============================================================================
void ApplicationController::checkWatchdog()
{
    // ------------------------------------------------------------------------
    // No watchdog activity is allowed during application shutdown.
    // ------------------------------------------------------------------------
    if (m_shuttingDown)
    {
        return;
    }


    // ------------------------------------------------------------------------
    // The watchdog Coil cannot be written while PLC is disconnected.
    //
    // In this case the cached state is marked as unknown.
    // After PLC reconnect the next watchdog check will force synchronization
    // of Coil 9035 with the current Python activity state.
    // ------------------------------------------------------------------------
    if (!m_modBusClient ||
        !m_modBusClient->isConnected())
    {
        m_watchdogStateKnown = false;
        return;
    }


    // ------------------------------------------------------------------------
    // Operational Python inactivity timeout:
    //
    //     300000 ms = 300 seconds = 5 minutes.
    // ------------------------------------------------------------------------
    constexpr qint64 WATCHDOG_TIMEOUT_MS = 300000;


    // ------------------------------------------------------------------------
    // Calculate how long Python has been inactive.
    // ------------------------------------------------------------------------
    const qint64 millisecondsSinceLastData =
        m_lastPythonDataTime.msecsTo(
            QDateTime::currentDateTime()
            );


    // ------------------------------------------------------------------------
    // Required watchdog state:
    //
    //     true  -> Python is active  -> Coil 9035 = ON
    //     false -> Python timeout    -> Coil 9035 = OFF
    // ------------------------------------------------------------------------
    const bool requiredWatchdogState =
        millisecondsSinceLastData <= WATCHDOG_TIMEOUT_MS;


    // ------------------------------------------------------------------------
    // If the required state is already known to be written to PLC,
    // there is nothing to do.
    //
    // m_watchdogTimer will continue checking Python activity every
    // 10 seconds, but no unnecessary Modbus request will be generated.
    // ------------------------------------------------------------------------
    if (m_watchdogStateKnown &&
        m_lastWatchdogState == requiredWatchdogState)
    {
        return;
    }


    // ------------------------------------------------------------------------
    // The watchdog state has changed, or its current PLC state is unknown.
    //
    // Write the required value to Coil 9035.
    // ------------------------------------------------------------------------
    const bool writeSuccessful =
        m_modBusClient->writeCoil(
            9035,
            requiredWatchdogState
            );


    // ------------------------------------------------------------------------
    // If the Modbus write failed, do not update the cached state.
    //
    // This allows the next watchdog cycle to try again.
    // ------------------------------------------------------------------------
    if (!writeSuccessful)
    {
        m_watchdogStateKnown = false;

        qWarning()
            << "[WATCHDOG] Failed to update Coil 9035";

        return;
    }


    // ------------------------------------------------------------------------
    // The value was successfully written to PLC.
    //
    // Remember it so that identical values are not written repeatedly.
    // ------------------------------------------------------------------------
    m_lastWatchdogState = requiredWatchdogState;
    m_watchdogStateKnown = true;


    // ------------------------------------------------------------------------
    // Log only actual watchdog state changes/synchronizations.
    // ------------------------------------------------------------------------
    if (requiredWatchdogState)
    {
        qDebug()
        << "[WATCHDOG] Python active, Coil 9035 = 1";
    }
    else
    {
        qDebug()
        << "[WATCHDOG] Python activity timeout, Coil 9035 = 0";
    }
}














// ============================================================================
// ApplicationController::readFullnessAndSend
//
// Reads the fullness state of nine ZB tanks and sends the result to Python.
// ============================================================================
void ApplicationController::readFullnessAndSend()
{
    if (m_shuttingDown) {
        return;
    }


    if (!m_modBusClient ||
        !m_localServer)
    {
        return;
    }


    // ------------------------------------------------------------------------
    // If PLC is temporarily unavailable, skip this polling cycle.
    //
    // ModBusClient is responsible for reconnecting.
    // ------------------------------------------------------------------------
    if (!m_modBusClient->isConnected()) {
        return;
    }


    QJsonArray zbArray;


    // ------------------------------------------------------------------------
    // Existing PLC address map:
    //
    //   ZB1 -> Coil 10260
    //   ...
    //   ZB9 -> Coil 10268
    // ------------------------------------------------------------------------
    for (int i = 1; i <= 9; ++i)
    {
        const quint16 address =
            static_cast<quint16>(
                10260 + (i - 1)
                );


        bool full = false;


        if (m_modBusClient->readCoil(
                address,
                full
                ))
        {
            QJsonObject zb;

            zb["number"] = i;
            zb["fullness"] = full ? 1 : 0;

            zbArray.append(zb);
        }
        else
        {
            qWarning()
            << "[APP] Failed to read fullness state for ZB"
            << i;
        }
    }


    // ------------------------------------------------------------------------
    // Preserve the existing Python protocol format.
    // ------------------------------------------------------------------------
    QJsonObject response;

    response["type"] = "read";


    QJsonObject data;

    data["zb"] = zbArray;


    response["data"] = data;


    m_localServer->broadcast(response);
}



















// ============================================================================
// ApplicationController::shutdown
//
// Performs controlled shutdown while the Qt event loop is still alive.
// ============================================================================
void ApplicationController::shutdown()
{
    // ------------------------------------------------------------------------
    // Protect against repeated shutdown calls.
    // ------------------------------------------------------------------------
    if (m_shuttingDown) {
        return;
    }


    m_shuttingDown = true;


    qDebug() << "[APP] Shutdown started";


    // ========================================================================
    // 1. STOP PERIODIC PLC POLLING
    // ========================================================================

    if (m_pollTimer &&
        m_pollTimer->isActive())
    {
        m_pollTimer->stop();

        qDebug() << "[APP] Poll timer stopped";
    }


    // ========================================================================
    // 2. STOP PYTHON WATCHDOG
    // ========================================================================

    if (m_watchdogTimer &&
        m_watchdogTimer->isActive())
    {
        m_watchdogTimer->stop();

        qDebug() << "[APP] Watchdog timer stopped";
    }


    // ========================================================================
    // 3. DISCONNECT FROM PLC
    //
    // disconnectFromPLC() performs intentional disconnect and prevents
    // the reconnect mechanism from starting again.
    // ========================================================================

    if (m_modBusClient)
    {
        m_modBusClient->disconnectFromPLC();

        qDebug() << "[APP] PLC disconnected";
    }


    // ========================================================================
    // 4. STOP LOCAL PYTHON SERVER
    // ========================================================================

    if (m_localServer)
    {
        m_localServer->stop();

        qDebug() << "[APP] LocalServer stopped";
    }


    qDebug() << "[APP] Shutdown completed";
}


// ============================================================================
// ApplicationController::updateTrayStatus
// ============================================================================
void ApplicationController::updateTrayStatus()
{
    if (!m_trayManager ||
        !m_modBusClient ||
        !m_localServer)
    {
        return;
    }


    // ------------------------------------------------------------------------
    // Left half:
    // PLC connection state.
    // ------------------------------------------------------------------------
    const bool plcConnected =
        m_modBusClient->isConnected();


    // ------------------------------------------------------------------------
    // Right half:
    // Python TCP connection state.
    //
    // This is intentionally independent from watchdog activity.
    // ------------------------------------------------------------------------
    const bool pythonConnected =
        (m_localServer->clientsCount() > 0);


    m_trayManager->updateStatus(
        plcConnected,
        pythonConnected
        );
}
