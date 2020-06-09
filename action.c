
#include "svnlayered-fuse.h"
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>
#include <limits.h>
#include <sys/param.h>

#include <svn_client.h>
#include <svn_pools.h>
#include <svn_hash.h>
#include <svn_error.h>
#include <svn_fs.h>
#include <svn_repos.h>
#include <svn_dirent_uri.h>
#include <svn_dso.h>
#include <svn_props.h>
#include <svn_xml.h>
#include <apr_tables.h>   /* for apr_array_header_t */

//////////////////////////////////////////////////////////////////////////
/// basic core logic
//////////////////////////////////////////////////////////////////////////

struct my_fuse_context* self() {
    struct fuse_context* p = fuse_get_context();
    return (struct my_fuse_context*)p;
}

char * dn_to_buf(struct dirname* np, const char* src, size_t src_len) {
    char * buf = self()->private_data->concat;
    if ( src_len == 0 ) {
        src_len = strnlen(src, MAXPATHLEN-1);
    }
    memcpy(buf, np->path, np->len);
    memcpy(buf + np->len, src, src_len);
    buf[np->len+src_len] = '\0';
    return buf;
}

const char* source_path_to_real_path(const char* callee , const char* src) {
    size_t ssrc = strnlen(src, MAXPATHLEN-1);
    struct dirname* np;

    SLIST_FOREACH(np, &(self()->private_data->dir_names), entries) {
        char * buf = dn_to_buf(np, src, ssrc);
        // use LSTAT not access to not loop over ourself!
        struct stat stbuf;
        int res = lstat(buf, &stbuf);
        if ( res ) continue;
        if ( np->spath == NULL ) return buf;
        //FIXME if ( is in delete list ) return NULL;
        if ( deleted(buf) ) return NULL;
        return buf;
    }
    return NULL;
}

inline char * top_layer_path(const char* src) {
   struct dirname* l = SLIST_FIRST( &(self()->private_data->dir_names)); // top layer / work dir
   return dn_to_buf(l, src, 0);
}


//////////////////////////////////////////////////////////////////////////
/// other core logic
//////////////////////////////////////////////////////////////////////////

static const svn_opt_revision_t HEAD = { svn_opt_revision_head, {0}};
static const svn_opt_revision_t WORKING = { svn_opt_revision_working, {0}};

int svn_prop_test(const char* f, const char* pname, apr_pool_t *pool, const char* value, size_t vsize) {

    apr_pool_t *scratch_pool = svn_pool_create(pool);
    apr_pool_t *iterpool = svn_pool_create(scratch_pool);
    apr_hash_t* result;

    svn_client_ctx_t *ctx;
    svn_client_create_context2(&ctx, NULL, pool);
    // const char* path = svn_dirent_internal_style("/home/digilan-token", pool);

    svn_error_t * err = svn_client_propget5( &result, NULL, pname, f
                                            , &WORKING , &WORKING
                                            , NULL , svn_depth_empty
                                            , NULL
                                            , ctx , scratch_pool , iterpool);

    if (err) {
        // svn_strerror(); ??
        LOG(LOG_ERR, "%s:%s svn prop %s, error code %d", "svn_prop_test", self()->private_data->mount, f, err->apr_err);
        return 0;
    }

    // int c = apr_hash_count(result);
    apr_array_header_t * aprops = svn_prop_hash_to_array(result, iterpool);
    for (int i = 0; i < aprops->nelts; i++)
    {
        svn_prop_t item = APR_ARRAY_IDX(aprops, i, svn_prop_t);
        const char *filename = item.name;
        const svn_string_t *propval = item.value;

        if ( propval->len != vsize || memcmp(propval->data, value, vsize) ) {
            continue;
        } else {
            svn_pool_destroy (iterpool);
            svn_pool_destroy (scratch_pool);
            return 1;
        }
    }

    svn_pool_destroy (iterpool);
    svn_pool_destroy (scratch_pool);
    return 0;
}

int deleted(const char* f) {
    return svn_prop_test(f, "deleted", self()->private_data->pool, "ON", 2);
}

int update(struct dirname* np, struct slf_param * param) {
    apr_array_header_t* paths = np->spath;
    apr_array_header_t* result;
    svn_client_ctx_t *ctx;
    svn_client_create_context2(&ctx, NULL, param->pool);
    apr_pool_t *scratch_pool = svn_pool_create(param->pool);

    svn_error_t * err = svn_client_update4(&result, paths, &HEAD,
                                            svn_depth_infinity, 0,
                                            1 /*ignore_externals*/, 1 /*allow_unver_obstructions*/,
                                            0 /*adds_as_modification*/, 1 /*make_parents*/,
                                            ctx, scratch_pool);

    if (err) {
        // svn_strerror(); ??
        LOG(LOG_ERR, "%s:%s svn up %s, error code %d", "update", param->mount, np->path, err->apr_err);
        return -EIO;
    }
    return 0;
}

int update_layer(const char* dname, struct slf_param * param) {
    struct dirname* np;
    LOG(LOG_INFO, "update_layer:%s `%s'", param->mount, dname);

    for (char* p = (char*)dname; (p = strchr(p, '_')); ++p) {
        *p = '/';
    }

    SLIST_FOREACH(np, &(param->dir_names), entries) {
        if (np->spath == NULL) continue;
        if (strcmp(dname, np->path) == 0) {
            int r = update(np, param);
            if (r == 0) {
                return 0;
            }
        }
    }
    return -ENOENT;
}