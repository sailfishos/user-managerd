TEMPLATE = app
TARGET = user-managerd

QT -= gui
QT += dbus

CONFIG += c++11 console link_pkgconfig
CONFIG -= app_bundle

# Hide warnings in libuser
QMAKE_CXXFLAGS += -Wno-deprecated-declarations

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

PKGCONFIG += libuser glib-2.0 sailfishaccesscontrol libsystemd mce-qt5

DBUS_SERVICE_NAME = org.sailfishos.usermanager
dbus_interface.files = $${DBUS_SERVICE_NAME}.xml
dbus_interface.header_flags = -i sailfishusermanagerinterface.h

DBUS_ADAPTORS += dbus_interface

SOURCES += \
    libuserhelper.cpp \
    systemdmanager.cpp \
    logging.cpp \
    main.cpp \
    sailfishusermanager.cpp

HEADERS += \
    libuserhelper.h \
    systemdmanager.h \
    logging.h \
    sailfishusermanager.h \
    sailfishusermanagerinterface.h

DISTFILES += \
    sailfishusermanager.pc.in \
    org.sailfishos.usermanager.xml \
    userdel_local.sh

target.path = /usr/bin/

include.files = sailfishusermanagerinterface.h $${DBUS_SERVICE_NAME}.xml
include.path = /usr/include/sailfishusermanager

pkgconfig.files = sailfishusermanager.pc
pkgconfig.path = $$[QT_INSTALL_LIBS]/pkgconfig
pkgconfig.CONFIG = no_check_exist
pkgconfig.commands = $$QMAKE_STREAM_EDITOR \
    -e 's~@VERSION@~$$VERSION~g' \
    -e 's~@LIBDIR@~$${QT_INSTALL_LIBS}~g' \
    -e 's~@INCLUDEDIR@~$${include.path}~g' \
    sailfishusermanager.pc.in > sailfishusermanager.pc

userdel.files = userdel_local.sh
userdel.path = /usr/sbin/

INSTALLS += target pkgconfig include userdel
