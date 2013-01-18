plugindir = @NGFD_PLUGIN_DIR@
plugin_LTLIBRARIES = libngfd_ffmemless.la
libngfd_ffmemless_la_SOURCES = plugin.c ffmemless.c
libngfd_ffmemless_la_LIBADD = @NGFD_PLUGIN_LIBS@
libngfd_ffmemless_la_LDFLAGS = -module -avoid-version
libngfd_ffmemless_la_CFLAGS = @NGFD_PLUGIN_CFLAGS@ -I$(top_srcdir)/src/include