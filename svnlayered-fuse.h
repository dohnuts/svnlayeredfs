#ifndef FUSE_CITY_H
#define FUSE_CITY_H

#define FUSE_USE_VERSION 27
#define PROGRAM "svnlayeredfs"
#define VERSION "0.5.0"

#define KEY_HELP (0)
#define KEY_VERSION (1)
#define KEY_LAYER (2)
#define KEY_RO (3)

#include <sys/stat.h>
#include <sys/queue.h>
#include <fuse.h>
#include <fuse_opt.h>
#include <svn_pools.h>

struct dirname {
    const char* path;
    size_t len;
    apr_array_header_t* spath;
    SLIST_ENTRY(dirname) entries;
};

struct slf_param {
    // help shown
    int help;
    // version information shown
    int version;
    //
    int ro;
    // underlay dir names
    SLIST_HEAD(list_dir_names, dirname) dir_names;
    // SLIST_HEAD(list_rev_dir_names, dirname) rev_dir_names;
    // mount location
    const char* mount;
    // buffer
    char* concat;
    // svn buffers
    apr_pool_t *        pool;
};

struct my_fuse_context {
    struct fuse*        fuse;
    uid_t               uid;            /* effective user id */
    gid_t               gid;            /* effective group id */
    pid_t               pid;            /* thread id */
    struct slf_param *private_data;	/* set by file system on mount */
    mode_t              umask;          /* umask of the thread */
};

#ifdef LOGS
#define LOG(lvl, fmt, arg...) syslog(lvl, fmt, ##arg)
#else
#define LOG(lvl, fmt, arg...) 
#endif

// the api
void operations(struct fuse_operations*);
void *slf_init(struct fuse_conn_info *conn);
void slf_destroy(void *data);
int slf_getattr(const char *src, struct stat *stbuf);
int slf_readdir(const char *src, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
int slf_statfs(const char *src, struct statvfs *stbuf);
int slf_open(const char *src, struct fuse_file_info *fi);
int slf_create(const char *src, mode_t mode, struct fuse_file_info *fi);
int slf_mknod(const char *src, mode_t mode, dev_t rdev);
int slf_read(const char *src, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int slf_write(const char *src, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int slf_release (const char *path, struct fuse_file_info *fi);
int slf_ftruncate(const char *src, off_t offset, struct fuse_file_info *fi);
int slf_truncate(const char *src, off_t offset);
int slf_unlink(const char *src);
int slf_rmdir(const char *src);
int slf_mkdir(const char *src, mode_t mode);
int slf_rename(const char *src, const char *dest);
int slf_utimens(const char *src, const struct timespec tv[2]);
int slf_setxattr(const char * a, const char *b, const char *c, size_t d, int e);
int slf_getxattr(const char *a, const char *b, char *c, size_t d);
int slf_listxattr(const char *a, char *b, size_t c);
int slf_removexattr(const char *a, const char *b);
int slf_chmod(const char *src, mode_t mode);
int slf_chown(const char *src, uid_t uid, gid_t gid);
int slf_flush(const char *a, struct fuse_file_info *c);
int slf_fsync(const char *a, int b, struct fuse_file_info *c);
int slf_fsyncdir(const char *a, int b, struct fuse_file_info *c);
int slf_opendir(const char *src, struct fuse_file_info * b);
int slf_releasedir(const char *a, struct fuse_file_info * b);
int slf_access(const char *src, int mask);
int slf_readlink(const char *src, char *buf, size_t size);
int slf_symlink(const char *src, const char *to);
void operations(struct fuse_operations* slf_oper);

// helper
struct my_fuse_context* self();
char * top_layer_path(const char*);
const char* source_path_to_real_path(const char* src, const char* real);
// action
int update_layer(const char*);
int deleted(const char*);

#endif
