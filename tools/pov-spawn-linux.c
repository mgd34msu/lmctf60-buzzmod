#define _GNU_SOURCE
#include "pov-spawn-linux.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/close_range.h>
#include <linux/openat2.h>
#include <linux/sched.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef SYS_openat2
#define SYS_openat2 437
#endif
#ifndef SYS_clone3
#define SYS_clone3 435
#endif
#ifndef SYS_pidfd_send_signal
#define SYS_pidfd_send_signal 424
#endif

struct sha256 { uint32_t h[8]; uint64_t bytes; unsigned char b[64]; size_t used; };
static const uint32_t k[64]={
0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};
static uint32_t rr(uint32_t x,unsigned n){return(x>>n)|(x<<(32U-n));}
static void transform(struct sha256*c,const unsigned char*p){uint32_t w[64],a,b,d,e,f,g,h,x,y,z;unsigned i;for(i=0;i<16;i++)w[i]=((uint32_t)p[4*i]<<24)|((uint32_t)p[4*i+1]<<16)|((uint32_t)p[4*i+2]<<8)|p[4*i+3];for(;i<64;i++){x=rr(w[i-15],7)^rr(w[i-15],18)^(w[i-15]>>3);y=rr(w[i-2],17)^rr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+x+w[i-7]+y;}a=c->h[0];b=c->h[1];z=c->h[2];d=c->h[3];e=c->h[4];f=c->h[5];g=c->h[6];h=c->h[7];for(i=0;i<64;i++){x=h+(rr(e,6)^rr(e,11)^rr(e,25))+((e&f)^((~e)&g))+k[i]+w[i];y=(rr(a,2)^rr(a,13)^rr(a,22))+((a&b)^(a&z)^(b&z));h=g;g=f;f=e;e=d+x;d=z;z=b;b=a;a=x+y;}c->h[0]+=a;c->h[1]+=b;c->h[2]+=z;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;}
static void shainit(struct sha256*c){static const uint32_t h[8]={0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};memcpy(c->h,h,sizeof(c->h));c->bytes=0;c->used=0;}
static void shaupdate(struct sha256*c,const unsigned char*p,size_t n){c->bytes+=n;while(n){size_t q=64-c->used;if(q>n)q=n;memcpy(c->b+c->used,p,q);c->used+=q;p+=q;n-=q;if(c->used==64){transform(c,c->b);c->used=0;}}}
static void shafinal(struct sha256*c,unsigned char out[32]){uint64_t bits=c->bytes*8U;unsigned i;c->b[c->used++]=0x80;if(c->used>56){while(c->used<64)c->b[c->used++]=0;transform(c,c->b);c->used=0;}while(c->used<56)c->b[c->used++]=0;for(i=0;i<8;i++)c->b[63-i]=(unsigned char)(bits>>(8*i));transform(c,c->b);for(i=0;i<8;i++){out[4*i]=(unsigned char)(c->h[i]>>24);out[4*i+1]=(unsigned char)(c->h[i]>>16);out[4*i+2]=(unsigned char)(c->h[i]>>8);out[4*i+3]=(unsigned char)c->h[i];}}
static void explain(char*w,size_t n,const char*f,...){va_list a;if(!w||!n)return;va_start(a,f);(void)vsnprintf(w,n,f,a);va_end(a);}

int pov_hash_fd(int fd,char out[65]){struct sha256 c;unsigned char d[32],buf[16384];static const char hex[]="0123456789abcdef";ssize_t n;off_t at=lseek(fd,0,SEEK_CUR);if(lseek(fd,0,SEEK_SET)<0)return-1;shainit(&c);while((n=read(fd,buf,sizeof(buf)))>0)shaupdate(&c,buf,(size_t)n);if(n<0)return-1;shafinal(&c,d);for(size_t i=0;i<32;i++){out[2*i]=hex[d[i]>>4];out[2*i+1]=hex[d[i]&15];}out[64]=0;if(at>=0)(void)lseek(fd,at,SEEK_SET);return 0;}

static int check_chain(const char*path,char*w,size_t wn){
    char copy[4096],*save=NULL,*part;int dir,next;struct stat s;uid_t uid=getuid();size_t length;
    if(!path||path[0]!='/'||(length=strlen(path))>=sizeof(copy)){errno=EINVAL;explain(w,wn,"path must be absolute");return-1;}
    memcpy(copy,path,length+1);dir=open("/",O_PATH|O_DIRECTORY|O_CLOEXEC);if(dir<0)return-1;
    part=strtok_r(copy+1,"/",&save);
    while(part){
        char*following=save;while(*following=='/')following++;
        if(!strcmp(part,".")||!strcmp(part,"..")){errno=EINVAL;goto bad;}
        next=openat(dir,part,O_PATH|O_NOFOLLOW|O_CLOEXEC);if(next<0)goto bad;
        if(fstat(next,&s)<0){close(next);goto bad;}
        if(S_ISLNK(s.st_mode)||(*following&&!S_ISDIR(s.st_mode))){close(next);errno=EPERM;goto bad;}
        if(S_ISDIR(s.st_mode)&&s.st_uid!=0&&s.st_uid!=uid){close(next);errno=EPERM;goto bad;}
        if(S_ISDIR(s.st_mode)&&(s.st_mode&(S_IWGRP|S_IWOTH))&&!(s.st_uid==0&&(s.st_mode&S_ISVTX))){close(next);errno=EPERM;goto bad;}
        close(dir);dir=next;part=strtok_r(NULL,"/",&save);
    }
    close(dir);return 0;
bad:explain(w,wn,"unsafe path chain: %s",strerror(errno));close(dir);return-1;
}

int pov_open_directory(const char*path,int require_private,char*w,size_t wn){
    struct open_how how={0};struct stat s;int fd;
    if(check_chain(path,w,wn)<0)return-1;
    how.flags=O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW;
    how.resolve=RESOLVE_NO_SYMLINKS|RESOLVE_NO_MAGICLINKS;
    fd=(int)syscall(SYS_openat2,AT_FDCWD,path,&how,sizeof(how));
    if(fd<0||fstat(fd,&s)<0||!S_ISDIR(s.st_mode)||s.st_uid!=getuid()||
       (s.st_mode&(S_IWGRP|S_IWOTH))||(require_private&&(s.st_mode&077))){
        if(fd>=0)close(fd);
        errno=EPERM;explain(w,wn,"directory is not owned and safe");return-1;
    }
    return fd;
}

int pov_open_regular(const char*path,int executable,struct pov_image*out,char*w,size_t wn){struct open_how how={0};struct stat s;if(check_chain(path,w,wn)<0)return-1;how.flags=O_RDONLY|O_CLOEXEC|O_NOFOLLOW;how.resolve=RESOLVE_NO_SYMLINKS|RESOLVE_NO_MAGICLINKS;out->fd=(int)syscall(SYS_openat2,AT_FDCWD,path,&how,sizeof(how));if(out->fd<0){explain(w,wn,"openat2: %s",strerror(errno));return-1;}if(fstat(out->fd,&s)<0||!S_ISREG(s.st_mode)||s.st_nlink!=1||s.st_uid!=getuid()||(s.st_mode&(S_IWGRP|S_IWOTH))||(executable&&!(s.st_mode&0111))){close(out->fd);out->fd=-1;errno=EPERM;explain(w,wn,"file is not an owned safe regular%s",executable?" executable":"");return-1;}out->dev=s.st_dev;out->ino=s.st_ino;out->size=s.st_size;if(pov_hash_fd(out->fd,out->sha256)<0){close(out->fd);out->fd=-1;return-1;}return 0;}
int pov_open_image(const char*path,struct pov_image*out,char*w,size_t wn){unsigned char id[SELFMAG];if(pov_open_regular(path,1,out,w,wn)<0)return-1;if(pread(out->fd,id,sizeof(id),0)!=(ssize_t)sizeof(id)||memcmp(id,ELFMAG,SELFMAG)){close(out->fd);out->fd=-1;errno=ENOEXEC;explain(w,wn,"executable is not native ELF");return-1;}return 0;}

int pov_read_start_ticks(pid_t pid,unsigned long long*out){char path[64],buf[4096],*p,*save=NULL,*f;int fd,field=3;ssize_t n;(void)snprintf(path,sizeof(path),"/proc/%ld/stat",(long)pid);fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return-1;n=read(fd,buf,sizeof(buf)-1);close(fd);if(n<=0)return-1;buf[n]=0;p=strrchr(buf,')');if(!p||p[1]!=' ')return-1;p+=2;f=strtok_r(p," ",&save);while(f&&field<22){f=strtok_r(NULL," ",&save);field++;}if(!f)return-1;errno=0;*out=strtoull(f,NULL,10);return errno?-1:0;}
static int mapfd(int from,int to){if(from==to){int f=fcntl(from,F_GETFD);return f<0?-1:fcntl(from,F_SETFD,f&~FD_CLOEXEC);}return dup3(from,to,0);}
static int validate_pidfd_number(int pidfd,pid_t pid){
    char path[64],bytes[1024],*line,*save=NULL;int fd;ssize_t n;
    snprintf(path,sizeof(path),"/proc/self/fdinfo/%d",pidfd);fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return-1;
    n=read(fd,bytes,sizeof(bytes)-1);close(fd);if(n<=0)return-1;bytes[n]=0;
    line=strtok_r(bytes,"\n",&save);while(line){if(!strncmp(line,"Pid:\t",5)){char*end;long value=strtol(line+5,&end,10);return *end==0&&value==(long)pid?0:-1;}line=strtok_r(NULL,"\n",&save);}errno=EINVAL;return-1;
}
static void child_run(const struct pov_image*i,char*const av[],char*const ep[],const struct pov_stdio*m,int ef,pid_t parent){int e;sigset_t empty;sigemptyset(&empty);if(prctl(PR_SET_PDEATHSIG,SIGKILL)<0||getppid()!=parent||sigprocmask(SIG_SETMASK,&empty,NULL)<0||mapfd(m->input,0)<0||mapfd(m->output,1)<0||mapfd(m->error,2)<0||(m->cwd>=0&&fchdir(m->cwd)<0)||syscall(SYS_close_range,3U,UINT_MAX,CLOSE_RANGE_CLOEXEC)<0)goto fail;execveat(i->fd,"",av,ep,AT_EMPTY_PATH);fail:e=errno;for(size_t off=0;off<sizeof(e);){ssize_t sent=write(ef,(const char*)&e+off,sizeof(e)-off);if(sent>0){off+=(size_t)sent;continue;}if(sent<0&&errno==EINTR)continue;break;}_exit(127);}
int pov_spawn_image(const struct pov_image*i,char*const av[],char*const ep[],const struct pov_stdio*m,struct pov_child*out,char*w,size_t wn){int er[2],pfd=-1,e=0;ssize_t got;pid_t pid,parent=getpid();struct clone_args ca={0};if(pipe2(er,O_CLOEXEC)<0)return-1;ca.flags=CLONE_PIDFD;ca.pidfd=(uint64_t)(uintptr_t)&pfd;ca.exit_signal=SIGCHLD;pid=(pid_t)syscall(SYS_clone3,&ca,sizeof(ca));if(pid<0&&(errno==ENOSYS||errno==EINVAL))pid=(pid_t)syscall(SYS_clone,(unsigned long)(SIGCHLD|CLONE_PIDFD),NULL,&pfd,NULL,NULL);if(pid<0){e=errno;close(er[0]);close(er[1]);errno=e;return-1;}if(pid==0){close(er[0]);child_run(i,av,ep,m,er[1],parent);}close(er[1]);do got=read(er[0],&e,sizeof(e));while(got<0&&errno==EINTR);close(er[0]);out->pid=pid;out->pidfd=pfd;out->reaped=0;memset(&out->status,0,sizeof(out->status));if(got!=0){if(got==(ssize_t)sizeof(e))errno=e;explain(w,wn,"execveat: %s",strerror(errno));(void)syscall(SYS_pidfd_send_signal,pfd,SIGKILL,NULL,0);(void)waitid(P_PIDFD,(id_t)pfd,&out->status,WEXITED);out->reaped=1;return-1;}if(validate_pidfd_number(pfd,pid)<0||pov_read_start_ticks(pid,&out->start_ticks)<0){(void)syscall(SYS_pidfd_send_signal,pfd,SIGKILL,NULL,0);(void)waitid(P_PIDFD,(id_t)pfd,&out->status,WEXITED);out->reaped=1;return-1;}return 0;}
static int read_path(const char*path,unsigned char**out,size_t*n){int fd=open(path,O_RDONLY|O_CLOEXEC);unsigned char*b;size_t used=0,cap=4096;ssize_t got;if(fd<0)return-1;b=malloc(cap);if(!b){close(fd);return-1;}while((got=read(fd,b+used,cap-used))>0){used+=(size_t)got;if(used==cap){if(cap>=1048576){free(b);close(fd);errno=E2BIG;return-1;}cap*=2;b=realloc(b,cap);if(!b){close(fd);return-1;}}}close(fd);if(got<0){free(b);return-1;}*out=b;*n=used;return 0;}
int pov_validate_child(const struct pov_child*c,const struct pov_image*i,char*const av[],char*w,size_t wn){char p[64],h[65];struct stat s;unsigned long long t;unsigned char*a;size_t n,need=0,off=0;int fd;if(pov_read_start_ticks(c->pid,&t)<0||t!=c->start_ticks)goto mismatch;(void)snprintf(p,sizeof(p),"/proc/%ld/exe",(long)c->pid);fd=open(p,O_RDONLY|O_CLOEXEC);if(fd<0||fstat(fd,&s)<0||s.st_dev!=i->dev||s.st_ino!=i->ino||s.st_size!=i->size||pov_hash_fd(fd,h)<0||strcmp(h,i->sha256)){if(fd>=0)close(fd);goto mismatch;}close(fd);for(size_t x=0;av[x];x++)need+=strlen(av[x])+1;(void)snprintf(p,sizeof(p),"/proc/%ld/cmdline",(long)c->pid);if(read_path(p,&a,&n)<0)return-1;if(n!=need){free(a);goto mismatch;}for(size_t x=0;av[x];x++){size_t q=strlen(av[x])+1;if(memcmp(a+off,av[x],q)){free(a);goto mismatch;}off+=q;}free(a);return 0;mismatch:errno=EPERM;explain(w,wn,"child identity mismatch");return-1;}
int pov_signal_child(const struct pov_child*c,int sig){if(c->reaped){errno=ESRCH;return-1;}return(int)syscall(SYS_pidfd_send_signal,c->pidfd,sig,NULL,0);}
int pov_reap_child(struct pov_child*c,int options){int r;if(c->reaped)return 0;memset(&c->status,0,sizeof(c->status));r=waitid(P_PIDFD,(id_t)c->pidfd,&c->status,WEXITED|options);if(r==0&&c->status.si_pid)c->reaped=1;return r;}
int pov_child_alive(const struct pov_child*c){struct pollfd p={.fd=c->pidfd,.events=POLLIN};return!c->reaped&&c->pidfd>=0&&poll(&p,1,0)==0;}
