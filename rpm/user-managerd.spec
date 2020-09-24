Name: user-managerd
Version: 0.5.17
Release: 1
Summary: Sailfish user manager daemon
License: BSD
Source0: %{name}-%{version}.tar.gz
URL: https://github.com/sailfishos/user-management-daemon.git
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5DBus)
BuildRequires: pkgconfig(libuser)
BuildRequires: pkgconfig(sailfishaccesscontrol) >= 0.0.3
BuildRequires: pkgconfig(libsystemd)
BuildRequires: pkgconfig(mce-qt5)
BuildRequires: sed
Requires: systemd
Requires: sailfish-setup >= 0.3.0
Requires: shadow-utils
%description
%{summary}.

%package devel
Summary: Sailfish user manager daemon development files
Requires: user-managerd

%description devel
%{summary}.

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_unitdir}/dbus-org.sailfishos.usermanager.service
%{_unitdir}/home-sailfish_guest.mount
%{_unitdir}/*/home-sailfish_guest.mount
%{_unitdir}/guest_disable_suw.service
%{_unitdir}/*/guest_disable_suw.service
%{_datadir}/dbus-1/system-services/*.service
%{_sysconfdir}/dbus-1/system.d/*.conf
%{_sbindir}/userdel_local.sh
%{_datadir}/user-managerd/remove.d

%files devel
%{_prefix}/include/sailfishusermanager
%{_libdir}/pkgconfig/sailfishusermanager.pc

%prep
%setup -q -n %{name}-%{version}
export VERSION=`echo %{version} | sed 's/+.*//'`
sed -i "s/^Version: .*/Version: "${VERSION}"/" sailfishusermanager.pc

%build
mkdir -p build && cd build
%qmake5 DEFINES+='TARGET_ARCH=\\\"\"%{_target_cpu}\"\\\"' -recursive ..
make %{?_smp_mflags}

%install
make -C build INSTALL_ROOT=%{buildroot} install
mkdir -p %{buildroot}%{_datadir}/user-managerd/remove.d
mkdir -p %{buildroot}%{_unitdir}/user@105000.service.wants/
ln -s ../home-sailfish_guest.mount %{buildroot}%{_unitdir}/user@105000.service.wants/
mkdir -p %{buildroot}%{_unitdir}/autologin@105000.service.wants/
ln -s ../home-sailfish_guest.mount %{buildroot}%{_unitdir}/autologin@105000.service.wants/
ln -s ../guest_disable_suw.service %{buildroot}%{_unitdir}/autologin@105000.service.wants/

%pre
systemctl stop dbus-org.sailfishos.usermanager.service || :

%post
systemctl daemon-reload
sed -i 's/^\#USERDEL_CMD.*/USERDEL_CMD \/usr\/sbin\/userdel_local.sh/' /etc/login.defs

%postun
if [ $1 -eq 0 ]; then
    sed -i 's/^USERDEL_CMD.*/\#USERDEL_CMD/' /etc/login.defs
fi
