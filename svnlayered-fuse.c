#include "svnlayered-fuse.h"
#include <string.h>
#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>


struct fuse_session *se;
struct fuse *fuse;

void print_usage() {
    fprintf(stderr, "usage: %s [options] -l/under -lbelow [-l...] <mountpoint>\n\n", PROGRAM);
    fprintf(stderr,
            "general options:\n"
            "    -o opt,[opt...]        mount options\n"
            "    -h   --help            print help\n"
            "    -V   --version         print version\n"
            "    -d                     debug version\n"
            "\n");
}

/**
 * Function to process arguments (called from fuse_opt_parse).
 *
 * @param data  Pointer to fusezip_param structure
 * @param arg is the whole argument or option
 * @param key determines why the processing function was called
 * @param outargs the current output argument list
 * @return -1 on error, 0 if arg is to be discarded, 1 if arg should be kept
 */
static int process_arg(void *data, const char *arg, int key, struct fuse_args *outargs)
{
    struct slf_param *param = (struct slf_param*)data;

    static int layer = 0;
    (void)outargs;

    // 'magic' fuse_opt_proc return codes
    const static int KEEP = 1;
    const static int DISCARD = 0;
    const static int ERROR = -1;

    switch (key) {
        case KEY_HELP: {
            print_usage();
            param->help = 1;
            return DISCARD;
        };
        case KEY_LAYER: {
            if ( layer == 1 ) {
                fprintf(stderr, "%s: -l dir !\n", PROGRAM);
                return ERROR;
            }
            layer = 1;
            return DISCARD;
        };
        case FUSE_OPT_KEY_NONOPT: {
            if ( layer == 1 ) {
                layer = 0;
                struct dirname* new = malloc(sizeof(struct dirname));
                new->path = arg;
                new->len = strnlen(arg, MAXPATHLEN-1);
                SLIST_INSERT_HEAD(&(param->dir_names), new, entries);
                // fprintf(stderr, "%s: %s targett !\n", PROGRAM, SLIST_FIRST(&(param->dir_names))->path); 
                return DISCARD;
            }
            if (param->mount == NULL) {
                param->mount = arg;
                fprintf(stderr, "%s: %s mountpoint !\n", PROGRAM, param->mount);
            } else {
                fprintf(stderr, "%s: only one mountpoint !\n", PROGRAM);
                return ERROR;
            }
            return KEEP;
        };
        default: {
            return DISCARD;
        };
    }
}

static const struct fuse_opt slf_opts[] = {
    FUSE_OPT_KEY("-h",          KEY_HELP),
    FUSE_OPT_KEY("--help",      KEY_HELP),
    FUSE_OPT_KEY("-V",          KEY_VERSION),
    FUSE_OPT_KEY("--version",   KEY_VERSION),
    FUSE_OPT_KEY("-l",          KEY_LAYER),
    FUSE_OPT_KEY("--layer",     KEY_LAYER),
    {NULL, 0, 0}
};

int main(int argc, char *argv[]) {
    if (sizeof(void*) > sizeof(uint64_t)) {
        fprintf(stderr,"%s: This program cannot be run on your system because of FUSE design limitation\n", PROGRAM);
        return EXIT_FAILURE;
    }
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    struct slf_param param;
    param.help = 0;
    param.version = 0;
    param.mount = NULL;
    param.concat = malloc(sizeof(char)*MAXPATHLEN*2);

    // SLIST_INIT(&param.rev_dir_names);
    SLIST_INIT(&param.dir_names);

    if (fuse_opt_parse(&args, &param, slf_opts, process_arg)) {
        fuse_opt_free_args(&args);
        return 1;
    } 
    
    // if all work is done inside options parsing...
    if (param.help) {
        // mostly testing
        while (!SLIST_EMPTY(&param.dir_names)) {
            struct dirname *n = SLIST_FIRST(&param.dir_names);
            // fprintf(stderr, "%s: %s del target !\n", PROGRAM, n->path);
            SLIST_REMOVE_HEAD(&param.dir_names, entries);
            free(n);
        }
        fuse_opt_free_args(&args);
        return 0;
    }


    openlog(PROGRAM, LOG_PID | LOG_PERROR, LOG_USER);

    static struct fuse_operations slf_oper;
    operations(&slf_oper);
    
    char *mountpoint;
    // this flag ignored because libzip does not supports multithreading
    int multithreaded;
    int res;

    fuse = fuse_setup(args.argc, args.argv, &slf_oper, sizeof(slf_oper), &mountpoint, &multithreaded, &param);
    // fuse_opt_free_args(&args);
    if (fuse == NULL) {
        return 1;
    }
    // Don't apply umask, use modes exactly as specified
    umask(0);

    se = fuse_get_session(fuse);
    fuse_set_signal_handlers(se);

    res = fuse_loop(fuse);
    fuse_remove_signal_handlers(se);
    fuse_teardown(fuse, mountpoint);
    return res;
}
