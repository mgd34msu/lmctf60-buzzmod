#define _GNU_SOURCE
#include "pov-spawn-linux.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/openat2.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <termios.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef SYS_openat2
#define SYS_openat2 437
#endif
#ifndef SYS_renameat2
#define SYS_renameat2 316
#endif
#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1U << 0)
#endif

#define LINE_MAXIMUM 1024
#define ARG_MAXIMUM 256
#define PATH_MAXIMUM 4096
#define CONFIG_EVIDENCE "input-config.cfg"

enum state { OFF, PREFLIGHT, SERVER_RECORDING, CLIENT_READY, POV_STARTED,
             STOPPING, FINALIZING, PUBLISHED, FAILED };

struct options {
    const char *q2, *client, *root, *game, *config, *normal_log;
    const char *lane_root, *wave, *server, *map, *spectator, *target;
    const char *supervisor_fd_text, *iterate_fd_text, *waveloop_fd_text;
    unsigned port, duration, stagger, finalize_delay, ready_timeout;
    pid_t parent_pid;
    unsigned long long parent_start;
};

struct lines {
    char bytes[LINE_MAXIMUM + 1];
    size_t used;
    int dropping;
};

struct run {
    enum state state;
    int lane_fd, server_log, client_log, events, normal_log, q2_write, self_fd;
    int server_read, client_read, signals;
    int start_latched, stop_latched, failed, interrupted;
    int server_entered, client_packet, client_active, record_confirmed;
    int q2_started, client_started;
    struct pov_child q2, client;
    struct pov_image q2_image, client_image, config_image;
    dev_t config_evidence_dev;
    ino_t config_evidence_ino;
    off_t config_evidence_size;
    char self_hash[65], iterate_hash[65], waveloop_hash[65];
    char lane_name[256], demo_rel[PATH_MAXIMUM], record_line[PATH_MAXIMUM + 32];
    char failure[512];
    struct lines server_lines, client_lines;
    struct timespec authority_started, pov_started;
};

static long long mono_ns(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) < 0) return -1;
    return (long long)t.tv_sec * 1000000000LL + t.tv_nsec;
}

static void set_failure(struct run *r, const char *format, ...) {
    va_list ap;
    if (r->failed) return;
    r->failed = 1;
    r->state = FAILED;
    va_start(ap, format);
    (void)vsnprintf(r->failure, sizeof(r->failure), format, ap);
    va_end(ap);
}

static int write_all(int fd, const void *data, size_t size) {
    const unsigned char *p = data;
    while (size) {
        ssize_t n = write(fd, p, size);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        p += (size_t)n; size -= (size_t)n;
    }
    return 0;
}

static int event(struct run *r, const char *name) {
    char line[256];
    int n = snprintf(line, sizeof(line), "%lld %s\n", mono_ns(), name);
    if (n < 0 || (size_t)n >= sizeof(line)) return -1;
    return write_all(r->events, line, (size_t)n);
}

static int safe_atom(const char *s, size_t maximum) {
    size_t n;
    if (!s || !(n = strlen(s)) || n > maximum) return 0;
    for (size_t i = 0; i < n; ++i)
        if (!((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') ||
              (s[i] >= '0' && s[i] <= '9') || s[i] == '_' || s[i] == '-' || s[i] == '.')) return 0;
    return strcmp(s, ".") && strcmp(s, "..");
}

static int parse_uint(const char *s, unsigned *out, unsigned low, unsigned high) {
    char *end; unsigned long v;
    if (!s || !*s) return -1;
    errno = 0; v = strtoul(s, &end, 10);
    if (errno || *end || v < low || v > high) return -1;
    *out = (unsigned)v; return 0;
}

static int option_value(int argc, char **argv, int *i, const char **out) {
    if (*i + 1 >= argc) return -1;
    *out = argv[++*i]; return 0;
}

static int parse_options(int argc, char **argv, struct options *o) {
    memset(o, 0, sizeof(*o)); o->finalize_delay = 3; o->ready_timeout = 30;
    for (int i = 1; i < argc; ++i) {
        const char *v = NULL;
#define VALUE(name, field) if (!strcmp(argv[i], name)) { if (option_value(argc,argv,&i,&v)<0) return -1; o->field=v; continue; }
        VALUE("--q2ded", q2) VALUE("--client", client) VALUE("--gamedir-root", root)
        VALUE("--game", game) VALUE("--config", config) VALUE("--normal-log", normal_log)
        VALUE("--lane-root", lane_root) VALUE("--wave", wave) VALUE("--server", server) VALUE("--map", map)
        VALUE("--spectator", spectator) VALUE("--target", target)
        VALUE("--supervisor-fd", supervisor_fd_text) VALUE("--iterate-fd", iterate_fd_text) VALUE("--waveloop-fd", waveloop_fd_text)
#undef VALUE
        if (!strcmp(argv[i], "--port") || !strcmp(argv[i], "--duration") ||
            !strcmp(argv[i], "--stagger") || !strcmp(argv[i], "--finalize-delay") ||
            !strcmp(argv[i], "--ready-timeout") || !strcmp(argv[i], "--parent-pid") ||
            !strcmp(argv[i], "--parent-start")) {
            const char *name = argv[i]; char *end; unsigned long long x;
            if (option_value(argc,argv,&i,&v)<0) return -1;
            errno=0; x=strtoull(v,&end,10); if(errno||*end)return -1;
            if(!strcmp(name,"--port")){if(x<1||x>65535)return-1;o->port=(unsigned)x;}
            else if(!strcmp(name,"--duration")){if(x<1||x>86400)return-1;o->duration=(unsigned)x;}
            else if(!strcmp(name,"--stagger")){if(x>1)return-1;o->stagger=(unsigned)x;}
            else if(!strcmp(name,"--finalize-delay")){if(x<1||x>30)return-1;o->finalize_delay=(unsigned)x;}
            else if(!strcmp(name,"--ready-timeout")){if(x<1||x>300)return-1;o->ready_timeout=(unsigned)x;}
            else if(!strcmp(name,"--parent-pid")){if(x<1||x>INT32_MAX)return-1;o->parent_pid=(pid_t)x;}
            else o->parent_start=x;
            continue;
        }
        return -1;
    }
    if(!o->q2||!o->client||!o->root||!o->game||!o->config||!o->normal_log||
       !o->lane_root||!o->wave||!o->server||!o->map||!o->spectator||!o->target||
       !o->supervisor_fd_text||!o->iterate_fd_text||!o->port||!o->duration||!o->parent_pid||!o->parent_start)return-1;
    if(strcmp(o->server,"s03")||!safe_atom(o->game,63)||!safe_atom(o->wave,63)||!safe_atom(o->map,63)||
       !safe_atom(o->spectator,31)||strncmp(o->target,"[SG]",4)||!safe_atom(o->target+4,31))return-1;
    return 0;
}

static int open_dir(const char *path, int require_private) {
    char why[128];
    if (!path || path[0] != '/' || strstr(path, "/../") || strstr(path, "/./")) { errno=EINVAL; return -1; }
    return pov_open_directory(path,require_private,why,sizeof(why));
}

static int parse_fd(const char *text) {
    unsigned value;
    if(parse_uint(text,&value,3,1048575)<0)return-1;
    return (int)value;
}

static int hash_inherited_regular(const char *text, char hash[65]) {
    int fd=parse_fd(text); struct stat st;
    if(fd<0||fstat(fd,&st)<0||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||
       (st.st_mode&(S_IWGRP|S_IWOTH)))return-1;
    return pov_hash_fd(fd,hash);
}

static int validate_pinned_self(struct run *r, const char *text) {
    int fd=parse_fd(text);struct stat pinned,self;char hash[65];
    if(fd<0||fstat(fd,&pinned)<0||fstat(r->self_fd,&self)<0||!S_ISREG(pinned.st_mode)||
       pinned.st_dev!=self.st_dev||pinned.st_ino!=self.st_ino||pinned.st_size!=self.st_size||
       pov_hash_fd(fd,hash)<0||strcmp(hash,r->self_hash))return-1;
    return 0;
}

static int open_self(char hash[65]) {
    unsigned char id[4]; int fd=open("/proc/self/exe",O_RDONLY|O_CLOEXEC); struct stat st;
    if(fd<0||fstat(fd,&st)<0||!S_ISREG(st.st_mode)||pread(fd,id,4,0)!=4||memcmp(id,"\177ELF",4)||pov_hash_fd(fd,hash)<0){if(fd>=0)close(fd);return-1;}return fd;
}

static int mkdir_private(int parent, const char *name) {
    if(mkdirat(parent,name,0700)<0)return-1;
    int fd=openat(parent,name,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);struct stat st;
    if(fd<0||fstat(fd,&st)<0||st.st_uid!=getuid()||(st.st_mode&0777)!=0700){if(fd>=0)close(fd);return-1;}return fd;
}

static int create_file(int dir, const char *name) {
    return openat(dir,name,O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
}

static int open_normal_log(const char *path) {
    char copy[PATH_MAXIMUM],*slash;int dir,fd;struct stat st;
    if(!path||path[0]!='/'||strlen(path)>=sizeof(copy))return-1;
    memcpy(copy,path,strlen(path)+1);slash=strrchr(copy,'/');if(!slash||!slash[1])return-1;*slash=0;
    if(!safe_atom(slash+1,255))return-1;
    dir=open_dir(*copy?copy:"/",0);if(dir<0)return-1;
    fd=openat(dir,slash+1,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);close(dir);
    if(fd<0||fstat(fd,&st)<0||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||st.st_nlink!=1||fchmod(fd,0600)<0||ftruncate(fd,0)<0){if(fd>=0)close(fd);errno=EPERM;return-1;}
    return fd;
}

static int make_lane(const struct options *o, struct run *r) {
    unsigned char random[16]; char hex[33]; static const char digits[]="0123456789abcdef";
    int root=open_dir(o->lane_root,0); if(root<0)return-1;
    for(int attempt=0;attempt<16;attempt++){
        if(getrandom(random,sizeof(random),0)!=(ssize_t)sizeof(random)){close(root);return-1;}
        for(size_t i=0;i<sizeof(random);i++){hex[2*i]=digits[random[i]>>4];hex[2*i+1]=digits[random[i]&15];}hex[32]=0;
        if(snprintf(r->lane_name,sizeof(r->lane_name),"pov-%s-s03.%s",o->wave,hex)>=(int)sizeof(r->lane_name)){close(root);return-1;}
        r->lane_fd=mkdir_private(root,r->lane_name);
        if(r->lane_fd>=0)break;
        if(errno!=EEXIST){close(root);return-1;}
    }
    close(root);if(r->lane_fd<0)return-1;
    r->server_log=create_file(r->lane_fd,"server.log");r->client_log=create_file(r->lane_fd,"client.log");r->events=create_file(r->lane_fd,"events.log");
    if(r->server_log<0||r->client_log<0||r->events<0)return-1;
    int home=mkdir_private(r->lane_fd,"home");int xdg=mkdir_private(r->lane_fd,"xdg");
    int y=xdg<0?-1:mkdir_private(xdg,"YamagiQ2");int g=y<0?-1:mkdir_private(y,o->game);int d=g<0?-1:mkdir_private(g,"demos");
    if(home<0||xdg<0||y<0||g<0||d<0)return-1;
    close(home);close(xdg);close(y);close(g);close(d);
    if(snprintf(r->demo_rel,sizeof(r->demo_rel),"xdg/YamagiQ2/%s/demos/pov.dm2",o->game)>=(int)sizeof(r->demo_rel))return-1;
    return 0;
}

static int send_command(struct run *r, const char *command) {
    size_t n=strlen(command); if(n>127||strchr(command,'\n')||strchr(command,'\r'))return-1;
    if(write_all(r->q2_write,command,n)<0||write_all(r->q2_write,"\n",1)<0)return-1;
    return 0;
}

static int open_output_pty(int *master,int *slave) {
    int m=posix_openpt(O_RDWR|O_NOCTTY|O_CLOEXEC),s=-1;
    char *name;struct termios t;
    if(m<0||grantpt(m)<0||unlockpt(m)<0||(name=ptsname(m))==NULL) { if(m>=0)close(m); return -1; }
    s=open(name,O_RDWR|O_NOCTTY|O_CLOEXEC);
    if(s<0||tcgetattr(s,&t)<0){if(s>=0)close(s);close(m);return-1;}
    t.c_oflag&=(tcflag_t)~(OPOST|ONLCR);
    if(tcsetattr(s,TCSANOW,&t)<0){close(s);close(m);return-1;}
    *master=m;*slave=s;return 0;
}

static int demo_nonzero(struct run *r) {
    int fd=openat(r->lane_fd,r->demo_rel,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);struct stat st;
    int ok;
    if(fd<0)return 0;
    ok=fstat(fd,&st)==0&&S_ISREG(st.st_mode)&&st.st_uid==getuid()&&st.st_size>0;
    close(fd);return ok;
}

static void accept_line(struct run *r, int server, const char *line) {
    if(server) {
        if(!strcmp(line,"pov_s03 entered the game"))r->server_entered=1;
        if((!strcmp(line,"Changing map")&&r->server_entered)||!strcmp(line,"Server shutdown"))set_failure(r,"server transition before completion");
    } else {
        if(!strcmp(line,"Serverdata packet received."))r->client_packet=1;
        else if(!strcmp(line,"Server active.")||!strcmp(line,"pov_s03 entered the game"))r->client_active=1;
        else if(!strcmp(line,r->record_line)&&r->start_latched)r->record_confirmed=1;
        else if(!strcmp(line,"Server disconnected")||!strcmp(line,"Changing map")||!strcmp(line,"Not recording a demo."))set_failure(r,"client rejected or transitioned");
    }
}

static int consume(struct run*r,int fd,int output,struct lines*l,int server){unsigned char b[4096];ssize_t n=read(fd,b,sizeof(b));if(n==0)return 1;if(n<0){if(errno==EINTR||errno==EAGAIN)return 0;return-1;}if(write_all(output,b,(size_t)n)<0)return-1;if(server&&write_all(r->normal_log,b,(size_t)n)<0)return-1;for(ssize_t i=0;i<n;i++){unsigned char c=b[i];if(l->dropping){if(c=='\n'){l->dropping=0;l->used=0;}continue;}if(c=='\n'){if(l->used&&l->bytes[l->used-1]=='\r')l->used--;l->bytes[l->used]=0;accept_line(r,server,l->bytes);l->used=0;}else if(l->used==LINE_MAXIMUM){l->dropping=1;l->used=0;}else l->bytes[l->used++]=(char)c;}return 0;}

static int child_wait(struct pov_child*c,unsigned seconds){long long end=mono_ns()+(long long)seconds*1000000000LL;while(pov_child_alive(c)&&mono_ns()<end){struct pollfd p={.fd=c->pidfd,.events=POLLIN};(void)poll(&p,1,100);}if(pov_child_alive(c))return-1;return pov_reap_child(c,0);}

static void drain_available(struct run*r,int server){
    int fd=server?r->server_read:r->client_read;
    int output=server?r->server_log:r->client_log;
    struct lines*lines=server?&r->server_lines:&r->client_lines;
    for(int i=0;i<1024;i++){
        struct pollfd p={fd,POLLIN|POLLHUP,0};
        if(poll(&p,1,0)<=0||!(p.revents&(POLLIN|POLLHUP)))break;
        if(consume(r,fd,output,lines,server)!=0)break;
    }
}

static void stop_and_reap(struct run*r) {
    if(r->client_started&&!r->client.reaped){(void)pov_signal_child(&r->client,SIGTERM);if(child_wait(&r->client,2)<0){(void)pov_signal_child(&r->client,SIGKILL);(void)child_wait(&r->client,2);}}
    if(r->q2_started&&!r->q2.reaped){if(r->q2_write>=0)(void)send_command(r,"quit");if(child_wait(&r->q2,2)<0){(void)pov_signal_child(&r->q2,SIGKILL);(void)child_wait(&r->q2,2);}}
}

static int hash_named(int dir,const char*name,char hash[65],int nonzero){int fd=openat(dir,name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);struct stat s;int ok=fd>=0&&fstat(fd,&s)==0&&S_ISREG(s.st_mode)&&(!nonzero||s.st_size>0)&&pov_hash_fd(fd,hash)==0;if(fd>=0)close(fd);return ok?0:-1;}

static int verify_config_evidence(struct run *r) {
    struct stat st;
    char hash[65];
    int fd=openat(r->lane_fd,CONFIG_EVIDENCE,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    int ok=fd>=0&&fstat(fd,&st)==0&&S_ISREG(st.st_mode)&&st.st_uid==getuid()&&
           st.st_nlink==1&&(st.st_mode&0777)==0600&&st.st_dev==r->config_evidence_dev&&
           st.st_ino==r->config_evidence_ino&&st.st_size==r->config_evidence_size&&
           st.st_size==r->config_image.size&&
           pov_hash_fd(fd,hash)==0&&!strcmp(hash,r->config_image.sha256);
    if(fd>=0)close(fd);
    return ok?0:-1;
}

static int copy_config_evidence(struct run *r) {
    unsigned char bytes[4096];
    struct stat st,evidence;
    off_t at=0;
    int fd=-1;
    if(fstat(r->config_image.fd,&st)<0||!S_ISREG(st.st_mode)||st.st_size<=0||
       st.st_size!=r->config_image.size)return-1;
    fd=create_file(r->lane_fd,CONFIG_EVIDENCE);
    if(fd<0)return-1;
    while(at<st.st_size){
        size_t want=(size_t)(st.st_size-at);
        ssize_t got;
        if(want>sizeof(bytes))want=sizeof(bytes);
        got=pread(r->config_image.fd,bytes,want,at);
        if(got<0&&errno==EINTR)continue;
        if(got<=0||write_all(fd,bytes,(size_t)got)<0)goto failed;
        at+=got;
    }
    if(fsync(fd)<0)goto failed;
    if(fstat(fd,&evidence)<0||!S_ISREG(evidence.st_mode)||evidence.st_uid!=getuid()||
       evidence.st_nlink!=1||(evidence.st_mode&0777)!=0600||evidence.st_size!=r->config_image.size)goto failed;
    if(close(fd)<0)return-1;
    r->config_evidence_dev=evidence.st_dev;
    r->config_evidence_ino=evidence.st_ino;
    r->config_evidence_size=evidence.st_size;
    return verify_config_evidence(r);
failed:
    { int saved=errno; close(fd); errno=saved; }
    return -1;
}

static int config_atom(const unsigned char *bytes,size_t size) {
    if (!size || size > 63 || (size==1&&bytes[0]=='.') || (size==2&&bytes[0]=='.'&&bytes[1]=='.')) return -1;
    for (size_t i=0;i<size;i++)
        if (!((bytes[i]>='A'&&bytes[i]<='Z')||(bytes[i]>='a'&&bytes[i]<='z')||
              (bytes[i]>='0'&&bytes[i]<='9')||bytes[i]=='_'||bytes[i]=='-'||bytes[i]=='.')) return -1;
    return 0;
}

static int config_line_safe(const unsigned char *line,size_t size) {
    size_t i,start;
    if (!size) return 0;
    if (size > LINE_MAXIMUM) return -1;
    if (size >= 5 && !memcmp(line,"exec ",5))
        return config_atom(line+5,size-5);
    if (size < 5 || memcmp(line,"set ",4)) return -1;
    i=4;start=i;
    while (i<size && ((line[i]>='A'&&line[i]<='Z')||(line[i]>='a'&&line[i]<='z')||
                      (line[i]>='0'&&line[i]<='9')||line[i]=='_'||line[i]=='-'||line[i]=='.')) i++;
    if (i==start || i-start>63 || i>=size || line[i]!=' ') return -1;
    while (i<size && line[i]==' ') i++;
    if (i==size) return -1;
    for (;i<size;i++)
        if (line[i]<' ' || line[i]>'~' || line[i]==';' || line[i]=='\\') return -1;
    return 0;
}

static int send_pinned_config(struct run *r) {
    struct stat st;
    unsigned char *bytes=NULL;
    char check[65];
    size_t size,at=0;
    ssize_t got;
    if (fstat(r->config_image.fd,&st)<0 || !S_ISREG(st.st_mode) || st.st_size<=0 ||
        st.st_size!=(off_t)r->config_image.size || st.st_size>1024*1024) return -1;
    size=(size_t)st.st_size;
    bytes=malloc(size);
    if (!bytes) return -1;
    while (at<size) {
        got=pread(r->config_image.fd,bytes+at,size-at,(off_t)at);
        if (got<0 && errno==EINTR) continue;
        if (got<=0) { free(bytes); return -1; }
        at+=(size_t)got;
    }
    if (pov_hash_fd(r->config_image.fd,check)<0 || strcmp(check,r->config_image.sha256)) { free(bytes); return -1; }
    at=0;
    while (at<size) {
        size_t end=at;
        while (end<size && bytes[end]!='\n') end++;
        if (end==size || (end>at && bytes[end-1]=='\r') || config_line_safe(bytes+at,end-at)<0) { free(bytes); return -1; }
        at=end+1;
    }
    if (write_all(r->q2_write,bytes,size)<0) { free(bytes); return -1; }
    free(bytes);
    return 0;
}

static int publish(struct run*r,const struct options*o) {
    char demo[65],server[65],client[65],events_hash[65],normal[65],check[65],manifest[4096];int fd,n;
    struct stat self_stat,iterate_stat;int iterate_fd=parse_fd(o->iterate_fd_text);
    if(fsync(r->server_log)<0||fsync(r->client_log)<0||fsync(r->events)<0||fsync(r->normal_log)<0)return-1;
    if(iterate_fd<0||fstat(r->self_fd,&self_stat)<0||fstat(iterate_fd,&iterate_stat)<0||
       pov_hash_fd(r->self_fd,check)<0||strcmp(check,r->self_hash)||
       pov_hash_fd(r->q2_image.fd,check)<0||strcmp(check,r->q2_image.sha256)||
       pov_hash_fd(r->client_image.fd,check)<0||strcmp(check,r->client_image.sha256)||
       pov_hash_fd(r->config_image.fd,check)<0||strcmp(check,r->config_image.sha256)||
       verify_config_evidence(r)<0||hash_inherited_regular(o->iterate_fd_text,check)<0||strcmp(check,r->iterate_hash))return-1;
    if(o->waveloop_fd_text&&(hash_inherited_regular(o->waveloop_fd_text,check)<0||strcmp(check,r->waveloop_hash)))return-1;
    event(r,"final_hashes");
    if(fsync(r->events)<0||hash_named(r->lane_fd,r->demo_rel,demo,1)<0||hash_named(r->lane_fd,"server.log",server,0)<0||hash_named(r->lane_fd,"client.log",client,0)<0||hash_named(r->lane_fd,"events.log",events_hash,0)<0||pov_hash_fd(r->normal_log,normal)<0)return-1;
    fd=create_file(r->lane_fd,"manifest.tmp");if(fd<0)return-1;
    n=snprintf(manifest,sizeof(manifest),
      "status=published\nwave=%s\nserver=s03\nport=%u\nstate=PUBLISHED\n"
      "supervisor_sha256=%s\nq2_sha256=%s\nclient_sha256=%s\nconfig_sha256=%s\n"
      "config_evidence=%s\nconfig_evidence_size=%lld\nconfig_evidence_sha256=%s\n"
      "iterate2_sha256=%s\nwaveloop_sha256=%s\ndemo_sha256=%s\nserver_log_sha256=%s\n"
      "client_log_sha256=%s\nevents_sha256=%s\nnormal_log_sha256=%s\n"
      "supervisor_dev=%llu\nsupervisor_ino=%llu\nsupervisor_size=%lld\n"
      "iterate2_dev=%llu\niterate2_ino=%llu\niterate2_size=%lld\n"
      "q2_dev=%llu\nq2_ino=%llu\nq2_size=%lld\nclient_dev=%llu\nclient_ino=%llu\nclient_size=%lld\n"
      "config_dev=%llu\nconfig_ino=%llu\nconfig_size=%lld\n"
      "q2_pid=%ld\nq2_start_ticks=%llu\nclient_pid=%ld\nclient_start_ticks=%llu\n",
      o->wave,o->port,r->self_hash,r->q2_image.sha256,r->client_image.sha256,r->config_image.sha256,
      CONFIG_EVIDENCE,(long long)r->config_image.size,r->config_image.sha256,
      r->iterate_hash,r->waveloop_hash[0]?r->waveloop_hash:"not-provided",demo,server,client,events_hash,normal,
      (unsigned long long)self_stat.st_dev,(unsigned long long)self_stat.st_ino,(long long)self_stat.st_size,
      (unsigned long long)iterate_stat.st_dev,(unsigned long long)iterate_stat.st_ino,(long long)iterate_stat.st_size,
      (unsigned long long)r->q2_image.dev,(unsigned long long)r->q2_image.ino,(long long)r->q2_image.size,
      (unsigned long long)r->client_image.dev,(unsigned long long)r->client_image.ino,(long long)r->client_image.size,
      (unsigned long long)r->config_image.dev,(unsigned long long)r->config_image.ino,(long long)r->config_image.size,
      (long)r->q2.pid,r->q2.start_ticks,(long)r->client.pid,r->client.start_ticks);
    if(n<0||(size_t)n>=sizeof(manifest)||write_all(fd,manifest,(size_t)n)<0||fsync(fd)<0){close(fd);return-1;}close(fd);
#ifdef POV_TESTING
    if(getenv("POV_TEST_FAIL_RENAME")){errno=EIO;return-1;}
#endif
    if(syscall(SYS_renameat2,r->lane_fd,"manifest.tmp",r->lane_fd,"manifest.txt",RENAME_NOREPLACE)<0)return-1;
    if(fsync(r->lane_fd)<0){int saved=errno;(void)unlinkat(r->lane_fd,"manifest.txt",0);(void)fsync(r->lane_fd);errno=saved;return-1;}
    r->state=PUBLISHED;return 0;
}

static void failure_file(struct run*r){int fd;if(r->lane_fd<0)return;fd=create_file(r->lane_fd,"failure.txt");if(fd<0)return;(void)write_all(fd,r->failure,strlen(r->failure));(void)write_all(fd,"\n",1);(void)fsync(fd);close(fd);(void)fsync(r->lane_fd);}

static int run_supervisor(const struct options*o,struct run*r) {
    char why[256],port[16],home[PATH_MAXIMUM],xdg[PATH_MAXIMUM];
    char q2a0[]="q2ded",clienta0[]="quake2";char *q2argv[24],*clientargv[28],*envp[16];
    int rootfd=-1,nullfd=-1,qin[2]={-1,-1},q2_master=-1,q2_slave=-1,client_master=-1,client_slave=-1;
    sigset_t mask;unsigned long long current_parent_start;
    r->state=PREFLIGHT;
    if(getppid()!=o->parent_pid||pov_read_start_ticks(o->parent_pid,&current_parent_start)<0||current_parent_start!=o->parent_start){set_failure(r,"parent generation mismatch");goto done;}
    if((r->self_fd=open_self(r->self_hash))<0||validate_pinned_self(r,o->supervisor_fd_text)<0||hash_inherited_regular(o->iterate_fd_text,r->iterate_hash)<0){set_failure(r,"supervisor or iterate image invalid");goto done;}
    if(o->waveloop_fd_text&&hash_inherited_regular(o->waveloop_fd_text,r->waveloop_hash)<0){set_failure(r,"waveloop provenance invalid");goto done;}
    if(pov_open_image(o->q2,&r->q2_image,why,sizeof(why))<0||pov_open_image(o->client,&r->client_image,why,sizeof(why))<0||pov_open_regular(o->config,0,&r->config_image,why,sizeof(why))<0){set_failure(r,"preflight image: %s",why);goto done;}
    rootfd=open_dir(o->root,0);if(rootfd<0||make_lane(o,r)<0||copy_config_evidence(r)<0||(r->normal_log=open_normal_log(o->normal_log))<0){set_failure(r,"private lane setup failed: %s",strerror(errno));goto done;}
    if(snprintf(home,sizeof(home),"%s/%s/home",o->lane_root,r->lane_name)>=(int)sizeof(home)||snprintf(xdg,sizeof(xdg),"%s/%s/xdg",o->lane_root,r->lane_name)>=(int)sizeof(xdg)||snprintf(r->record_line,sizeof(r->record_line),"recording to %s/%s.",xdg,r->demo_rel+4)>=(int)sizeof(r->record_line)){set_failure(r,"private path too long");goto done;}
    (void)snprintf(port,sizeof(port),"%u",o->port);
    char env_home[PATH_MAXIMUM+6],env_data[PATH_MAXIMUM+16],env_config[PATH_MAXIMUM+18],env_cache[PATH_MAXIMUM+17],env_state[PATH_MAXIMUM+17];
    char inherited[5][PATH_MAXIMUM];static const char*inherit_names[]={"DISPLAY","WAYLAND_DISPLAY","XAUTHORITY","XDG_RUNTIME_DIR","PULSE_SERVER"};
    snprintf(env_home,sizeof(env_home),"HOME=%s",home);snprintf(env_data,sizeof(env_data),"XDG_DATA_HOME=%s",xdg);snprintf(env_config,sizeof(env_config),"XDG_CONFIG_HOME=%s",home);snprintf(env_cache,sizeof(env_cache),"XDG_CACHE_HOME=%s",home);snprintf(env_state,sizeof(env_state),"XDG_STATE_HOME=%s",home);
    int ei=0;envp[ei++]=env_home;envp[ei++]=env_data;envp[ei++]=env_config;envp[ei++]=env_cache;envp[ei++]=env_state;envp[ei++]=(char*)"LANG=C";
    for(size_t x=0;x<sizeof(inherit_names)/sizeof(inherit_names[0]);x++){const char*value=getenv(inherit_names[x]);if(value&&strlen(value)<PATH_MAXIMUM-32){snprintf(inherited[x],sizeof(inherited[x]),"%s=%s",inherit_names[x],value);envp[ei++]=inherited[x];}}
    envp[ei]=NULL;
    sigemptyset(&mask);sigaddset(&mask,SIGINT);sigaddset(&mask,SIGTERM);sigaddset(&mask,SIGHUP);sigaddset(&mask,SIGQUIT);if(sigprocmask(SIG_BLOCK,&mask,NULL)<0||(r->signals=signalfd(-1,&mask,SFD_CLOEXEC|SFD_NONBLOCK))<0){set_failure(r,"signalfd failed");goto done;}
    nullfd=open("/dev/null",O_RDONLY|O_CLOEXEC);if(nullfd<0||pipe2(qin,O_CLOEXEC)<0||open_output_pty(&q2_master,&q2_slave)<0||open_output_pty(&client_master,&client_slave)<0){set_failure(r,"pipe setup failed");goto done;}
    int qi=0;q2argv[qi++]=q2a0;q2argv[qi++]=(char*)"+set";q2argv[qi++]=(char*)"game";q2argv[qi++]=(char*)o->game;q2argv[qi++]=(char*)"+set";q2argv[qi++]=(char*)"dedicated";q2argv[qi++]=(char*)"1";q2argv[qi++]=(char*)"+set";q2argv[qi++]=(char*)"port";q2argv[qi++]=port;q2argv[qi++]=(char*)"+set";q2argv[qi++]=(char*)"net_port";q2argv[qi++]=port;q2argv[qi++]=(char*)"+set";q2argv[qi++]=(char*)"maxclients";q2argv[qi++]=(char*)"16";q2argv[qi]=NULL;
    struct pov_stdio sm={qin[0],q2_slave,q2_slave,rootfd};if(pov_spawn_image(&r->q2_image,q2argv,envp,&sm,&r->q2,why,sizeof(why))<0){set_failure(r,"q2 spawn: %s",why);goto done;}r->q2_started=1;close(qin[0]);qin[0]=-1;close(q2_slave);q2_slave=-1;r->q2_write=qin[1];qin[1]=-1;r->server_read=q2_master;q2_master=-1;if(fcntl(r->server_read,F_SETFL,fcntl(r->server_read,F_GETFL)|O_NONBLOCK)<0||pov_validate_child(&r->q2,&r->q2_image,q2argv,why,sizeof(why))<0){set_failure(r,"q2 identity: %s",why);goto done;}
    if(sigaddset(&mask,SIGPIPE)<0||sigprocmask(SIG_BLOCK,&mask,NULL)<0){set_failure(r,"SIGPIPE block failed");goto done;}
    char command[128];if(send_pinned_config(r)<0||snprintf(command,sizeof(command),"map %s",o->map)>=(int)sizeof(command)||send_command(r,command)<0){set_failure(r,"config or map failed");goto done;}snprintf(command,sizeof(command),"serverrecord wave%s-s03",o->wave);if(send_command(r,command)<0||event(r,"serverrecord")<0){set_failure(r,"serverrecord failed");goto done;}r->state=SERVER_RECORDING;
    char connect[64];snprintf(connect,sizeof(connect),"127.0.0.1:%u",o->port);int ci=0;clientargv[ci++]=clienta0;clientargv[ci++]=(char*)"+set";clientargv[ci++]=(char*)"basedir";clientargv[ci++]=(char*)o->root;clientargv[ci++]=(char*)"+set";clientargv[ci++]=(char*)"game";clientargv[ci++]=(char*)o->game;clientargv[ci++]=(char*)"+set";clientargv[ci++]=(char*)"name";clientargv[ci++]=(char*)o->spectator;clientargv[ci++]=(char*)"+set";clientargv[ci++]=(char*)"spectator";clientargv[ci++]=(char*)"1";clientargv[ci++]=(char*)"+set";clientargv[ci++]=(char*)"recording_format";clientargv[ci++]=(char*)"dm2";clientargv[ci++]=(char*)"+connect";clientargv[ci++]=connect;clientargv[ci]=NULL;
    struct pov_stdio cm={nullfd,client_slave,client_slave,r->lane_fd};if(pov_spawn_image(&r->client_image,clientargv,envp,&cm,&r->client,why,sizeof(why))<0){set_failure(r,"client spawn: %s",why);goto done;}r->client_started=1;close(client_slave);client_slave=-1;r->client_read=client_master;client_master=-1;if(fcntl(r->client_read,F_SETFL,fcntl(r->client_read,F_GETFL)|O_NONBLOCK)<0||pov_validate_child(&r->client,&r->client_image,clientargv,why,sizeof(why))<0){set_failure(r,"client identity: %s",why);goto done;}event(r,"client_launch");clock_gettime(CLOCK_MONOTONIC,&r->authority_started);
    long long deadline=mono_ns()+(long long)o->ready_timeout*1000000000LL;int stagger_stage=0;
    while(!r->failed&&r->state!=FINALIZING){
        struct pollfd p[5]={{r->server_read,POLLIN|POLLHUP,0},{r->client_read,POLLIN|POLLHUP,0},{r->signals,POLLIN,0},{r->q2.pidfd,POLLIN,0},{r->client.pidfd,POLLIN,0}};
        int timeout=100; if(poll(p,5,timeout)<0&&errno!=EINTR){set_failure(r,"poll failed");break;}
        if(p[0].revents&(POLLIN|POLLHUP)){int x=consume(r,r->server_read,r->server_log,&r->server_lines,1);if(x<0)set_failure(r,"server stream failed");}
        if(p[1].revents&(POLLIN|POLLHUP)){int x=consume(r,r->client_read,r->client_log,&r->client_lines,0);if(x<0)set_failure(r,"client stream failed");}
        if(p[2].revents&POLLIN){struct signalfd_siginfo si;if(read(r->signals,&si,sizeof(si))==(ssize_t)sizeof(si)){r->interrupted=1;set_failure(r,"parent signal %u",si.ssi_signo);}}
        if((p[3].revents&POLLIN)&&r->state!=FINALIZING)set_failure(r,"q2 exited before requested duration");
        if((p[4].revents&POLLIN)&&r->state!=FINALIZING)set_failure(r,"client exited before requested duration");
        if(!r->start_latched&&r->server_entered&&r->client_active){r->state=CLIENT_READY;snprintf(command,sizeof(command),"sv povrecord %s %s",o->spectator,o->target);if(send_command(r,command)<0){set_failure(r,"native directive failed");break;}r->start_latched=1;event(r,"native_directive");}
        if(r->start_latched&&r->record_confirmed&&demo_nonzero(r)&&r->state!=POV_STARTED){r->state=POV_STARTED;clock_gettime(CLOCK_MONOTONIC,&r->pov_started);event(r,"record_confirmation");}
        long long now=mono_ns();if(r->state!=POV_STARTED&&now>=deadline){set_failure(r,"readiness timeout");break;}
        if(r->state==POV_STARTED){long long elapsed=now-((long long)r->pov_started.tv_sec*1000000000LL+r->pov_started.tv_nsec);if(o->stagger){static const unsigned at[]={65,135,210,450,515};static const char*cmd[]={"set sv_botfill \"3:3\"","set sv_botfill \"4:4\"","set sv_botfill \"5:5\"","set sv_botfill \"5:4\"","set sv_botfill \"5:5\""};while(stagger_stage<5&&elapsed>=(long long)at[stagger_stage]*1000000000LL){if(send_command(r,cmd[stagger_stage++])<0){set_failure(r,"stagger failed");break;}}}if(elapsed>=(long long)o->duration*1000000000LL){event(r,"duration_complete");r->state=STOPPING;snprintf(command,sizeof(command),"sv povrecord off %s",o->spectator);if(!r->stop_latched){r->stop_latched=1;if(send_command(r,command)<0){set_failure(r,"stop failed");break;}event(r,"stop");}r->state=FINALIZING;}}
    }
    if(!r->failed&&r->state==FINALIZING){
        long long final_end=mono_ns()+(long long)o->finalize_delay*1000000000LL;
        while(!r->failed&&mono_ns()<final_end){
            long long left=final_end-mono_ns();int milliseconds=(int)(left/1000000LL);if(milliseconds>100)milliseconds=100;if(milliseconds<1)milliseconds=1;
            struct pollfd final_wait[3]={{r->server_read,POLLIN|POLLHUP,0},{r->client_read,POLLIN|POLLHUP,0},{r->signals,POLLIN,0}};
            int waited=poll(final_wait,3,milliseconds);
            if(waited<0&&errno!=EINTR){set_failure(r,"finalize poll failed");break;}
            if(final_wait[0].revents&(POLLIN|POLLHUP))drain_available(r,1);
            if(final_wait[1].revents&(POLLIN|POLLHUP))drain_available(r,0);
            if(final_wait[2].revents&POLLIN){struct signalfd_siginfo si;(void)read(r->signals,&si,sizeof(si));set_failure(r,"signal during finalize");break;}
        }
        if(r->failed)goto done;
        event(r,"finalize_client");
        if(pov_signal_child(&r->client,SIGTERM)<0||child_wait(&r->client,3)<0){set_failure(r,"client reap failed");goto done;}
        drain_available(r,0);event(r,"client_reap");
        if(send_command(r,"quit")<0){set_failure(r,"q2 quit failed");goto done;}
        event(r,"q2_quit");
        if(child_wait(&r->q2,3)<0||r->q2.status.si_code!=CLD_EXITED||r->q2.status.si_status!=0){set_failure(r,"q2 clean exit failed");goto done;}
        drain_available(r,1);event(r,"q2_reap");
        if(publish(r,o)<0){set_failure(r,"manifest publish failed: %s",strerror(errno));goto done;}
        return 0;
    }
done:
    if(r->start_latched&&!r->stop_latched&&r->q2_started&&pov_child_alive(&r->q2)){r->stop_latched=1;snprintf(command,sizeof(command),"sv povrecord off %s",o->spectator);(void)send_command(r,command);}
    stop_and_reap(r);failure_file(r);if(rootfd>=0)close(rootfd);if(nullfd>=0)close(nullfd);for(int i=0;i<2;i++)if(qin[i]>=0)close(qin[i]);if(q2_master>=0)close(q2_master);if(q2_slave>=0)close(q2_slave);if(client_master>=0)close(client_master);if(client_slave>=0)close(client_slave);return 1;
}

#ifndef POV_SUPERVISOR_NO_MAIN
int main(int argc,char**argv){struct options o;struct run r;umask(077);memset(&r,0,sizeof(r));r.state=OFF;r.lane_fd=r.server_log=r.client_log=r.events=r.normal_log=r.q2_write=r.server_read=r.client_read=r.signals=r.self_fd=-1;r.q2.pidfd=r.client.pidfd=-1;r.q2_image.fd=r.client_image.fd=r.config_image.fd=-1;if(parse_options(argc,argv,&o)<0){dprintf(2,"pov-supervisor: invalid arguments\n");return 2;}int rc=run_supervisor(&o,&r);if(rc)dprintf(2,"pov-supervisor: %s\n",r.failure[0]?r.failure:"failed");return rc;}
#endif
