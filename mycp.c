#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct { bool v, i, f; } Opt;

static void perr(const char *fmt, ...) {
    va_list ap; fputs("cp: ", stderr);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fprintf(stderr, ": %s\n", strerror(errno));
}
static void perr_msg(const char *fmt, ...) {
    va_list ap; fputs("cp: ", stderr);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
}
static inline const char* base(const char *p){ const char *s=strrchr(p,'/'); return s? s+1 : p; }
static inline bool is_dir(const char *p){ struct stat st; return stat(p,&st)==0 && S_ISDIR(st.st_mode); }
static inline bool same_file(const struct stat*a,const struct stat*b){ return a->st_ino==b->st_ino && a->st_dev==b->st_dev; }

static bool ask_overwrite(const char *dst){
    fprintf(stderr,"cp: overwrite '%s'? ",dst); fflush(stderr);
    int c=getchar(); int d; while((d=getchar())!='\n' && d!=EOF){}  // очистим строку
    return c=='y'||c=='Y';
}

static int copy1(const char *src, const char *dst, const Opt *o){
    struct stat ss, ds; int dst_ok = (stat(dst,&ds)==0);
    if (stat(src,&ss)<0){ perr("cannot stat '%s'",src); return -1; }
    if (!S_ISREG(ss.st_mode)){ perr_msg("omitting non-regular file '%s'",src); return -1; }
    if (dst_ok && S_ISDIR(ds.st_mode)){ perr_msg("cannot overwrite directory '%s' with non-directory",dst); return -1; }
    if (dst_ok && same_file(&ss,&ds)){ perr_msg("'%s' and '%s' are the same file",src,dst); return -1; }
    if (dst_ok){
        if (o->i && !ask_overwrite(dst)) return 0;
        if (o->f) unlink(dst);
    }
    int in = open(src,O_RDONLY);
    if (in<0){ perr("cannot open '%s' for reading",src); return -1; }
    int out = open(dst,O_WRONLY|O_CREAT|O_TRUNC, ss.st_mode & 0777);
    if (out<0){ int e=errno; close(in); errno=e; perr("cannot create regular file '%s'",dst); return -1; }

    char buf[131072]; int rc=0;
    while (1){
        ssize_t n=read(in,buf,sizeof buf);
        if (n==0) break;
        if (n<0){ perr("error reading '%s'",src); rc=-1; break; }
        for (ssize_t off=0; off<n; ){
            ssize_t m=write(out,buf+off,(size_t)(n-off));
            if (m<=0){ perr("error writing '%s'",dst); rc=-1; break; }
            off+=m;
        }
        if (rc) break;
    }
    if (close(in)!=0 || close(out)!=0) rc=-1;
    if (!rc && o->v) printf("'%s' -> '%s'\n",src,dst);
    return rc;
}

static void usage(const char *p){
    fprintf(stderr,"Usage: %s [OPTIONS] SRC DST\n       %s [OPTIONS] SRC... DIR\n",p,p);
}



int main(int argc, char **argv){
    int PATH_MAX = 4096;
    Opt o={0}; const char *paths[argc]; int n=0; bool endopts=false;
    for (int i=1;i<argc;i++){
        const char *a=argv[i];
        if (!endopts && strcmp(a,"--")==0){ endopts=true; continue; }
        if (!endopts && !strcmp(a,"--verbose")){ o.v=true; continue; }
        if (!endopts && !strcmp(a,"--interactive")){ o.i=true; o.f=false; continue; }
        if (!endopts && !strcmp(a,"--force")){ o.f=true; o.i=false; continue; }
        if (!endopts && a[0]=='-' && a[1]){
            for (const char *p=a+1; *p; ++p){
                if (*p=='v') o.v=true;
                else if (*p=='i'){ o.i=true; o.f=false; }
                else if (*p=='f'){ o.f=true; o.i=false; }
                else { usage(argv[0]); return 1; }
            }
            continue;
        }
        paths[n++]=a;
    }
    if (n<2){ usage(argv[0]); return 1; }

    int rc=0;
    if (n==2){
        const char *src=paths[0], *dst=paths[1];
        if (is_dir(dst)){
            char to[PATH_MAX]; snprintf(to,sizeof to,"%s/%s",dst,base(src));
            if (copy1(src,to,&o)!=0) rc=1;
        }else{
            if (copy1(src,dst,&o)!=0) rc=1;
        }
    }else{
        const char *dir=paths[n-1];
        if (!is_dir(dir)){ perr_msg("target '%s' is not a directory",dir); return 1; }
        for (int i=0;i<n-1;i++){
            char to[PATH_MAX]; snprintf(to,sizeof to,"%s/%s",dir,base(paths[i]));
            if (copy1(paths[i],to,&o)!=0) rc=1;
        }
    }
    return rc;
}
