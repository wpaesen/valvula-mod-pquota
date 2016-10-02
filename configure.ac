dnl  mod-pquota: quota module with punish period for valvula
dnl  Copyright (C) 2015 Wouter Paesen <wouter@blue-gate.be>
dnl
dnl  This program is free software; you can redistribute it and/or
dnl  modify it under the terms of the GNU General Public License as
dnl  published by the Free Software Foundation; either version 2.1 of
dnl  the License, or (at your option) any later version.
dnl
dnl  This program is distributed in the hope that it will be useful,
dnl  but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
dnl  General Public License for more details.
dnl
dnl  You should have received a copy of the GNU General Public License
dnl  along with this program; if not, write to the Free Software
dnl  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
dnl  02111-1307 USA
dnl  
dnl  You may find a copy of the license under this software is
dnl  released at COPYING file.
dnl

AC_INIT(src/mod-pquota.h)

PQUOTA_VERSION="0.0.4"
AC_SUBST(PQUOTA_VERSION)

axl_string_version=axl-`axl-knife --version`
AC_SUBST(axl_string_version)

AC_CONFIG_AUX_DIR(.)
AM_INIT_AUTOMAKE(mod-pquota, $PQUOTA_VERSION)
AM_CONFIG_HEADER(config.h)
AM_MAINTAINER_MODE

AC_PROG_CC
AC_ISC_POSIX
AC_HEADER_STDC
AM_PROG_LIBTOOL

compiler_options=""
STRICT_PROTOTYPES=""
if test "$compiler" = "gcc" ; then
   compiler_options="-Wstrict-prototypes -Wall -Werror -g -ansi -fstack-protector-all -Wstack-protector"
   echo "Detected gcc compiler: $compiler, adding options: $compiler_options"
fi
AC_SUBST(compiler_options)

AC_CHECK_PROG(PKG_CONFIG, pkg-config, "yes", "no")
if test "$PKG_CONFIG" = "no" ; then
  AC_MSG_ERROR([You need to install pkg-config to compile mod-pqouta. See: http://pkgconfig.freedesktop.org/releases/])
fi

dnl LibAxl library support.
PKG_CHECK_MODULES(AXL, axl >= 0.7.1.b3508)
AC_SUBST(AXL_CFLAGS)
AC_SUBST(AXL_LIBS)

dnl valvula development headers
PKG_CHECK_MODULES(VALVULA, valvula >= 1.0.8.b310 valvulad >= 1.0.8.b310)
AC_SUBST(VALVULA_CFLAGS)
AC_SUBST(VALVULA_LIBS)

dnl general libries subsitution
dnl AC_SUBST(LIBRARIES_CFLAGS)
dnl AC_SUBST(LIBRARIES_LIBS)

dnl get current platform and add especific flags
case $host in
  *-*-beos*)
    echo "Found BEOS platform: $host.."
    ;;
  *-*-cygwin*)
    echo "Found cygwin platform: $host.."
    ;;
  *-*-mingw*)
    echo "Found mingw platform: $host.."
    ADDITIONAL_LIBS=-lws2_32
    ;;
  *)
    echo "Found platform: $host.."
    ;;
esac
AC_SUBST(ADDITIONAL_LIBS)

# check additional flags for exarg
AC_TRY_LINK([#define _GNU_SOURCE
#include <stdio.h>], 
[
  char * result;
  return vasprintf (&result, "This is a test: %d", NULL);
], [have_vasprintf=yes],[have_vasprintf=no])
echo "Checking vasprintf support: $have_vasprintf"

dnl support for vasprintf
case $have_vasprintf in
yes)
	EXARG_FLAGS="-DHAVE_VASPRINTF $EXARG_FLAGS"
	echo "vasprintf support found"
     ;;
esac

dnl check for windows platform
case $host in
*-*-mingw*)
	EXARG_FLAGS="-DOS_WIN32 $EXARG_FLAGS"
	echo "windows platform found"
	;;	
*)
	echo "platform found: $host"
    ;;
esac
AC_SUBST(EXARG_FLAGS)	

AC_OUTPUT([
Makefile
src/Makefile
])
