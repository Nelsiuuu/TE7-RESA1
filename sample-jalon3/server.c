#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

#define MAX_CLIENTS 128


/*
 * Un element de la liste chainee
 * represente un client connecte.
 */
struct client {
    int fd;
    char nickname[NICK_LEN];
    struct sockaddr_in address;
    time_t connection_time;
    struct client *next;
};


/* Ajouter un client dans la liste */
void add_client(struct client **list,
                int fd,
                struct sockaddr_in address)
{
    struct client *new_client;

    new_client = malloc(sizeof(struct client));

    if (new_client == NULL) {
        perror("malloc");
        close(fd);
        return;
    }

    new_client->fd = fd;
    new_client->nickname[0] = '\0';
    new_client->address = address;
    new_client->connection_time = time(NULL);

    new_client->next = *list;
    *list = new_client;
}


/* Supprimer un client */
void remove_client(struct client **list,
                   int fd)
{
    struct client *current = *list;
    struct client *previous = NULL;

    while (current != NULL) {

        if (current->fd == fd) {

            if (previous == NULL)
                *list = current->next;
            else
                previous->next = current->next;

            close(current->fd);
            free(current);

            return;
        }

        previous = current;
        current = current->next;
    }
}


/* Chercher un client avec son fd */
struct client *find_by_fd(struct client *list,
                          int fd)
{
    while (list != NULL) {

        if (list->fd == fd)
            return list;

        list = list->next;
    }

    return NULL;
}


/* Chercher un utilisateur avec son pseudo */
struct client *find_by_nick(struct client *list,
                            const char *nickname)
{
    while (list != NULL) {

        if (strcmp(list->nickname, nickname) == 0) {

            return list;
        }

        list = list->next;
    }

    return NULL;
}


/* Fonction pratique pour envoyer une reponse du serveur. */
int server_reply(int fd,
                 enum msg_type type,
                 const char *infos,
                 const char *text)
{
    struct message response;

    int length = 0;

    if (text != NULL)
        length = strlen(text);


    build_msg(&response, type, "Server", infos, length);

    return send_msg(fd, &response, text);
}


/* /nick */
void handle_nickname(struct client *sender,
                     struct client *clients,
                     struct message *msg)
{
    struct client *existing;

    char text[MSG_LEN];


    /* Verifier le format */
    if (!is_valid_nick(msg->infos)) {

        server_reply(sender->fd,
                     NICKNAME_NEW,
                     "",
                     "Invalid nickname: letters and digits only");

        return;
    }


    /* Verifier si quelqu'un d'autre utilise deja ce pseudo */
    existing = find_by_nick(clients,
                            msg->infos);


    if (existing != NULL && existing != sender) {

        snprintf(text,
                 sizeof(text),
                 "Nickname %s is already used",
                 msg->infos);


        server_reply(sender->fd,
                     NICKNAME_NEW,
                     "",
                     text);

        return;
    }


    /* Premier pseudo */
    if (sender->nickname[0] == '\0') {

        snprintf(text,
                 sizeof(text),
                 "Welcome on the chat %s",
                 msg->infos);
    }

    /* Changement de pseudo */
    else {

        snprintf(text,
                 sizeof(text),
                 "You are now known as %s",
                 msg->infos);
    }


    strncpy(sender->nickname,
            msg->infos,
            NICK_LEN - 1);

    sender->nickname[NICK_LEN - 1] = '\0';


    /*
     * infos contient le pseudo accepte.
     * Le client peut donc mettre a jour son pseudo local.
     */
    server_reply(sender->fd,
                 NICKNAME_NEW,
                 sender->nickname,
                 text);
}


/* /who */
void handle_who(struct client *sender,
                struct client *clients)
{
    char text[MSG_LEN];

    int used;


    used = snprintf(text,
                    sizeof(text),
                    "Online users are");


    while (clients != NULL) {

        if (clients->nickname[0] != '\0' &&
            used < (int)sizeof(text)) {

            used += snprintf(text + used,
                             sizeof(text) - used,
                             "\n - %s",
                             clients->nickname);
        }

        clients = clients->next;
    }


    server_reply(sender->fd,
                 NICKNAME_LIST,
                 "",
                 text);
}


/* /whois */
void handle_whois(struct client *sender,
                  struct client *clients,
                  struct message *msg)
{
    struct client *target;

    char text[MSG_LEN];
    char date[64];
    char ip[INET_ADDRSTRLEN];


    target = find_by_nick(clients,
                          msg->infos);


    if (target == NULL) {

        snprintf(text,
                 sizeof(text),
                 "User %s does not exist",
                 msg->infos);


        server_reply(sender->fd,
                     NICKNAME_INFOS,
                     "",
                     text);

        return;
    }


    strftime(date,
             sizeof(date),
             "%Y/%m/%d@%H:%M",
             localtime(&target->connection_time));


    inet_ntop(AF_INET,
              &target->address.sin_addr,
              ip,
              sizeof(ip));


    snprintf(text,
             sizeof(text),
             "%s connected since %s with IP address %s and port number %d",
             target->nickname,
             date,
             ip,
             ntohs(target->address.sin_port));


    server_reply(sender->fd,
                 NICKNAME_INFOS,
                 "",
                 text);
}


/* /msg <pseudo> <message> */
void handle_unicast(struct client *sender,
                    struct client *clients,
                    struct message *msg,
                    char *payload)
{
    struct client *target;
    struct message forward;
    char text[MSG_LEN];


    target = find_by_nick(clients, msg->infos);


    if (target == NULL) {

        snprintf(text,
                 sizeof(text),
                 "User %s does not exist",
                 msg->infos);


        server_reply(sender->fd,
                     UNICAST_SEND,
                     "",
                     text);

        return;
    }


    /*
     * On reconstruit le message.
     * Le serveur utilise le vrai pseudo de l'expediteur.
     */
    build_msg(&forward,
              UNICAST_SEND,
              sender->nickname,
              target->nickname,
              msg->pld_len);


    send_msg(target->fd,
             &forward,
             payload);
}


/* /msgall */
void handle_broadcast(struct client *sender,
                      struct client *clients,
                      struct message *msg,
                      char *payload)
{
    struct message forward;


    build_msg(&forward,
              BROADCAST_SEND,
              sender->nickname,
              "",
              msg->pld_len);


    while (clients != NULL) {

        /*
         * Envoyer seulement aux autres
         * utilisateurs ayant un pseudo.
         */
        if (clients->fd != sender->fd &&
            clients->nickname[0] != '\0') {

            send_msg(clients->fd,
                     &forward,
                     payload);
        }

        clients = clients->next;
    }
}


/*
 * Traiter un message recu d'un client.
 */
int handle_client_message(struct client *sender,
                          struct client *clients)
{
    struct message msg; // struct reçue de client1
    struct message echo; // struct envoyée à client2 (du serv)

    char payload[MSG_LEN];


    if (recv_msg(sender->fd,
                 &msg,
                 payload) < 0) {

        return -1;
    }


    /*
     * /nick est autorise meme si
     * l'utilisateur n'a pas encore de pseudo.
     */
    if (msg.type == NICKNAME_NEW) {

        handle_nickname(sender,
                        clients,
                        &msg);

        return 0;
    }


    /*
     * Toutes les autres commandes demandent
     * d'avoir choisi un pseudo.
     */
    if (sender->nickname[0] == '\0') {

        server_reply(sender->fd,
                     ECHO_SEND,
                     "",
                     "Choose a nickname first: /nick <nickname>");

        return 0;
    }


    switch (msg.type) {

        case NICKNAME_LIST:

            handle_who(sender,
                       clients);

            break;


        case NICKNAME_INFOS:

            handle_whois(sender,
                         clients,
                         &msg);

            break;


        case UNICAST_SEND:

            handle_unicast(sender,
                           clients,
                           &msg,
                           payload);

            break;


        case BROADCAST_SEND:

            handle_broadcast(sender,
                             clients,
                             &msg,
                             payload);

            break;


        case ECHO_SEND:

            build_msg(&echo,
                      ECHO_SEND,
                      sender->nickname,
                      "",
                      msg.pld_len);

            send_msg(sender->fd,
                     &echo,
                     payload);

            break;
        
        case FILE_REQUEST:

            handle_file_request(sender, clients, &msg, payload);

            break;

        default:

            server_reply(sender->fd,
                         ECHO_SEND,
                         "",
                         "Unsupported message type");

            break;
    }


    return 0;
}

void handle_file_request(struct client *sender,
                         struct client *clients,
                         struct message *msg,
                         char *payload)
    {
        struct client *target;
        struct message forward;
        char text[MSG_LEN];

        target = find_by_nick(clients, msg->infos);

        if (target == NULL) {

            snprintf(text, sizeof(text),
                    "User %s does not exist", msg->infos);

            server_reply(sender->fd, FILE_REQUEST, "", text);
            return;
        }

        // le recepteur doit savoir a qui repondre
        build_msg(&forward,
                FILE_REQUEST,
                sender->nickname,
                sender->nickname,
                msg->pld_len);

        send_msg(target->fd, &forward, payload);
    }


/* Creation de la socket serveur */
int handle_bind(char *port)
{
    struct addrinfo hints;
    struct addrinfo *result;
    struct addrinfo *rp;

    int sfd;

    int yes = 1;


    memset(&hints,
           0,
           sizeof(struct addrinfo));


    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;


    if (getaddrinfo(NULL,
                    port,
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


        /*
         * Permet de reutiliser rapidement le port
         * apres l'arret du serveur.
         */
        setsockopt(sfd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &yes,
                   sizeof(yes));


        if (bind(sfd,
                 rp->ai_addr,
                 rp->ai_addrlen) == 0) {

            break;
        }


        close(sfd);
    }


    if (rp == NULL) {

        fprintf(stderr,
                "Could not bind\n");

        freeaddrinfo(result);

        exit(EXIT_FAILURE);
    }


    freeaddrinfo(result);

    return sfd;
}


int main(int argc, char *argv[])
{
    int server_fd;
    int client_fd;

    int i;


    struct sockaddr_in client_address;

    socklen_t client_length;


    struct pollfd fds[MAX_CLIENTS];


    struct client *clients = NULL;


    if (argc != 2) {

        fprintf(stderr,
                "Usage: %s <server_port>\n",
                argv[0]);

        return EXIT_FAILURE;
    }


    server_fd = handle_bind(argv[1]);


    if (listen(server_fd,
               SOMAXCONN) < 0) {

        perror("listen");

        return EXIT_FAILURE;
    }


    /*
     * Initialisation du tableau poll.
     */
    for (i = 0; i < MAX_CLIENTS; i++) {

        fds[i].fd = -1;
        fds[i].events = POLLIN;
    }


    /*
     * fds[0] est reserve au serveur.
     */
    fds[0].fd = server_fd;


    printf("Server listening on port %s\n",
           argv[1]);


    while (1) {

        if (poll(fds,
                 MAX_CLIENTS,
                 -1) < 0) {

            perror("poll");

            break;
        }


        /*
         * Nouvelle connexion.
         */
        if (fds[0].revents & POLLIN) {

            client_length =
                sizeof(client_address);


            client_fd =
                accept(server_fd,
                       (struct sockaddr *)&client_address,
                       &client_length);


            if (client_fd >= 0) {

                add_client(&clients,
                           client_fd,
                           client_address);


                printf("New client connected: %s:%d\n",
                       inet_ntoa(client_address.sin_addr),
                       ntohs(client_address.sin_port));


                /*
                 * Chercher une case libre
                 * dans le tableau poll.
                 */
                for (i = 1;
                     i < MAX_CLIENTS;
                     i++) {

                    if (fds[i].fd == -1) {

                        fds[i].fd = client_fd;

                        break;
                    }
                }


                /*
                 * Plus de place.
                 */
                if (i == MAX_CLIENTS) {

                    printf("Too many clients\n");

                    remove_client(&clients,
                                  client_fd);
                }
            }
        }


        /*
         * Regarder quels clients ont envoye
         * quelque chose.
         */
        for (i = 1;
             i < MAX_CLIENTS;
             i++) {

            if (fds[i].fd == -1)
                continue;


            if (fds[i].revents & POLLIN) {

                struct client *sender;

                sender =
                    find_by_fd(clients,
                               fds[i].fd);


                if (sender == NULL)
                    continue;


                if (handle_client_message(sender,
                                          clients) < 0) {

                    printf("Client disconnected\n");


                    remove_client(&clients,
                                  fds[i].fd);


                    fds[i].fd = -1;
                }
            }
        }
    }


    close(server_fd);

    return EXIT_SUCCESS;
}