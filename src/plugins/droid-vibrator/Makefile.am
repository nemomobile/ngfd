plugindir = @NGFD_PLUGIN_DIR@
plugin_LTLIBRARIES = libngfd_droid-vibrator.la
libngfd_droid_vibrator_la_SOURCES = plugin.c
libngfd_droid_vibrator_la_LIBADD = @NGFD_PLUGIN_LIBS@ $(DROID_VIBRATOR_LIBS)
libngfd_droid_vibrator_la_LDFLAGS = -module -avoid-version
libngfd_droid_vibrator_la_CFLAGS = @NGFD_PLUGIN_CFLAGS@ -I$(top_srcdir)/src/include $(ANDROID_HEADERS_CFLAGS)

