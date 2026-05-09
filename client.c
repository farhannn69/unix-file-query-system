#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_PORT 50000 // server portnum

int main(int argc, char *argv[]) {
    int client_sd;
    struct sockaddr_in servAdd;
    char cmd[1024];
    static char resp[1048576]; // buff to hold response
    int bytes_read; // num bytes read from server
    const char *dest_ip; // w26server ip add 
    int dest_port = SERVER_PORT; // always connect w26server

    if (argc != 2) { // only arg is the server ip
        fprintf(stderr, "Usage: %s <IP>\n", argv[0]);
        exit(1);
    }
    dest_ip = argv[1];
    printf("Connecting to w26server at %s...\n", dest_ip);

    if ((client_sd = socket(AF_INET, SOCK_STREAM, 0)) < 0) { // create sockett
        fprintf(stderr, "Can't create socket.\n");
        exit(1);
    }

    // fill server addr
    memset(&servAdd, 0, sizeof(servAdd));
    servAdd.sin_family = AF_INET;
    servAdd.sin_port = htons((uint16_t)dest_port);

    if (inet_pton(AF_INET, dest_ip, &servAdd.sin_addr) <= 0) { //convert and validate ip
        fprintf(stderr, "Invalid IP: %s\n", dest_ip);
        close(client_sd);
        exit(1); //
    }

    if (connect(client_sd, (struct sockaddr *)&servAdd, sizeof(servAdd)) < 0) { // connect to server
        fprintf(stderr, "Can't connect to server at %s:%d.\n", dest_ip, dest_port);
        close(client_sd);
        exit(1);
    }

    printf("Connected.\n");

    while (1) {
        printf("w26client$ "); // always running for user
        fflush(stdout);

        if (fgets(cmd, sizeof(cmd), stdin) == NULL) {
            break; // quit on EOF or error on stdin
        }

        cmd[strcspn(cmd, "\n")] = '\0'; // replace newline with null

        if (strlen(cmd) == 0) { // skip empty input
            continue;
        }

        if (strcmp(cmd, "quitc") == 0) { // checkfor quitc
            write(client_sd, cmd, strlen(cmd));
            printf("Exiting on quitc.\n");
            break;
        }

        if (strncmp(cmd, "dirlist", 7) == 0) { // checkfor dirlist -a or -t
            if (strcmp(cmd, "dirlist -a") != 0 && strcmp(cmd, "dirlist -t") != 0) {
                printf("Invalid syntax. Use: dirlist -a/dirlist -t\n");
                continue; // if wrong, ask again
            }
        }

        if (strncmp(cmd, "fn ", 3) == 0) { // checkfor fn 
            char *arg = cmd + 3; // ptr to everything aftr fn
            if (strlen(arg) == 0 || strchr(arg, ' ') != NULL) { // fname empty, has space
                printf("Invalid syntax. Use: fn <filename>\n");
                continue; // if wrong, ask again
            }
        } else if (strcmp(cmd, "fn") == 0) {
            printf("Invalid syntax. Use: fn <filename>\n"); // just typed fn, no arg
            continue; // if wrong, ask again
        }

        if (strncmp(cmd, "fz ", 3) == 0) { // checkfor fz
            long sz1, sz2;
            char remaining[100];
            int parsed = sscanf(cmd + 3, "%ld %ld %99s", &sz1, &sz2, remaining); // if a 3rd token exists, cmnd has too many args
            if (parsed != 2 || sz1 < 0 || sz2 < 0 || sz1 > sz2) {
                printf("Invalid syntax. Use: fz <size1> <size2> (size1 <= size2)\n");
                continue;
            }
        } else if (strcmp(cmd, "fz") == 0) {
            printf("Invalid syntax. Use: fz <size1> <size2>\n"); // just typed fz, no args
            continue; // ask again if worng
        }

        if (strncmp(cmd, "ft ", 3) == 0) { //checkfor ft
            char arg_copy[256];
            strncpy(arg_copy, cmd + 3, sizeof(arg_copy) - 1);
            arg_copy[sizeof(arg_copy) - 1] = '\0'; // store a copy of the rest

            // tokenize to count and validate each ext
            char *ext_tokens[4]; // check up to 4 to detect extra
            int ext_n = 0;
            char *tok = strtok(arg_copy, " "); // find first one
            while (tok != NULL && ext_n < 4) {
                ext_tokens[ext_n++] = tok;
                tok = strtok(NULL, " "); // start where it ended
            }

            int ft_bad = 0;
            if (ext_n == 0 || ext_n > 3) {
                ft_bad = 1; // need at least 1, maxx 3
            } else {
                int i;
                for (i = 0; i < ext_n && !ft_bad; i++) {
                    if (strlen(ext_tokens[i]) < 1) { // each token must be a nonempty string
                        ft_bad = 1;
                    }
                }
            }
            if (ft_bad) {
                printf("Invalid syntax. Use: ft <ext1> <ext2> <ext3>\n");
                continue;
            }
        } else if (strcmp(cmd, "ft") == 0) { // only typed ft, no args
            printf("Invalid syntax. Use: ft <ext1> <ext2> <ext3>\n"); // just typed ft, no args
            continue;
        }

        if (strncmp(cmd, "fdb ", 4) == 0) { // check fdb cmnd 
            const char *dte = cmd + 4; // extract date part
            int date_ok = 1;
            // exactly 10 chars fr YYYY-MM-DD
            if (strlen(dte) != 10) {
                date_ok = 0;
            } else {
                int i;
                for (i = 0; i < 10 && date_ok; i++) {
                    if (i == 4 || i == 7) { // these positions must be hyphen
                        if (dte[i] != '-') date_ok = 0;
                    } else {
                        if (dte[i] < '0' || dte[i] > '9') date_ok = 0; // should only be digits
                    }
                }
            }
            if (!date_ok) {
                printf("Invalid syntax. Use: fdb YYYY-MM-DD\n"); // ask again if wrong
                continue;
            }
        } else if (strcmp(cmd, "fdb") == 0) {
            printf("Invalid syntax. Use: fdb YYYY-MM-DD\n"); // just typed fdb, no date arg
            continue;
        }

        if (strncmp(cmd, "fda ", 4) == 0) { // check fda cmnd
            const char *dte = cmd + 4; // extract date part
            int date_ok = 1;
            // exactly 10 chars fr YYYY-MM-DD
            if (strlen(dte) != 10) {
                date_ok = 0;
            } else {
                int i;
                for (i = 0; i < 10 && date_ok; i++) {
                    if (i == 4 || i == 7) { // these positions must be hyphen
                        if (dte[i] != '-') date_ok = 0;
                    } else {
                        if (dte[i] < '0' || dte[i] > '9') date_ok = 0; // should only be digits
                    }
                }
            }
            if (!date_ok) {
                printf("Invalid syntax. Use: fda YYYY-MM-DD\n"); // ask again if wrong
                continue;
            }
        } else if (strcmp(cmd, "fda") == 0) {
            printf("Invalid syntax. Use: fda YYYY-MM-DD\n"); // just typed fda, no date arg
            continue;
        }

        write(client_sd, cmd, strlen(cmd)); // send checked cmnd to the server

        // rcv and print server response — loop until null terminator received
        memset(resp, 0, sizeof(resp));
        int total = 0;
        int disc = 0;
        while (total < (int)sizeof(resp) - 1) {
            bytes_read = read(client_sd, resp + total, sizeof(resp) - 1 - total);
            if (bytes_read <= 0) {
                printf("Server disconnected.\n");
                disc = 1;
                break;
            }
            total += bytes_read;
            if (memchr(resp + total - bytes_read, '\0', bytes_read) != NULL)
                break; // null terminator received, full response is here
        }
        if (disc) break;
        if (total > 0 && resp[total - 1] == '\0') total--; // strip trailing \0
        resp[total] = '\0';
        printf("%s\n", resp); // prints response
    }
    close(client_sd); // closed client conn 
    return 0;
}
