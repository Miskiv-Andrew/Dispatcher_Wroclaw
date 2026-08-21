#include "modbus_addresses.h"
#include <QDebug>

// ------------------------------------------------------------
// Реализация вспомогательных функций
// ------------------------------------------------------------

quint16 getZBAddress(quint16 baseAddress, int zbIndex, int step)
{
    if (zbIndex < 1 || zbIndex > 9) {
        qWarning() << "Invalid zbIndex:" << zbIndex;
        return baseAddress;
    }
    return baseAddress + zbIndex * step;
}

quint16 getIsotopeAddress(int zbIndex, int isotopeNum, const QString &paramType)
{
    // Для резервной цистерны (ZB3) — отдельные адреса для изотопов 4 и 5
    if (zbIndex == 2) {  // ZB3 имеет индекс 2
        if (isotopeNum == 4) {
            if (paramType == "name") return ADDR_ISOTOPE4_NAME;
            if (paramType == "activity") return ADDR_ISOTOPE4_ACT;
            if (paramType == "concentration") return ADDR_ISOTOPE4_CONC;
        }
        if (isotopeNum == 5) {
            if (paramType == "name") return ADDR_ISOTOPE5_NAME;
            if (paramType == "activity") return ADDR_ISOTOPE5_ACT;
            if (paramType == "concentration") return ADDR_ISOTOPE5_CONC;
        }
    }

    // Для остальных цистерн и изотопов 1-3
    quint16 baseAddress = 0;
    int step = 2;
    if (paramType == "name") {
        if (isotopeNum == 1) baseAddress = ADDR_ISOTOPE1_NAME_START;
        else if (isotopeNum == 2) baseAddress = ADDR_ISOTOPE2_NAME_START;
        else if (isotopeNum == 3) baseAddress = ADDR_ISOTOPE3_NAME_START;
        else return 0;
        step = 1;
    } else if (paramType == "activity") {
        if (isotopeNum == 1) baseAddress = ADDR_ISOTOPE1_ACT_START;
        else if (isotopeNum == 2) baseAddress = ADDR_ISOTOPE2_ACT_START;
        else if (isotopeNum == 3) baseAddress = ADDR_ISOTOPE3_ACT_START;
        else return 0;
        step = 2;
    } else if (paramType == "concentration") {
        if (isotopeNum == 1) baseAddress = ADDR_ISOTOPE1_CONC_START;
        else if (isotopeNum == 2) baseAddress = ADDR_ISOTOPE2_CONC_START;
        else if (isotopeNum == 3) baseAddress = ADDR_ISOTOPE3_CONC_START;
        else return 0;
        step = 2;
    } else {
        return 0;
    }

    return getZBAddress(baseAddress, zbIndex, step);
}

quint16 getZBCoilAddress(const QString &coilType, int zbIndex)
{
    if (zbIndex < 0 || zbIndex > 8) {
        qWarning() << "Invalid zbIndex:" << zbIndex;
        return 0;
    }

    quint16 baseAddress = 0;
    if (coilType == "high_sens") baseAddress = ADDR_COIL_HIGH_SENS_START;
    else if (coilType == "low_sens") baseAddress = ADDR_COIL_LOW_SENS_START;
    else if (coilType == "valid") baseAddress = ADDR_COIL_VALID_START;
    else if (coilType == "device_connection") baseAddress = ADDR_COIL_DEVICE_CONN_START;
    else if (coilType == "ready_to_drain") baseAddress = ADDR_COIL_READY_TO_DRAIN;
    else {
        qWarning() << "Unknown coilType:" << coilType;
        return 0;
    }

    return baseAddress + zbIndex;
}

quint16 getZBDiscreteInputAddress(int zbIndex)
{
    if (zbIndex < 0 || zbIndex > 8) {
        qWarning() << "Invalid zbIndex:" << zbIndex;
        return ADDR_DI_FULLNESS_START;
    }
    return ADDR_DI_FULLNESS_START + zbIndex;
}

quint16 getCZAddress(quint16 baseAddress, int czIndex, int step)
{
    if (czIndex < 0 || czIndex > 2) {
        qWarning() << "Invalid czIndex:" << czIndex;
        return baseAddress;
    }
    return baseAddress + czIndex * step;
}

quint16 getCZCoilAddress(const QString &coilType, int czIndex)
{
    if (czIndex < 0 || czIndex > 2) {
        qWarning() << "Invalid czIndex:" << czIndex;
        return 0;
    }

    quint16 baseAddress = 0;
    if (coilType == "high_sens") baseAddress = ADDR_COIL_HIGH_SENS_CZ_START;
    else if (coilType == "low_sens") baseAddress = ADDR_COIL_LOW_SENS_CZ_START;
    else if (coilType == "valid") baseAddress = ADDR_COIL_VALID_CZ_START;
    else if (coilType == "device_connection") baseAddress = ADDR_COIL_DEVICE_CONN_CZ_START;
    else {
        qWarning() << "Unknown coilType:" << coilType;
        return 0;
    }

    return baseAddress + czIndex;
}

bool isReserveCistern(int zbIndex)
{
    return (zbIndex == 2);  // ZB3
}