#ifndef MODBUS_ADDRESSES_H
#define MODBUS_ADDRESSES_H

#include <QString>

// ------------------------------------------------------------
// Базовые адреса для цистерн (ZB1…ZB9)
// ------------------------------------------------------------
constexpr quint16 ADDR_SN_ZB_START         = 9071;
constexpr quint16 ADDR_TEMP_ZB_START       = 9089;
constexpr quint16 ADDR_PAED_ZB_START       = 9107;
constexpr quint16 ADDR_ISOTOPE1_NAME_START = 9125;
constexpr quint16 ADDR_ISOTOPE1_ACT_START  = 9134;
constexpr quint16 ADDR_ISOTOPE1_CONC_START = 9311;
constexpr quint16 ADDR_ISOTOPE2_NAME_START = 9152;
constexpr quint16 ADDR_ISOTOPE2_ACT_START  = 9161;
constexpr quint16 ADDR_ISOTOPE2_CONC_START = 9329;
constexpr quint16 ADDR_ISOTOPE3_NAME_START = 9179;
constexpr quint16 ADDR_ISOTOPE3_ACT_START  = 9188;
constexpr quint16 ADDR_ISOTOPE3_CONC_START = 9347;

// ------------------------------------------------------------
// Дополнительные адреса для резервной цистерны ZB3 (изотопы 4 и 5)
// ------------------------------------------------------------
constexpr quint16 ADDR_ISOTOPE4_NAME = 9365;
constexpr quint16 ADDR_ISOTOPE4_ACT  = 9367;
constexpr quint16 ADDR_ISOTOPE4_CONC = 9371;
constexpr quint16 ADDR_ISOTOPE5_NAME = 9366;
constexpr quint16 ADDR_ISOTOPE5_ACT  = 9369;
constexpr quint16 ADDR_ISOTOPE5_CONC = 9373;

// ------------------------------------------------------------
// Coils для цистерн
// ------------------------------------------------------------
constexpr quint16 ADDR_COIL_CONNECTION         = 9035;   // общий для всей системы
constexpr quint16 ADDR_COIL_HIGH_SENS_START    = 9224;   // ZB1…ZB9
constexpr quint16 ADDR_COIL_LOW_SENS_START     = 9233;
constexpr quint16 ADDR_COIL_VALID_START        = 9242;
constexpr quint16 ADDR_COIL_DEVICE_CONN_START  = 9251;
constexpr quint16 ADDR_COIL_READY_TO_DRAIN     = 9269;

// ------------------------------------------------------------
// Discrete Input для цистерн (состояние бака)
// ------------------------------------------------------------
constexpr quint16 ADDR_DI_FULLNESS_START       = 9260;

// ------------------------------------------------------------
// Базовые адреса для настенных детекторов (CZ1…CZ3)
// ------------------------------------------------------------
constexpr quint16 ADDR_SN_CZ_START   = 9281;
constexpr quint16 ADDR_TEMP_CZ_START = 9287;
constexpr quint16 ADDR_PAED_CZ_START = 9293;

// ------------------------------------------------------------
// Coils для настенных детекторов
// ------------------------------------------------------------
constexpr quint16 ADDR_COIL_HIGH_SENS_CZ_START = 9299;
constexpr quint16 ADDR_COIL_LOW_SENS_CZ_START  = 9302;
constexpr quint16 ADDR_COIL_VALID_CZ_START     = 9305;
constexpr quint16 ADDR_COIL_DEVICE_CONN_CZ_START = 9308;

// ------------------------------------------------------------
// Вспомогательные функции
// ------------------------------------------------------------

/**
 * @brief Возвращает адрес регистра для цистерны по базовому адресу и индексу
 * @param baseAddress Базовый адрес (например, ADDR_SN_ZB_START)
 * @param zbIndex Номер цистерны (0 = ZB1, 1 = ZB2, …, 8 = ZB9)
 * @param step Шаг между адресами (обычно 2 для 32-битных, 1 для 16-битных)
 * @return Адрес регистра для указанной цистерны
 */
quint16 getZBAddress(quint16 baseAddress, int zbIndex, int step = 2);

/**
 * @brief Возвращает адрес регистра для конкретного изотопа в цистерне
 * @param zbIndex Номер цистерны (0…8)
 * @param isotopeNum Номер изотопа (1…5)
 * @param paramType Тип параметра: "name", "activity", "concentration"
 * @return Адрес регистра
 */
quint16 getIsotopeAddress(int zbIndex, int isotopeNum, const QString &paramType);

/**
 * @brief Возвращает адрес Coil для цистерны
 * @param coilType Тип Coil: "high_sens", "low_sens", "valid", "device_connection", "ready_to_drain"
 * @param zbIndex Номер цистерны (0…8)
 * @return Адрес Coil
 */
quint16 getZBCoilAddress(const QString &coilType, int zbIndex);

/**
 * @brief Возвращает адрес Discrete Input (состояние бака) для цистерны
 * @param zbIndex Номер цистерны (0…8)
 * @return Адрес Discrete Input
 */
quint16 getZBDiscreteInputAddress(int zbIndex);

/**
 * @brief Возвращает адрес регистра для настенного детектора
 * @param baseAddress Базовый адрес (например, ADDR_SN_CZ_START)
 * @param czIndex Номер детектора (0 = CZ1, 1 = CZ2, 2 = CZ3)
 * @param step Шаг между адресами (обычно 2 для 32-битных)
 * @return Адрес регистра
 */
quint16 getCZAddress(quint16 baseAddress, int czIndex, int step = 2);

/**
 * @brief Возвращает адрес Coil для настенного детектора
 * @param coilType Тип Coil: "high_sens", "low_sens", "valid", "device_connection"
 * @param czIndex Номер детектора (0…2)
 * @return Адрес Coil
 */
quint16 getCZCoilAddress(const QString &coilType, int czIndex);

/**
 * @brief Проверяет, является ли цистерна резервной (ZB3)
 * @param zbIndex Номер цистерны
 * @return true, если цистерна резервная
 */
bool isReserveCistern(int zbIndex);

#endif // MODBUS_ADDRESSES_H