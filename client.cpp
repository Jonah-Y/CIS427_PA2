#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <iostream>

#define SERVER_PORT 5432
#define MAX_LINE 256

using namespace std;

int main(int argc, char * argv[]) {
    struct hostent *hp;
    struct sockaddr_in sin;
    char *host;
    char buf[MAX_LINE];
    char response[MAX_LINE * 10];
    int s;
    int len;
   
    if (argc == 2) {
        host = argv[1];
    } else {
        fprintf(stderr, "usage: %s <hostname>\n", argv[0]);
        exit(1);
    }
   
    /* translate host name into IP address */
    hp = gethostbyname(host);
    if (!hp) {
        fprintf(stderr, "client: unknown host: %s\n", host);
        exit(1);
    }
   
    /* build address structure */
    bzero((char *)&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    bcopy(hp->h_addr, (char *)&sin.sin_addr, hp->h_length);
    sin.sin_port = htons(SERVER_PORT);
   
    /* create socket */
    if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        perror("client: socket");
        exit(1);
    }
   
    /* connect to server */
    if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("client: connect");
        close(s);
        exit(1);
    }
   
    cout << "Connected to server" << endl;
    cout << "available commands: BUY, SELL, LIST, BALANCE, QUIT, SHUTDOWN" << endl;
    cout << "------------------------------------------------------------" << endl;
   
    /* main communication loop */
    while (fgets(buf, sizeof(buf), stdin)) {
        buf[MAX_LINE-1] = '\0';
        len = strlen(buf);
       
        if (len <= 1) {
            continue;
        }
       
        /* send command */
        send(s, buf, len, 0);
        
     bool is_quit = (strncmp(buf, "QUIT", 4) == 0)
                    || (strncmp(buf, "SHUTDOWN" , 8) == 0);

        /* receive response */
        memset(response, 0, sizeof(response));
        int bytes_received = recv(s, response, sizeof(response) - 1, 0);
       
        if (bytes_received <= 0
            || strncmp(buf, "503", 3) == 0) { //TODO is this the best way to handle if the server capacity is full?
            cout << "Server closed connection" << endl;
            break;
        }
       
        fprintf(stdout, "%s", response);
       
        if (is_quit) {
            cout << "Exiting client..." << endl;
            break;
        }
    }
   
    close(s);
    return 0;
}
