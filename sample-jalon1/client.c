#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common.h"

void echo_client(int sockfd) {
	char buff[MSG_LEN];
	int n;
	while (1) {
		// Cleaning memory
		memset(buff, 0, MSG_LEN);
		// Getting message from client
		printf("Message: ");
		n = 0;
		while ((buff[n++] = getchar()) != '\n') {} // trailing '\n' will be sent

		// Sending messages (ECHO)
		// first the length of the msg, then the msg itself
		int len = strlen(buff);
		if (send(sockfd, &len, sizeof(int), 0) <= 0) {
			break;
		}
		if (send(sockfd, buff, strlen(buff), 0) <= 0) {
			break;
		}
		printf("%d\n", len);
		printf("Message sent!\n");
		// Cleaning memory
		memset(buff, 0, MSG_LEN);		
		// Receiving message
		int rlen;
		if (recv(sockfd, &rlen, sizeof(int), 0) <= 0) {
			break;
		}
		if (recv(sockfd, buff, rlen, 0) <= 0) {
			break;
		}
		printf("%d\n", rlen);
		printf("Received: %s", buff);
	}
}

int handle_connect(char *serv_name, char *serv_port) {
	struct addrinfo hints, *result, *rp;
	int sfd;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(serv_name, serv_port, &hints, &result) != 0) {
		perror("getaddrinfo()");
		exit(EXIT_FAILURE);
	}
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype,rp->ai_protocol);
		if (sfd == -1) {
			continue;
		}
		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
			break;
		}
		close(sfd);
	}
	if (rp == NULL) {
		fprintf(stderr, "Could not connect\n");
		exit(EXIT_FAILURE);
	}
	freeaddrinfo(result);
	return sfd;
}

int main(int argc, char *argv[]) {
	if (argc != 3) {
    fprintf(stderr, "Usage: %s <server_name> <server_port>\n", argv[0]);
    exit(EXIT_FAILURE);
	}



	int sfd;
	sfd = handle_connect(argv[1], argv[2]);
	echo_client(sfd);
	close(sfd);
	return EXIT_SUCCESS;

	
}

