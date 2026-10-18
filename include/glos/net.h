/* Sockets for GLOS apps (M10 step B; sdk/libglos/socket.c, DJGPP): a small
 * BSD layer on GLOS_FN_NET (glos/api.h), for portable code that makes TCP
 * connections. IPv4 and TCP only, no listening.
 *
 *   socket, connect, send, recv, shutdown (SHUT_RD only), close, setsockopt
 *   (SO_RCVTIMEO, SO_SNDTIMEO, TCP_NODELAY), getsockopt (SO_ERROR, SO_TYPE),
 *   getpeername, fcntl's O_NONBLOCK (as glos_fcntl); getaddrinfo, freeaddrinfo,
 *   gai_strerror, gethostbyname; inet_addr, inet_aton, inet_ntoa, inet_pton,
 *   inet_ntop. A socket is a file descriptor (DJGPP's file-system
 *   extensions), so read, write, close and select work on it too.
 *
 * The program must be a GLOS app (glos_init()); before that, and without
 * GLOS, socket() fails with ENOSYS. A blocking connect gives up after 60
 * seconds (SO_SNDTIMEO sets another time). include/glos/posix has
 * <sys/socket.h>, <arpa/inet.h>, <netdb.h> and <netinet/tcp.h>, which bring
 * this in, for code that includes those. */
#ifndef GLOS_NET_H
#define GLOS_NET_H

#include <errno.h>
#include <netinet/in.h>                 /* (DJGPP's: htonl, htons, ntohl, ntohs) */
#include <stddef.h>
#include <sys/types.h>
#include <time.h>                       /* (struct timeval, select) */

#ifdef __cplusplus
extern "C" {
#endif

/* errno values DJGPP lacks (its own end at 41). */
#ifndef ECONNREFUSED
#define ENETDOWN        60
#define ETIMEDOUT       61
#define ECONNREFUSED    62
#define ECONNRESET      63
#define ENOTCONN        64
#define EINPROGRESS     65
#define EALREADY        66
#define EISCONN         67
#define ENOTSOCK        68
#define EAFNOSUPPORT    69
#define EPROTONOSUPPORT 70
#define EOPNOTSUPP      71
#define ENOBUFS         72
#define EHOSTUNREACH    73
#define EDESTADDRREQ    74
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK     EAGAIN
#endif

typedef unsigned socklen_t;
typedef unsigned short sa_family_t;
typedef unsigned long in_addr_t;
typedef unsigned short in_port_t;

#define AF_UNSPEC       0
#define AF_INET         2
#define PF_UNSPEC       AF_UNSPEC
#define PF_INET         AF_INET
#define SOCK_STREAM     1
#define IPPROTO_IP      0
#define IPPROTO_TCP     6

#define INADDR_ANY      0x00000000UL
#define INADDR_LOOPBACK 0x7F000001UL
#define INADDR_NONE     0xFFFFFFFFUL

#define INET_ADDRSTRLEN 16

struct in_addr {
    in_addr_t s_addr;                   /* network order */
};

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_in {
    sa_family_t sin_family;             /* AF_INET */
    in_port_t sin_port;                 /* network order */
    struct in_addr sin_addr;
    unsigned char sin_zero[8];
};

struct sockaddr_storage {
    sa_family_t ss_family;
    char ss_data[30];
};

#define SOL_SOCKET      0xFFFF
#define SO_TYPE         0x1008
#define SO_ERROR        0x1007
#define SO_RCVTIMEO     0x1006
#define SO_SNDTIMEO     0x1005
#define SO_KEEPALIVE    0x0008          /* (taken, and nothing done) */
#define SO_REUSEADDR    0x0004          /* (taken, and nothing done) */
#define TCP_NODELAY     0x0001

#define MSG_DONTWAIT    0x40
#define MSG_NOSIGNAL    0x4000          /* (GLOS raises no SIGPIPE: taken) */

#define SHUT_RD         0
#define SHUT_WR         1
#define SHUT_RDWR       2

int socket(int domain, int type, int protocol);
int connect(int fd, const struct sockaddr *addr, socklen_t len);
ssize_t send(int fd, const void *buf, size_t n, int flags);
ssize_t recv(int fd, void *buf, size_t n, int flags);
int shutdown(int fd, int how);
int setsockopt(int fd, int level, int name, const void *val, socklen_t len);
int getsockopt(int fd, int level, int name, void *val, socklen_t *len);
int getpeername(int fd, struct sockaddr *addr, socklen_t *len);
int glos_socket_id(int fd);             /* the GLOS_FN_NET connection behind a socket, or -1 */

/* fcntl with O_NONBLOCK for sockets: DJGPP's fcntl tells a file-system extension only the descriptor, not the
   command. Other descriptors and commands go on to DJGPP's. */
int glos_fcntl(int fd, int cmd, ...);
#define fcntl glos_fcntl

/* Names. */
struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    socklen_t ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};

#define AI_PASSIVE      0x01
#define AI_CANONNAME    0x02
#define AI_NUMERICHOST  0x04
#define AI_NUMERICSERV  0x08

#define EAI_AGAIN       2               /* no answer in time */
#define EAI_BADFLAGS    3
#define EAI_FAIL        4               /* the network is down */
#define EAI_FAMILY      5
#define EAI_MEMORY      6
#define EAI_NONAME      8
#define EAI_SERVICE     9
#define EAI_SOCKTYPE    10
#define EAI_SYSTEM      11

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);
const char *gai_strerror(int e);

struct hostent {
    char *h_name;
    char **h_aliases;
    int h_addrtype;
    int h_length;
    char **h_addr_list;
};
#define h_addr h_addr_list[0]
extern int h_errno;
#define HOST_NOT_FOUND  1
#define TRY_AGAIN       2
#define NO_RECOVERY     3
struct hostent *gethostbyname(const char *name);

in_addr_t inet_addr(const char *s);
int inet_aton(const char *s, struct in_addr *a);
char *inet_ntoa(struct in_addr a);
int inet_pton(int af, const char *s, void *dst);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);

const char *glos_net_strerror(int e);   /* strerror() for the errno values above too */

#ifdef __cplusplus
}
#endif
#endif
