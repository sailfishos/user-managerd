QT -= gui
QT += dbus

CONFIG += c++11 console link_pkgconfig
CONFIG -= app_bundle

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
    LICENSE \
    README.md \
    dbus-org.sailfishos.usermanager.service \
    org.sailfishos.usermanager.conf \
    org.sailfishos.usermanager.service \
    org.sailfishos.usermanager.xml \
    rpm/user-managerd.spec \
    home-sailfish_guest.mount \
    userdel_local.sh

target.path = /usr/bin/

systemd.files = dbus-$${DBUS_SERVICE_NAME}.service home-sailfish_guest.mount guest_disable_suw.service
systemd.path = /usr/lib/systemd/system/

service.files = $${DBUS_SERVICE_NAME}.service
service.path = /usr/share/dbus-1/system-services/

conf.files = $${DBUS_SERVICE_NAME}.conf
conf.path = /etc/dbus-1/system.d/

pkgconfig.files = sailfishusermanager.pc
pkgconfig.path = $$[QT_INSTALL_LIBS]/pkgconfig

include.files = sailfishusermanagerinterface.h $${DBUS_SERVICE_NAME}.xml
include.path = /usr/include/sailfishusermanager

userdel.files = userdel_local.sh
userdel.path = /usr/sbin/

INSTALLS += target systemd service conf pkgconfig include userdel
