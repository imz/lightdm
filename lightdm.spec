%define _libexecdir %_prefix/libexec
%define _localstatedir %_var
%def_enable introspection
%def_enable qt
%def_enable qt5

Name: lightdm
Version: 1.16.7
Release: alt4.M80P.1
Summary: Lightweight Display Manager
Group: Graphical desktop/Other
License: GPLv3+
Url: https://launchpad.net/lightdm
#To get source code use the command "bzr branch lp:lightdm"

Source: %name-%version.tar
Source2: %name.pam
Source3: %name-autologin.pam
Source4: %name.wms
##Source5: %name-greeter-session.sh
Source6: %name-tmpfiles.conf
Source7: %name-greeter.pam
Source8: %name.rules
Source9: %name.service
Source10: %name-login-unknown.control

Patch1: %name-%version-%release.patch

# Requires: %name-greeter
# Requires: accountsservice
Requires: dbus-tools-gui

BuildRequires: gcc-c++ intltool
BuildRequires: pkgconfig(glib-2.0) >= 2.30 pkgconfig(gio-2.0) >= 2.26  pkgconfig(gio-unix-2.0)  pkgconfig(xdmcp)  pkgconfig(xcb)
BuildRequires: pkgconfig(gobject-2.0)
BuildRequires: pkgconfig(xcb)
BuildRequires: libdbus-glib-devel
BuildRequires: gtk-doc yelp-tools itstool
BuildRequires: libpam-devel
BuildRequires: libgcrypt-devel
BuildRequires: libvala-devel vala-tools
BuildRequires: libaudit-devel
BuildRequires: pkgconfig(glib-2.0) pkgconfig(gio-2.0) >= 2.26 pkgconfig(gio-unix-2.0) pkgconfig(gobject-2.0) pkgconfig(libxklavier) pkgconfig(x11)
%{?_enable_introspection:BuildRequires: gobject-introspection-devel}
%{?_enable_qt:BuildRequires: pkgconfig(QtCore) pkgconfig(QtDBus) pkgconfig(QtGui) /usr/bin/moc-qt4}
%{?_enable_qt5:BuildRequires: pkgconfig(Qt5Core) pkgconfig(Qt5DBus) pkgconfig(Qt5Gui) /usr/bin/moc-qt5}

%description
LightDM is a lightweight, cross-desktop display manager. Its main features are
a well-defined greeter API allowing multiple GUIs, support for all display
manager use cases, with plugins where appropriate, low code complexity, and
fast performance. Due to its cross-platform nature greeters can be written in
several toolkits, including HTML/CSS/Javascript.

%package -n liblightdm-gobject
Group: System/Libraries
Summary: LightDM GObject Greeter Library
License: LGPLv2+

%description -n liblightdm-gobject
A library for LightDM greeters based on GObject which interfaces with LightDM
and provides common greeter functionality.

%package -n liblightdm-qt
Group: System/Libraries
Summary: LightDM Qt Greeter Library
License: LGPLv2+

%description -n liblightdm-qt
A library for LightDM greeters based on Qt which interfaces with LightDM and
provides common greeter functionality.

%package -n liblightdm-qt5
Group: System/Libraries
Summary: LightDM Qt5 Greeter Library
License: LGPLv2+

%description -n liblightdm-qt5
A library for LightDM greeters based on Qt5 which interfaces with LightDM and
provides common greeter functionality.

%package devel
Group: Development/C
Summary: Development Files for LightDM
Requires: %name = %version-%release

%description devel
This package provides all necessary files for developing plugins, greeters, and
additional interface libraries for LightDM.

%package devel-doc
Summary: Development package for %name
Group: Development/Documentation
BuildArch: noarch
Conflicts: %name < %version

%description devel-doc
Contains developer documentation for %name.

%package gir
Summary: GObject introspection data for the %name
Group: System/Libraries
Requires: %name = %version-%release

%description gir
GObject introspection data for the %name

%package gir-devel
Summary: GObject introspection devel data for the %name
Group: System/Libraries
BuildArch: noarch
Requires: %name-gir = %version-%release

%description gir-devel
GObject introspection devel data for the %name

%prep
%setup
%patch1 -p1

%build
%autoreconf
%configure \
	%{subst_enable introspection} \
	--disable-static \
	--disable-tests \
	--enable-gtk-doc \
	%{?_enable_qt:--enable-liblightdm-qt} \
	%{?_enable_qt5:--enable-liblightdm-qt5} \
	--with-user-session=default \
	--libexecdir=%_libexecdir \
	--with-greeter-user=_ldm \
	--with-greeter-session=%name-default-greeter \
	CXXFLAGS=-std=c++11

%make_build

%install
%makeinstall_std

# We don't ship AppAmor
rm -rf %buildroot%_sysconfdir/apparmor.d/
# omit upstart support
rm -rf %buildroot%_sysconfdir/init

mkdir -p %buildroot%_sysconfdir/%name/lightdm.conf.d
mkdir -p %buildroot%_datadir/%name/lightdm.conf.d
mkdir -p %buildroot%_datadir/%name/remote-sessions
mkdir -p %buildroot%_sysconfdir/%name/sessions
mkdir -p %buildroot%_sysconfdir/X11/wms-methods.d
mkdir -p %buildroot%_sysconfdir/pam.d
mkdir -p %buildroot%_localstatedir/log/%name
mkdir -p %buildroot%_localstatedir/cache/%name
mkdir -p %buildroot%_localstatedir/run/%name
mkdir -p %buildroot%_localstatedir/lib/{lightdm-data,ldm}

# install pam config
install -p -m 644 %SOURCE2 %buildroot%_sysconfdir/pam.d/%name
install -p -m 644 %SOURCE3 %buildroot%_sysconfdir/pam.d/%name-autologin
#install -p -m 644 %SOURCE7 %buildroot%_sysconfdir/pam.d/%name-greeter

# install external hook for update_wms
install -m755 %SOURCE4 %buildroot%_sysconfdir/X11/wms-methods.d/%name

# install script to launch dbus
##install -m755 %%SOURCE5 %buildroot%_libexecdir/%name/%name-greeter-session

install -Dpm 644 %SOURCE6 %buildroot/lib/tmpfiles.d/%name.conf
install -m644 -p -D %SOURCE8 %buildroot%_datadir/polkit-1/rules.d/%name.rules
install -m644 -p -D %SOURCE9 %buildroot%_unitdir/%name.service
echo "GDK_CORE_DEVICE_EVENTS=true" > %buildroot%_localstatedir/lib/ldm/.pam_environment

install -m0755 -p -D %SOURCE10 %buildroot%_controldir/%name-login-unknown

%find_lang --with-gnome %name

%pre
%_sbindir/groupadd -r -f _ldm >/dev/null 2>&1 || :
%_sbindir/useradd -M -r -d %_localstatedir/lib/ldm -s /bin/false -c "LightDM daemon" -g _ldm _ldm >/dev/null 2>&1 || :


%post
if [ $1 -eq 1 ] ; then
        SYSTEMCTL=/sbin/systemctl
        # Initial installation
        $SYSTEMCTL preset %name.service > /dev/null 2>&1 ||:
fi

%preun
if [ $1 -eq 0 ] ; then
        SYSTEMCTL=/sbin/systemctl
        # Package removal, not upgrade
        $SYSTEMCTL --no-reload disable %name.service > /dev/null 2>&1 ||:
        $SYSTEMCTL stop %name.service > /dev/null 2>&1 ||:
fi


%files -f %name.lang
%doc NEWS
%config %_sysconfdir/dbus-1/system.d/org.freedesktop.DisplayManager.conf
%dir %_sysconfdir/%name
%dir %_sysconfdir/%name/sessions
%_sysconfdir/X11/wms-methods.d/%name
%config(noreplace) %_sysconfdir/%name/*.conf
%config(noreplace) %_sysconfdir/pam.d/%{name}*
%_sbindir/%name
%_unitdir/%name.service
%_man1dir/*
%_bindir/dm-tool
%_libexecdir/*
%attr(775,root,_ldm) %dir %_localstatedir/log/%name
%attr(775,_ldm,_ldm) %dir %_localstatedir/cache/%name
%attr(750,_ldm,_ldm) %dir %_localstatedir/lib/ldm
%attr(640,_ldm,_ldm) %_localstatedir/lib/ldm/.pam_environment
%attr(750,_ldm,_ldm) %dir %_localstatedir/lib/lightdm-data
%attr(775,_ldm,_ldm) %dir %_localstatedir/run/%name
/lib/tmpfiles.d/%name.conf
%_datadir/polkit-1/rules.d/%name.rules
%_datadir/bash-completion/completions/*
%_controldir/*

%files -n liblightdm-gobject
%_libdir/liblightdm-gobject-?.so.*

%if_enabled introspection
%files gir
%_typelibdir/*.typelib

%files gir-devel
%_girdir/*.gir
%endif

%if_enabled qt
%files -n liblightdm-qt
%_libdir/liblightdm-qt-?.so.*
%endif

%if_enabled qt5
%files -n liblightdm-qt5
%_libdir/liblightdm-qt5-?.so.*
%endif

%files devel
%_libdir/*.so
%_includedir/*
%_pkgconfigdir/*.pc
%_datadir/vala/vapi/*

%files devel-doc
%_datadir/gtk-doc/html/*

%changelog
* Fri Jul 28 2017 Paul Wolneykien <manowar@altlinux.org> 1.16.7-alt6
- Fix the control script: Resolve the links because sed -i replaces
  the files.

* Fri Jul 21 2017 Paul Wolneykien <manowar@altlinux.org> 1.16.7-alt4.M80P.1
- Build for p8 branch. Build with CXXFLAGS=-std=c++11.

* Fri Jul 21 2017 Paul Wolneykien <manowar@altlinux.org> 1.16.7-alt5
- Added control script 'lightdm-login-unknown'.

* Wed Jul 12 2017 Paul Wolneykien <manowar@altlinux.org> 1.16.7-alt4
- Add the 'login-unknown' configuration option.
- Package the bash completion scripts.

* Thu Jun 29 2017 Paul Wolneykien <manowar@altlinux.org> 1.16.7-alt2.M80P.1
- Build for p8 branch.

* Thu Jun 22 2017 Paul Wolneykien <manowar@altlinux.org> 1.16.7-alt3
- When is switching to the greeter, unconditionally use an existing
  one, if any, trying to reset it to the new user name.

* Tue Jun 20 2017 Paul Wolneykien <manowar@altlinux.org> 1.16.7-alt2
- Fixed "Error writing to daemon: Broken pipe".

* Thu Mar 03 2016 Alexey Shabalin <shaba@altlinux.ru> 1.16.7-alt1
- 1.16.7

* Fri Oct 09 2015 Sergey V Turchin <zerg@altlinux.org> 1.14.0-alt1.1
- NMU: rebuild with new libgcrypt (ALT#31350)

* Tue May 12 2015 Alexey Shabalin <shaba@altlinux.ru> 1.14.0-alt1
- 1.14.0

* Mon Nov 10 2014 Alexey Shabalin <shaba@altlinux.ru> 1.12.1-alt1
- 1.12.1

* Thu Sep 18 2014 Alexey Shabalin <shaba@altlinux.ru> 1.11.9-alt1
- 1.11.9

* Tue Jul 01 2014 Alexey Shabalin <shaba@altlinux.ru> 1.11.4-alt1
- 1.11.4
- add systemd unit

* Tue May 06 2014 Alexey Shabalin <shaba@altlinux.ru> 1.11.1-alt1
- 1.11.1

* Fri Apr 11 2014 Sergey V Turchin <zerg@altlinux.org> 1.9.13-alt1.1
- NMU: fix plymouth support
- NMU: fix to use /etc/X11/xinit/xserverrc

* Mon Mar 31 2014 Alexey Shabalin <shaba@altlinux.ru> 1.9.13-alt1
- 1.9.13

* Tue May 28 2013 Alexey Shabalin <shaba@altlinux.ru> 1.7.0-alt1
- 1.7.0

* Tue Apr 09 2013 Alexey Shabalin <shaba@altlinux.ru> 1.5.3-alt1
- 1.5.3

* Mon Feb 04 2013 Alexey Shabalin <shaba@altlinux.ru> 1.5.0-alt1
- 1.5.0
- make greeter and user sessions inherit the system default locale (patch from opensuse)

* Thu Jan 31 2013 Alexey Shabalin <shaba@altlinux.ru> 1.4.0-alt2
- don't requires accountservices
- requires any greeter

* Wed Jan 30 2013 Alexey Shabalin <shaba@altlinux.ru> 1.4.0-alt1
- 1.4.0

* Thu Jul 05 2012 Michael Shigorin <mike@altlinux.org> 1.2.2-alt1.1
- NMU: lightdm-greeter-session expects dbus-launch (closes: #27438)

* Thu May 17 2012 Alexey Shabalin <shaba@altlinux.ru> 1.2.2-alt1
- 1.2.2

* Sun Mar 11 2012 Alexey Shabalin <shaba@altlinux.ru> 1.1.7-alt1
- 1.1.7
- Add patch and wrapper script to launch dbus for the greeter so that
  we can safely kill it when the greeter ends. (import from ubuntu)

* Wed Mar 07 2012 Alexey Shabalin <shaba@altlinux.ru> 1.1.6-alt1
- 1.1.6

* Thu Feb 16 2012 Alexey Shabalin <shaba@altlinux.ru> 1.1.3-alt1
- 1.1.3

* Tue Dec 13 2011 Alexey Shabalin <shaba@altlinux.ru> 1.0.6-alt1
- 1.0.6
- fixed CVE-2011-4105, CVE-2011-3153
- patches from ubuntu

* Wed Oct 05 2011 Alexey Shabalin <shaba@altlinux.ru> 1.0.0-alt2
- set ignore unknown options

* Tue Oct 04 2011 Alexey Shabalin <shaba@altlinux.ru> 1.0.0-alt1
- 1.0.0
- fix PATH env

* Fri Sep 23 2011 Alexey Shabalin <shaba@altlinux.ru> 0.9.8-alt1
- 0.9.8

* Tue Sep 20 2011 Alexey Shabalin <shaba@altlinux.ru> 0.9.7-alt2
- add alternatives for greeters
- fix dir for lightdm-set-defaults
- define XSESSION_DIR for ALTLinux

* Mon Sep 19 2011 Alexey Shabalin <shaba@altlinux.ru> 0.9.7-alt1
- 0.9.7

* Mon Sep 19 2011 Alexey Shabalin <shaba@altlinux.ru> 0.9.5-alt2
- fix: package hook for wms-methods.d

* Wed Sep 14 2011 Alexey Shabalin <shaba@altlinux.ru> 0.9.5-alt1
- 0.9.5

* Fri Sep 02 2011 Alexey Shabalin <shaba@altlinux.ru> 0.9.4-alt2
- add hook for wms-methods.d based on gdm

* Mon Aug 29 2011 Alexey Shabalin <shaba@altlinux.ru> 0.9.4-alt1
- 0.9.4

* Tue May 17 2011 Mykola Grechukh <gns@altlinux.ru> 0.3.3-alt2.2
- hacked to run Xsession with session name not exec (it's ALT Linux
  here, kids...)

* Mon May 16 2011 Alexey Shabalin <shaba@altlinux.ru> 0.3.3-alt2
- add pam config file

* Thu May 12 2011 Alexey Shabalin <shaba@altlinux.ru> 0.3.3-alt1
- initial package

