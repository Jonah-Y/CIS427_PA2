#ifndef UTILS_HPP
#define UTILS_HPP
#include "sqlite3.h"

/** Callback function for the sqlite database.
 *  Prints each record processed in each SELECT statement executed within the SQL argument.
 */
int callback(void *data, int argc, char **argv, char **azColName);

/** Used to count the number of entries in a table.
 *  Meant to be passed to sqlite3_exec with an sql query of SELECT COUNT(*) FROM table;
 */
int count_rows(void *count, int argc, char **argv, char **azColName);

/** This creates the Users table if it doesn't exist and adds a default user
 *  if there are no users in the table.
 */
void create_users(sqlite3* db);

/** This creates the Stocks table if it doesn't exist and inserts the Mag 7 stocks
 *  for the default user if the table is empty.
 */
void create_stocks(sqlite3* db);

/** Buys an amount of stocks and responds to the client with the new balance.
 *  Creates or updates a record in the Stocks table if one does not exist.
 */
int buy_command(int socket, char* request, sqlite3* db);

/** Sells an amount of stock and responds to the client with the new balance. */
int sell_command(int socket, char* request, sqlite3* db);

/** Shuts down the server and client. */
int shutdown_command(int socket, char* request, sqlite3* db) ;

/** Lists every stock in the database. */
int list_command(int socket, char* request, sqlite3* db);

/** Displays the balance for user 1 */
int balance_command(int socket, char* request, sqlite3* db);

/** Terminates the client */
int quit_command(int socket, char* request, sqlite3* db);

#endif
