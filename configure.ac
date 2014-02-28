AC_PREREQ(2.61)
AC_INIT(ngfd, 0.60)

AC_CONFIG_MACRO_DIR([m4])
AM_CONFIG_HEADER([config.h])
AC_CONFIG_SRCDIR([src/ngf/main.c])
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)

# Disable static libraries.
AC_DISABLE_STATIC

# Initialize programs.

AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_INSTALL
AM_PROG_LIBTOOL

# Enable extra GCC flags.
if test "$GCC" = "yes"; then
    CFLAGS="$CFLAGS -Wall -Wextra -Werror"
fi

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h netinet/in.h stdlib.h string.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_STRUCT_TM

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([dup2 localtime_r memmove memset socket strchr strdup strerror strrchr strtoul])

# Checks for glib and gobject.
PKG_CHECK_MODULES(GLIB, glib-2.0 gobject-2.0)
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

# Checks for gmodule.
PKG_CHECK_MODULES(GMODULE, gmodule-2.0)
AC_SUBST(GMODULE_CFLAGS)
AC_SUBST(GMODULE_LIBS)

# Checks for check unit test framework.
PKG_CHECK_MODULES(CHECK, check)
AC_SUBST(CHECK_LIBS)
AC_SUBST(CHECK_CFLAGS)


AC_ARG_ENABLE([coverage],
     	AS_HELP_STRING([--enable-coverage],[Enable coverage @<:@default=false@:>@]),
	[case "${enableval}" in
		yes) coverage=true ; CHECK_CFLAGS="${CHECK_CFLAGS} -fprofile-arcs -ftest-coverage" ; CHECK_LIBS="${CHECK_LIBS} -lgcov" ;;
		no)  coverage=false ;;
		*) AC_MSG_ERROR([bad value ${enableval} for --enable-coverage]) ;;
	esac],
	[coverage=false])
AM_CONDITIONAL([COVERAGE], [test x$coverage = xtrue])

#AC_SUBST(CHECK_LIBS)
#AC_SUBST(CHECK_CFLAGS)


# NGFD build compatibility variables.
AC_SUBST(NGFD_CFLAGS, "$GLIB_CFLAGS $GMODULE_CFLAGS")
AC_SUBST(NGFD_LIBS, "$GLIB_LIBS $GMODULE_LIBS")
AC_SUBST(NGFD_PLUGIN_CFLAGS, "$NGFD_CFLAGS")
AC_SUBST(NGFD_PLUGIN_LIBS, "$NGFD_LIBS")

AC_SUBST(NGFD_CONF_DIR, "\$(datadir)/ngfd")
AC_SUBST(NGFD_PLUGIN_CONF_DIR, "\$(NGFD_CONF_DIR)/plugins.d")
AC_SUBST(NGFD_PLUGIN_DIR, "\$(libdir)/ngf")

AC_SUBST(NGFD_TESTS_DIR, "/opt/tests/ngfd")

# Debug flag.

AC_ARG_ENABLE([debug],
    AS_HELP_STRING([--enable-debug],[Enable debug @<:@default=false@:>@]),
    [case "${enableval}" in
        yes) debug=true ; CFLAGS="${CFLAGS} -DENABLE_DEBUG" ;;
        no)  debug=false ;;
        *) AC_MSG_ERROR([bad value ${enableval} for --enable-debug]) ;;
    esac],
    [debug=false])
AM_CONDITIONAL([DEBUG], [test x$debug = xtrue])

# DBus plugin

PKG_CHECK_MODULES(DBUS, dbus-glib-1 >= 0.70 dbus-1 >= 0.70, [has_dbus=yes], [has_dbus=no])
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

if test x$has_dbus = xyes; then
    enable_dbus=yes
else
    enable_dbus=no
fi

AM_CONDITIONAL(BUILD_DBUS, test x$enable_dbus = xyes)

# FF memless plugin
AC_CHECK_HEADERS([linux/input.h sys/ioctl.h], [has_ffm=yes], [has_ffm=no])
AC_SUBST(FFMEMLESS_CFLAGS, "")
AC_SUBST(FFMEMLESS_LIBS, "")

if test x$has_ffm = xyes; then
    enable_ffm=yes
else
    enable_ffm=no
fi
AM_CONDITIONAL(BUILD_FFMEMLESS, test x$enable_ffm = xyes)

# GStreamer plugin

PKG_CHECK_MODULES(GST, gstreamer-0.10 gstreamer-controller-0.10, [has_gst=yes], [has_gst=no])
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_LIBS)

if test x$has_gst = xyes; then
    enable_gst=yes
else
    enable_gst=no
fi

AM_CONDITIONAL(BUILD_GST, test x$enable_gst = xyes)

# libcanberra plugin
PKG_CHECK_MODULES(CANBERRA, libcanberra, [has_canberra=yes], [has_canberra=no])
AC_SUBST(CANBERRA_CFLAGS)
AC_SUBST(CANBERRA_LIBS)

if test x$has_canberra = xyes; then
    enable_canberra=yes
else
    enable_canberra=no
fi

AM_CONDITIONAL(BUILD_CANBERRA, test x$enable_canberra = xyes)

# Immvibe plugin

AC_CHECK_HEADERS([ImmVibe.h ImmVibeCore.h ImmVibeOS.h], [has_immvibe=yes], [has_immvibe=no])
AC_SUBST(IMMVIBE_CFLAGS, "")
AC_SUBST(IMMVIBE_LIBS, "-limmvibe")

if test x$has_immvibe = xyes; then
    enable_immvibe=yes
else
    enable_immvibe=no
fi

AM_CONDITIONAL(BUILD_IMMVIBE, test x$enable_immvibe = xyes)

# Profile plugin

PKG_CHECK_MODULES(PROFILE, profile, [has_profile=yes], [has_profile=no])
AC_SUBST(PROFILE_CFLAGS)
AC_SUBST(PROFILE_LIBS)

if test x$has_profile = xyes; then
    enable_profile=yes
else
    enable_profile=no
fi

AM_CONDITIONAL(BUILD_PROFILE, test x$enable_profile = xyes)

# MCE plugin

PKG_CHECK_MODULES(MCE, mce, [has_mce=yes], [has_mce=no])

if test x$has_mce = xyes; then
    enable_mce=yes
else
    enable_mce=no
fi

AM_CONDITIONAL(BUILD_MCE, test x$enable_mce = xyes)

enable_droid_vibrator=no

PKG_CHECK_MODULES([ANDROID_HEADERS], [android-headers], [has_android_headers=yes], [has_android_headers=no])

if test x$has_android_headers = xyes; then
    AC_SUBST(ANDROID_HEADERS_CFLAGS)
    AC_SUBST(ANDROID_HEADERS_LIBS)

    # Droid Vibrator HAL plugin (via libdroid)
    PKG_CHECK_MODULES([DROID_VIBRATOR], [libvibrator], [has_droid_vibrator=yes], [has_droid_vibrator=no])
    AC_SUBST(DROID_VIBRATOR_CFLAGS)
    AC_SUBST(DROID_VIBRATOR_LIBS)

    if test x$has_droid_vibrator = xyes; then
        enable_droid_vibrator=yes
    else
        enable_droid_vibrator=no
    fi
fi

AM_CONDITIONAL(BUILD_DROID_VIBRATOR, test x$enable_droid_vibrator = xyes)

# Stream restore plugin needs DBus

if test x$enable_dbus = xyes ; then
    enable_streamrestore=yes
else
    enable_streamrestore=no
fi

AM_CONDITIONAL(BUILD_STREAMRESTORE, test x$enable_streamrestore = xyes)

# Tonegen plugin needs DBus

if test x$enable_dbus = xyes ; then
    enable_tonegen=yes
else
    enable_tonegen=no
fi

AM_CONDITIONAL(BUILD_TONEGEN, test x$enable_tonegen = xyes)

echo "
 == $PACKAGE_NAME $VERSION ==

    Compiler:               ${CC}
    CFLAGS:                 ${CFLAGS}
    Code coverage:          ${coverage}
    Debug enabled:          ${debug}

    DBus plugin:            ${enable_dbus}
    GStreamer plugin:       ${enable_gst}
    Canberra plugin:        ${enable_canberra}
    Immvibe plugin:         ${enable_immvibe}
    Profile plugin:         ${enable_profile}
    MCE plugin:             ${enable_mce}
    Stream restore plugin:  ${enable_streamrestore}
    Tone generator plugin:  ${enable_tonegen}
    Droid Vibrator plugin:  ${enable_droid_vibrator}
"

AC_CONFIG_FILES([
Makefile
src/Makefile
src/ngf/Makefile
src/include/Makefile
src/include/ngf/Makefile
src/plugins/Makefile
src/plugins/fake/Makefile
src/plugins/dbus/Makefile
src/plugins/ffmemless/Makefile
src/plugins/transform/Makefile
src/plugins/mce/Makefile
src/plugins/immvibe/Makefile
src/plugins/resource/Makefile
src/plugins/profile/Makefile
src/plugins/streamrestore/Makefile
src/plugins/tonegen/Makefile
src/plugins/gst/Makefile
src/plugins/canberra/Makefile
src/plugins/callstate/Makefile
src/plugins/droid-vibrator/Makefile
doc/Makefile
data/Makefile
data/events.d/Makefile
data/plugins.d/Makefile
data/ngf-plugin.pc
tests/Makefile
])

AC_OUTPUT
