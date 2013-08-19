TESTS = \
       test-value \
       test-request \
       test-proplist \
       test-context \
       test-core \
       test-inputinterface \
       test-plugin \
       test-sinkinterface

testsdir = @NGFD_TESTS_DIR@
tests_PROGRAMS = \
       test-value \
       test-request \
       test-proplist \
       test-context \
       test-core \
       test-inputinterface \
       test-plugin \
       test-sinkinterface

tests_DATA = \
       tests.xml

INCLUDES = -I$(top_srcdir)/src/include

test_value_SOURCES = test-value.c $(top_srcdir)/src/ngf/value.c $(top_srcdir)/src/ngf/log.c
test_value_CFLAGS = @CHECK_CFLAGS@ @NGFD_CFLAGS@
test_value_LDADD = @CHECK_LIBS@ @NGFD_LIBS@

test_request_SOURCES = test-request.c $(top_srcdir)/src/ngf/request.c $(top_srcdir)/src/ngf/proplist.c $(top_srcdir)/src/ngf/value.c $(top_srcdir)/src/ngf/log.c
test_request_CFLAGS = @CHECK_CFLAGS@ @NGFD_CFLAGS@
test_request_LDADD = @CHECK_LIBS@ @NGFD_LIBS@

test_proplist_SOURCES = test-proplist.c $(top_srcdir)/src/ngf/proplist.c $(top_srcdir)/src/ngf/value.c $(top_srcdir)/src/ngf/log.c
test_proplist_CFLAGS = @CHECK_CFLAGS@ @NGFD_CFLAGS@
test_proplist_LDADD = @CHECK_LIBS@ @NGFD_LIBS@

test_context_SOURCES = test-context.c $(top_srcdir)/src/ngf/value.c $(top_srcdir)/src/ngf/log.c $(top_srcdir)/src/ngf/proplist.c
test_context_CFLAGS = @CHECK_CFLAGS@ @NGFD_CFLAGS@
test_context_LDADD = @CHECK_LIBS@ @NGFD_LIBS@

test_core_SOURCES = test-core.c $(top_srcdir)/src/ngf/core.c $(top_srcdir)/src/ngf/hook.c $(top_srcdir)/src/ngf/sinkinterface.c $(top_srcdir)/src/ngf/context.c $(top_srcdir)/src/ngf/value.c $(top_srcdir)/src/ngf/log.c $(top_srcdir)/src/ngf/proplist.c $(top_srcdir)/src/ngf/plugin.c $(top_srcdir)/src/ngf/event.c $(top_srcdir)/src/ngf/request.c $(top_srcdir)/src/ngf/core-player.c $(top_srcdir)/src/ngf/core-hooks.c
test_core_CFLAGS = @CHECK_CFLAGS@ @NGFD_CFLAGS@
test_core_LDADD = @CHECK_LIBS@ @NGFD_LIBS@

test_inputinterface_SOURCES = test-inputinterface.c $(top_srcdir)/src/ngf/inputinterface.c $(top_srcdir)/src/ngf/core.c $(top_srcdir)/src/ngf/hook.c $(top_srcdir)/src/ngf/sinkinterface.c $(top_srcdir)/src/ngf/context.c $(top_srcdir)/src/ngf/value.c $(top_srcdir)/src/ngf/log.c $(top_srcdir)/src/ngf/proplist.c $(top_srcdir)/src/ngf/plugin.c $(top_srcdir)/src/ngf/event.c $(top_srcdir)/src/ngf/request.c $(top_srcdir)/src/ngf/core-player.c $(top_srcdir)/src/ngf/core-hooks.c
test_inputinterface_CFLAGS = @CHECK_CFLAGS@ @NGFD_CFLAGS@
test_inputinterface_LDADD = @CHECK_LIBS@ @NGFD_LIBS@

test_plugin_SOURCES = test-plugin.c $(top_srcdir)/src/ngf/core.c $(top_srcdir)/src/ngf/hook.c $(top_srcdir)/src/ngf/context.c $(top_srcdir)/src/ngf/value.c $(top_srcdir)/src/ngf/log.c $(top_srcdir)/src/ngf/proplist.c $(top_srcdir)/src/ngf/plugin.c $(top_srcdir)/src/ngf/event.c $(top_srcdir)/src/ngf/request.c $(top_srcdir)/src/ngf/core-player.c $(top_srcdir)/src/ngf/core-hooks.c
test_plugin_CFLAGS = @CHECK_CFLAGS@ @NGFD_CFLAGS@
test_plugin_LDADD = @CHECK_LIBS@ @NGFD_LIBS@

test_sinkinterface_SOURCES = test-sinkinterface.c $(top_srcdir)/src/ngf/sinkinterface.c $(top_srcdir)/src/ngf/core.c $(top_srcdir)/src/ngf/hook.c $(top_srcdir)/src/ngf/context.c $(top_srcdir)/src/ngf/value.c $(top_srcdir)/src/ngf/log.c $(top_srcdir)/src/ngf/proplist.c $(top_srcdir)/src/ngf/plugin.c $(top_srcdir)/src/ngf/event.c $(top_srcdir)/src/ngf/request.c $(top_srcdir)/src/ngf/core-hooks.c
test_sinkinterface_CFLAGS = @CHECK_CFLAGS@ @NGFD_CFLAGS@
test_sinkinterface_LDADD = @CHECK_LIBS@ @NGFD_LIBS@

plugindir = @NGFD_PLUGIN_DIR@
plugin_LTLIBRARIES = libngfd_test_fake.la
libngfd_test_fake_la_SOURCES = test-fake-plugin.c
libngfd_test_fake_la_LIBADD = @NGFD_PLUGIN_LIBS@
libngfd_test_fake_la_LDFLAGS = -module -avoid-version
libngfd_test_fake_la_CFLAGS = @NGFD_PLUGIN_CFLAGS@ -I$(top_srcdir)/src/include
