TEMPLATE = aux

DBUS_SERVICE_NAME = org.sailfishos.usermanager

DISTFILES += \
    dbus-org.sailfishos.usermanager.service \
    org.sailfishos.usermanager.conf \
    org.sailfishos.usermanager.service \
    home-sailfish_guest.mount

systemd.files = \
    dbus-$${DBUS_SERVICE_NAME}.service \
    home-sailfish_guest.mount \
    guest_disable_suw.service
systemd.path = /usr/lib/systemd/system/

service.files = $${DBUS_SERVICE_NAME}.service
service.path = /usr/share/dbus-1/system-services/

conf.files = $${DBUS_SERVICE_NAME}.conf
conf.path = /etc/dbus-1/system.d/

INSTALLS += systemd service conf
