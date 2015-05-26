Name:       ngfd

Summary:    Non-graphic feedback service for sounds and other events
Version:    0.92
Release:    1
Group:      System/Daemons
License:    LGPL 2.1
URL:        https://meego.gitorious.org/maemo-multimedia/ngfd
Source0:    %{name}-%{version}.tar.gz
Source1:    ngfd.service
Requires:   %{name}-settings
Requires:   systemd
Requires:   systemd-user-session-targets
BuildRequires:  systemd
BuildRequires:  pkgconfig(glib-2.0) >= 2.18.0
BuildRequires:  pkgconfig(dbus-1) >= 1.0.2
BuildRequires:  pkgconfig(dbus-glib-1)
BuildRequires:  pkgconfig(libpulse)
BuildRequires:  pkgconfig(gstreamer-0.10)
BuildRequires:  pkgconfig(gstreamer-controller-0.10)
BuildRequires:  pkgconfig(gio-2.0)
BuildRequires:  pkgconfig(gobject-2.0)
BuildRequires:  pkgconfig(gthread-2.0)
BuildRequires:  pkgconfig(check)
BuildRequires:  pkgconfig(mce)
BuildRequires:  pkgconfig(profile)
BuildRequires:  pkgconfig(libcanberra)
BuildRequires:  doxygen
Obsoletes:      tone-generator <= 1.5.4
Provides:       tone-generator > 1.5.4

%description
This package contains the daemon servicing the non-graphical feedback
requests.


%package plugin-devel
Summary:    Development package for ngfd plugin creation
Group:      Development/Libraries
Requires:   %{name} = %{version}-%{release}

%description plugin-devel
This package contains header files for creating plugins to non-graphical feedback daemon.

%package plugin-fake
Summary:    Fake plugins for ngfd testing
Group:      System/Libraries
Requires:   %{name} = %{version}-%{release}

%description plugin-fake
Fake plugins for ngfd testing.

%package settings-basic
Summary:    Example settings for ngfd
Group:      System/Libraries
Requires:   %{name} = %{version}-%{release}
Provides:   %{name}-settings

%description settings-basic
Example settings for ngfd.

%package plugin-doc
Summary:    Documentation package for ngfd plugin creation
Group:      Documentation
Requires:   %{name} = %{version}-%{release}

%description plugin-doc
This package contains documentation to header files for creating plugins to non-graphical feedback daemon.

%package tests
Summary:    Test suite for ngfd
Group:      System/Daemons
Requires:   %{name} = %{version}-%{release}
Requires:   %{name}-plugin-fake = %{version}-%{release}

%description tests
This package contains test suite for ngfd.

%prep
%setup -q -n %{name}-%{version}


%build
%autogen --enable-debug
make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%make_install

install -D -m 644 %{SOURCE1} %{buildroot}%{_userunitdir}/ngfd.service
mkdir -p %{buildroot}%{_userunitdir}/user-session.target.wants
ln -s ../ngfd.service %{buildroot}%{_userunitdir}/user-session.target.wants/
ln -s ngfd.service %{buildroot}%{_userunitdir}/dbus-com.nokia.NonGraphicFeedback1.Backend.service
mkdir -p %{buildroot}%{_userunitdir}/actdead-session.target.wants
ln -s ../ngfd.service %{buildroot}%{_userunitdir}/actdead-session.target.wants/

%post
# We require reboot for system update so lets not start/stop services in
# package installation/upgrades/removals
systemctl-user daemon-reload || :

%postun
# .. as above.
systemctl-user daemon-reload || :

%files
%defattr(-,root,root,-)
%doc COPYING
%config(noreplace) %{_sysconfdir}/dbus-1/system.d/%{name}.conf
%{_datadir}/dbus-1/services/com.nokia.NonGraphicFeedback1.Backend.service
%{_bindir}/%{name}
%{_libdir}/ngf/libngfd_dbus.so
%{_libdir}/ngf/libngfd_resource.so
%{_libdir}/ngf/libngfd_transform.so
%{_libdir}/ngf/libngfd_gst.so
%{_libdir}/ngf/libngfd_canberra.so
%{_libdir}/ngf/libngfd_mce.so
%{_libdir}/ngf/libngfd_streamrestore.so
%{_libdir}/ngf/libngfd_tonegen.so
%{_libdir}/ngf/libngfd_callstate.so
%{_libdir}/ngf/libngfd_profile.so
%{_libdir}/ngf/libngfd_ffmemless.so
%{_userunitdir}/ngfd.service
%{_userunitdir}/dbus-com.nokia.NonGraphicFeedback1.Backend.service
%{_userunitdir}/user-session.target.wants/ngfd.service
%{_userunitdir}/actdead-session.target.wants/ngfd.service

%files plugin-devel
%defattr(-,root,root,-)
%doc COPYING
%{_includedir}/ngf/*
%{_libdir}/pkgconfig/ngf-plugin.pc

%files plugin-fake
%defattr(-,root,root,-)
%{_libdir}/ngf/libngfd_fake.so
%{_libdir}/ngf/libngfd_test_fake.so

%files settings-basic
%defattr(-,root,root,-)
%doc COPYING
%{_datadir}/ngfd/

%files plugin-doc
%defattr(-,root,root,-)
%doc COPYING
%{_docdir}/ngfd-plugin/html/*

%files tests
%defattr(-,root,root,-)
/opt/tests/ngfd/*
