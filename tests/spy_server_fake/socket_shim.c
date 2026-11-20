/* Host-test shim for an isolated Unix socketpair in the restricted runner. */
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

ssize_t __wrap_send(int fd, const void *buffer, size_t length, int flags)
{
    if(flags) { errno=ENOTSUP;return -1; }
    return write(fd,buffer,length);
}

ssize_t __wrap_recv(int fd, void *buffer, size_t length, int flags)
{
    if(flags==MSG_DONTWAIT) {
        struct pollfd pollfd={.fd=fd,.events=POLLIN};
        int ready=poll(&pollfd,1,0);
        if(ready==0) { errno=EAGAIN;return -1; }
        if(ready<0)return -1;
    } else if(flags) { errno=ENOTSUP;return -1; }
    return read(fd,buffer,length);
}
