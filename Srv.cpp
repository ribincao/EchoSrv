#include <sys/socket.h>
#include <iostream>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define SERV_PORT 4000
#define LISTEN 5
#define MAXLINE 0xFFFF

void str_echo(int sockfd)
{
	ssize_t		n;
	char		buf[MAXLINE];

again:
	while ((n = read(sockfd, buf, MAXLINE)) > 0)
		write(sockfd, buf, n);

	if (n < 0 && errno == EINTR) {
        goto again;
    } else if (n < 0) {
        std::cerr << "str_echo: read error" << std::endl;
        exit(-1);
    }
}

int main(int argc, char **argv)
{
	int					fd, connfd;
	pid_t				childpid;
	socklen_t			clilen;
	struct sockaddr_in	cliaddr, servaddr;

	fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) { 
        std::cerr << "socket" << std::endl;
        exit(-1);
    }
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port        = htons(SERV_PORT);

    int optval = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if(bind(fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        std::cerr << "bind" << std::endl;
        exit(-1);
    }

	if(listen(fd, LISTEN) < 0) {
        std::cerr << "listen" << std::endl;
        exit(-1);
    }

	for ( ; ; ) {
		clilen = sizeof(cliaddr);
		connfd = accept(fd, (struct sockaddr *) &cliaddr, &clilen);
		if ( (childpid = fork()) == 0) {	/* child process */
			close(fd);	/* close listening socket */
			str_echo(connfd);	/* process the request */
			exit(0);
		}
		close(connfd);			/* parent closes connected socket */
	}
}
