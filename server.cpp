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
#include <pthread.h>


using namespace std;

#define SERVER_PORT 5432
#define MAX_PENDING 5
#define MAX_LINE    256
#define MAX_CONNECTIONS 10  //TODO make sure everything works right with server at capacity
#define THREAD_COUNT 10

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
} Pool;

struct handle_client_args {
    int fd;
    sqlite3* db;
};

void add_client(int new_s, Pool *pool);
void check_clients(Pool *pool, sqlite3 *db, pthread_t* thread_handles, int listening_s);
void* handle_client(void* args);


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

    pthread_t* thread_handles;
    thread_handles = (pthread_t*)malloc(THREAD_COUNT * sizeof(pthread_t));

    /* wait for connection, then receive and print text */
    printf("Waiting on connection\n");
    while(1) {
        pool.ready_set = pool.read_set;     //save the current state
        pool.nready = select(pool.maxfd+1, &pool.ready_set, &pool.write_set, NULL, NULL);

        if(FD_ISSET(s, &pool.ready_set)) {  // Check if there is an incoming conn
		    new_s = accept(s, (struct sockaddr *) &sin, &addr_len);
		    if (new_s >= 0) {
                printf("Connected\n");
                add_client(new_s, &pool);
            }
	    }
	    check_clients(&pool, db, thread_handles, s);  // check if any data needs to be sent/received from clients
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
    // TODO: test to make sure this all works
    const char* response = "503 Service Unavailable because the capacity is full";
    send(new_s, response, strlen(response), 0);
    close(new_s);
}

void check_clients(Pool *pool, sqlite3 *db, pthread_t* thread_handles, int listening_s) {
    for (int i = 0; i <= pool->maxi; i++) {
        int fd = pool->clientfd[i];

        if (fd >= 0 && FD_ISSET(fd, &pool->ready_set)) {

            // TODO fix this it is a little funky
            // Remove the client from the pool and ready set before handing off to a thread,
            // so that file descriptors are not handled multiple times concurrently.
            pool->clientfd[i] = -1;
            FD_CLR(fd, &pool->read_set);
            while (pool->maxi >= 0 && pool->clientfd[pool->maxi] < 0)
                pool->maxi--;
            if (fd == pool->maxfd) {
                pool->maxfd = listening_s;
                for (int j = 0; j <= pool->maxi; j++)
                    if (pool->clientfd[j] >= 0 && pool->clientfd[j] > pool->maxfd)
                        pool->maxfd = pool->clientfd[j];
            }

            // create a new thread for this connection
            handle_client_args* args = new handle_client_args{fd, db};
            pthread_create(&thread_handles[i], NULL, handle_client, (void*)args);
        }
    }
}


void* handle_client(void* args) {
    handle_client_args* data = static_cast<handle_client_args*>(args);
    int new_s = data->fd;
    sqlite3* db = data->db;
    delete data;

    char buf[MAX_LINE];
    int buf_len;

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
                //close(s);     TODO this needs to be thought out and fixed
                //exit(0);
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
    return NULL;
}