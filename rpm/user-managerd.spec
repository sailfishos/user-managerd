Name: user-managerd
Version: 0.8.0
Release: 1
Summary: Sailfish User Manager Daemon
License: BSD
Source0: %{name}-%{version}.tar.gz
URL: https://github.com/sailfishos/user-managerd/
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5DBus)
BuildRequires: pkgconfig(libuser)
BuildRequires: pkgconfig(sailfishaccesscontrol) >= 0.0.3
BuildRequires: pkgconfig(libsystemd)
BuildRequires: pkgconfig(mce-qt5)
BuildRequires: sed
BuildRequires: mer-qdoc-template
Requires: systemd
Requires: sailfish-setup >= 0.1.12
Requires: shadow-utils
%description
%{summary}.

%package devel
Summary: Sailfish User Manager Daemon development files
Requires: user-managerd

%description devel
%{summary}.

%package doc
Summary: Sailfish User Manager Daemon documentation

%description doc
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

%files doc
%{_docdir}/%{name}/

%prep
%autosetup -n %{name}-%{version}

%build
%qmake5 "VERSION=%{version}"
make %{?_smp_mflags}

%install
%qmake5_install

mkdir -p %{buildroot}%{_datadir}/user-managerd/remove.d
mkdir -p %{buildroot}%{_unitdir}/user@105000.service.wants/
ln -s ../home-sailfish_guest.mount %{buildroot}%{_unitdir}/user@105000.service.wants/
mkdir -p %{buildroot}%{_unitdir}/autologin@105000.service.wants/
ln -s ../home-sailfish_guest.mount %{buildroot}%{_unitdir}/autologin@105000.service.wants/
ln -s ../guest_disable_suw.service %{buildroot}%{_unitdir}/autologin@105000.service.wants/

mkdir -p %{buildroot}/%{_docdir}/%{name}
cp -R doc/html/* %{buildroot}/%{_docdir}/%{name}/

%pre
systemctl stop dbus-org.sailfishos.usermanager.service || :

%post
systemctl daemon-reload
sed -i 's/^\#USERDEL_CMD.*/USERDEL_CMD \/usr\/sbin\/userdel_local.sh/' /etc/login.defs

%postun
if [ $1 -eq 0 ]; then
    sed -i 's/^USERDEL_CMD.*/\#USERDEL_CMD/' /etc/login.defs
fi
