#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int fd;
    const char *writefile;
    const char *writestr;
    size_t bytes_written = 0;
    size_t length;

    openlog("writer", LOG_PID, LOG_USER);

    if (argc < 3) {
        syslog(LOG_ERR, "Two arguments are required: a file path and a string");
        fprintf(stderr, "Usage: %s <file> <string>\n", argv[0]);
        closelog();
        return 1;
    }

    writefile = argv[1];
    writestr = argv[2];
    length = strlen(writestr);

    fd = open(writefile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        syslog(LOG_ERR, "Unable to open %s: %s", writefile, strerror(errno));
        closelog();
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

    while (bytes_written < length) {
        ssize_t result = write(fd, writestr + bytes_written,
                               length - bytes_written);

        if (result == -1) {
            if (errno == EINTR) {
                continue;
            }

            syslog(LOG_ERR, "Unable to write to %s: %s", writefile,
                   strerror(errno));
            close(fd);
            closelog();
            return 1;
        }

        if (result == 0) {
            syslog(LOG_ERR, "Unable to complete write to %s", writefile);
            close(fd);
            closelog();
            return 1;
        }

        bytes_written += (size_t)result;
    }

    if (close(fd) == -1) {
        syslog(LOG_ERR, "Unable to close %s: %s", writefile,
               strerror(errno));
        closelog();
        return 1;
    }

    closelog();
    return 0;
}
