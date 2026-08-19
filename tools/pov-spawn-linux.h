#ifndef POV_SPAWN_LINUX_H
#define POV_SPAWN_LINUX_H

#include <signal.h>
#include <stddef.h>
#include <sys/types.h>

#define POV_SHA256_HEX_SIZE 65

struct pov_image {
    int fd;
    dev_t dev;
    ino_t ino;
    off_t size;
    char sha256[POV_SHA256_HEX_SIZE];
};

struct pov_child {
    pid_t pid;
    int pidfd;
    unsigned long long start_ticks;
    int reaped;
    siginfo_t status;
};

struct pov_stdio { int input, output, error, cwd; };

int pov_hash_fd(int fd, char out[POV_SHA256_HEX_SIZE]);
int pov_open_directory(const char *path, int require_private,
                       char *why, size_t why_size);
int pov_open_regular(const char *path, int executable, struct pov_image *out,
                     char *why, size_t why_size);
int pov_open_image(const char *path, struct pov_image *out,
                   char *why, size_t why_size);
int pov_read_start_ticks(pid_t pid, unsigned long long *out);
int pov_spawn_image(const struct pov_image *image, char *const argv[],
                    char *const envp[], const struct pov_stdio *stdio_map,
                    struct pov_child *out, char *why, size_t why_size);
int pov_validate_child(const struct pov_child *child,
                       const struct pov_image *image, char *const argv[],
                       char *why, size_t why_size);
int pov_signal_child(const struct pov_child *child, int signal_number);
int pov_reap_child(struct pov_child *child, int options);
int pov_child_alive(const struct pov_child *child);

#endif
