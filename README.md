# User Management Daemon

Daemon for handling Sailfish OS device users.

## Brief Description:


This daemon provides D-Bus API for adding, removing and editing additional
Sailfish OS device users. The main system user is created during the first boot
of the device. This user can then use this API to add additional users which do
only have access to some of the available functionality and services.

## API:

[Interface definition: org.sailfishos.usermanager.xml](file://org.sailfishos.usermanager.xml)

## Operation:

Systemd launches the daemon automatically when a call to the interface is made.
In the case of error an D-Bus error is responded. The daemon quits after one minute if
there are no more incoming messages.
