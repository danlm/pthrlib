# Test for a working setcontext implementation.
#
# Copyright (C) 2003 Richard W.M. Jones <rich@annexia.org>
#
# Older versions of Linux had the setcontext/makecontext functions
# in glibc, but the functions were null. Duh! So the only way to
# determine if these functions are implemented and actually work
# is to try them out.

result=no

if $CC $CFLAGS \
    $srcdir/conf/test_setcontext.c -o conf/test_setcontext 2>/dev/null
then
    if conf/test_setcontext | grep 'ok' >/dev/null 2>&1; then
	result=yes
    fi
fi

if [ "x$result" = "xyes" ]; then
    echo "#define HAVE_WORKING_SETCONTEXT 1" >> config.h
else
    echo "/* #define HAVE_WORKING_SETCONTEXT 1 */" >> config.h
fi
