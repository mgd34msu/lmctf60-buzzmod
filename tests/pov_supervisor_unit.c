#define _GNU_SOURCE
#include "../tools/pov-spawn-linux.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); failures++; } } while (0)

static int child_mode(void) {
    DIR *directory = opendir("/proc/self/fd");
    struct dirent *entry;
    int extra = 0;
    if (!directory) return 20;
    while ((entry = readdir(directory)) != NULL) {
        char *end; long fd = strtol(entry->d_name, &end, 10);
        if (*entry->d_name && !*end && fd > 2 && fd != dirfd(directory)) extra++;
    }
    closedir(directory);
    if (extra || getenv("PATH") || getenv("BASH_ENV") || getenv("LD_PRELOAD") ||
        getenv("LD_LIBRARY_PATH")) return 21;
    if (write(1, "child-ok\n", 9) != 9) return 22;
    for (;;) pause();
}

static void hash_test(void) {
    char path[] = "/tmp/pov-hash.XXXXXX", hash[65];
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    CHECK(write(fd, "abc", 3) == 3);
    CHECK(pov_hash_fd(fd, hash) == 0);
    CHECK(!strcmp(hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    close(fd); unlink(path);
}

static void rejection_test(void) {
    char directory[] = "/tmp/pov-open.XXXXXX", file[512], link[512], why[128];
    struct pov_image image;
    CHECK(mkdtemp(directory) != NULL);
    snprintf(file, sizeof(file), "%s/script", directory);
    int fd = open(file, O_WRONLY|O_CREAT|O_EXCL, 0700);
    CHECK(fd >= 0); CHECK(write(fd, "#!/bin/sh\nexit 0\n", 17) == 17); close(fd);
    CHECK(pov_open_image(file, &image, why, sizeof(why)) < 0);
    snprintf(link, sizeof(link), "%s/link", directory); CHECK(symlink(file, link) == 0);
    CHECK(pov_open_image(link, &image, why, sizeof(why)) < 0);
    CHECK(pov_open_regular(directory, 0, &image, why, sizeof(why)) < 0);
    char lexical[1024];
    snprintf(lexical, sizeof(lexical), "%s/../%s/script", directory,
             strrchr(directory, '/') + 1);
    CHECK(pov_open_regular(lexical, 0, &image, why, sizeof(why)) < 0);
    CHECK(chmod(file, 0777) == 0);
    CHECK(pov_open_regular(file, 0, &image, why, sizeof(why)) < 0);
    unlink(link);unlink(file);rmdir(directory);
}

static void spawn_test(const char *program) {
    struct pov_image image; struct pov_child child; struct pov_stdio mapping;
    char why[256], *argv[] = {(char *)program, (char *)"--native-child", NULL};
    char *envp[] = {(char *)"LANG=C", NULL}; int output[2], nullfd;
    CHECK(pov_open_image(program, &image, why, sizeof(why)) == 0);
    CHECK(pipe2(output, O_CLOEXEC) == 0);
    nullfd = open("/dev/null", O_RDONLY|O_CLOEXEC); CHECK(nullfd >= 0);
    mapping.input=nullfd;mapping.output=output[1];mapping.error=output[1];mapping.cwd=-1;
    CHECK(pov_spawn_image(&image,argv,envp,&mapping,&child,why,sizeof(why)) == 0);
    close(output[1]);
    CHECK(pov_validate_child(&child,&image,argv,why,sizeof(why)) == 0);
    char line[16]={0}; CHECK(read(output[0],line,sizeof(line)) == 9); CHECK(!strcmp(line,"child-ok\n"));
    CHECK(pov_signal_child(&child,SIGTERM) == 0);
    CHECK(pov_reap_child(&child,0) == 0); CHECK(child.reaped);
    close(output[0]);close(nullfd);close(image.fd);close(child.pidfd);
}

static int copy_file(const char *source, const char *target) {
    unsigned char bytes[16384]; ssize_t n; int in=open(source,O_RDONLY|O_CLOEXEC);
    int out=open(target,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0700);
    if(in<0||out<0)return-1;
    while((n=read(in,bytes,sizeof(bytes)))>0)if(write(out,bytes,(size_t)n)!=n)return-1;
    close(in);close(out);return n<0?-1:0;
}

static void replacement_test(const char *program) {
    char directory[]="/tmp/pov-replace.XXXXXX",original[512],replacement[512],why[128];
    struct pov_image image;struct pov_child child;struct pov_stdio mapping;int output[2],nullfd;
    CHECK(mkdtemp(directory)!=NULL);snprintf(original,sizeof(original),"%s/image",directory);
    snprintf(replacement,sizeof(replacement),"%s/new",directory);CHECK(copy_file(program,original)==0);
    CHECK(pov_open_image(original,&image,why,sizeof(why))==0);CHECK(copy_file("/bin/true",replacement)==0);
    CHECK(rename(replacement,original)==0);CHECK(pipe2(output,O_CLOEXEC)==0);nullfd=open("/dev/null",O_RDONLY|O_CLOEXEC);
    char *child_argv[]={original,(char*)"--native-child",NULL};char *envp[]={(char*)"LANG=C",NULL};
    mapping.input=nullfd;mapping.output=output[1];mapping.error=output[1];mapping.cwd=-1;
    CHECK(pov_spawn_image(&image,child_argv,envp,&mapping,&child,why,sizeof(why))==0);close(output[1]);
    CHECK(pov_validate_child(&child,&image,child_argv,why,sizeof(why))==0);
    char line[16]={0};CHECK(read(output[0],line,sizeof(line))==9);CHECK(!strcmp(line,"child-ok\n"));
    CHECK(pov_signal_child(&child,SIGTERM)==0);CHECK(pov_reap_child(&child,0)==0);
    close(output[0]);close(nullfd);close(image.fd);close(child.pidfd);unlink(original);rmdir(directory);
}

int main(int argc, char **argv) {
    char resolved[4096];
    if (argc == 2 && !strcmp(argv[1], "--native-child")) return child_mode();
    CHECK(realpath(argv[0], resolved) != NULL);
    hash_test();rejection_test();spawn_test(resolved);replacement_test(resolved);
    if (failures) fprintf(stderr, "%d failures\n", failures);
    return failures ? 1 : 0;
}
