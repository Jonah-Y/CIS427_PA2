#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include "sqlite3.h"
#include <strings.h>       
#include <sys/select.h>
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
#include <fcntl.h>


using namespace std;

#define SERVER_PORT 5432
#define MAX_PENDING 5
#define MAX_LINE    256
#define MAX_CONNECTIONS 10

// list of valid commands
unordered_set<string> valid_commands = {
    "BUY",
    "SELL",
    "BALANCE",
    "LIST",
    "SHUTDOWN",
    "QUIT"
};

typedef struct { /* represents a pool of connected descriptors */ 
    int maxfd;        /* largest descriptor in read_set */    
    fd_set read_set;  /* set of all active read descriptors */
    fd_set write_set;  /* set of all active read descriptors */  
    fd_set ready_set; /* subset of descriptors ready for reading  */ 
    int nready;       /* number of ready descriptors from select */    
    int maxi;         /* highwater index into client array */ 
    int clientfd[FD_SETSIZE];    /* set of active descriptors */ 
 	// ADD WHAT WOULD BE HELPFUL FOR PROJECT1
} Pool;

void add_client(int new_s, Pool *pool);
void check_clients(Pool *p, sqlite3 * db);


int main(int argc, char* argv[]) {
    sqlite3 *db;
    char *zErrMsg = 0;
    int rc;
    const char *sql;

    struct sockaddr_in sin;
    char buf[MAX_LINE];
    int buf_len;
    socklen_t addr_len;
    int s, new_s;   // sockets
    int opts = 1; 

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

    /* set the options to allow address reuse and be non blocking */
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opts, sizeof(opts));
    if((opts = fcntl(s, F_GETFL)) < 0) {
        printf("Error getting socket options");
    }
    opts = (opts | O_NONBLOCK);
    if(fcntl(s, F_SETFL, opts) < 0) {
        printf("Error adding O_NONBLOCK option.");
    }
    
    /* bind and begin listening */
    if ((bind(s, (struct sockaddr *)&sin, sizeof(sin))) < 0) {  
        perror("bind error");
        exit(1);
    }
    listen(s, MAX_PENDING);

    addr_len = sizeof(sin);

    /* initialize the pool of client socket descriptors */
    Pool pool;
    pool.maxfd = s;
    FD_ZERO(&pool.read_set);
    FD_ZERO(&pool.write_set);
    pool.ready_set = pool.read_set;
    FD_SET(s, &pool.read_set);
    pool.nready = 0;
    pool.maxi = -1;
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        pool.clientfd[i] = -1;
    }

    /* wait for connection, then receive and print text */
    while(1) {
        pool.ready_set = pool.read_set;     //save the current state
        pool.nready = select(pool.maxfd+1, &pool.ready_set, &pool.write_set, NULL, NULL);

        printf("Waiting on connection\n");
        if(FD_ISSET(s, &pool.ready_set)) {  // Check if there is an incoming conn
		    new_s = accept(s, (struct sockaddr *) &sin, &addr_len); // accept it
		    add_client(new_s, &pool);	// add the client by the incoming socket fd
	    }
	
	    check_clients(&pool, db);  // check if any data needs to be sent/received from clients
        //TODO add all the code below to the check_clients function with threads for each client

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

void add_client(int new_s, Pool *pool) {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (pool->clientfd[i] == -1) {
            pool->clientfd[i] = new_s;
            FD_SET(new_s, &pool->read_set);
            if (new_s > pool->maxfd)
                pool->maxfd = new_s;
            if (i > pool->maxi)
                pool->maxi = i;
            return;
        }
    }
    //the connection pool is full
    close(new_s);
}

void check_clients(Pool *p, sqlite3 *db) {
    // TODO: loop over client fds; for each i with p->clientfd[i] >= 0, if FD_ISSET(p->clientfd[i], &p->ready_set)
    // then recv(), parse request, call buy_command/sell_command/etc., handle QUIT (remove from pool)

}