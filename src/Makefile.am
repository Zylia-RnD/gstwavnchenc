# Note: wavnchencdir is set in configure

##############################################################################
# TODO: change libgstwavnchenc.la to something else, e.g. libmysomething.la     #
##############################################################################
plugin_LTLIBRARIES = libgstwavnchenc.la

##############################################################################
# TODO: for the next set of variables, name the prefix if you named the .la, #
#  e.g. libmysomething.la => libmysomething_la_SOURCES                       #
#                            libmysomething_la_CFLAGS                        #
#                            libmysomething_la_LIBADD                        #
#                            libmysomething_la_LDFLAGS                       #
##############################################################################

# sources used to compile this plug-in
libgstwavnchenc_la_SOURCES = gstwavnchenc.c gstwavnchenc.h

# compiler and linker flags used to compile this wavnchenc, set in configure.ac
#libgstwavnchenc_la_CFLAGS = $(GST_CFLAGS)
#libgstwavnchenc_la_LIBADD = $(GST_LIBS)
#libgstwavnchenc_la_LDFLAGS = $(GST_wavnchenc_LDFLAGS)
#libgstwavnchenc_la_LIBTOOLFLAGS = --tag=disable-static

libgstwavnchenc_la_CFLAGS =  \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstwavnchenc_la_LIBADD = 	\
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-1.0 \
	-lgstriff-1.0 \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstwavnchenc_la_LDFLAGS = $(GST_wavnchenc_LDFLAGS)
libgstwavnchenc_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstplugin.h
