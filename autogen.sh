#!/bin/sh
# you can either set the environment variables AUTOCONF and AUTOMAKE
# to the right versions, or leave them unset and get the RedHat 7.3 defaults
if test -z $AUTOMAKE; then 
  export AUTOMAKE=automake
  export ACLOCAL=aclocal
fi

# if you would want to be running autoheader as well, you will have to do
# something similar as above for it
if test -z $AUTOCONF; then export AUTOCONF=autoconf; fi
echo "#!/bin/sh" > autoregen.sh
echo "./autogen.sh $@ \$@" >> autoregen.sh
chmod +x autoregen.sh

set -x

# initialize i18n support
CONFIGURE=configure.ac
if grep "^AM_[A-Z0-9_]\{1,\}_GETTEXT" "$CONFIGURE" >/dev/null; then
 if grep "sed.*POTFILES" "$CONFIGURE" >/dev/null; then
  GETTEXTIZE=""
 else
  if grep "^AM_GLIB_GNU_GETTEXT" "$CONFIGURE" >/dev/null; then
    GETTEXTIZE="glib-gettextize"
  else
    GETTEXTIZE="gettextize"
  fi

  $GETTEXTIZE --version < /dev/null > /dev/null 2>&1
  if test $? -ne 0; then
    echo
    echo "**Error**: You must have \`$GETTEXTIZE' installed" \
         "to compile $PKG_NAME."
    DIE=1
  fi
 fi
fi
(grep "^AC_PROG_INTLTOOL" "$CONFIGURE" >/dev/null) && {
(intltoolize --version) < /dev/null > /dev/null 2>&1 || {
 echo
 echo "**Error**: You must have \`intltoolize' installed" \
      "to compile $PKG_NAME."
 DIE=1
 }
}

# if any of these steps fails, the others will not execute, which is good
# we want to treat errors as soon as possible
$ACLOCAL || exit 1
libtoolize --force || exit 1
autoheader || exit 1
$AUTOMAKE -a || exit 1
$AUTOCONF || exit 1

if test "$GETTEXTIZE"; then
 echo "Creating $dr/aclocal.m4 ..."
 test -r aclocal.m4 || touch aclocal.m4
 echo "Running $GETTEXTIZE...  Ignore non-fatal messages."
 echo "no" | $GETTEXTIZE --force --copy
 echo "Making aclocal.m4 writable ..."
 test -r aclocal.m4 && chmod u+w aclocal.m4
fi
if grep "^AC_PROG_INTLTOOL" "$CONFIGURE" >/dev/null; then
 echo "Running intltoolize..."
 intltoolize --copy --force --automake
fi

#./configure --enable-maintainer-mode $*
./configure $@

