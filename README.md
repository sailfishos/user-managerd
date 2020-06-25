# User Management Daemon

Daemon for handling Sailfish OS device users.

## Brief Description

This daemon provides D-Bus API for adding, removing and editing additional
Sailfish OS device users. The main system user is created during the first boot
of the device. This user can then use this API to add additional users which do
only have access to some of the available functionality and services.

## API

[Interface definition can be found in org.sailfishos.usermanager.xml file.](file://org.sailfishos.usermanager.xml)

## Operation

Systemd launches the daemon automatically when a call to the interface is made.
In the case of error an D-Bus error is responded. The daemon quits after one minute if
there are no more incoming messages.

### Quota

By default new users are set to have quota for /home partition and they may
reserve at most 20 % of blocks or 2 GB whichever is smaller. Hard limit is set
to 120 % of the soft limit but soft limit will become hard limit after grace
period (default is 7 days). Quota limits are set only on user creation and can
be adjusted with setquota afterwards. If kernel does not support quota or it is
not enabled on /home partition the limits are not set.
