# NOTE
# This configure.ac is only used when building libqcdm separately from
# ModemManager.
#

AC_PREREQ(2.52)

AC_INIT(libwmc, 0.1, dcbw@redhat.com, libwmc)
AM_INIT_AUTOMAKE([1.9 subdir-objects tar-ustar no-dist-gzip dist-bzip2])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AM_MAINTAINER_MODE

AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_HEADERS(config.h)

dnl Define _GNU_SOURCE for various things like strcasestr()
AC_GNU_SOURCE

dnl Required programs
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_LIBTOOL

dnl
dnl Tests
dnl
AC_ARG_WITH(tests, AS_HELP_STRING([--with-tests], [Build libwmc tests]))
AM_CONDITIONAL(WITH_TESTS, test "x$with_tests" = "xyes")
case $with_tests in
    yes)
        PKG_CHECK_MODULES(MM, glib-2.0 >= 2.20)
	AC_SUBST(MM_CFLAGS)
	AC_SUBST(MM_LIBS)
        ;;
    *) ;;
esac

AM_CONDITIONAL(WMC_STANDALONE, test "yes" = "yes")

AC_CONFIG_FILES([
Makefile
src/Makefile
tests/Makefile
])
AC_OUTPUT

