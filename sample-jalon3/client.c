#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common.h"


/*
 * Transforme ce que tape l'utilisateur
 * en struct message + payload.
 */
int prepare_message(char *line,
                    char *nickname,
                    char *pending_nick,
                    struct message *msg,
                    char **payload)
{
    char *argument;
    char *space;

    *payload = NULL;


    /* /nick <pseudo> */
    if (strncmp(line, "/nick ", 6) == 0) {

        argument = line + 6;

        if (!is_valid_nick(argument)) {
            printf("[Client] : invalid nickname\n");
            return -1;
        }

        build_msg(msg,
                  NICKNAME_NEW,
                  nickname,
                  argument,
                  0);

        return 0;
    }


    /* /who */
    if (strcmp(line, "/who") == 0) {

        build_msg(msg,
                  NICKNAME_LIST,
                  nickname,
                  "",
                  0);

        return 0;
    }


    /* /whois <pseudo> */
    if (strncmp(line, "/whois ", 7) == 0) {

        argument = line + 7;

        build_msg(msg,
                  NICKNAME_INFOS,
                  nickname,
                  argument,
                  0);

        return 0;
    }


    /* /msgall <message> */
    if (strncmp(line, "/msgall ", 8) == 0) {

        *payload = line + 8;

        build_msg(msg,
                  BROADCAST_SEND,
                  nickname,
                  "",
                  strlen(*payload));

        return 0;
    }


    /* /msg <pseudo> <message> */
    if (strncmp(line, "/msg ", 5) == 0) {

        space = strchr(line + 5, ' ');

        if (space == NULL) {
            printf("[Client] : usage: /msg <nickname> <message>\n");
            return -1;
        }

        *space = '\0';

        *payload = space + 1;

        build_msg(msg,
                  UNICAST_SEND,
                  nickname,
                  line + 5,
                  strlen(*payload));

        return 0;
    }

    /* /send <pseudo> <fichier> */
    if(strncmp(line, "/send ", 6) == 0) {

        space = strchr(line + 6, ' ');

        if (space == NULL) {
            printf("[Client] : usage: /send <nickname> <filename>\n");
            return -1;
        }

        *space = '\0';

        *payload = space + 1;

        build_msg(msg,
                  FILE_REQUEST,
                  nickname,
                  line + 6,
                  strlen(*payload));

        return 0;
    }

    /* /reject */
    if (strcmp(line, "/reject") == 0) {

        // rien a refuser si personne n'a rien propose
        if (pending_nick[0] == '\0') {
            printf("[Client] : no pending file request\n");
            return -1;
        }

        build_msg(msg,
                  FILE_REJECT,
                  nickname,
                  pending_nick,    // a qui repondre
                  0);

        pending_nick[0] = '\0';    // la demande est traitee

        return 0;
    }


    /* Commande inconnue */
    if (line[0] == '/') {

        printf("[Client] : unknown command\n");
        return -1;
    }


    /* Pas de commande = echo */
    *payload = line;

    build_msg(msg,
              ECHO_SEND,
              nickname,
              "",
              strlen(line));

    return 0;
}


void run_client(int sockfd)
{
    struct pollfd fds[2];
    struct message msg;
    char line[MSG_LEN];     // qui veut m'envoyer un msg
    char payload[MSG_LEN];      // et quel msg
    char nickname[NICK_LEN];
    char *to_send;
    char pending_nick[NICK_LEN];    // qui veut m'envoyer un fichier
    char pending_file[MSG_LEN];     // et quel fichier

    nickname[0] = '\0';
    pending_nick[0] = '\0';



    /*
     * fds[0] = clavier
     * fds[1] = serveur
     */
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    fds[1].fd = sockfd;
    fds[1].events = POLLIN;


    printf("Connected to server.\n");
    printf("Choose a nickname with /nick <nickname>\n");


    while (1) {

        if (poll(fds, 2, -1) < 0) {
            perror("poll");
            break;
        }


        /* L'utilisateur a tape quelque chose */
        if (fds[0].revents & POLLIN) {

            if (fgets(line, MSG_LEN, stdin) == NULL)
                break;

            /* Enlever le \n ajoute par fgets */
            line[strcspn(line, "\n")] = '\0';


            if (strcmp(line, "/quit") == 0)
                break;


            if (line[0] == '\0')
                continue;


            if (prepare_message(line,
                                nickname,
                                pending_nick,
                                &msg,
                                &to_send) < 0) {

                continue;
            }


            if (send_msg(sockfd,
                         &msg,
                         to_send) < 0) {

                break;
            }
        }


        /* Le serveur a envoye quelque chose */
        if (fds[1].revents & POLLIN) {

            if (recv_msg(sockfd,
                         &msg,
                         payload) < 0) {

                printf("[Client] : server disconnected\n");
                break;
            }


            /*
             * Si /nick a reussi,
             * infos contient le pseudo accepte.
             */
            if (msg.type == NICKNAME_NEW &&
                msg.infos[0] != '\0') {

                strncpy(nickname,
                        msg.infos,
                        NICK_LEN - 1);

                nickname[NICK_LEN - 1] = '\0';
            }

            if (msg.type == FILE_REQUEST) {

                strncpy(pending_nick,
                        msg.infos,
                        NICK_LEN - 1);
                strncpy(pending_file,
                        payload,
                        MSG_LEN - 1);

                printf("[%s] wants to send you %s. /accept or /reject\n",
                       msg.nick_sender, payload);

                continue;

            }


            printf("[%s] : %s\n",
                   msg.nick_sender,
                   payload);
        }
    }
}


/* Connexion au serveur */
int handle_connect(char *server_name,
                   char *server_port)
{
    struct addrinfo hints;
    struct addrinfo *result;
    struct addrinfo *rp;

    int sfd;

    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(server_name,
                    server_port,
                    &hints,
                    &result) != 0) {

        perror("getaddrinfo");
        exit(EXIT_FAILURE);
    }

	
    for (rp = result;
         rp != NULL;
         rp = rp->ai_next) {

        sfd = socket(rp->ai_family,
                     rp->ai_socktype,
                     rp->ai_protocol);

        if (sfd == -1)
            continue;


        if (connect(sfd,
                    rp->ai_addr,
                    rp->ai_addrlen) == 0) {

            break;
        }


        close(sfd);
    }


    if (rp == NULL) {

        fprintf(stderr, "Could not connect\n");
        freeaddrinfo(result);
        exit(EXIT_FAILURE);
    }


    freeaddrinfo(result);

    return sfd;
}


int main(int argc, char *argv[])
{
    int sfd;


    if (argc != 3) {

        fprintf(stderr,
                "Usage: %s <server_name> <server_port>\n",
                argv[0]);

        return EXIT_FAILURE;
    }


    sfd = handle_connect(argv[1],
                         argv[2]);


    run_client(sfd);


    close(sfd);

    return EXIT_SUCCESS;
}