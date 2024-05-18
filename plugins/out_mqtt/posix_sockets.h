#if !defined(__POSIX_SOCKET_TEMPLATE_H__)
#define __POSIX_SOCKET_TEMPLATE_H__

#include <stdio.h>
#include <sys/types.h>
#if !defined(WIN32)
#include <sys/socket.h>
#include <netdb.h>
#else
#include <ws2tcpip.h>
#endif
#if defined(__VMS)
#include <ioctl.h>
#endif
#include <fcntl.h>

/*
    A template for opening a non-blocking POSIX socket.
*/
int open_nb_socket(const char* addr, const char* port);

#endif
