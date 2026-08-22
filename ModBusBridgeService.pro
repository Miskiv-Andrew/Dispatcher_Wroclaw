QT += core network widgets

CONFIG += c++17 cmdline

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
        applicationcontroller.cpp \
        localserver.cpp \
        main.cpp \
        modbus_addresses.cpp \
        modbus_client.cpp \
        traymanager.cpp

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

HEADERS += \
    applicationcontroller.h \
    localserver.h \
    modbus_addresses.h \
    modbus_client.h \
    traymanager.h
