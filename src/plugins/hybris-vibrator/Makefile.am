plugindir = @NGFD_PLUGIN_DIR@
plugin_LTLIBRARIES = libngfd_hybris-vibrator.la
libngfd_hybris_vibrator_la_SOURCES = plugin.c
libngfd_hybris_vibrator_la_LIBADD = @NGFD_PLUGIN_LIBS@ $(HYBRIS_VIBRATOR_LIBS)
libngfd_hybris_vibrator_la_LDFLAGS = -module -avoid-version
libngfd_hybris_vibrator_la_CFLAGS = @NGFD_PLUGIN_CFLAGS@ -I$(top_srcdir)/src/include $(ANDROID_HEADERS_CFLAGS)

