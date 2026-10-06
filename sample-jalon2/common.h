#ifndef COMMON_H
#define COMMON_H

#include <ctype.h>
#include <string.h>
#include <sys/socket.h>

#include "msg_struct.h"

#define MSG_LEN 1024


/* Envoie exactement length octets */
static int send_all(int sockfd, const void *buffer, int length)
{
    int total = 0;
    int n;

    while (total < length) {

        n = send(sockfd,
                 (char *)buffer + total,
                 length - total,
                 0);

        if (n <= 0)
            return -1;

        total += n;
    }

    return 0;
}


/* Recoit exactement length octets */
static int recv_all(int sockfd, void *buffer, int length)
{
    int total = 0;
    int n;

    while (total < length) {

        n = recv(sockfd,
                 (char *)buffer + total,
                 length - total,
                 0);

        if (n <= 0)
            return -1;

        total += n;
    }

    return 0;
}


/* Construit une struct message propre */
static void build_msg(struct message *msg,
                      enum msg_type type,
                      const char *nick,
                      const char *infos,
                      int pld_len)
{
    memset(msg, 0, sizeof(struct message)); 

    msg->type = type;
    msg->pld_len = pld_len;

    if (nick != NULL)
        strncpy(msg->nick_sender, nick, NICK_LEN - 1);

    if (infos != NULL)
        strncpy(msg->infos, infos, INFOS_LEN - 1);
}


/* Envoie d'abord la structure, puis le payload */
static int send_msg(int sockfd,
                    struct message *msg,
                    const char *payload)
{
    if (send_all(sockfd, msg, sizeof(struct message)) < 0)
        return -1;

    if (msg->pld_len > 0 && payload != NULL) {

        if (send_all(sockfd, payload, msg->pld_len) < 0)
            return -1;
    }

    return 0;
}


/* Recoit d'abord la structure, puis le payload */
static int recv_msg(int sockfd,
                    struct message *msg,
                    char *payload)
{
    if (recv_all(sockfd, msg, sizeof(struct message)) < 0)
        return -1;

    if (msg->pld_len < 0 || msg->pld_len >= MSG_LEN)
        return -1;

    if (msg->pld_len > 0) {

        if (recv_all(sockfd, payload, msg->pld_len) < 0)
            return -1;
    }

    payload[msg->pld_len] = '\0';

    return 0;
}


/* Un pseudo doit contenir uniquement lettres et chiffres */
static int is_valid_nick(const char *nick)
{
    int i;

    if (nick[0] == '\0' || strlen(nick) >= NICK_LEN)
        return 0;

    for (i = 0; nick[i] != '\0'; i++) {

        if (!isalnum((unsigned char)nick[i]))
            return 0;
    }

    return 1;
}

#endif