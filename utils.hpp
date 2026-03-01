#ifndef UTILS_HPP
#define UTILS_HPP

#include "sqlite3.h"
#include <string>

/** Callback function for the sqlite database.
 *  Prints each record processed in each SELECT statement executed within the SQL argument.
 */
int callback(void *data, int argc, char **argv, char **azColName);

/** Used to count the number of entries in a table.
 *  Meant to be passed to sqlite3_exec with an sql query of SELECT COUNT(*) FROM table;
 */
int count_rows(void *count, int argc, char **argv, char **azColName);

/** Creates the Users table if it doesn't exist and seeds the four required PA2 accounts
 *  (Root, Mary, John, Moe) if the table is empty.
 */
void create_users(sqlite3* db);

/** Creates the Stocks table if it doesn't exist and inserts the Mag 7 stocks
 *  for the Root user if the table is empty.
 */
void create_stocks(sqlite3* db);

/** PA2: Verifies username and password against the Users table.
 *  Username match is case-insensitive, password match is case-sensitive.
 *  Returns true on valid credentials.
 */
bool verify_login(sqlite3* db, const std::string& username, const std::string& password);

/** Buys an amount of stocks and responds to the client with the new balance.
 *  Creates or updates a record in the Stocks table if one does not exist.
 */
int buy_command(int socket, char* request, sqlite3* db);

/** Sells an amount of stock and responds to the client with the new balance. */
int sell_command(int socket, char* request, sqlite3* db);

/** PA2 update: non-root users see only their own records, root sees all records.
 *  @param username  the logged-in user's username (used as the DB key)
 *  @param is_root   true if the logged-in user is root
 */
int list_command(int socket, char* request, sqlite3* db,
                 const std::string& username, bool is_root);

/** PA2 update: shows balance for the currently logged-in user.
 *  @param username  the logged-in user's username (used as the DB key)
 */
int balance_command(int socket, char* request, sqlite3* db, const std::string& username);

/** PA2 update: only root can shut down the server, non-root gets a 403.
 *  Returns -99 on successful root shutdown so the caller can exit.
 */
int shutdown_command(int socket, char* request, sqlite3* db, bool is_root);

/** Terminates the client connection. */
int quit_command(int socket, char* request, sqlite3* db);

#endif