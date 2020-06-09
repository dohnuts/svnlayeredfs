
PROG=svnlayered-fuse
SRCS=action.c api.c ${PROG}.c

CFLAGS+= -I /usr/local/include/apr-1 \
 -I /usr/local/include/subversion-1/ \
 -Wno-int-conversion
 
LDFLAGS=\
  -Oz \
  -L /usr/local/lib/ \
  -L /usr/local/lib/db4/ \
  -l m \
  -l z \
  -l lz4 \
  -l sqlite3 \
  -l ssl \
  -l crypto \
  -l sasl2 \
  -l c -l expat \
  -l db \
  -l serf-1 \
  -l pthread \
  -l iconv \
  -l intl \
  /usr/local/lib/libaprutil-1.a \
  /usr/local/lib/libapr-1.a \
  /usr/local/lib/libsvn_client-1.a \
  /usr/local/lib/libsvn_fs-1.a \
  /usr/local/lib/libsvn_repos-1.a \
  /usr/local/lib/libsvn_subr-1.a \
  /usr/local/lib/libsvn_wc-1.a \
  /usr/local/lib/libsvn_delta-1.a\
  /usr/local/lib/libsvn_diff-1.a \
  /usr/local/lib/libsvn_ra-1.a \
  /usr/local/lib/libsvn_ra_local-1.a \
  /usr/local/lib/libsvn_fs_util-1.a \
  /usr/local/lib/libsvn_fs_fs-1.a \
  /usr/local/lib/libsvn_fs_x-1.a \
  /usr/local/lib/libsvn_fs_base-1.a \
  /usr/local/lib/libsvn_ra_svn-1.a \
  /usr/local/lib/libsvn_ra_serf-1.a \
  -l fuse

.include <bsd.prog.mk>
