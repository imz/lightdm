%define _libexecdir %_prefix/libexec
%define _localstatedir %_var
%def_enable gobject
%def_enable introspection
%def_enable qt

Name: lightdm
Version: 0.9.7
Release: alt2
Summary: Lightweight Display Manager
Group: Graphical desktop/Other
License: GPLv3+
Url: https://launchpad.net/lightdm
#To get source code use the command "bzr branch lp:lightdm"

Source: %name-%version.tar
Source2: %name.pam
Source3: %name-autologin.pam
Source4: %name.wms

Patch1: %name-%version-%release.patch

Requires: %name-greeter
Requires: accountsservice

BuildRequires: gcc-c++ intltool gnome-common
BuildRequires: glib2-devel libgio-devel >= 2.26
BuildRequires: libgtk+3-devel
BuildRequires: libxcb-devel libXdmcp-devel
BuildRequires: libdbus-glib-devel
BuildRequires: gtk-doc
BuildRequires: libpam-devel
%{?_enable_gobject:BuildRequires: libxklavier-devel libX11-devel}
%{?_enable_introspection:BuildRequires: gobject-introspection-devel}
%{?_enable_qt:BuildRequires: libqt4-devel}

%description
LightDM is a lightweight, cross-desktop display manager. Its main features are
a well-defined greeter API allowing multiple GUIs, support for all display
manager use cases, with plugins where appropriate, low code complexity, and
fast performance. Due to its cross-platform nature greeters can be written in
several toolkits, including HTML/CSS/Javascript.

%package -n liblightdm-gobject
Group: System/Libraries
Summary: LightDM GObject Greeter Library

%description -n liblightdm-gobject
A library for LightDM greeters based on GObject which interfaces with LightDM
and provides common greeter functionality.

%package -n liblightdm-qt
Group: System/Libraries
Summary: LightDM Qt Greeter Library

%description -n liblightdm-qt
A library for LightDM greeters based on Qt which interfaces with LightDM and
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

%package gtk-greeter
Group: Graphical desktop/Other
Summary: LightDM GTK+ Greeter
Requires: %name = %version-%release
Provides: %name-greeter

%description gtk-greeter
This package provides a GTK+-based LightDM greeter engine.

%package qt-greeter
Group: Graphical desktop/Other
Summary: LightDM Qt Greeter
Requires: %name = %version-%release
Provides: %name-greeter

%description qt-greeter
This package provides a Qt-based LightDM greeter engine.

%prep
%setup
%patch1 -p1
%__subst "s|moc |moc-qt4 |" liblightdm-qt/Makefile.am greeters/qt/Makefile.am tests/src/Makefile.am
%__subst "s|uic |uic-qt4 |" liblightdm-qt/Makefile.am greeters/qt/Makefile.am tests/src/Makefile.am

%build
%autoreconf
%configure \
	%{subst_enable introspection} \
	--disable-static \
	--enable-gtk-doc \
%if_enabled gobject
	--enable-liblightdm-gobject \
%endif
%if_enabled qt
	--enable-liblightdm-qt \
%endif
	--with-user-session=default \
	--libexecdir=%_libexecdir \
	--with-greeter-user=_ldm \
	--with-greeter-session=%name-default-greeter

%make_build

%install
%make_install DESTDIR=%buildroot install

# fix location
mv %buildroot%_libexecdir/lightdm-set-defaults \
    %buildroot%_libexecdir/lightdm/lightdm-set-defaults

mkdir -p %buildroot%_sysconfdir/%name/sessions
mkdir -p %buildroot%_sysconfdir/X11/wms-methods.d
mkdir -p %buildroot%_sysconfdir/pam.d
mkdir -p %buildroot%_localstatedir/log/%name
mkdir -p %buildroot%_localstatedir/cache/%name
mkdir -p %buildroot%_localstatedir/run/%name
mkdir -p %buildroot%_localstatedir/lib/ldm

# install pam config
install -p -m 644 %SOURCE2 %buildroot%_sysconfdir/pam.d/%name
install -p -m 644 %SOURCE3 %buildroot%_sysconfdir/pam.d/%name-autologin

# install external hook for update_wms
install -m755 %SOURCE4 %buildroot%_sysconfdir/X11/wms-methods.d/%name

%find_lang %name

cd %buildroot
# Add alternatives for xgreeters
mkdir -p ./%_altdir
printf '%_datadir/xgreeters/%name-default-greeter.desktop\t%_datadir/xgreeters/%name-gtk-greeter.desktop\t100\n' >./%_altdir/%name-gtk-greeter
printf '%_datadir/xgreeters/%name-default-greeter.desktop\t%_datadir/xgreeters/%name-qt-greeter.desktop\t200\n' >./%_altdir/%name-qt-greeter

%pre
%_sbindir/groupadd -r -f _ldm >/dev/null 2>&1 || :
%_sbindir/useradd -M -r -d %_localstatedir/lib/ldm -s /bin/false -c "LightDM daemon" -g _ldm _ldm >/dev/null 2>&1 || :

%files -f %name.lang
%doc AUTHORS COPYING NEWS README
%config %_sysconfdir/dbus-1/system.d/org.freedesktop.DisplayManager.conf
%dir %_sysconfdir/%name
%dir %_sysconfdir/%name/sessions
%_sysconfdir/X11/wms-methods.d/lightdm
%config(noreplace) %_sysconfdir/%name/%name.conf
%config(noreplace) %_sysconfdir/%name/keys.conf
%config(noreplace) %_sysconfdir/%name/users.conf
%config(noreplace) %_sysconfdir/pam.d/%name
%config(noreplace) %_sysconfdir/pam.d/%name-autologin
%_sbindir/%name
%_man1dir/%name.*
%_bindir/dm-tool
%dir %_datadir/xgreeters
%_libexecdir/*
%attr(775,root,_ldm) %dir %_localstatedir/log/%name
%attr(775,_ldm,_ldm) %dir %_localstatedir/cache/%name
%attr(750,_ldm,_ldm) %dir %_localstatedir/lib/ldm
%ghost %attr(751,_ldm,_ldm) %dir %_localstatedir/run/%name

%if_enabled gobject
%files -n liblightdm-gobject
%_libdir/liblightdm-gobject-?.so.*

%files gtk-greeter
%_altdir/%name-gtk-greeter
%_sbindir/%name-gtk-greeter
%_datadir/%name-gtk-greeter
%_datadir/xgreeters/%name-gtk-greeter.desktop
%config(noreplace) %_sysconfdir/%name/%name-gtk-greeter.conf

%if_enabled introspection
%files gir
%_typelibdir/*.typelib

%files gir-devel
%_girdir/*.gir
%endif
%endif

%if_enabled qt
%files -n liblightdm-qt
%_libdir/liblightdm-qt-?.so.*

%files qt-greeter
%_altdir/%name-qt-greeter
%_sbindir/%name-qt-greeter
%_datadir/xgreeters/%name-qt-greeter.desktop
%endif

%files devel
%_libdir/*.so
%_includedir/*
%_pkgconfigdir/*.pc
%_datadir/vala/vapi/*.vapi

%files devel-doc
%_datadir/gtk-doc/html/*

%changelog
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

