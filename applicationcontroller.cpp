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
    // 3. PYTHON CONNECTION STATE -> SYSTEM TRAY
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
    // 4. PYTHON COMMANDS -> PLC
    //
    // The complete Python -> PLC command path now belongs to
    // ApplicationController.
    //
    // main.cpp no longer handles these commands.
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
    // 5. EXPLICIT FULLNESS REQUEST FROM PYTHON
    // ========================================================================

    connect(
        m_localServer,
        &LocalServer::requestFullness,
        this,
        &ApplicationController::readFullnessAndSend
        );


    // ========================================================================
    // 6. PYTHON ACTIVITY MONITORING
    //
    // Receiving a working command means that the Python application
    // is active.
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
    // 7. PERIODIC PLC POLLING
    // ========================================================================

    m_pollTimer->setInterval(10000);


    connect(
        m_pollTimer,
        &QTimer::timeout,
        this,
        &ApplicationController::readFullnessAndSend
        );


    m_pollTimer->start();


    // ========================================================================
    // 8. PYTHON WATCHDOG
    //
    // The check itself runs every 2 seconds.
    //
    // NOTE:
    // The current Python inactivity timeout is intentionally left at
    // 10 seconds for testing. It can later be changed to the operational
    // value (for example, 5 minutes).
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
    // 9. INITIAL SYSTEM TRAY STATE
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
// ============================================================================
void ApplicationController::checkWatchdog()
{
    // ------------------------------------------------------------------------
    // No watchdog activity is allowed during shutdown.
    // ------------------------------------------------------------------------
    if (m_shuttingDown) {
        return;
    }


    // ------------------------------------------------------------------------
    // Watchdog Coil cannot be written while PLC is disconnected.
    // ------------------------------------------------------------------------
    if (!m_modBusClient ||
        !m_modBusClient->isConnected())
    {
        return;
    }


    const qint64 secondsSinceLastData =
        m_lastPythonDataTime.secsTo(
            QDateTime::currentDateTime()
            );


    // ------------------------------------------------------------------------
    // TEST VALUE:
    //
    // 10 seconds is currently used to make watchdog testing convenient.
    //
    // The operational value can later be changed to approximately
    // five minutes.
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
        m_modBusClient->writeCoil(
            9035,
            true
            );
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
