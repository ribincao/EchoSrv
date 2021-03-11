

https://github.com/froghui/yolanda

我们可以使用 fgets 方法等待标准输入，但是一旦这样做，就没有办法在套接字有数据的时候读出数据；我们也可以使用 read 方法等待套接字有数据返回，但是这样做，也没有办法在标准输入有数据的情况下，读入数据并发送给对方。I/O 多路复用的设计初衷就是解决这样的场景。我们可以把标准输入、套接字等都看做 I/O 的一路，多路复用的意思，就是在任何一路 I/O 有“事件”发生的情况下，通知应用程序去处理相应的 I/O 事件，这样我们的程序就变成了“多面手”，在同一时刻仿佛可以处理多个 I/O 事件。

## select

使用 select 函数，通知内核挂起进程，当一个或多个 I/O 事件发生后，控制权返还给应用程序，由应用程序进行 I/O 事件的处理。

```C++

int select(int maxfd, fd_set *readset, fd_set *writeset, fd_set *exceptset, const struct timeval *timeout);
//	返回：若有就绪描述符则为其数目，若超时则为0，若出错则为-1
```

maxfd 表示的是待测试的描述符基数，它的值是待测试的最大描述符加 1。比如现在的 select 待测试的描述符集合是{0,1,4}，那么 maxfd 就是 5

紧接着的是三个描述符集合，分别是读描述符集合 readset、写描述符集合 writeset 和异常描述符集合 exceptset，这三个分别通知内核，在哪些描述符上检测数据可以读，可以写和有异常发生

```C++
//	设置描述符集合
void FD_ZERO(fd_set *fdset);　　　　　　
void FD_SET(int fd, fd_set *fdset);　　
void FD_CLR(int fd, fd_set *fdset);　　　
int  FD_ISSET(int fd, fd_set *fdset);
```

```C++
a[maxfd-1], ..., a[1], a[0]
```

- FD_ZERO 用来将这个向量的所有元素都设置成 0；
- FD_SET 用来把对应套接字 fd 的元素，a[fd]设置成 1；
- FD_CLR 用来把对应套接字 fd 的元素，a[fd]设置成 0；
- FD_ISSET 对这个向量进行检测，判断出对应套接字的元素 a[fd]是 0 还是 1。

最后一个参数是 timeval 结构体时间：

```C++
struct timeval {
  long   tv_sec; /* seconds */
  long   tv_usec; /* microseconds */
};
```

这个参数设置成不同的值，会有不同的可能：

- 第一个可能是设置成空 (NULL)，表示如果没有 I/O 事件发生，则 select 一直等待下去。
- 第二个可能是设置一个非零的值，这个表示等待固定的一段时间后从 select 阻塞调用中返回，这在第 12 讲超时的例子里曾经使用过。
- 第三个可能是将 tv_sec 和 tv_usec 都设置成 0，表示根本不等待，检测完毕立即返回。这种情况使用得比较少。

```C++

int main(int argc, char **argv) {
    if (argc != 2) {
        error(1, 0, "usage: select01 <IPaddress>");
    }
    int socket_fd = tcp_client(argv[1], SERV_PORT);

    char recv_line[MAXLINE], send_line[MAXLINE];
    int n;

    fd_set readmask;
    fd_set allreads;
    FD_ZERO(&allreads);
    FD_SET(0, &allreads);
    FD_SET(socket_fd, &allreads);

    for (;;) {
      	//	每次 select 调用完成之后，记得要重置待测试集合
        readmask = allreads;
      	//	描述符基数是当前最大描述符 +1；
        int rc = select(socket_fd + 1, &readmask, NULL, NULL, NULL);

        if (rc <= 0) {
            error(1, errno, "select failed");
        }

        if (FD_ISSET(socket_fd, &readmask)) {
            n = read(socket_fd, recv_line, MAXLINE);
            if (n < 0) {
                error(1, errno, "read error");
            } else if (n == 0) {
                error(1, 0, "server terminated \n");
            }
            recv_line[n] = 0;
            fputs(recv_line, stdout);
            fputs("\n", stdout);
        }

        if (FD_ISSET(STDIN_FILENO, &readmask)) {
            if (fgets(send_line, MAXLINE, stdin) != NULL) {
                int i = strlen(send_line);
                if (send_line[i - 1] == '\n') {
                    send_line[i - 1] = 0;
                }

                printf("now sending %s\n", send_line);
                ssize_t rt = write(socket_fd, send_line, strlen(send_line));
                if (rt < 0) {
                    error(1, errno, "write failed ");
                }
                printf("send bytes: %zu \n", rt);
            }
        }
    }

}
```

select 检测套接字可写，完全是基于套接字本身的特性来说的，具体来说有以下几种情况。

- 第一种是套接字发送缓冲区足够大，如果我们使用套接字进行 write 操作，将不会被阻塞，直接返回。
- 第二种是连接的写半边已经关闭，如果继续进行写操作将会产生 SIGPIPE 信号。
- 第三种是套接字上有错误待处理，使用 write 函数去执行写操作，不阻塞，且返回 -1。

> 接收低水位标记是让select返回"可读"时套接字接收缓冲区中所需的数据量。对于TCP,其默认值为1。
>
> 发送低水位标记是让select返回"可写"时套接字发送缓冲区中所需的可用空间。对于TCP，其默认值常为2048.

## poll

poll 是除了 select 之外，另一种普遍使用的 I/O 多路复用技术，和 select 相比，它和内核交互的数据结构有所变化，另外，也突破了文件描述符的个数限制。

```C++
int poll(struct pollfd *fds, unsigned long nfds, int timeout); 
//	返回值：若有就绪描述符则为其数目，若超时则为0，若出错则为-1
```

第一个参数是一个 pollfd 的数组。其中 pollfd 的结构如下：

```C++
struct pollfd {
    int    fd;       /* file descriptor */
    short  events;   /* events to look for */
    short  revents;  /* events returned */
 };
```

这个结构体由三个部分组成，首先是描述符 fd，然后是描述符上待检测的事件类型 events，注意这里的 events 可以表示多个不同的事件，具体的实现可以通过使用二进制掩码位操作来完成，例如，POLLIN 和 POLLOUT 可以表示读和写事件。

```C++
#define    POLLIN    0x0001    /* any readable data available */
#define    POLLPRI   0x0002    /* OOB/Urgent readable data */
#define    POLLOUT   0x0004    /* file descriptor is writeable */
```

和 select 非常不同的地方在于，poll 每次检测之后的结果不会修改原来的传入值，而是将结果保留在 revents 字段中，这样就不需要每次检测完都得重置待检测的描述字和感兴趣的事件。我们可以把 revents 理解成“returned events”。

events 类型的事件可以分为两大类，第一类是可读事件，有以下几种：

```C++
#define POLLIN     0x0001    /* any readable data available */
#define POLLPRI    0x0002    /* OOB/Urgent readable data */
#define POLLRDNORM 0x0040    /* non-OOB/URG data available */
#define POLLRDBAND 0x0080    /* OOB/Urgent readable data */
```

第二类是可写事件，有以下几种：

```C++
#define POLLOUT    0x0004    /* file descriptor is writeable */
#define POLLWRNORM POLLOUT   /* no write type differentiation */
#define POLLWRBAND 0x0100    /* OOB/Urgent data can be written */
```

还有另一大类事件，没有办法通过 poll 向系统内核递交检测请求，只能通过“returned events”来加以检测，这类事件是各种错误事件。

```C++
#define POLLERR    0x0008    /* 一些错误发送 */
#define POLLHUP    0x0010    /* 描述符挂起*/
#define POLLNVAL   0x0020    /* 请求的事件无效*/
```

参数 nfds 描述的是数组 fds 的大小，简单说，就是向 poll 申请的事件检测的个数。最后一个参数 timeout，描述了 poll 的行为。如果是一个 <0 的数，表示在有事件发生之前永远等待；如果是 0，表示不阻塞进程，立即返回；如果是一个 >0 的数，表示 poll 调用方等待指定的毫秒数后返回。

关于返回值，当有错误发生时，poll 函数的返回值为 -1；如果在指定的时间到达之前没有任何事件发生，则返回 0，否则就返回检测到的事件个数，也就是“returned events”中非 0 的描述符个数。poll 函数有一点非常好，如果我们不想对某个 pollfd 结构进行事件检测，可以把它对应的 pollfd 结构的 fd 成员设置成一个负值。这样，poll 函数将忽略这样的 events 事件，检测完成以后，所对应的“returned events”的成员值也将设置为 0。

```C++
#define INIT_SIZE 128

int main(int argc, char **argv) {
    int listen_fd, connected_fd;
    int ready_number;
    ssize_t n;
    char buf[MAXLINE];
    struct sockaddr_in client_addr;

    listen_fd = tcp_server_listen(SERV_PORT);

    //初始化pollfd数组，这个数组的第一个元素是listen_fd，其余的用来记录将要连接的connect_fd
    struct pollfd event_set[INIT_SIZE];
    event_set[0].fd = listen_fd;
    event_set[0].events = POLLRDNORM;

    // 用-1表示这个数组位置还没有被占用
    int i;
    for (i = 1; i < INIT_SIZE; i++) {
        event_set[i].fd = -1;
    }

    for (;;) {
        if ((ready_number = poll(event_set, INIT_SIZE, -1)) < 0) {
            error(1, errno, "poll failed ");
        }

        if (event_set[0].revents & POLLRDNORM) {
            socklen_t client_len = sizeof(client_addr);
            connected_fd = accept(listen_fd, (struct sockaddr *) &client_addr, &client_len);

            //找到一个可以记录该连接套接字的位置
            for (i = 1; i < INIT_SIZE; i++) {
                if (event_set[i].fd < 0) {
                    event_set[i].fd = connected_fd;
                    event_set[i].events = POLLRDNORM;
                    break;
                }
            }

            if (i == INIT_SIZE) {
                error(1, errno, "can not hold so many clients");
            }

            if (--ready_number <= 0)
                continue;
        }

        for (i = 1; i < INIT_SIZE; i++) {
            int socket_fd;
            if ((socket_fd = event_set[i].fd) < 0)
                continue;
            if (event_set[i].revents & (POLLRDNORM | POLLERR)) {
                if ((n = read(socket_fd, buf, MAXLINE)) > 0) {
                    if (write(socket_fd, buf, n) < 0) {
                        error(1, errno, "write error");
                    }
                } else if (n == 0 || errno == ECONNRESET) {
                    close(socket_fd);
                    event_set[i].fd = -1;
                } else {
                    error(1, errno, "read error");
                }

                if (--ready_number <= 0)
                    break;
            }
        }
    }
}
```

## 非阻塞

读操作：如果套接字对应的接收缓冲区没有数据可读，在非阻塞情况下 read 调用会立即返回，一般返回 EWOULDBLOCK 或 EAGAIN 出错信息

写操作：在非阻塞 I/O 的情况下，如果套接字的发送缓冲区已达到了极限，不能容纳更多的字节，那么操作系统内核会尽最大可能从应用程序拷贝数据到发送缓冲区中，并立即从 write 等函数调用中返回。可想而知，在拷贝动作发生的瞬间，有可能一个字符也没拷贝，有可能所有请求字符都被拷贝完成，那么这个时候就需要返回一个数值，告诉应用程序到底有多少数据被成功拷贝到了发送缓冲区中，应用程序需要再次调用 write 函数，以输出未完成拷贝的字节。

- 非阻塞 I/O 需要这样：拷贝→返回→再拷贝→再返回

- 阻塞 I/O 需要这样：拷贝→直到所有数据拷贝至发送缓冲区完成→返回。

关于 read 和 write 还有几个结论

- read 总是在接收缓冲区有数据时就立即返回，不是等到应用程序给定的数据充满才返回。当接收缓冲区为空时，阻塞模式会等待，非阻塞模式立即返回 -1，并有 EWOULDBLOCK 或 EAGAIN 错误。
- 和 read 不同，阻塞模式下，write 只有在发送缓冲区足以容纳应用程序的输出字节时才返回；而非阻塞模式下，则是能写入多少就写入多少，并返回实际写入的字节数。
- 阻塞模式下的 write 有个特例, 就是对方主动关闭了套接字，这个时候 write 调用会立即返回，并通过返回值告诉应用程序实际写入的字节数，如果再次对这样的套接字进行 write 操作，就会返回失败。失败是通过返回值 -1 来通知到应用程序的。



accept

```C++

if (FD_ISSET(listen_fd, &readset)) {
    printf("listening socket readable\n");
    sleep(5);
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    int fd = accept(listen_fd, (struct sockaddr *) &ss, &slen);
```

这里的休眠时间非常关键，这样，在监听套接字上有可读事件发生时，并没有马上调用 accept。由于客户端发生了 RST 分节，该连接被接收端内核从自己的已完成队列中删除了，此时再调用 accept，由于没有已完成连接（假设没有其他已完成连接），accept 一直阻塞，更为严重的是，该线程再也没有机会对其他 I/O 事件进行分发，相当于该服务器无法对其他 I/O 进行服务。如果我们将监听套接字设为非阻塞，上述的情形就不会再发生。只不过对于 accept 的返回值，需要正确地处理各种看似异常的错误，例如忽略 EWOULDBLOCK、EAGAIN 等。一定要将监听套接字设置为非阻塞的

connect



非阻塞IO+select

```C++

#define MAX_LINE 1024
#define FD_INIT_SIZE 128

char rot13_char(char c) {
    if ((c >= 'a' && c <= 'm') || (c >= 'A' && c <= 'M'))
        return c + 13;
    else if ((c >= 'n' && c <= 'z') || (c >= 'N' && c <= 'Z'))
        return c - 13;
    else
        return c;
}

//数据缓冲区
struct Buffer {
    int connect_fd;  //连接字
    char buffer[MAX_LINE];  //实际缓冲
    size_t writeIndex;      //缓冲写入位置
    size_t readIndex;       //缓冲读取位置
    int readable;           //是否可以读
};

struct Buffer *alloc_Buffer() {
    struct Buffer *buffer = malloc(sizeof(struct Buffer));
    if (!buffer)
        return NULL;
    buffer->connect_fd = 0;
    buffer->writeIndex = buffer->readIndex = buffer->readable = 0;
    return buffer;
}

void free_Buffer(struct Buffer *buffer) {
    free(buffer);
}

int onSocketRead(int fd, struct Buffer *buffer) {
    char buf[1024];
    int i;
    ssize_t result;
    while (1) {
        result = recv(fd, buf, sizeof(buf), 0);
        if (result <= 0)
            break;

        for (i = 0; i < result; ++i) {
            if (buffer->writeIndex < sizeof(buffer->buffer))
                buffer->buffer[buffer->writeIndex++] = rot13_char(buf[i]);
            if (buf[i] == '\n') {
                buffer->readable = 1;  //缓冲区可以读
            }
        }
    }

    if (result == 0) {
        return 1;
    } else if (result < 0) {
        if (errno == EAGAIN)
            return 0;
        return -1;
    }

    return 0;
}

int onSocketWrite(int fd, struct Buffer *buffer) {
    while (buffer->readIndex < buffer->writeIndex) {
        ssize_t result = send(fd, buffer->buffer + buffer->readIndex, buffer->writeIndex - buffer->readIndex, 0);
        if (result < 0) {
            if (errno == EAGAIN)
                return 0;
            return -1;
        }

        buffer->readIndex += result;
    }

    if (buffer->readIndex == buffer->writeIndex)
        buffer->readIndex = buffer->writeIndex = 0;

    buffer->readable = 0;

    return 0;
}

int main(int argc, char **argv) {
    int listen_fd;
    int i, maxfd;
		//	应用层缓冲区
    struct Buffer *buffer[FD_INIT_SIZE];
    for (i = 0; i < FD_INIT_SIZE; ++i) {
        buffer[i] = alloc_Buffer();
    }

    listen_fd = tcp_nonblocking_server_listen(SERV_PORT);//	fcntl(fd, F_SETFL, O_NONBLOCK);

    fd_set readset, writeset, exset;
    FD_ZERO(&readset);
    FD_ZERO(&writeset);
    FD_ZERO(&exset);

    while (1) {
        maxfd = listen_fd;

        FD_ZERO(&readset);
        FD_ZERO(&writeset);
        FD_ZERO(&exset);

        // listener加入readset
        FD_SET(listen_fd, &readset);

        for (i = 0; i < FD_INIT_SIZE; ++i) {
            if (buffer[i]->connect_fd > 0) {
                if (buffer[i]->connect_fd > maxfd)
                    maxfd = buffer[i]->connect_fd;
                FD_SET(buffer[i]->connect_fd, &readset);
                if (buffer[i]->readable) {
                    FD_SET(buffer[i]->connect_fd, &writeset);
                }
            }
        }

        if (select(maxfd + 1, &readset, &writeset, &exset, NULL) < 0) {
            error(1, errno, "select error");
        }

        if (FD_ISSET(listen_fd, &readset)) {
            printf("listening socket readable\n");
            sleep(5);
            struct sockaddr_storage ss;
            socklen_t slen = sizeof(ss);
           int fd = accept(listen_fd, (struct sockaddr *) &ss, &slen);
            if (fd < 0) {
                error(1, errno, "accept failed");
            } else if (fd > FD_INIT_SIZE) {
                error(1, 0, "too many connections");
                close(fd);
            } else {
              	//	把套接字设置为非阻塞
                make_nonblocking(fd);
                if (buffer[fd]->connect_fd == 0) {
                    buffer[fd]->connect_fd = fd;
                } else {
                    error(1, 0, "too many connections");
                }
            }
        }

        for (i = 0; i < maxfd + 1; ++i) {
            int r = 0;
            if (i == listen_fd)
                continue;

            if (FD_ISSET(i, &readset)) {
                r = onSocketRead(i, buffer[i]);
            }
            if (r == 0 && FD_ISSET(i, &writeset)) {
                r = onSocketWrite(i, buffer[i]);
            }
            if (r) {
                buffer[i]->connect_fd = 0;
                close(i);
            }
        }
    }
}
```

非阻塞IO下读缓冲的作用有很多，你提到了通过设置缓冲区，减少系统调用的次数是一个方面，另外，别忘了读取的数据是需要在应用层进行报文解析的，一个应用层缓冲区显然是比较方便的，否则，需要不断的进行数据的读取，直至解析到完整的报文。我认为，应用层缓冲是"空间换时间"的一个比较好的例子。

非阻塞IO下使用应用层写缓冲区可以让还未来得及发出的数据先保存在应用层Buffer中，然后等到可写的时候再将数据从应用层Buffer写到fd的发送缓冲区中

## epoll

```C++
int epoll_create(int size);
int epoll_create1(int flags);
//	返回值: 若成功返回一个大于0的值，表示epoll实例；若返回-1表示出错
```

关于这个参数 size，在一开始的 epoll_create 实现中，是用来告知内核期望监控的文件描述字大小，然后内核使用这部分的信息来初始化内核数据结构，在新的实现中，这个参数不再被需要，因为内核可以动态分配需要的内核数据结构。我们只需要注意，每次将 size 设置成一个大于 0 的整数就可以了。

epoll_create1() 的用法和 epoll_create() 基本一致，如果 epoll_create1() 的输入 flags 为 0，则和 epoll_create() 一样，内核自动忽略。可以增加如 EPOLL_CLOEXEC 的额外选项，如果你有兴趣的话，可以研究一下这个选项有什么意义

```C++
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
//	返回值: 若成功返回0；若返回-1表示出错
```

在创建完 epoll 实例之后，可以通过调用 epoll_ctl 往这个 epoll 实例增加或删除监控的事件。函数 epll_ctl 有 4 个入口参数。

第一个参数 epfd 是刚刚调用 epoll_create 创建的 epoll 实例描述字，可以简单理解成是 epoll 句柄。

第二个参数表示增加还是删除一个监控事件，它有三个选项可供选择：

- EPOLL_CTL_ADD： 向 epoll 实例注册文件描述符对应的事件；
- EPOLL_CTL_DEL：向 epoll 实例删除文件描述符对应的事件；
- EPOLL_CTL_MOD： 修改文件描述符对应的事件。

第三个参数是注册的事件的文件描述符，比如一个监听套接字

第四个参数表示的是注册的事件类型，并且可以在这个结构体里设置用户需要的数据，其中最为常见的是使用联合结构里的 fd 字段，表示事件所对应的文件描述符。

```C++
typedef union epoll_data {
     void        *ptr;
     int          fd;
     uint32_t     u32;
     uint64_t     u64;
 } epoll_data_t;

 struct epoll_event {
     uint32_t     events;      /* Epoll events */
     epoll_data_t data;        /* User data variable */
 };
```

events：

- EPOLLIN：表示对应的文件描述字可以读；
- EPOLLOUT：表示对应的文件描述字可以写；
- EPOLLRDHUP：表示套接字的一端已经关闭，或者半关闭；
- EPOLLHUP：表示对应的文件描述字被挂起；
- EPOLLET：设置为 edge-triggered，默认为 level-triggered。

```C++
int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
//	返回值: 成功返回的是一个大于0的数，表示事件的个数；返回0表示的是超时时间到；若出错返回-1.
```

第一个参数是 epoll 实例描述字，也就是 epoll 句柄。

第二个参数返回给用户空间需要处理的 I/O 事件，这是一个数组，数组的大小由 epoll_wait 的返回值决定，这个数组的每个元素都是一个需要待处理的 I/O 事件，其中 events 表示具体的事件类型，事件类型取值和 epoll_ctl 可设置的值一样，这个 epoll_event 结构体里的 data 值就是在 epoll_ctl 那里设置的 data，也就是用户空间和内核空间调用时需要的数据。

第三个参数是一个大于 0 的整数，表示 epoll_wait 可以返回的最大事件值。

第四个参数是 epoll_wait 阻塞调用的超时值，如果这个值设置为 -1，表示不超时；如果设置为 0 则立即返回，即使没有任何 I/O 事件发生。

```C++

#include "lib/common.h"

#define MAXEVENTS 128

char rot13_char(char c) {
    if ((c >= 'a' && c <= 'm') || (c >= 'A' && c <= 'M'))
        return c + 13;
    else if ((c >= 'n' && c <= 'z') || (c >= 'N' && c <= 'Z'))
        return c - 13;
    else
        return c;
}

int main(int argc, char **argv) {
    int listen_fd, socket_fd;
    int n, i;
    int efd;
    struct epoll_event event;
    struct epoll_event *events;

    listen_fd = tcp_nonblocking_server_listen(SERV_PORT);

    efd = epoll_create1(0);
    if (efd == -1) {
        error(1, errno, "epoll create failed");
    }

    event.data.fd = listen_fd;
    event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, listen_fd, &event) == -1) {
        error(1, errno, "epoll_ctl add listen fd failed");
    }

    /* Buffer where events are returned */
    events = calloc(MAXEVENTS, sizeof(event));

    while (1) {
        n = epoll_wait(efd, events, MAXEVENTS, -1);
        printf("epoll_wait wakeup\n");
        for (i = 0; i < n; i++) {
            if ((events[i].events & EPOLLERR) ||
                (events[i].events & EPOLLHUP) ||
                (!(events[i].events & EPOLLIN))) {
                fprintf(stderr, "epoll error\n");
                close(events[i].data.fd);
                continue;
            } else if (listen_fd == events[i].data.fd) {
                struct sockaddr_storage ss;
                socklen_t slen = sizeof(ss);
                int fd = accept(listen_fd, (struct sockaddr *) &ss, &slen);
                if (fd < 0) {
                    error(1, errno, "accept failed");
                } else {
                    make_nonblocking(fd);
                    event.data.fd = fd;
                    event.events = EPOLLIN | EPOLLET; //edge-triggered
                    if (epoll_ctl(efd, EPOLL_CTL_ADD, fd, &event) == -1) {
                        error(1, errno, "epoll_ctl add connection fd failed");
                    }
                }
                continue;
            } else {
                socket_fd = events[i].data.fd;
                printf("get event on socket fd == %d \n", socket_fd);
                while (1) {
                    char buf[512];
                    if ((n = read(socket_fd, buf, sizeof(buf))) < 0) {
                        if (errno != EAGAIN) {
                            error(1, errno, "read error");
                            close(socket_fd);
                        }
                        break;
                    } else if (n == 0) {
                        close(socket_fd);
                        break;
                    } else {
                        for (i = 0; i < n; ++i) {
                            buf[i] = rot13_char(buf[i]);
                        }
                        if (write(socket_fd, buf, n) < 0) {
                            error(1, errno, "write error");
                        }
                    }
                }
            }
        }
    }

    free(events);
    close(listen_fd);
}
```

条件触发的意思是只要满足事件的条件，比如有数据需要读，就一直不断地把这个事件传递给用户；而边缘触发的意思是只有第一次满足条件的时候才触发，之后就不会再传递同样的事件了

## C10k

### 瓶颈

- 文件句柄

```C++
ulimit -n
```

这意味着最多可以服务的连接数上限只能是 1024。不过，我们可以对这个值进行修改，比如用 root 权限修改 /etc/sysctl.conf 文件，使得系统可以支持 10000 个描述符上限

```C++
fs.file-max = 10000
net.ipv4.ip_conntrack_max = 10000
net.ipv4.netfilter.ip_conntrack_max = 10000
```

- 系统内存

每个 TCP 连接占用的资源可不止一个连接套接字这么简单，在前面的章节中，我们多少接触到了类似发送缓冲区、接收缓冲区这些概念。每个 TCP 连接都需要占用一定的发送缓冲区和接收缓冲区。这里有一段 shell 代码，分别显示了在 Linux 4.4.0 下发送缓冲区和接收缓冲区的值。

```shell
$cat   /proc/sys/net/ipv4/tcp_wmem
4096  16384  4194304
$ cat   /proc/sys/net/ipv4/tcp_rmem
4096  87380  6291456
```

这三个值分别表示了最小分配值、默认分配值和最大分配值。按照默认分配值计算，一万个连接需要的内存消耗为

```C++
发送缓冲区： 16384*10000 = 160M bytes
接收缓冲区： 87380*10000 = 880M bytes
```

当然，我们的应用程序本身也需要一定的缓冲区来进行数据的收发，为了方便，我们假设每个连接需要 128K 的缓冲区，那么 1 万个链接就需要大约 1.2G 的应用层缓冲。这样，我们可以得出大致的结论，支持 1 万个并发连接，内存并不是一个巨大的瓶颈。

- 网络带宽

假设 1 万个连接，每个连接每秒传输大约 1KB 的数据，那么带宽需要 10000 x 1KB/s x8 = 80Mbps。这在今天的动辄万兆网卡的时代简直小菜一碟。

### 解决

我们知道，在网络编程中，涉及到频繁的用户态 - 内核态数据拷贝，设计不够好的程序可能在低并发的情况下工作良好，一旦到了高并发情形，其性能可能呈现出指数级别的损失。要想解决 C10K 问题，就需要从两个层面上来统筹考虑。

- 应用程序如何和操作系统配合，感知 I/O 事件发生，并调度处理在上万个套接字上的 I/O 操作？前面讲过的阻塞 I/O、非阻塞 I/O 讨论的就是这方面的问题。
- 应用程序如何分配进程、线程资源来服务上万个连接？这在接下来会详细讨论。

#### 阻塞I/O+进程

apache服务器

这种方式最为简单直接，每个连接通过 fork 派生一个子进程进行处理，因为一个独立的子进程负责处理了该连接所有的 I/O，所以即便是阻塞 I/O，多个连接之间也不会互相影响。这个方法虽然简单，但是效率不高，扩展性差，资源占用率高。

```C++
do{
   accept connections
   fork for conneced connection fd
   process_run(fd)
}
```

```C++
#include "lib/common.h"

#define MAX_LINE 4096

char rot13_char(char c) {
    if ((c >= 'a' && c <= 'm') || (c >= 'A' && c <= 'M'))
        return c + 13;
    else if ((c >= 'n' && c <= 'z') || (c >= 'N' && c <= 'Z'))
        return c - 13;
    else
        return c;
}

void child_run(int fd) {
    char outbuf[MAX_LINE + 1];
    size_t outbuf_used = 0;
    ssize_t result;

    while (1) {
        char ch;
        result = recv(fd, &ch, 1, 0);
        if (result == 0) {
            break;
        } else if (result == -1) {
            perror("read");
            break;
        }

        if (outbuf_used < sizeof(outbuf)) {
            outbuf[outbuf_used++] = rot13_char(ch);
        }

        if (ch == '\n') {
            send(fd, outbuf, outbuf_used, 0);
            outbuf_used = 0;
            continue;
        }
    }
}

void sigchld_handler(int sig) {
  	//	等待子进程关闭，循环回收，因为如果有多个子进程同时结束的话内核只会产生一次SIGCHLD信号至少信号处理函数只会唤醒一次,如果不循环就无法取得所有已终止的子进程数据
    while (waitpid(-1, 0, WNOHANG) > 0);
    return;
}

int main(int c, char **v) {
    int listener_fd = tcp_server_listen(SERV_PORT);
    signal(SIGCHLD, sigchld_handler);//	注册信号处理函数
    while (1) {
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        int fd = accept(listener_fd, (struct sockaddr *) &ss, &slen);
        if (fd < 0) {
            error(1, errno, "accept failed");
            exit(1);
        }

        if (fork() == 0) {
            close(listener_fd);//	子进程关闭监听描述符
            child_run(fd);
            exit(0);
        } else {
            close(fd);
        }
    }

    return 0;
}
```

#### 阻塞I/O+线程

- 线程

调用 pthread_create 函数来创建一个线程，每个线程都有一个线程 ID（tid）唯一来标识，其数据类型为 pthread_t，一般是 unsigned int。pthread_create 函数的第一个输出参数 tid 就是代表了线程 ID，如果创建线程成功，tid 就返回正确的线程 ID。每个线程都会有很多属性，比如优先级、是否应该成为一个守护进程等，这些值可以通过 pthread_attr_t 来描述，一般我们不会特殊设置，可以直接指定这个参数为 NULL。第三个参数为新线程的入口函数，该函数可以接收一个参数 arg，类型为指针，如果我们想给线程入口函数传多个值，那么需要把这些值包装成一个结构体，再把这个结构体的地址作为 pthread_create 的第四个参数，在线程入口函数内，再将该地址转为该结构体的指针对象。

```C++

int pthread_create(pthread_t *tid, const pthread_attr_t *attr,
　　　　　　　　　　　void *(*func)(void *), void *arg);

//	返回：若成功则为0，若出错则为正的Exxx值
```

在新线程的入口函数内，可以执行 pthread_self 函数返回线程 tid。

```C++
pthread_t pthread_self(void)
```

终止一个线程最直接的方法是在父线程内调用以下函数：

```C++
void pthread_exit(void *status)
```

当调用这个函数之后，父线程会等待其他所有的子线程终止，之后父线程自己终止。当然，如果一个子线程入口函数直接退出了，那么子线程也就自然终止了。所以，绝大多数的子线程执行体都是一个无限循环。也可以通过调用 pthread_cancel 来主动终止一个子线程，和 pthread_exit 不同的是，它可以指定某个子线程终止。

```C++
int pthread_cancel(pthread_t tid)
```

我们可以通过调用 pthread_join 回收已终止线程的资源，当调用 pthread_join 时，主线程会阻塞，直到对应 tid 的子线程自然终止。和 pthread_cancel 不同的是，它不会强迫子线程终止。

```C++
int pthread_join(pthread_t tid, void ** thread_return)
```

一个线程的重要属性是可结合的，或者是分离的。一个可结合的线程是能够被其他线程杀死和回收资源的；而一个分离的线程不能被其他线程杀死或回收资源。一般来说，默认的属性是可结合的。我们可以通过调用 pthread_detach 函数可以分离一个线程：

```C++
int pthread_detach(pthread_t tid)
```

在高并发的例子里，每个连接都由一个线程单独处理，在这种情况下，服务器程序并不需要对每个子线程进行终止，这样的话，每个子线程可以在入口函数开始的地方，把自己设置为分离的，这样就能在它终止后自动回收相关的线程资源了，就不需要调用 pthread_join 函数了。

进程模型占用的资源太大，幸运的是，还有一种轻量级的资源模型，这就是线程。通过为每个连接调用 pthread_create 创建一个单独的线程，也可以达到上面使用进程的效果

```C++
do{
   accept connections
   pthread_create for conneced connection fd
   thread_run(fd)
}while(true)
```

```C++

#include "lib/common.h"

char rot13_char(char c) {
    if ((c >= 'a' && c <= 'm') || (c >= 'A' && c <= 'M'))
        return c + 13;
    else if ((c >= 'n' && c <= 'z') || (c >= 'N' && c <= 'Z'))
        return c - 13;
    else
        return c;
}

void loop_echo(int fd) {
    char outbuf[MAX_LINE + 1];
    size_t outbuf_used = 0;
    ssize_t result;
    while (1) {
        char ch;
        result = recv(fd, &ch, 1, 0);

        //断开连接或者出错
        if (result == 0) {
            break;
        } else if (result == -1) {
            error(1, errno, "read error");
            break;
        }

        if (outbuf_used < sizeof(outbuf)) {
            outbuf[outbuf_used++] = rot13_char(ch);
        }

        if (ch == '\n') {
            send(fd, outbuf, outbuf_used, 0);
            outbuf_used = 0;
            continue;
        }
    }
}

void thread_run(void *arg) {
    pthread_detach(pthread_self());
    int fd = (int) arg;
    loop_echo(fd);
}

int main(int c, char **v) {
    int listener_fd = tcp_server_listen(SERV_PORT);
    pthread_t tid;
    
    while (1) {
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        int fd = accept(listener_fd, (struct sockaddr *) &ss, &slen);
        if (fd < 0) {
            error(1, errno, "accept failed");
        } else {
            pthread_create(&tid, NULL, &thread_run, (void *) fd);
        }
    }

    return 0;
}
```

因为线程的创建是比较消耗资源的，况且不是每个连接在每个时刻都需要服务，因此，我们可以预先通过创建一个线程池，并在多个连接中复用线程池来获得某种效率上的提升。

```C++
create thread pool
do{
   accept connections
   get connection fd
   push_queue(fd)
}while(true)
```

对此，需要引入两个重要的概念，一个是锁 mutex，一个是条件变量 condition。锁很好理解，加锁的意思就是其他线程不能进入；条件变量则是在多个线程需要交互的情况下，用来线程间同步的原语。

```C++

//定义一个队列
typedef struct {
    int number;  //队列里的描述字最大个数
    int *fd;     //这是一个数组指针
    int front;   //当前队列的头位置
    int rear;    //当前队列的尾位置
    pthread_mutex_t mutex;  //锁
    pthread_cond_t cond;    //条件变量
} block_queue;

//初始化队列
void block_queue_init(block_queue *blockQueue, int number) {
    blockQueue->number = number;
    blockQueue->fd = calloc(number, sizeof(int));
    blockQueue->front = blockQueue->rear = 0;
    pthread_mutex_init(&blockQueue->mutex, NULL);
    pthread_cond_init(&blockQueue->cond, NULL);
}

//往队列里放置一个描述字fd
void block_queue_push(block_queue *blockQueue, int fd) {
    //一定要先加锁，因为有多个线程需要读写队列
    pthread_mutex_lock(&blockQueue->mutex);
    //将描述字放到队列尾的位置
    blockQueue->fd[blockQueue->rear] = fd;
    //如果已经到最后，重置尾的位置
    if (++blockQueue->rear == blockQueue->number) {
        blockQueue->rear = 0;
    }
    printf("push fd %d", fd);
    //通知其他等待读的线程，有新的连接字等待处理
    pthread_cond_signal(&blockQueue->cond);
    //解锁
    pthread_mutex_unlock(&blockQueue->mutex);
}

//从队列里读出描述字进行处理
int block_queue_pop(block_queue *blockQueue) {
    //加锁
    pthread_mutex_lock(&blockQueue->mutex);
    //判断队列里没有新的连接字可以处理，就一直条件等待，直到有新的连接字入队列
    while (blockQueue->front == blockQueue->rear)
        pthread_cond_wait(&blockQueue->cond, &blockQueue->mutex);
    //取出队列头的连接字
    int fd = blockQueue->fd[blockQueue->front];
    //如果已经到最后，重置头的位置
    if (++blockQueue->front == blockQueue->number) {
        blockQueue->front = 0;
    }
    printf("pop fd %d", fd);
    //解锁
    pthread_mutex_unlock(&blockQueue->mutex);
    //返回连接字
    return fd;
} 

void thread_run(void *arg) {
    pthread_t tid = pthread_self();
    pthread_detach(tid);

    block_queue *blockQueue = (block_queue *) arg;
    while (1) {
        int fd = block_queue_pop(blockQueue);
        printf("get fd in thread, fd==%d, tid == %d", fd, tid);
        loop_echo(fd);
    }
}

int main(int c, char **v) {
    int listener_fd = tcp_server_listen(SERV_PORT);

    block_queue blockQueue;
    block_queue_init(&blockQueue, BLOCK_QUEUE_SIZE);

    thread_array = calloc(THREAD_NUMBER, sizeof(Thread));
    int i;
    for (i = 0; i < THREAD_NUMBER; i++) {
        pthread_create(&(thread_array[i].thread_tid), NULL, &thread_run, (void *) &blockQueue);
    }

    while (1) {
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        int fd = accept(listener_fd, (struct sockaddr *) &ss, &slen);
        if (fd < 0) {
            error(1, errno, "accept failed");
        } else {
            block_queue_push(&blockQueue, fd);
        }
    }

    return 0;
}
```

#### 非阻塞 I/O + readiness notification + 单线程（IO事件分发）

应用程序其实可以采取轮询的方式来对保存的套接字集合进行挨个询问，从而找出需要进行 I/O 处理的套接字，像给出的伪码一样，其中 is_readble 和 is_writeable 可以通过对套接字调用 read 或 write 操作来判断。

```C++

for fd in fdset{
   if(is_readable(fd) == true){
     handle_read(fd)
   }else if(is_writeable(fd)==true){
     handle_write(fd)
   }
}
```

但这个方法有一个问题，如果这个 fdset 有一万个之多，每次循环判断都会消耗大量的 CPU 时间，而且极有可能在一个循环之内，没有任何一个套接字准备好可读，或者可写。既然这样，CPU 的消耗太大，那么干脆让操作系统来告诉我们哪个套接字可以读，哪个套接字可以写。在这个结果发生之前，我们把 CPU 的控制权交出去，让操作系统来把宝贵的 CPU 时间调度给那些需要的进程，这就是 select、poll 这样的 I/O 分发技术。

通过使用 poll、epoll 等 I/O 分发技术，可以设计出基于套接字的事件驱动程序，从而满足高性能、高并发的需求。

事件驱动模型，也被叫做反应堆模型（reactor），或者是 Event loop 模型。这个模型的核心有两点。

- 它存在一个无限循环的事件分发线程，或者叫做 reactor 线程、Event loop 线程。这个事件分发线程的背后，就是 poll、epoll 等 I/O 分发技术的使用
- 所有的 I/O 操作都可以抽象成事件，每个事件必须有回调函数来处理。acceptor 上有连接建立成功、已连接套接字上发送缓冲区空出可以写、通信管道 pipe 上有数据可以读，这些都是一个个事件，通过事件分发，这些事件都可以一一被检测，并调用对应的回调函数加以处理。

```C++
do {
    poller.dispatch()
    for fd in registered_fdset{
         if(is_readable(fd) == true){
           handle_read(fd)
         }else if(is_writeable(fd)==true){
           handle_write(fd)
     }
}while(ture)
```

但是，这样的方法需要每次 dispatch 之后，对所有注册的套接字进行逐个排查，效率并不是最高的。如果 dispatch 调用返回之后只提供有 I/O 事件或者 I/O 变化的套接字，这样排查的效率不就高很多了么？这就是前面我们讲到的 epoll 设计。

```C++
do {
    poller.dispatch()
    for fd_event in active_event_set{
         if(is_readable_event(fd_event) == true){
           handle_read(fd_event)
         }else if(is_writeable_event(fd_event)==true){
           handle_write(fd_event)
     }
}while(ture)
```

#### 非阻塞 I/O + readiness notification + 多线程（IO事件分发）

前面的做法是所有的 I/O 事件都在一个线程里分发，如果我们把线程引入进来，可以利用现代 CPU 多核的能力，让每个核都可以作为一个 I/O 分发器进行 I/O 事件的分发。这就是所谓的主从 reactor 模式（两个线程池，主处理accptor，从处理read write）。基于 epoll/poll/select 的 I/O 事件分发器可以叫做 reactor，也可以叫做事件驱动，或者事件轮询（eventloop）。我没有把基于 select/poll 的所谓“level triggered”通知机制和基于 epoll 的“edge triggered”通知机制分开（C10K 问题总结里是分开的），我觉得这只是 reactor 机制的实现高效性问题，而不是编程模式的巨大区别。

#### 异步I/O+多线程

异步非阻塞 I/O 模型是一种更为高效的方式，当调用结束之后，请求立即返回，由操作系统后台完成对应的操作，当最终操作完成，就会产生一个信号，或者执行一个回调函数来完成 I/O 处理。

#### C10M

 XDP 的方式，在内核协议栈之前处理网络包；或者用 DPDK 直接跳过网络协议栈，在用户空间通过轮询的方式直接处理网络包



## 异步

这个程序展示了如何使用 aio 系列函数来完成异步读写。主要用到的函数有:

- aio_write：用来向内核提交异步写操作；
- aio_read：用来向内核提交异步读操作；
- aio_error：获取当前异步操作的状态；
- aio_return：获取异步操作读、写的字节数。





任何一个网络程序，所做的事情可以总结成下面几种

- read：从套接字收取数据；
- decode：对收到的数据进行解析；
- compute：根据解析之后的内容，进行计算和处理；
- encode：将处理之后的结果，按照约定的格式进行编码；
- send：最后，通过套接字把结果发送出去。





- 主从是为了分离已连接和accept

- 工作线程是为了分离I/O密集和CPU密集

第27讲很重要

1：阻塞IO+多进程——实现简单，性能一般

2：阻塞IO+多线程——相比于阻塞IO+多进程，减少了上下文切换所带来的开销，性能有所提高。

3：阻塞IO+线程池——相比于阻塞IO+多线程，减少了线程频繁创建和销毁的开销，性能有了进一步的提高。

4：Reactor+线程池——相比于阻塞IO+线程池，采用了更加先进的事件驱动设计思想，资源占用少、效率高、扩展性强，是支持高性能高并发场景的利器。

5：主从Reactor+线程池——相比于Reactor+线程池，将连接建立事件和已建立连接的各种IO事件分离，主Reactor只负责处理连接事件，从Reactor只负责处理各种IO事件，这样能增加客户端连接的成功率，并且可以充分利用现在多CPU的资源特性进一步的提高IO事件的处理效率。
6：主 - 从Reactor模式的核心思想是，主Reactor线程只负责分发 Acceptor 连接建立，已连接套接字上的 I/O 事件交给 从Reactor 负责分发。其中 sub-reactor 的数量，可以根据 CPU 的核数来灵活设置。















