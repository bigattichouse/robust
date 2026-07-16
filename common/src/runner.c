/*
 * runner.c — fork/env per-design-point run loop, shared by every tool.
 *
 * For each design row: fork, export <PREFIX>_RUN_ID and <PREFIX>_<factor>=<value>
 * environment variables, exec the user's script via `/bin/sh -c`, and wait.
 * doe_run prints exit codes (used by `morris run` / `sobol run`); doe_run_capture
 * captures each child's stdout as a numeric response (used by the orchestrator).
 *
 * SECURITY: factor values reach the script as <PREFIX>_<factor> environment *values*
 * (set via setenv — never spliced into a command string). A model script must treat
 * them as data, not code: do not interpolate <PREFIX>_* into a shell/eval/awk program,
 * or an adversarial .space could inject through your script. Env var names are guarded
 * against '=' and length overflow.
 */

#define _POSIX_C_SOURCE 200809L

#include "doe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/* In the forked child: export RUN_ID and one env var per factor. _exit on error. */
static void child_set_env(const doe_space_t *space, const char *prefix, size_t row,
                          doe_value_fn get_value, void *ctx) {
    char ridbuf[32];
    char envname[DOE_MAX_NAME + 64];

    snprintf(ridbuf, sizeof ridbuf, "%zu", row + 1);
    snprintf(envname, sizeof envname, "%s_RUN_ID", prefix);
    setenv(envname, ridbuf, 1);

    for (size_t col = 0; col < space->factor_count; col++) {
        const char *name = space->factors[col].name;
        if (strchr(name, '=') != NULL) {
            fprintf(stderr, "Error: factor name '%s' contains '='\n", name);
            _exit(1);
        }
        int nw = snprintf(envname, sizeof envname, "%s_%s", prefix, name);
        if (nw < 0 || nw >= (int)sizeof envname) {
            fprintf(stderr, "Error: factor name '%s' too long for env var\n", name);
            _exit(1);
        }
        const char *val = get_value(ctx, row, col);
        setenv(envname, val ? val : "", 1);
    }
}

int doe_run(const doe_space_t *space, const char *prefix, const char *script,
            size_t rows, doe_value_fn get_value, void *ctx, char *err) {
    for (size_t row = 0; row < rows; row++) {
        pid_t pid = fork();

        if (pid == 0) {
            child_set_env(space, prefix, row, get_value, ctx);
            execl("/bin/sh", "sh", "-c", script, (char *)NULL);
            perror("exec failed");
            _exit(127);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status)) {
                printf("Run %zu exit %d\n", row + 1, WEXITSTATUS(status));
            } else {
                printf("Run %zu terminated abnormally\n", row + 1);
            }
        } else {
            snprintf(err, DOE_ERR_SIZE, "fork failed at run %zu", row + 1);
            return -1;
        }
    }
    return 0;
}

int doe_run_capture(const doe_space_t *space, const char *prefix, const char *script,
                    size_t rows, doe_value_fn get_value, void *ctx,
                    double *responses, char *err) {
    for (size_t row = 0; row < rows; row++) {
        int fds[2];
        if (pipe(fds) != 0) {
            snprintf(err, DOE_ERR_SIZE, "pipe failed at run %zu", row + 1);
            return -1;
        }

        pid_t pid = fork();
        if (pid == 0) {
            close(fds[0]);
            if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(126);
            close(fds[1]);
            child_set_env(space, prefix, row, get_value, ctx);
            execl("/bin/sh", "sh", "-c", script, (char *)NULL);
            _exit(127);
        } else if (pid > 0) {
            close(fds[1]);

            char buf[256];
            size_t n = 0;
            ssize_t r;
            while (n < sizeof buf - 1 && (r = read(fds[0], buf + n, sizeof buf - 1 - n)) > 0) {
                n += (size_t)r;
            }
            buf[n] = '\0';
            { char junk[256]; while (read(fds[0], junk, sizeof junk) > 0) { } }  /* drain */
            close(fds[0]);

            int status;
            waitpid(pid, &status, 0);
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                snprintf(err, DOE_ERR_SIZE, "run %zu: script exited non-zero", row + 1);
                return -1;
            }

            char *endp;
            double v = strtod(buf, &endp);
            if (endp == buf) {
                snprintf(err, DOE_ERR_SIZE, "run %zu: no numeric output on stdout", row + 1);
                return -1;
            }
            responses[row] = v;
        } else {
            snprintf(err, DOE_ERR_SIZE, "fork failed at run %zu", row + 1);
            return -1;
        }
    }
    return 0;
}
