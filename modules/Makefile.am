################################################################################
#
# IRCd Modules
#

AM_CXXFLAGS = \
	###

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	@JS_CPPFLAGS@ \
	@BOOST_CPPFLAGS@ \
	-include $(top_srcdir)/include/ircd/ircd.pic.h \
	-include $(top_srcdir)/include/ircd/mods/mapi.h \
	###

AM_LDFLAGS = \
	-L$(top_srcdir)/ircd \
	@JS_LDFLAGS@ \
	@BOOST_LDFLAGS@ \
	###

AM_LDFLAGS += \
	-avoid-version \
	-module \
	-Wl,-fuse-ld=gold \
	-Wl,--shared \
	-Wl,--export-dynamic \
	-Wl,--no-gnu-unique \
	-Wl,--no-allow-shlib-undefined \
	-Wl,--gc-sections \
	-Wl,--icf=all \
	-Wl,--rosegment \
	$(PLATFORM_LDFLAGS) \
	# -Wl,--no-undefined \
	# -export-symbols-regex IRCD_MODULE* \
	# -export-symbols-regex * \
	###

###############################################################################
#
# /
#

moduledir = @moduledir@

webroot_la_SOURCES = webroot.cc
console_la_SOURCES = console.cc
vm_la_SOURCES = vm.cc

module_LTLIBRARIES = \
	webroot.la \
	console.la \
	vm.la \
	###
