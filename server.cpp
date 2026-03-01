#include <cstring>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include "sqlite3.h"
#include <strings.h>
#include <sys/select.h>
#include <unordered_set>
#include <map>
#include <string>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include "utils.hpp"

using namespace std;

#define SERVER_PORT    5432
#define MAX_PENDING    5
#define MAX_LINE       256
#define MAX_CONNECTIONS 10
#define THREAD_COUNT   10

// Valid commands for this server
unordered_set<string> valid_commands = {
    "BUY", "SELL", "BALANCE", "LIST", "SHUTDOWN", "QUIT",
    "LOGIN", "LOGOUT"
};

// Mutex to protect SQLite from concurrent access across threads
pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;

// Tracks currently logged-in users for the WHO command: username -> IP address
// Protected by active_users_mutex since multiple threads can login/logout at once
map<string, string> active_users;
pthread_mutex_t active_users_mutex = PTHREAD_MUTEX_INITIALIZER;

// Pool of active client descriptors managed by select()
typedef struct {
    int maxfd;                    // largest descriptor in read_set
    fd_set read_set;              // set of all active read descriptors
    fd_set write_set;             // set of all active write descriptors
    fd_set ready_set;             // subset of descriptors ready for reading
    int nready;                   // number of ready descriptors from select
    int maxi;                     // highwater index into client array
    int clientfd[FD_SETSIZE];     // set of active descriptors
    char client_ip[FD_SETSIZE][INET_ADDRSTRLEN];  // IP address for each slot
} Pool;

// Arguments passed to each client thread
struct handle_client_args {
    int fd;
    sqlite3* db;
    char ip[INET_ADDRSTRLEN];   // client IP address, used for WHO command
};

void add_client(int new_s, const char* ip, Pool *pool);
void check_clients(Pool *pool, sqlite3 *db, pthread_t* thread_handles, int listening_s);
void* handle_client(void* args);


int main(int argc, char* argv[]) {
    sqlite3 *db;
    char *zErrMsg = 0;
    int rc;

    struct sockaddr_in sin;
    int buf_len;
    socklen_t addr_len;
    int s, new_s;
    int opts = 1;

    // Open the database and check for errors
    rc = sqlite3_open("users_and_stocks.db", &db);
    if (rc) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return 0;
    } else {
        fprintf(stderr, "Opened database successfully\n");
    }

    // Create tables and seed the four required user accounts
    create_users(db);
    create_stocks(db);

    // Build address data structure
    bzero((char *)&sin, sizeof(sin));
    sin.sin_family      = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port        = htons(SERVER_PORT);

    // Setup passive open
    if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket error");
        exit(1);
    }

    // Set the options to allow address reuse and be non-blocking
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opts, sizeof(opts));
    if ((opts = fcntl(s, F_GETFL)) < 0) {
        printf("Error getting socket options");
    }
    opts = (opts | O_NONBLOCK);
    if (fcntl(s, F_SETFL, opts) < 0) {
        printf("Error adding O_NONBLOCK option.");
    }

    // Bind and begin listening
    if ((bind(s, (struct sockaddr *)&sin, sizeof(sin))) < 0) {
        perror("bind error");
        exit(1);
    }
    listen(s, MAX_PENDING);

    addr_len = sizeof(sin);

    // Initialize the pool of client socket descriptors
    Pool pool;
    pool.maxfd = s;
    FD_ZERO(&pool.read_set);
    FD_ZERO(&pool.write_set);
    pool.ready_set = pool.read_set;
    FD_SET(s, &pool.read_set);
    pool.nready = 0;
    pool.maxi   = -1;
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        pool.clientfd[i] = -1;
    }

    pthread_t* thread_handles = (pthread_t*)malloc(THREAD_COUNT * sizeof(pthread_t));

    fprintf(stdout, "Server listening on port %d\n", SERVER_PORT);
    printf("Waiting on connection\n");

    // Main select loop - waits for new connections or data from existing clients
    while (1) {
        pool.ready_set = pool.read_set;  // save the current state
        pool.nready = select(pool.maxfd + 1, &pool.ready_set, &pool.write_set, NULL, NULL);

        if (FD_ISSET(s, &pool.ready_set)) {  // check if there is an incoming connection
            new_s = accept(s, (struct sockaddr *) &sin, &addr_len);
            if (new_s >= 0) {
                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &sin.sin_addr, client_ip, sizeof(client_ip));
                printf("Connected from %s\n", client_ip);
                add_client(new_s, client_ip, &pool);
            }
        }
        // Check if any data needs to be sent/received from existing clients
        check_clients(&pool, db, thread_handles, s);
    }

    free(thread_handles);
    sqlite3_close(db);
    return 0;
}

/** Adds a new client socket to the pool so select() will watch it. */
void add_client(int new_s, const char* ip, Pool *pool) {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (pool->clientfd[i] == -1) {
            pool->clientfd[i] = new_s;
            strncpy(pool->client_ip[i], ip, INET_ADDRSTRLEN);
            FD_SET(new_s, &pool->read_set);
            if (new_s > pool->maxfd)
                pool->maxfd = new_s;
            if (i > pool->maxi)
                pool->maxi = i;
            return;
        }
    }
    // Server is at capacity - notify client and close
    const char* response = "503 Service Unavailable: server is at capacity\n";
    send(new_s, response, strlen(response), 0);
    close(new_s);
}

/** Iterates over ready client descriptors and spawns a thread for each one. */
void check_clients(Pool *pool, sqlite3 *db, pthread_t* thread_handles, int listening_s) {
    for (int i = 0; i <= pool->maxi; i++) {
        int fd = pool->clientfd[i];

        if (fd >= 0 && FD_ISSET(fd, &pool->ready_set)) {
            // Remove the client from the pool before handing off to a thread
            // so the same descriptor is not handled concurrently
            char ip[INET_ADDRSTRLEN];
            strncpy(ip, pool->client_ip[i], INET_ADDRSTRLEN);
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

            // Create a new thread to handle this client connection
            handle_client_args* args = new handle_client_args();
            args->fd = fd;
            args->db = db;
            strncpy(args->ip, ip, INET_ADDRSTRLEN);
            pthread_create(&thread_handles[i], NULL, handle_client, (void*)args);
        }
    }
}

/** Thread function: handles the full lifecycle of one client connection.
 *  Each thread gets its own session state (logged_in, session_username).
 */
void* handle_client(void* args) {
    handle_client_args* data = static_cast<handle_client_args*>(args);
    int new_s   = data->fd;
    sqlite3* db = data->db;
    char client_ip[INET_ADDRSTRLEN];
    strncpy(client_ip, data->ip, INET_ADDRSTRLEN);
    delete data;

    char buf[MAX_LINE];
    int  buf_len;

    // Session state - unique to this thread/connection, starts unauthenticated
    bool   logged_in        = false;
    string session_username = "";

    while ((buf_len = recv(new_s, buf, sizeof(buf) - 1, 0)) > 0) {

        // Null-terminate and strip trailing newline
        buf[buf_len] = '\0';
        if (buf_len > 0 && (buf[buf_len-1] == '\n' || buf[buf_len-1] == '\r'))
            buf[buf_len-1] = '\0';

        printf("Received: %s\n", buf);
        fflush(stdout);

        string request(buf);

        // QUIT is always allowed even without login
        if (request == "QUIT") {
            quit_command(new_s, buf, db);
            printf("Client sent QUIT. Closing connection.\n");
            break;
        }

        // LOGIN - parse credentials and verify against the database
        if (request.find("LOGIN ", 0) == 0) {
            if (logged_in) {
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "400 Already logged in as %s\n", session_username.c_str());
                send(new_s, msg, strlen(msg), 0);
                continue;
            }

            string cmd, username, password;
            istringstream iss(request);
            iss >> cmd >> username >> password;

            if (username.empty() || password.empty()) {
                const char* fmt_err =
                    "400 message format error\n"
                    "Correct format: LOGIN <UserID> <Password>\n";
                send(new_s, fmt_err, strlen(fmt_err), 0);
                continue;
            }

            pthread_mutex_lock(&db_mutex);
            bool ok = verify_login(db, username, password);
            pthread_mutex_unlock(&db_mutex);

            if (ok) {
                logged_in        = true;
                session_username = username;
                // add to active users map so WHO can see this session
                pthread_mutex_lock(&active_users_mutex);
                active_users[session_username] = client_ip;
                pthread_mutex_unlock(&active_users_mutex);
                printf("[LOGIN] '%s' logged in successfully.\n", session_username.c_str());
                const char* okMsg = "200 OK\n";
                send(new_s, okMsg, strlen(okMsg), 0);
            } else {
                const char* fail = "403 Wrong UserID or Password\n";
                printf("[LOGIN] Failed attempt for user '%s'.\n", username.c_str());
                send(new_s, fail, strlen(fail), 0);
            }
            continue;
        }

        // LOGOUT - clear session and close this connection
        if (request == "LOGOUT") {
            if (!logged_in) {
                const char* err = "400 Not logged in\n";
                send(new_s, err, strlen(err), 0);
                continue;
            }
            // remove from active users map
            pthread_mutex_lock(&active_users_mutex);
            active_users.erase(session_username);
            pthread_mutex_unlock(&active_users_mutex);
            printf("[LOGOUT] '%s' logged out.\n", session_username.c_str());
            logged_in        = false;
            session_username = "";
            const char* okMsg = "200 OK\n";
            send(new_s, okMsg, strlen(okMsg), 0);
            printf("Connection closed after LOGOUT.\n");
            break;
        }

        // All other commands require the user to be logged in
        if (!logged_in) {
            const char* warning = "Please login first\n";
            fprintf(stderr, "[AUTH] Blocked command - not logged in.\n");
            send(new_s, warning, strlen(warning), 0);
            continue;
        }

        // Check root status once - used by LIST, SHUTDOWN, and WHO
        bool is_root = (strcasecmp(session_username.c_str(), "root") == 0);

        // WHO - root only, no DB access needed, just reads the active_users map
        if (request == "WHO") {
            if (!is_root) {
                const char* denied = "403 Forbidden: only root can use WHO\n";
                send(new_s, denied, strlen(denied), 0);
                continue;
            }
            pthread_mutex_lock(&active_users_mutex);
            string response = "200 OK\nThe list of active users:\n";
            for (auto& entry : active_users) {
                response += entry.first + " " + entry.second + "\n";
            }
            pthread_mutex_unlock(&active_users_mutex);
            send(new_s, response.c_str(), response.length(), 0);
            continue;
        }

        // Lock the database mutex for all commands that touch the DB
        pthread_mutex_lock(&db_mutex);

        if (request.find("BUY", 0) == 0) {
            buy_command(new_s, buf, db);

        } else if (request.find("SELL", 0) == 0) {
            sell_command(new_s, buf, db);

        } else if (request == "LIST") {
            list_command(new_s, buf, db, session_username, is_root);

        } else if (request == "BALANCE") {
            balance_command(new_s, buf, db, session_username);

        } else if (request.find("DEPOSIT", 0) == 0) {
            deposit_command(new_s, buf, db, session_username);

        } else if (request.find("LOOKUP", 0) == 0) {
            lookup_command(new_s, buf, db, session_username);

        } else if (request == "SHUTDOWN") {
            int result = shutdown_command(new_s, buf, db, is_root);
            pthread_mutex_unlock(&db_mutex);
            if (result == -99) {
                exit(0);
            }
            continue;  // non-root was refused, keep the connection open

        } else {
            pthread_mutex_unlock(&db_mutex);
            fprintf(stderr, "Invalid message request: %s\n", request.c_str());
            const char* error_code =
                "400 invalid command\n"
                "Valid commands: LOGIN, LOGOUT, BUY, SELL, LIST, BALANCE, "
                "DEPOSIT, LOOKUP, WHO, SHUTDOWN, QUIT\n";
            send(new_s, error_code, strlen(error_code), 0);
            continue;
        }

        pthread_mutex_unlock(&db_mutex);
    }

    // Clean up session if the client disconnected without logging out
    if (logged_in) {
        pthread_mutex_lock(&active_users_mutex);
        active_users.erase(session_username);
        pthread_mutex_unlock(&active_users_mutex);
    }

    close(new_s);
    return NULL;
}