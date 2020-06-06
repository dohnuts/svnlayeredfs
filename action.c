
#include "svnlayered-fuse.h"
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>
#include <limits.h>
#include <sys/param.h>

//////////////////////////////////////////////////////////////////////////
/// basic core logic
//////////////////////////////////////////////////////////////////////////

inline char * dn_to_buf(struct dirname* np, const char* src, size_t src_len) {
    char * buf = self()->private_data->concat;
    if ( src_len == 0 ) {
        src_len = strnlen(src, MAXPATHLEN-1);
    }
    memcpy(buf, np->path, np->len);
    memcpy(buf + np->len, src, src_len);
    buf[np->len+src_len] = '\0';
    return buf;
}

// //should resolve symlink sometimes @@ , because they are relative
const char* source_path_to_real_path(const char* callee , const char* src) {
    // look some kind of hash / tree to get the file ? the only way it sould  be usefull,
    // if we kep real underpath is to flush it on svn up ...
    // so it would be path->level
    // source path
    // https://ftp.eu.openbsd.org/pub/OpenBSD/6.6/packages/amd64/uthash-2.1.0.tgz
    // the time of hash calculation must < of the underliying does file exists call... ( and it s prob tmpfs )
    size_t ssrc = strnlen(src, MAXPATHLEN-1);
    struct dirname* np;

    SLIST_FOREACH(np, &(self()->private_data->dir_names), entries) {
        // stat may be prob faster,,, memcpy(void *dst, const void *src, size_t len);
        // LOG(LOG_INFO, "FS(%s) src(%s)+(%s)=>%s", self()->private_data->mount, src, np->path, buf);
        char * buf = dn_to_buf(np, src, ssrc);
        if( access( buf, F_OK ) == 0 ) {
            LOG(LOG_INFO, "%s:%s %s -> %s", callee, self()->private_data->mount, src, buf);
            return buf;
        }
    }
    LOG(LOG_INFO, "%s:%s %s -> NOTHING", callee, self()->private_data->mount, src);
    return NULL;
}

inline char * new_path(const char* src) {
   struct dirname* l = SLIST_FIRST( &(self()->private_data->dir_names)); // top layer / work dir
   return dn_to_buf(l, src, 0);
}


//////////////////////////////////////////////////////////////////////////
/// other core logic
//////////////////////////////////////////////////////////////////////////

int update_layer(const char* dname) {
    struct dirname* np;

    SLIST_FOREACH(np, &(self()->private_data->dir_names), entries) {
        if (strcmp(dname,np->path) == 0) {
            return 0;
        }
    }
    return -ENOENT;
}
