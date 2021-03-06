#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iostream>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define MAXLINE 0xFFFF
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 4000
static int	read_cnt;
static char	*read_ptr;
static char	read_buf[MAXLINE];

static ssize_t
my_read(int fd, char *ptr)
{

	if (read_cnt <= 0) {
again:
		if ( (read_cnt = read(fd, read_buf, sizeof(read_buf))) < 0) {
			if (errno == EINTR)
				goto again;
			return(-1);
		} else if (read_cnt == 0)
			return(0);
		read_ptr = read_buf;
	}

	read_cnt--;
	*ptr = *read_ptr++;
	return(1);
}

ssize_t
readline(int fd, char *vptr, size_t maxlen)
{
	ssize_t	n, rc;
	char	c, *ptr;

	ptr = vptr;
	for (n = 1; n < maxlen; n++) {
		if ( (rc = my_read(fd, &c)) == 1) {
			*ptr++ = c;
			if (c == '\n')
				break;	/* newline is stored, like fgets() */
		} else if (rc == 0) {
			*ptr = 0;
			return(n - 1);	/* EOF, n - 1 bytes were read */
		} else
			return(-1);		/* error, errno set by read() */
	}

	*ptr = 0;	/* null terminate like fgets() */
	return(n);
}

void str_cli(FILE *fp, int sockfd)
{
	char	sendline[MAXLINE], recvline[MAXLINE];
	while (fgets(sendline, MAXLINE, fp) != NULL) {
		write(sockfd, sendline, strlen(sendline));
		if(readline(sockfd, recvline, MAXLINE) == 0) {
			std::cerr << "str_cli: server terminated prematurely" << std::endl;
			exit(-1);
		}
		fputs(recvline, stdout);
	}
}
int main(int argc, char **argv)
{
	int					sockfd;
	struct sockaddr_in	servaddr;

	// bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(SERVER_PORT);
	servaddr.sin_addr.s_addr = inet_addr(SERVER_IP);
	// inet_pton(AF_INET, argv[1], &servaddr.sin_addr);

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0) { 
        std::cerr << "socket" << std::endl;
        exit(-1);
    }
	
	int ret = connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
	std::cout << ret << std::endl;
	if(ret < 0) {
        std::cerr << "connect" << std::endl;
		perror("connect");
        exit(-1);
    }

	str_cli(stdin, sockfd);		/* do it all */

	exit(0);
}
