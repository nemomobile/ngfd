plugindir = @NGFD_PLUGIN_DIR@
plugin_LTLIBRARIES = libngfd_profile.la
libngfd_profile_la_SOURCES = plugin.c
libngfd_profile_la_LIBADD = @NGFD_PLUGIN_LIBS@ @DBUS_LIBS@ @PROFILE_LIBS@
libngfd_profile_la_LDFLAGS = -module -avoid-version
libngfd_profile_la_CFLAGS = @NGFD_PLUGIN_CFLAGS@ @DBUS_CFLAGS@ @PROFILE_CFLAGS@ -I$(top_srcdir)/src/include

