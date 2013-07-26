plugindir = @NGFD_PLUGIN_DIR@
plugin_LTLIBRARIES = libngfd_canberra.la
libngfd_canberra_la_SOURCES = plugin.c
libngfd_canberra_la_LIBADD = @NGFD_PLUGIN_LIBS@ @CANBERRA_LIBS@
libngfd_canberra_la_LDFLAGS = -module -avoid-version
libngfd_canberra_la_CFLAGS = @NGFD_PLUGIN_CFLAGS@ @CANBERRA_CFLAGS@ -I$(top_srcdir)/src/include
