%define _libexecdir %_prefix/libexec
%define _localstatedir %_var
%def_enable gobject
%def_enable vala
%def_enable introspection
%def_enable qt

Name: lightdm
Version: 0.3.3
Release: alt2.2
Summary: Lightweight Display Manager
Group: Graphical desktop/Other
License: GPLv3+
Url: https://launchpad.net/lightdm
#To get source code use the command "bzr branch lp:lightdm"

Source: %name-%version.tar
Source1: %name.conf
Source2: %name.pam

Patch1: lightdm-xsession-by-name.patch

Requires: %name-greeter

BuildRequires: gcc-c++ intltool gnome-common
BuildRequires: glib2-devel libgio-devel >= 2.26
BuildRequires: libgtk+2-devel
BuildRequires: libxcb-devel libXdmcp-devel
BuildRequires: libdbus-glib-devel
BuildRequires: gtk-doc
BuildRequires: libpam-devel
BuildRequires: python-modules-compiler
%{?_enable_gobject:BuildRequires: libxklavier-devel libX11-devel}
%{?_enable_vala:BuildRequires: vala-tools libvala-devel}
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

%package python-greeter
Summary: LightDM PyGObject Greeter
Group: Graphical desktop/Other
BuildArch: noarch
Requires: %name = %version-%release
Provides: %name-greeter

%description python-greeter
This package provides a PyGObject-based LightDM greeter engine.

%package vala-greeter
Summary: LightDM Vala Greeter
Group: Graphical desktop/Other
Requires: %name = %version-%release
Provides: %name-greeter

%description vala-greeter
This package provides a Vala-based LightDM greeter engine.

%prep
%setup
%patch1 -p1
%__subst "s|moc |moc-qt4 |" liblightdm-qt/Makefile.am greeters/qt/Makefile.am

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
	--with-config-file=%_sysconfdir/X11/%name/%name.conf \
	--with-log-dir=%_localstatedir/log/%name \
	--with-xauth-dir=%_localstatedir/run/%name/authority \
	--with-default-pam-service=%name \
	--libexecdir=%_libexecdir/%name \
	--with-greeter-user=_ldm

#	--with-cache-dir=%_localstatedir/cache/%name
#	--with-default-session=default \

%make_build

%install
%make_install DESTDIR=%buildroot install

#mkdir -p %buildroot%_sysconfdir/X11/%name/sessions
#mkdir -p %buildroot%_sysconfdir/X11/wms-methods.d
mkdir -p %buildroot%_sysconfdir/X11/%name
mkdir -p %buildroot%_sysconfdir/pam.d
mkdir -p %buildroot%_localstatedir/log/%name
mkdir -p %buildroot%_localstatedir/cache/%name

# install lightdm config
install -p -m 644 %SOURCE1 %buildroot%_sysconfdir/X11/%name/%name.conf

# install pam config
install -p -m 644 %SOURCE2 %buildroot%_sysconfdir/pam.d/%name

# install external hook for update_wms
#install -m755 %%SOURCE2 %buildroot%_sysconfdir/X11/wms-methods.d/%name

%pre
%_sbindir/groupadd -r -f _ldm >/dev/null 2>&1 || :
%_sbindir/useradd -M -r -d %_localstatedir/lib/ldm -s /bin/false -c "LightDM daemon" -g _ldm _ldm >/dev/null 2>&1 || :

%files
%doc ChangeLog README COPYING
%config %_sysconfdir/dbus-1/system.d/org.lightdm.LightDisplayManager.conf
%config %_sysconfdir/X11/%name/%name.conf
%config %_sysconfdir/pam.d/%name
%_bindir/%name
%_man1dir/%name.*
%dir %_datadir/%name
%_datadir/%name/themes
%dir %_libexecdir/%name
%dir %_localstatedir/log/%name
%attr(775, _ldm, _ldm) %dir %_localstatedir/cache/%name

%files python-greeter
%_libexecdir/%name/lightdm-example-python-gtk-greeter

%if_enabled gobject
%files -n liblightdm-gobject
%_libdir/liblightdm-gobject-0.so.*

%files gtk-greeter
%_libexecdir/%name/lightdm-example-gtk-greeter
%_datadir/lightdm-example-gtk-greeter/greeter.ui

%if_enabled vala
%files vala-greeter
%_libexecdir/%name/lightdm-example-vala-gtk-greeter

%if_enabled introspection
%files gir
%_typelibdir/*.typelib

%files gir-devel
%_girdir/*.gir
%endif
%endif
%endif

%if_enabled qt
%files -n liblightdm-qt
%_libdir/liblightdm-qt-0.so.*

%files qt-greeter
%_libexecdir/%name/lightdm-example-qt-greeter
%endif

%files devel
%_libdir/*.so
%_includedir/*
%_pkgconfigdir/*.pc
%if_enabled vala
%_datadir/vala/vapi/*.vapi
%endif

%files devel-doc
%_datadir/gtk-doc/html/*

%changelog
* Tue May 17 2011 Mykola Grechukh <gns@altlinux.ru> 0.3.3-alt2.2
- hacked to run Xsession with session name not exec (it's ALT Linux
  here, kids...)

* Mon May 16 2011 Alexey Shabalin <shaba@altlinux.ru> 0.3.3-alt2
- add pam config file

* Thu May 12 2011 Alexey Shabalin <shaba@altlinux.ru> 0.3.3-alt1
- initial package

