#include "svnlayered-fuse.h"
#include <string.h>
#include <syslog.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SPRP(X) source_path_to_real_path(__FUNCTION__,X)


//////////////////////////////////////////////////////////////////////////
/**
 * Initialize filesystem
 *
 * Report current working dir and archive file name to syslog.
 *
 * 
struct fuse_conn_info {
	uint32_t	proto_major;
	uint32_t	proto_minor;
	uint32_t	async_read;
	uint32_t	max_write;
	uint32_t	max_readahead;
	uint32_t	capable;
	uint32_t	want;
	uint32_t	max_background;
	uint32_t	congestion_threshold;
	uint32_t	reserved[23];
};
 * @return filesystem-private data
 */
void *slf_init(struct fuse_conn_info *conn)
{
    if (conn->capable & FUSE_CAP_FLOCK_LOCKS)
        conn->want |= FUSE_CAP_FLOCK_LOCKS;
    if (conn->capable & FUSE_CAP_EXPORT_SUPPORT)
        conn->want |= FUSE_CAP_EXPORT_SUPPORT;

    LOG(LOG_INFO, "Mounting file system on %s", self()->private_data->mount);
    return conn;
}

void slf_destroy(void *data)
{
    return;
}


//////////////////////////////////////////////////////////////////////////
///  FS hook         
//////////////////////////////////////////////////////////////////////////

int slf_getattr(const char *src, struct stat *stbuf)
{
    int res;
    const char *path = SPRP(src);
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, path);
    if (path == NULL)
        return -errno;
    res = lstat(path, stbuf);
    // TODO fix st_nlink  ?
    if (res == -1)
        return -errno;

    if (!S_ISDIR(stbuf->st_mode)) {
        return 0;
    }
    // fix st_nlink  for fancy optim of fts_read @@

    size_t ssrc = strnlen(src, MAXPATHLEN);
    // const char *path = SPRP(src); //carefull //non factorized , but same thing here
    stbuf->st_nlink = 0; /* number of hard links */
    struct dirname* np;
    SLIST_FOREACH(np, &(self()->private_data->dir_names), entries) {
        struct dirent *de;
        DIR *dp;
        char * buf = self()->private_data->concat;
        memcpy(buf, np->path, np->len);
        memcpy(buf + np->len, src, ssrc);
        buf[np->len+ssrc] = '\0';

        if ( access( buf, F_OK ) == 0 ) {
            path = buf;
            dp = opendir(path);
            if (dp == NULL)
                return -errno;
            while ((de = readdir(dp)) != NULL) {
                if (de->d_type == DT_DIR) {
                    stbuf->st_nlink++;
                }
            }

            closedir(dp);
        }
    }

    return 0;
}


struct findpath {
    char path[MAXPATHLEN+1];
    // char fullpath[MAXPATHLEN+1];
    struct stat st;
    SLIST_ENTRY(findpath) entries;
};

int slf_readdir(const char *src, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    int res;
    DIR *dp;
    struct dirent *de;
    struct findpath* it, * new;
    SLIST_HEAD(list_found_names, findpath) found_names;

    size_t ssrc = strnlen(src, MAXPATHLEN-1);
    struct dirname* np;
    char * buf = self()->private_data->concat;
    char *path = (char *)src;
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, path);
    if (offset > 0) {
        // code in lib says ignore . . . if call two time in a row keeping the result could help...
        // especially if offset is > 0 
        // LOG(LOG_ERR, "Unsupported offset (%s, req %s) -> FUFUFU %lld", self()->private_data->mount, src, offset);
        // return 0;
    }

    SLIST_INIT(&found_names);

    // const char *path = SPRP(src); //carefull //non factorized , but same thing here
    SLIST_FOREACH(np, &(self()->private_data->dir_names), entries) {
        memcpy(buf, np->path, np->len);
        memcpy(buf + np->len, src, ssrc);
        buf[np->len+ssrc] = '\0';

        if( access( buf, F_OK ) == 0 ) {
            path = buf;
            dp = opendir(path);
            if (dp == NULL)
                return -errno;

gnext:
            while ((de = readdir(dp)) != NULL) {
                if (strncmp(".", de->d_name, 2) == 0) goto gnext;
                if (strncmp("..", de->d_name, 3) == 0) goto gnext;
                SLIST_FOREACH(it, &(found_names), entries) {
                    // LOG(LOG_INFO, "readdir CHECK add(%s, req %s) -> %s == %s", self()->private_data->mount, src, it->path, de->d_name);
                    if (strncmp(it->path,de->d_name,MAXPATHLEN) == 0)
                        goto gnext;
                }
                new = malloc(sizeof(struct findpath));
                memset(new, 0, sizeof(struct findpath));
                strncpy(new->path, de->d_name, de->d_namlen);
                new->st.st_ino = 0;
                new->st.st_mode = de->d_type << 12;

                SLIST_INSERT_HEAD(&(found_names), new, entries);
                filler(buffer, new->path, &(new->st), 0);
            }

            closedir(dp);
        }
    }
    if (path == NULL)
        return -errno;
    
    filler(buffer,".", 0, NULL);
    filler(buffer,"..", 0, NULL);
    while (!SLIST_EMPTY(&found_names)) {
        struct findpath *n = SLIST_FIRST(&found_names);
        SLIST_REMOVE_HEAD(&found_names, entries);
        free(n);
    }

    return 0;
}

int slf_statfs(const char *src, struct statvfs *stbuf)
{
    int res;
    const char *path = new_path("/");
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, path);
    res = statvfs(path, stbuf);
    if (res == -1)
        return -errno;
    return 0;
}

int slf_open(const char *src, struct fuse_file_info *fi)
{
    int res;
    const char *path = SPRP(src);
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, path);
    if (path == NULL) {
        path = new_path(path);
    }
    res = open(path, fi->flags);
    if (res == -1)
        return -errno;

    fi->fh = res;
    return 0;
}

int slf_create(const char *src, mode_t mode, struct fuse_file_info *fi)
{
    int res;
    const char *path = SPRP(src);
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, path);
    if (path == NULL) {
        path = new_path(path);
    }
    res = open(path, fi->flags, mode);
    if (res == -1)
        return -errno;

    fi->fh = res;
    return 0;
}

int slf_mknod(const char *src, mode_t mode, dev_t rdev)
{
    int res;
    const char *path = SPRP(src);
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, path);
    if (path == NULL) {
        path = new_path(path);
    }
    int dirfd = AT_FDCWD;
    if (S_ISREG(mode)) {
        res = openat(dirfd, path, O_CREAT | O_EXCL | O_WRONLY, mode);
        if (res >= 0)
            res = close(res);
    } else if (S_ISDIR(mode)) {
        res = mkdirat(dirfd, path, mode);
    } else if (S_ISFIFO(mode)) {
        res = mkfifoat(dirfd, path, mode);
    } else if (S_ISSOCK(mode)) {
        return -ENOTSUP;
    } else {
        res = mknodat(dirfd, path, mode, rdev);
    }
    if (res == -1)
        return -errno;
    return 0;
}

int slf_read(const char *src, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	int fd;
	int res;

	if (fi == NULL) {
        const char *path = SPRP(src);
        if (path == NULL)
            return -errno;
		fd = open(path, O_RDONLY);
    }
	else {
		fd = fi->fh;
        LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, src);
    }
	
	if (fd == -1)
		return -errno;

	res = pread(fd, buf, size, offset);
	if (res == -1)
		res = -errno;

	if(fi == NULL)
		close(fd);
	return res;
}

int slf_write(const char *src, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int fd;
    int res;

    if(fi != NULL) {
        fd = fi->fh;
        LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, src);
    } else {
        const char *path = SPRP(src);
        if (path == NULL)
            return -errno;
        fd = open(path, O_WRONLY);
    }    
    if (fd == -1)
        return -errno;

    res = pwrite(fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    if(fi == NULL)
        close(fd);
    return res;
}

int slf_release (const char *path, struct fuse_file_info *fi)
{
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, path);
    (void) path;
    close(fi->fh);
    return 0;
}

int slf_ftruncate(const char *src, off_t offset, struct fuse_file_info *fi)
{
    int res;

    if (fi != NULL)
    {
        LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, src);
        res = ftruncate(fi->fh, offset);
        if (res == -1)
            return -errno;
        return 0;
    }

    const char *path = SPRP(src);
    if (path == NULL)
        return -errno;
    res = truncate(path, offset);
    if (res == -1)
        return -errno;

    return 0;
}

int slf_truncate(const char *src, off_t offset)
{
    int res;
    const char *path = SPRP(src);
    if (path == NULL)
        return -errno;

    res = truncate(path, offset);
    if (res == -1)
        return -errno;

    return 0;
}

int slf_unlink(const char *src)
{
    const char *path = SPRP(src);
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, path);
    const char *npath = new_path(src);
    if (path != npath) {
        return -EPERM;
    }

    int res = unlink(path);
    if (res == -1)
        return -errno;

    return 0;
}

int slf_rmdir(const char *src)
{
    const char *path = SPRP(src);
    const char *npath = new_path(path);
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, npath);
    if (path != npath) {
        return -EPERM;
    }
    int res = rmdir(path);
    if (res == -1)
        return -errno;
    return 0;
}

int slf_mkdir(const char *src, mode_t mode)
{
    const char *path = SPRP(src);
    if ( path != NULL ) {
        return -EEXIST;
    }
    const char *npath = new_path(src);
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, npath);
    int res = mkdir(npath, mode);
    if (res == -1)
        return -errno;

    return 0;
}

int slf_rename(const char *src, const char *dest)
{
    const char *path = SPRP(src);
    if ( path == NULL ) {
        return -ENOENT;
    }
    const char *npath = new_path(src);
    if (path != npath) {
        return -EPERM;
    }
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, npath);
    const char *to = SPRP(dest);
    if (to == NULL) {
        to = new_path(dest);
    }
    int res = rename(npath, to);
    if (res == -1)
        return -errno;
    return 0;
}

int slf_utimens(const char *src, const struct timespec tv[2])
{
    int res;
    const char *path = SPRP(src); //should resolve symlink sometimes @@
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, path);
    if (path == NULL)
        return -errno;

    res = utimensat(0, path, tv, 0);
    if (res == -1)
        return -errno;

    return 0;
}

int slf_setxattr(const char * a, const char *b, const char *c, size_t d, int e)
{
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, a);
    return -ENOTSUP;
}

int slf_getxattr(const char *a, const char *b, char *c, size_t d)
{
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, a);
    return -ENOTSUP;
}

int slf_listxattr(const char *a, char *b, size_t c)
{
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, a);
    return -ENOTSUP;
}

int slf_removexattr(const char *a, const char *b)
{
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, a);
    return -ENOTSUP;
}

int slf_chmod(const char *src, mode_t mode)
{
    int res;
    const char *path = SPRP(src);
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, path);
    if (path == NULL)
        return -errno;
    const char *npath = new_path(src);
    if (path != npath) {
        return -EPERM;
    }

    res = chmod(path, mode); 
    if (res == -1)
        return -errno;

    return 0;
}

int slf_chown(const char *src, uid_t uid, gid_t gid)
{
    int res;
    const char *path = SPRP(src);
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, path);
    if (path == NULL)
        return -errno;
    const char *npath = new_path(src);
    if (path != npath) {
        return -EPERM;
    }

    res = chown(path, uid, gid); //should resolve symlink sometimes @@ or use lchmod,,,
    if (res == -1)
        return -errno;

    return 0;
}

int slf_flush(const char *a, struct fuse_file_info *c)
{
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, a);
    return 0;
}

int slf_fsync(const char *a, int b, struct fuse_file_info *c)
{
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, a);
    return 0;
}

int slf_fsyncdir(const char *a, int b, struct fuse_file_info *c)
{
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, a);
    return 0;
}

int slf_opendir(const char *src, struct fuse_file_info * b)
{
    int res;
    const char *path = SPRP(src);
    struct stat st;
    res = lstat(path, &st);
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, src);
    if (S_ISDIR(st.st_mode)) {
        return 0;
    }
    return -ENOTDIR;
}

int slf_releasedir(const char *a, struct fuse_file_info * b)
{
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, a);
    return 0;
}

int slf_access(const char *src, int mask)
{
    int res;
    const char *path = SPRP(src);
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, path);
    if (path == NULL)
        return -errno;
    res = access(path, mask);
    if (res == -1)
        return -errno;

    return 0;
}

int slf_readlink(const char *src, char *buf, size_t size)
{
    int res;
    const char *path = SPRP(src);
    LOG(LOG_INFO, "%s:%s:%s", __FUNCTION__, self()->private_data->mount, path);
    if (path == NULL)
        return -errno;

    res = readlink(path, buf, size - 1);
    if (res == -1)
        return -errno;

    buf[res] = '\0';
    return 0;
}

/*what does that mean @@ where is THE link lol we ont have a medium*/
// ln -s /home/below/subbelow /mnt/lnsb ok, ln -s /mnt/subbelow /mnt/lnsb NOK => a FD problem ?
int slf_symlink(const char *src, const char *to)
{
    if (strcmp("svnup",src) ==0 ) return update_layer(to);
    const char *dest = new_path(to);
    LOG(LOG_INFO, "%s:%s:%s->%s", __FUNCTION__, self()->private_data->mount, src, dest);
    int res = symlink(src, dest);
    if (res == -1)
        LOG(LOG_INFO, "FAILED %s:%s:%s->%s %d", __FUNCTION__, self()->private_data->mount, src, dest, errno);
        return -errno;

    LOG(LOG_INFO, "POST %s:%s:%s->%s 0", __FUNCTION__, self()->private_data->mount, src, dest);
    return 0;
}


void operations(struct fuse_operations* slf_oper) {
    slf_oper->init       =   slf_init;
    slf_oper->destroy    =   slf_destroy;
    slf_oper->readdir    =   slf_readdir;
    slf_oper->getattr    =   slf_getattr;
    slf_oper->statfs     =   slf_statfs;
    slf_oper->open       =   slf_open;
    slf_oper->read       =   slf_read;
    slf_oper->write      =   slf_write;
    slf_oper->release    =   slf_release;
    slf_oper->unlink     =   slf_unlink;
    slf_oper->rmdir      =   slf_rmdir;
    slf_oper->mkdir      =   slf_mkdir;
    slf_oper->rename     =   slf_rename;
    slf_oper->create     =   slf_create;
    slf_oper->mknod      =   slf_mknod;
    slf_oper->chmod      =   slf_chmod;
    slf_oper->chown      =   slf_chown;
    slf_oper->flush      =   slf_flush;
    slf_oper->fsync      =   slf_fsync;
    slf_oper->fsyncdir   =   slf_fsyncdir;
    slf_oper->opendir    =   slf_opendir;
    slf_oper->releasedir =   slf_releasedir;
    slf_oper->access     =   slf_access;
    slf_oper->utimens    =   slf_utimens;
    slf_oper->ftruncate  =   slf_ftruncate;
    slf_oper->truncate   =   slf_truncate;
    slf_oper->setxattr   =   slf_setxattr;
    slf_oper->getxattr   =   slf_getxattr;
    slf_oper->listxattr  =   slf_listxattr;
    slf_oper->removexattr=   slf_removexattr;
    slf_oper->readlink   =   slf_readlink;
    slf_oper->symlink    =   slf_symlink;
}   
 
