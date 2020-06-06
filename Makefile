
PROG=svnlayered-fuse
SRCS=action.c api.c ${PROG}.c

CFLAGS+= -I /usr/local/include/apr-1 \
 -I /usr/local/include/subversion-1/ \
 -Wno-int-conversion
 

LDFLAGS=-L /usr/local/lib/ \
  -l svn_client-1 \
  -l svn_fs-1 \
  -l svn_repos-1 \
  -l svn_subr-1 \
  -l aprutil-1 \
  -l apr-1 \
  -l fuse


.include <bsd.prog.mk>
