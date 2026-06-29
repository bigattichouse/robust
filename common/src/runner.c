/*
 * runner.c — fork/env per-design-point run loop, shared by every tool.
 *
 * For each design row: fork, export <PREFIX>_RUN_ID and <PREFIX>_<factor>=<value>
 * environment variables, exec the user's script via `/bin/sh -c`, and wait.
 * Generalized over doe_value_fn so morris, sobol, ofat, and grid all reuse it.
 * Pattern lifted from taguchi's cmd_run.
 */

#define _POSIX_C_SOURCE 200809L

#include "doe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

int doe_run(const doe_space_t *space, const char *prefix, const char *script,
            size_t rows, doe_value_fn get_value, void *ctx, char *err) {
    for (size_t row = 0; row < rows; row++) {
        pid_t pid = fork();

        if (pid == 0) {
            /* child: set env then exec the script */
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
