EXTRA_DIST = mod-pquota.xml

INCLUDES = -Wall -g -ansi -DCOMPILATION_DATE=`date +%s` \
	   -DVERSION=\"$(TURBULENCE_VERSION)\" \
	   $(AXL_CFLAGS) $(VORTEX_CFLAGS) $(EXARG_CFLAGS) $(VALVULA_CFLAGS)

lib_LTLIBRARIES      = mod-pquota.la
mod_pquota_la_SOURCES  = mod-pquota.c mod-pquota.h
mod_pquota_la_LDFLAGS  = -module -ldl /usr/lib/libvalvula.la /usr/lib/libvalvulad.la

# reconfigure module installation directory
libdir = $(prefix)/lib/valvulad/modules

# configure site module installation
modconfdir   = $(sysconfdir)/valvula/mods-available
modconf_DATA = mod-pquota.xml
