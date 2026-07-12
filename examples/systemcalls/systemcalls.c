#include "systemcalls.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static bool execute_command(char *const command[], const char *outputfile)
{
    int output_fd = -1;
    int status;
    pid_t child_pid;
    pid_t wait_result;
    bool close_succeeded = true;

    if (command == NULL || command[0] == NULL) {
        return false;
    }

    if (outputfile != NULL) {
        output_fd = open(outputfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (output_fd == -1) {
            return false;
        }
    }

    child_pid = fork();
    if (child_pid == -1) {
        if (output_fd != -1) {
            close(output_fd);
        }
        return false;
    }

    if (child_pid == 0) {
        if (output_fd != -1 && output_fd != STDOUT_FILENO) {
            if (dup2(output_fd, STDOUT_FILENO) == -1) {
                _exit(EXIT_FAILURE);
            }
            close(output_fd);
        }

        execv(command[0], command);
        _exit(EXIT_FAILURE);
    }

    if (output_fd != -1 && close(output_fd) == -1) {
        close_succeeded = false;
    }

    do {
        wait_result = waitpid(child_pid, &status, 0);
    } while (wait_result == -1 && errno == EINTR);

    return close_succeeded && wait_result == child_pid &&
           WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS;
}

static char **build_command(int count, va_list args)
{
    char **command;
    int index;

    if (count < 1) {
        return NULL;
    }

    command = calloc((size_t)count + 1U, sizeof(*command));
    if (command == NULL) {
        return NULL;
    }

    for (index = 0; index < count; index++) {
        command[index] = va_arg(args, char *);
    }

    return command;
}

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{
    int status;

    if (cmd == NULL) {
        return false;
    }

    status = system(cmd);
    return status != -1 && WIFEXITED(status) &&
           WEXITSTATUS(status) == EXIT_SUCCESS;
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    char **command;
    bool succeeded;

    va_start(args, count);
    command = build_command(count, args);
    va_end(args);

    if (command == NULL) {
        return false;
    }

    succeeded = execute_command(command, NULL);
    free(command);
    return succeeded;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    char **command;
    bool succeeded;

    if (outputfile == NULL) {
        return false;
    }

    va_start(args, count);
    command = build_command(count, args);
    va_end(args);

    if (command == NULL) {
        return false;
    }

    succeeded = execute_command(command, outputfile);
    free(command);
    return succeeded;
}
