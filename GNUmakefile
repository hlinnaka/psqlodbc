#-------------------------------------------------------------------------
#
# GNUMakefile for psqlodbc (Postgres ODBC driver)
#
# $Header: /cvsroot/psqlodbc/psqlodbc/Attic/GNUmakefile,v 1.18 2001/08/27 00:38:55 petere Exp $
#
#-------------------------------------------------------------------------

subdir = src/interfaces/odbc
top_builddir = ../../..
include $(top_builddir)/src/Makefile.global

# Shared library parameters
NAME = psqlodbc
SO_MAJOR_VERSION = 0
SO_MINOR_VERSION = 27

override CPPFLAGS := -I$(srcdir) $(CPPFLAGS) -DHAVE_CONFIG_H -DODBCINSTDIR='"$(odbcinst_ini_dir)"'


OBJS = info.o bind.o columninfo.o connection.o convert.o drvconn.o \
        environ.o execute.o lobj.o misc.o options.o \
        pgtypes.o psqlodbc.o qresult.o results.o socket.o parse.o statement.o \
        gpps.o tuple.o tuplelist.o dlg_specific.o odbcapi.o $(OBJX)

ifdef MULTIBYTE
OBJS += multibyte.o
endif

SHLIB_LINK = $(filter -lm, $(LIBS))

all: all-lib

# Shared library stuff
include $(top_srcdir)/src/Makefile.shlib

# Symbols must be resolved to the version in the shared library because
# the driver manager (e.g., iodbc) provides some symbols with the same
# names and we don't want those.  (This issue is probably ELF specific.)
LINK.shared += $(shlib_symbolic)


install: all installdirs
	$(INSTALL_DATA) $(srcdir)/odbcinst.ini $(DESTDIR)$(odbcinst_ini_dir)/odbcinst.ini
	$(INSTALL_DATA) $(srcdir)/odbc.sql $(DESTDIR)$(datadir)/odbc.sql
	$(MAKE) install-lib

installdirs:
	$(mkinstalldirs) $(DESTDIR)$(libdir) $(DESTDIR)$(odbcinst_ini_dir) $(DESTDIR)$(datadir)

uninstall: uninstall-lib
	rm -f $(DESTDIR)$(datadir)/odbc.sql
# XXX Uninstall the .ini file as well?

clean distclean maintainer-clean: clean-lib
	rm -f $(OBJS)
