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
    // According to the current PLC register map, watchdog state is stored in
    // one 16-bit Holding Register:
    //
    //     Holding Register 9035
    //
    // Values:
    //
    //     0 = Python application is inactive
    //     1 = Python application is active
    //
    // After PLC disconnect/reconnect we cannot assume that Holding Register
    // 9035 still contains the previously written value. The PLC may have
    // restarted or its internal state may have changed.
    //
    // Therefore the cached watchdog state is marked as unknown whenever the
    // PLC connection changes.
    //
    // On the next watchdog timer cycle checkWatchdog() will write the current
    // required state to Holding Register 9035 again.
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
    // Watchdog state is stored in Holding Register 9035 as int16:
    //
    //     0 = Python application is inactive
    //     1 = Python application is active
    //
    // Holding Register 9035 is written only when:
    //
    //     - watchdog state changes 1 -> 0;
    //     - watchdog state changes 0 -> 1;
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
// Writes ZB tank data received from Python to PLC.
//
// The method handles:
//
//     - device serial number;
//     - temperature;
//     - PAED;
//     - isotope identification;
//     - isotope activity;
//     - isotope concentration;
//     - high-sensitivity channel state;
//     - low-sensitivity channel state;
//     - measurement validity;
//     - device connection state;
//     - ready-to-drain state.
//
// Logical PLC parameters are stored as one 16-bit Holding Register each.
//
// The Bridge does not invert logical values received from Python.
// Values are written exactly according to the PLC register map:
//
//     0 -> logical state 0
//     1 -> logical state 1
//
// Only values 0 and 1 are accepted for logical registers.
// Invalid logical values are ignored and reported in the application log.
//
// ModBusClient applies the configured address offset internally, therefore
// all addresses in this method are logical addresses from the PLC register
// table.
// ============================================================================
void ApplicationController::writeZBToPLC(
    int zbNumber,
    const QJsonObject &data
    )
{
    // ------------------------------------------------------------------------
    // Do not start new PLC operations during application shutdown.
    // ------------------------------------------------------------------------
    if (m_shuttingDown)
    {
        return;
    }


    // ------------------------------------------------------------------------
    // PLC must be connected before any write operation.
    // ------------------------------------------------------------------------
    if (!m_modBusClient ||
        !m_modBusClient->isConnected())
    {
        qWarning()
        << "[ERROR] No connection to PLC";

        return;
    }


    // ========================================================================
    // 1. PLC REGISTER MAP
    // ========================================================================

    // ------------------------------------------------------------------------
    // Main ZB parameters.
    // ------------------------------------------------------------------------
    const quint16 baseSN       = 9071;
    const quint16 baseTemp     = 9089;
    const quint16 basePaed     = 9107;


    // ------------------------------------------------------------------------
    // Isotope 1.
    // ------------------------------------------------------------------------
    const quint16 baseIso1Name = 9125;
    const quint16 baseIso1Act  = 9134;
    const quint16 baseIso1Conc = 9311;


    // ------------------------------------------------------------------------
    // Isotope 2.
    // ------------------------------------------------------------------------
    const quint16 baseIso2Name = 9152;
    const quint16 baseIso2Act  = 9161;
    const quint16 baseIso2Conc = 9329;


    // ------------------------------------------------------------------------
    // Isotope 3.
    // ------------------------------------------------------------------------
    const quint16 baseIso3Name = 9179;
    const quint16 baseIso3Act  = 9188;
    const quint16 baseIso3Conc = 9347;


    // ------------------------------------------------------------------------
    // Logical ZB parameters.
    //
    // Each ZB occupies exactly one 16-bit Holding Register in every group.
    //
    // ZB1 uses the base address.
    // ZB2 uses base + 1.
    // ...
    // ZB9 uses base + 8.
    // ------------------------------------------------------------------------
    const quint16 baseHighSensitivity = 9224;
    const quint16 baseLowSensitivity  = 9233;
    const quint16 baseValid           = 9242;
    const quint16 baseDeviceConnection = 9251;
    const quint16 baseReadyToDrain    = 9269;


    // ------------------------------------------------------------------------
    // Additional isotope addresses used only by ZB3.
    // ------------------------------------------------------------------------
    const quint16 iso4Name = 9365;
    const quint16 iso4Act  = 9367;
    const quint16 iso4Conc = 9371;

    const quint16 iso5Name = 9366;
    const quint16 iso5Act  = 9369;
    const quint16 iso5Conc = 9373;


    // ------------------------------------------------------------------------
    // Convert ZB number 1..9 to zero-based index 0..8.
    // ------------------------------------------------------------------------
    const int idx =
        zbNumber - 1;


    // ========================================================================
    // 2. SERIAL NUMBER
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
    // 3. TEMPERATURE
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
    // 4. PAED
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
    // 5. LOGICAL ZB PARAMETERS
    //
    // According to the current PLC specification all logical values are
    // transferred through Holding Registers as int16 values 0 or 1.
    //
    // No Coil write is used here.
    // ========================================================================

    // ------------------------------------------------------------------------
    // Local helper for writing one logical parameter.
    //
    // The helper:
    //   1. checks whether the field exists in incoming JSON;
    //   2. verifies that the JSON value is numeric;
    //   3. accepts only integer value 0 or 1;
    //   4. writes one Holding Register.
    //
    // Invalid values are not written to PLC.
    // ------------------------------------------------------------------------
    const auto writeLogicalRegister =
        [this, &data, zbNumber](
            const char *jsonField,
            quint16 address
            )
    {
        // ----------------------------------------------------------------
        // Missing field means that this parameter was simply not supplied
        // by Python in the current command.
        // ----------------------------------------------------------------
        if (!data.contains(jsonField))
        {
            return;
        }


        const QJsonValue jsonValue =
            data.value(jsonField);


        // ----------------------------------------------------------------
        // Logical PLC values must be numeric.
        // ----------------------------------------------------------------
        if (!jsonValue.isDouble())
        {
            qWarning()
            << "[APP] Invalid logical value for ZB"
            << zbNumber
            << ", field"
            << jsonField
            << ": numeric value 0 or 1 expected";

            return;
        }


        const double rawValue =
            jsonValue.toDouble();


        // ----------------------------------------------------------------
        // Accept only exact integer values 0 and 1.
        //
        // Values such as:
        //
        //     -1
        //      2
        //      0.5
        //
        // must never be sent to a logical PLC register.
        // ----------------------------------------------------------------
        if (rawValue != 0.0 &&
            rawValue != 1.0)
        {
            qWarning()
            << "[APP] Invalid logical value for ZB"
            << zbNumber
            << ", field"
            << jsonField
            << ":"
            << rawValue
            << "(expected 0 or 1)";

            return;
        }


        const quint16 plcValue =
            static_cast<quint16>(
                rawValue
                );


        // ----------------------------------------------------------------
        // Write one 16-bit Holding Register.
        //
        // ModBusClient applies m_addressOffset internally.
        // ----------------------------------------------------------------
        const bool writeSuccessful =
            m_modBusClient->writeHoldingRegister(
                address,
                plcValue
                );


        if (!writeSuccessful)
        {
            qWarning()
            << "[APP] Failed to write logical parameter for ZB"
            << zbNumber
            << ", field"
            << jsonField
            << ", address"
            << address;
        }
    };


    // ------------------------------------------------------------------------
    // High-sensitivity channel state.
    //
    // PLC addresses:
    //
    //     ZB1 -> 9224
    //     ...
    //     ZB9 -> 9232
    //
    // The value is written exactly as received from Python.
    // ------------------------------------------------------------------------
    writeLogicalRegister(
        "high_sensitivity",
        static_cast<quint16>(
            baseHighSensitivity + idx
            )
        );


    // ------------------------------------------------------------------------
    // Low-sensitivity channel state.
    //
    // PLC addresses:
    //
    //     ZB1 -> 9233
    //     ...
    //     ZB9 -> 9241
    // ------------------------------------------------------------------------
    writeLogicalRegister(
        "low_sensitivity",
        static_cast<quint16>(
            baseLowSensitivity + idx
            )
        );


    // ------------------------------------------------------------------------
    // Measurement validity state.
    //
    // PLC addresses:
    //
    //     ZB1 -> 9242
    //     ...
    //     ZB9 -> 9250
    //
    // IMPORTANT:
    // The Bridge does not reinterpret or invert this value.
    // ------------------------------------------------------------------------
    writeLogicalRegister(
        "valid",
        static_cast<quint16>(
            baseValid + idx
            )
        );


    // ------------------------------------------------------------------------
    // Device connection state.
    //
    // PLC addresses:
    //
    //     ZB1 -> 9251
    //     ...
    //     ZB9 -> 9259
    //
    // PLC meaning:
    //
    //     0 = disconnected
    //     1 = connected
    // ------------------------------------------------------------------------
    writeLogicalRegister(
        "device_connection",
        static_cast<quint16>(
            baseDeviceConnection + idx
            )
        );


    // ------------------------------------------------------------------------
    // Ready-to-drain state.
    //
    // PLC addresses:
    //
    //     ZB1 -> 9269
    //     ...
    //     ZB9 -> 9277
    //
    // PLC meaning:
    //
    //     0 = not ready
    //     1 = ready
    // ------------------------------------------------------------------------
    writeLogicalRegister(
        "ready_to_drain",
        static_cast<quint16>(
            baseReadyToDrain + idx
            )
        );


    // ========================================================================
    // 6. ISOTOPES
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


    // ========================================================================
    // 7. OPERATION COMPLETED
    // ========================================================================

    qDebug()
        << QString(
               "[WRITE] Tank ZB%1: data written"
               ).arg(
                   zbNumber
                   );
}











// ============================================================================
// ApplicationController::writeCZToPLC
//
// Writes CZ wall-detector data received from Python to PLC.
//
// The method handles:
//
//     - device serial number;
//     - temperature;
//     - PAED;
//     - high-sensitivity channel state;
//     - low-sensitivity channel state;
//     - measurement validity;
//     - device connection state.
//
// Logical PLC parameters are stored as one 16-bit Holding Register each.
//
// The Bridge does not invert logical values received from Python.
// Values are written exactly according to the PLC register map:
//
//     0 -> logical state 0
//     1 -> logical state 1
//
// Only values 0 and 1 are accepted for logical registers.
// Invalid logical values are ignored and reported in the application log.
//
// ModBusClient applies the configured address offset internally, therefore
// all addresses in this method are logical addresses from the PLC register
// table.
// ============================================================================
void ApplicationController::writeCZToPLC(
    int czNumber,
    const QJsonObject &data
    )
{
    // ------------------------------------------------------------------------
    // Do not start new PLC operations during application shutdown.
    // ------------------------------------------------------------------------
    if (m_shuttingDown)
    {
        return;
    }


    // ------------------------------------------------------------------------
    // PLC must be connected before any write operation.
    // ------------------------------------------------------------------------
    if (!m_modBusClient ||
        !m_modBusClient->isConnected())
    {
        qWarning()
        << "[ERROR] No connection to PLC";

        return;
    }


    // ========================================================================
    // 1. PLC REGISTER MAP
    // ========================================================================

    // ------------------------------------------------------------------------
    // Main CZ parameters.
    // ------------------------------------------------------------------------
    const quint16 baseSN   = 9281;
    const quint16 baseTemp = 9287;
    const quint16 basePaed = 9293;


    // ------------------------------------------------------------------------
    // Logical CZ parameters.
    //
    // Each CZ device occupies exactly one 16-bit Holding Register
    // in every logical parameter group.
    //
    // CZ1 uses the base address.
    // CZ2 uses base + 1.
    // CZ3 uses base + 2.
    // ------------------------------------------------------------------------
    const quint16 baseHighSensitivity  = 9299;
    const quint16 baseLowSensitivity   = 9302;
    const quint16 baseValid            = 9305;
    const quint16 baseDeviceConnection = 9308;


    // ------------------------------------------------------------------------
    // Convert CZ number 1..3 to zero-based index 0..2.
    // ------------------------------------------------------------------------
    const int idx =
        czNumber - 1;


    // ========================================================================
    // 2. SERIAL NUMBER
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
    // 3. TEMPERATURE
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
    // 4. PAED
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
    // 5. LOGICAL CZ PARAMETERS
    //
    // According to the current PLC specification all logical values are
    // transferred through Holding Registers as int16 values 0 or 1.
    //
    // No Coil write is used here.
    // ========================================================================

    // ------------------------------------------------------------------------
    // Local helper for writing one logical parameter.
    //
    // The helper:
    //   1. checks whether the field exists in incoming JSON;
    //   2. verifies that the JSON value is numeric;
    //   3. accepts only integer value 0 or 1;
    //   4. writes one Holding Register.
    //
    // Invalid values are not written to PLC.
    // ------------------------------------------------------------------------
    const auto writeLogicalRegister =
        [this, &data, czNumber](
            const char *jsonField,
            quint16 address
            )
    {
        // ----------------------------------------------------------------
        // Missing field means that this parameter was not supplied
        // by Python in the current command.
        // ----------------------------------------------------------------
        if (!data.contains(jsonField))
        {
            return;
        }


        const QJsonValue jsonValue =
            data.value(jsonField);


        // ----------------------------------------------------------------
        // Logical PLC values must be numeric.
        // ----------------------------------------------------------------
        if (!jsonValue.isDouble())
        {
            qWarning()
            << "[APP] Invalid logical value for CZ"
            << czNumber
            << ", field"
            << jsonField
            << ": numeric value 0 or 1 expected";

            return;
        }


        const double rawValue =
            jsonValue.toDouble();


        // ----------------------------------------------------------------
        // Accept only exact integer values 0 and 1.
        //
        // Values such as:
        //
        //     -1
        //      2
        //      0.5
        //
        // must never be sent to a logical PLC register.
        // ----------------------------------------------------------------
        if (rawValue != 0.0 &&
            rawValue != 1.0)
        {
            qWarning()
            << "[APP] Invalid logical value for CZ"
            << czNumber
            << ", field"
            << jsonField
            << ":"
            << rawValue
            << "(expected 0 or 1)";

            return;
        }


        const quint16 plcValue =
            static_cast<quint16>(
                rawValue
                );


        // ----------------------------------------------------------------
        // Write one 16-bit Holding Register.
        //
        // ModBusClient applies m_addressOffset internally.
        // ----------------------------------------------------------------
        const bool writeSuccessful =
            m_modBusClient->writeHoldingRegister(
                address,
                plcValue
                );


        if (!writeSuccessful)
        {
            qWarning()
            << "[APP] Failed to write logical parameter for CZ"
            << czNumber
            << ", field"
            << jsonField
            << ", address"
            << address;
        }
    };


    // ------------------------------------------------------------------------
    // High-sensitivity channel state.
    //
    // PLC addresses:
    //
    //     CZ1 -> 9299
    //     CZ2 -> 9300
    //     CZ3 -> 9301
    // ------------------------------------------------------------------------
    writeLogicalRegister(
        "high_sensitivity",
        static_cast<quint16>(
            baseHighSensitivity + idx
            )
        );


    // ------------------------------------------------------------------------
    // Low-sensitivity channel state.
    //
    // PLC addresses:
    //
    //     CZ1 -> 9302
    //     CZ2 -> 9303
    //     CZ3 -> 9304
    // ------------------------------------------------------------------------
    writeLogicalRegister(
        "low_sensitivity",
        static_cast<quint16>(
            baseLowSensitivity + idx
            )
        );


    // ------------------------------------------------------------------------
    // Measurement validity state.
    //
    // PLC addresses:
    //
    //     CZ1 -> 9305
    //     CZ2 -> 9306
    //     CZ3 -> 9307
    //
    // IMPORTANT:
    // The Bridge does not reinterpret or invert this value.
    // ------------------------------------------------------------------------
    writeLogicalRegister(
        "valid",
        static_cast<quint16>(
            baseValid + idx
            )
        );


    // ------------------------------------------------------------------------
    // Device connection state.
    //
    // PLC addresses:
    //
    //     CZ1 -> 9308
    //     CZ2 -> 9309
    //     CZ3 -> 9310
    //
    // PLC meaning:
    //
    //     0 = disconnected
    //     1 = connected
    // ------------------------------------------------------------------------
    writeLogicalRegister(
        "device_connection",
        static_cast<quint16>(
            baseDeviceConnection + idx
            )
        );


    // ========================================================================
    // 6. OPERATION COMPLETED
    // ========================================================================

    qDebug()
        << QString(
               "[WRITE] Wall detector CZ%1: data written"
               ).arg(
                   czNumber
                   );
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
// Checks Python activity and controls PLC watchdog Holding Register 9035.
//
// The method is called every 10 seconds by m_watchdogTimer.
//
// Python is considered inactive if no working command has been received
// for more than 5 minutes.
//
// PLC representation:
//
//     1 = Python application is active
//     0 = Python application is inactive
//
// IMPORTANT:
// Holding Register 9035 is NOT written on every watchdog timer cycle.
// A Modbus write is performed only when:
//     1. the watchdog state changes;
//     2. the watchdog state is unknown, for example after PLC reconnect.
//
// The watchdog state is stored in PLC as one 16-bit Holding Register.
// Boolean state is converted to int16 value 0 or 1 before writing.
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
    // The watchdog Holding Register cannot be written while PLC is
    // disconnected.
    //
    // In this case the cached state is marked as unknown.
    // After PLC reconnect the next watchdog check will force synchronization
    // of Holding Register 9035 with the current Python activity state.
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
    //     true  -> Python is active  -> Holding Register 9035 = 1
    //     false -> Python timeout    -> Holding Register 9035 = 0
    //
    // The internal watchdog logic remains boolean.
    // Only the Modbus representation changes from Coil to int16 value 0/1
    // stored in one Holding Register.
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
    // Convert the internal boolean state to the PLC representation:
    //
    //     false -> int16 value 0
    //     true  -> int16 value 1
    //
    // Address 9035 remains unchanged.
    // ModBusClient::writeHoldingRegister() applies the configured address
    // offset internally.
    // ------------------------------------------------------------------------
    const quint16 watchdogValue =
        requiredWatchdogState ? 1 : 0;


    // ------------------------------------------------------------------------
    // The watchdog state has changed, or its current PLC state is unknown.
    //
    // Write one 16-bit Holding Register using Modbus Function Code 0x06.
    // ------------------------------------------------------------------------
    const bool writeSuccessful =
        m_modBusClient->writeHoldingRegister(
            9035,
            watchdogValue
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
            << "[WATCHDOG] Failed to update Holding Register 9035";

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
        << "[WATCHDOG] Python active, Holding Register 9035 = 1";
    }
    else
    {
        qDebug()
        << "[WATCHDOG] Python activity timeout, Holding Register 9035 = 0";
    }
}











// ============================================================================
// ApplicationController::readFullnessAndSend
//
// Reads the fullness state of all nine ZB tanks from PLC and sends the result
// to the connected Python application.
//
// According to the current PLC register map, tank fullness is stored in:
//
//     ZB1 -> Holding Register 9260
//     ZB2 -> Holding Register 9261
//     ZB3 -> Holding Register 9262
//     ZB4 -> Holding Register 9263
//     ZB5 -> Holding Register 9264
//     ZB6 -> Holding Register 9265
//     ZB7 -> Holding Register 9266
//     ZB8 -> Holding Register 9267
//     ZB9 -> Holding Register 9268
//
// Each logical value is stored as one 16-bit Holding Register:
//
//     0 = tank is empty
//     1 = tank is full
//
// IMPORTANT:
// The Python protocol remains unchanged. Python still receives:
//
//     "fullness": 0
//
// or:
//
//     "fullness": 1
//
// ModBusClient::readHoldingRegister() applies the configured address offset
// internally, therefore the logical PLC addresses above are used here without
// any manual address correction.
// ============================================================================
void ApplicationController::readFullnessAndSend()
{
    // ------------------------------------------------------------------------
    // Do not start new PLC operations during application shutdown.
    // ------------------------------------------------------------------------
    if (m_shuttingDown)
    {
        return;
    }


    // ------------------------------------------------------------------------
    // Both ModBusClient and LocalServer are required for this operation.
    // ------------------------------------------------------------------------
    if (!m_modBusClient ||
        !m_localServer)
    {
        return;
    }


    // ------------------------------------------------------------------------
    // If PLC is temporarily unavailable, skip this polling cycle.
    //
    // ModBusClient is responsible for restoring the PLC connection.
    // ------------------------------------------------------------------------
    if (!m_modBusClient->isConnected())
    {
        return;
    }


    QJsonArray zbArray;


    // ------------------------------------------------------------------------
    // Current PLC register map:
    //
    //     ZB1 -> Holding Register 9260
    //     ...
    //     ZB9 -> Holding Register 9268
    //
    // Each register contains one int16 logical value:
    //
    //     0 = empty
    //     1 = full
    // ------------------------------------------------------------------------
    constexpr quint16 FULLNESS_BASE_ADDRESS = 9260;


    for (int i = 1; i <= 9; ++i)
    {
        const quint16 address =
            static_cast<quint16>(
                FULLNESS_BASE_ADDRESS + (i - 1)
                );


        // --------------------------------------------------------------------
        // PLC stores the logical state as one 16-bit Holding Register.
        // --------------------------------------------------------------------
        quint16 fullnessValue = 0;


        if (m_modBusClient->readHoldingRegister(
                address,
                fullnessValue
                ))
        {
            // ----------------------------------------------------------------
            // Only values 0 and 1 are valid according to the PLC register map.
            //
            // Do not silently convert an unexpected PLC value such as 2, 100
            // or 65535 into a valid logical state. Such a value indicates an
            // incorrect PLC state or register configuration.
            // ----------------------------------------------------------------
            if (fullnessValue > 1)
            {
                qWarning()
                << "[APP] Invalid fullness value for ZB"
                << i
                << ":"
                << fullnessValue
                << "(expected 0 or 1)";

                continue;
            }


            QJsonObject zb;

            zb["number"] = i;

            // ----------------------------------------------------------------
            // Preserve the existing Python protocol.
            //
            // The PLC int16 value is forwarded as JSON integer 0 or 1.
            // ----------------------------------------------------------------
            zb["fullness"] =
                static_cast<int>(
                    fullnessValue
                    );


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
    // Preserve the existing Bridge -> Python JSON protocol:
    //
    // {
    //     "type": "read",
    //     "data": {
    //         "zb": [
    //             { "number": 1, "fullness": 0 },
    //             ...
    //         ]
    //     }
    // }
    // ------------------------------------------------------------------------
    QJsonObject response;

    response["type"] = "read";


    QJsonObject data;

    data["zb"] = zbArray;


    response["data"] = data;


    // ------------------------------------------------------------------------
    // Send the current tank states to all connected Python clients.
    // ------------------------------------------------------------------------
    m_localServer->broadcast(
        response
        );
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
