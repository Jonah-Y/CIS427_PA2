#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include "sqlite3.h"
#include <strings.h>       
#include <unordered_set>
#include <string>
#include <iostream>
#include <stdio.h>
#include <sys/types.h>     
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>        
#include "utils.hpp"

using namespace std;

#define SERVER_PORT 5432
#define MAX_PENDING 5
#define MAX_LINE    256

// list of valid commands
unordered_set<string> valid_commands = {
    "BUY",
    "SELL",
    "BALANCE",
    "LIST",
    "SHUTDOWN",
    "QUIT"
};


int main(int argc, char* argv[]) {
    sqlite3 *db;
    char *zErrMsg = 0;
    int rc;
    const char *sql;

    struct sockaddr_in sin;
    char buf[MAX_LINE];
    int buf_len;
    socklen_t addr_len;
    int s, new_s;          

    // Open the database and check for errors
    rc = sqlite3_open("users_and_stocks.db", &db);
    if( rc ) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return(0);
    } else {
        fprintf(stderr, "Opened database successfully\n");
    }


    // create the users table if needed and add a default user if empty
    create_users(db);

    // create the stocks table if needed and add samole entries if empty
    create_stocks(db);


    /* build address data structure */
    bzero((char *)&sin, sizeof(sin));  
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(SERVER_PORT);


    /* setup passive open */
    if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0) {  
        perror("socket error");
        exit(1);
    }
    if ((bind(s, (struct sockaddr *)&sin, sizeof(sin))) < 0) {  
        perror("bind error");
        exit(1);
    }
    listen(s, MAX_PENDING);


    /* wait for connection, then receive and print text */
    while(1) {
        printf("Waiting on connection\n");
        if ((new_s = accept(s, (struct sockaddr *)&sin, &addr_len)) < 0) {  
            perror("accept error");
            exit(1);
        }
        printf("Connected\n");
        while ((buf_len = recv(new_s, buf, sizeof(buf), 0))) {
            buf[buf_len-1] = '\0';
            printf("Recieved: %s\n", buf);
            fflush(stdout);
            
            string request(buf);
            
            if (request.find("BUY", 0) == 0) {
                buy_command(new_s, buf, db);
            } else if (request.find("SELL", 0) == 0) {
                sell_command(new_s, buf, db);
            } else if (request == "LIST") {
                list_command(new_s, buf, db);
            } else if (request == "BALANCE") {
                balance_command(new_s, buf, db);
            } else if (request == "SHUTDOWN") {
                int result = shutdown_command(new_s, buf, db);
                if (result == -99) {
                    close(new_s);
                    close(s);
                    exit(0);
                }
            } else if (request == "QUIT") {
                quit_command(new_s, buf, db);
                break;
            } else {
                fprintf(stderr, "Invalid message request: %s\n", request.c_str());
                const char* error_code = "400 invalid command\nPlease use BUY, SELL, LIST, BALANCE, SHUTDOWN, or QUIT commands\n";
                send(new_s, error_code, strlen(error_code), 0);
            }
        }
        close(new_s);      
    }


    sqlite3_close(db);
    return 0;
}