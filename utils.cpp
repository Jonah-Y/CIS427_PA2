#include "utils.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include "sqlite3.h"
#include <string>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

/** Callback function for the sqlite database.
 *  Prints each record processed in each SELECT statement executed within the SQL argument.
 */
int callback(void *data, int argc, char **argv, char **azColName) {
    fprintf(stderr, "%s: ", (const char*)data);
    for (int i = 0; i < argc; i++) {
        printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
    }
    printf("\n");
    return 0;
}

/** Used to count the number of entries in a table.
 *  Meant to be passed to sqlite3_exec with an sql query of SELECT COUNT(*) FROM table;
 */
int count_rows(void *count, int argc, char **argv, char **azColName) {
    if (argc == 1) {
        *static_cast<int*>(count) = atoi(argv[0]);
    } else {
        fprintf(stderr, "Invalid use of count_rows()");
    }
    return 0;
}

/** Creates the Users table if it doesn't exist and seeds the four required PA2 accounts.
 *  Uses TEXT primary key so the username is also the row ID — no integer ID lookup needed.
 *  The four accounts (Root/Mary/John/Moe) are inserted only when the table is empty.
 */
void create_users(sqlite3* db) {
    const char *sql;
    int rc;
    char *zErrMsg = 0;

    // Create the Users table - ID is TEXT so the username serves as the primary key
    sql = "CREATE TABLE IF NOT EXISTS Users ("
          "ID TEXT PRIMARY KEY,"
          "email TEXT NOT NULL,"
          "first_name TEXT,"
          "last_name TEXT,"
          "user_name TEXT NOT NULL,"
          "password TEXT,"
          "usd_balance DOUBLE NOT NULL);";

    rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
    } else {
        fprintf(stdout, "Users table created successfully\n");
    }

    // Check if there are any users and if not seed the four required accounts
    sql = "SELECT COUNT(*) FROM Users;";
    int user_count = 0;
    cout << "Checking user count" << endl;

    rc = sqlite3_exec(db, sql, count_rows, (void*) &user_count, &zErrMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
    } else {
        fprintf(stdout, "Operation done successfully\n");
    }

    if (user_count == 0) {
        fprintf(stdout, "Creating the users because no users currently exist\n");

        // Insert all four required PA2 accounts at once
        sql = "INSERT INTO Users (ID, email, first_name, last_name, user_name, password, usd_balance) VALUES"
              "('Root', 'root@default.com', 'Root', 'Admin', 'Root_User', 'Root01', 100.00),"
              "('Mary', 'mary@default.com', 'Mary', 'Juana', 'Mary_User', 'Mary01', 100.00),"
              "('John', 'john@default.com', 'John', 'Mayer', 'John_User', 'John01', 100.00),"
              "('Moe',  'moe@default.com',  'Moe',  'Lester','Moe_User',  'Moe01',  100.00);";

        rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "SQL error: %s\n", zErrMsg);
            sqlite3_free(zErrMsg);
        } else {
            fprintf(stdout, "Records created successfully\n");
        }
    } else {
        fprintf(stdout, "There are currently %d users in the database\n", user_count);
    }
}

/** Creates the Stocks table if it doesn't exist and inserts the Mag 7 stocks
 *  for the Root user if the table is empty.
 */
void create_stocks(sqlite3* db) {
    const char *sql;
    int rc;
    char *zErrMsg = 0;

    // Create the Stocks table - user_id references the TEXT primary key in Users
    sql = "CREATE TABLE IF NOT EXISTS Stocks ("
          "ID INTEGER PRIMARY KEY AUTOINCREMENT,"
          "stock_symbol VARCHAR(4) NOT NULL,"
          "stock_name VARCHAR(20) NOT NULL,"
          "stock_balance DOUBLE,"
          "user_id TEXT,"
          "FOREIGN KEY (user_id) REFERENCES Users (ID));";

    rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
    } else {
        fprintf(stdout, "Stocks table created successfully\n");
    }

    // Add seed stocks if the table is empty
    sql = "SELECT COUNT(*) FROM Stocks;";
    int stock_count = 0;
    cout << "Checking if Stocks table has entries" << endl;

    rc = sqlite3_exec(db, sql, count_rows, (void*) &stock_count, &zErrMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
    } else {
        fprintf(stdout, "Check operation was successful\n");
    }

    if (stock_count == 0) {
        fprintf(stdout, "Inserting stocks for the Root user because no stocks currently exist\n");

        sql = "INSERT INTO Stocks (stock_symbol, stock_name, stock_balance, user_id) VALUES "
              "('AAPL', 'Apple',     0, 'Root'),"
              "('MSFT', 'Microsoft', 0, 'Root'),"
              "('AMZN', 'Amazon',    0, 'Root'),"
              "('GOOG', 'Alphabet',  0, 'Root'),"
              "('META', 'Meta',      0, 'Root'),"
              "('NVDA', 'Nvidia',    0, 'Root'),"
              "('TSLA', 'Tesla',     0, 'Root');";

        rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "SQL error: %s\n", zErrMsg);
            sqlite3_free(zErrMsg);
        } else {
            fprintf(stdout, "Records created successfully\n");
        }
    } else {
        fprintf(stdout, "There are currently %d stocks in the database\n", stock_count);
    }
}

/** PA2: Callback that captures the first column of the first returned row as a double. */
static int getBalance_callback(void *data, int argc, char **argv, char **azColName) {
    double* balance = static_cast<double*>(data);
    *balance = atof(argv[0]);
    return 0;
}

/** Logs the SQL error, sends error message to the client, and frees zErrMsg. */
static void handle_SQL_error(int socket, char* zErrMsg) {
    char response[256];
    snprintf(response, sizeof(response), "500 SQL error: %s\n", zErrMsg);
    fprintf(stderr, "%sError message sent to client\n", response);
    send(socket, response, strlen(response), 0);
    sqlite3_free(zErrMsg);
}

/** PA2: Verifies username and password against the Users table.
 *  The Users table uses the username as the TEXT primary key (ID column).
 *  LOWER() on ID provides case-insensitive username matching.
 *  Returns true if credentials match, false otherwise.
 */
bool verify_login(sqlite3* db, const string& username, const string& password) {
    char sql[512];
    snprintf(sql, sizeof(sql),
        "SELECT ID FROM Users WHERE LOWER(ID) = LOWER('%s') AND password = '%s';",
        username.c_str(), password.c_str());

    // use a small struct so the callback can signal "found"
    int found = 0;
    char* zErrMsg = nullptr;

    // reuse count_rows callback — it sets found=1 if any row is returned
    auto found_callback = [](void* data, int argc, char** argv, char** cols) -> int {
        if (argc >= 1 && argv[0]) {
            *static_cast<int*>(data) = 1;
        }
        return 0;
    };

    int rc = sqlite3_exec(db, sql, found_callback, &found, &zErrMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[verify_login] SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }
    return found == 1;
}

/** Buys an amount of stocks and responds to the client with the new balance.
 *  Creates or updates a record in the Stocks table if one does not exist.
 *  user_id is a TEXT string matching the Users.ID primary key.
 */
int buy_command(int socket, char* request, sqlite3* db) {
    // parse input string
    char *saveptr = nullptr;
    char *buy = strtok_r(request, " ", &saveptr);
    char *stock_symbol = strtok_r(nullptr, " ", &saveptr);
    char *amount_str = strtok_r(nullptr, " ", &saveptr);
    char *price_str = strtok_r(nullptr, " ", &saveptr);
    char *user_id = strtok_r(nullptr, " ", &saveptr);

    char response[256];
    char sql[256];
    int rc;
    char *zErrMsg = 0;

    // check for format errors
    if (!buy || !stock_symbol || !amount_str || !price_str || !user_id) {
        fprintf(stderr, "BUY command received but incorrectly formatted\n");
        fprintf(stderr, "Error message sent to client\n");
        snprintf(response, sizeof(response),
                 "403 message format error\nCorrect format: BUY <stock_symbol> <amount_to_buy> <price_per_stock> <user_id>\n");
        send(socket, response, strlen(response), 0);
        return -1;
    }

    // convert numeric values from char* to numbers
    double num_shares_to_buy;
    double price_per_stock;
    try {
        num_shares_to_buy = stod(amount_str); // throws exception if conversion fails
        price_per_stock = stod(price_str);
    } catch (const std::exception&) {
        fprintf(stderr, "Amount or price are not valid numbers\n");
        fprintf(stderr, "Error message sent to client\n");
        snprintf(response, sizeof(response),
                 "403 message format error\nAmount and price fields must be numbers.\n");
        send(socket, response, strlen(response), 0);
        return -1;
    }

    if (num_shares_to_buy <= 0 || price_per_stock <= 0) {
        fprintf(stderr, "Amount or price are invalid because they are <= 0\n");
        fprintf(stderr, "Error message sent to client\n");
        snprintf(response, sizeof(response),
                 "403 message format error\nAmount and price fields must be positive and non-zero.\n");
        send(socket, response, strlen(response), 0);
        return -1;
    }

    // calculate stockprice
    double shares_bought_USD = num_shares_to_buy * price_per_stock;

    // check if the user is in the database and if so get the balance
    double user_balance = -1;
    snprintf(sql, sizeof(sql),
             "SELECT usd_balance FROM Users WHERE ID='%s'", user_id);

    rc = sqlite3_exec(db, sql, getBalance_callback, &user_balance, &zErrMsg);
    if (rc != SQLITE_OK) { handle_SQL_error(socket, zErrMsg); return -1; }
    if (user_balance < 0) {
        snprintf(response, sizeof(response), "500 user %s does not exist\n", user_id);
        fprintf(stderr, "%sError message sent to client\n", response);
        send(socket, response, strlen(response), 0);
        return -1;
    }
    if (user_balance < shares_bought_USD) {
        snprintf(response, sizeof(response),
                 "403 User has an insufficient balance of $%.2f for a transaction of $%.2f\n",
                 user_balance, shares_bought_USD);
        fprintf(stderr, "%sError message sent to client\n", response);
        send(socket, response, strlen(response), 0);
        return -1;
    }

    // update the Users balance in the database
    user_balance -= shares_bought_USD;
    snprintf(sql, sizeof(sql),
             "UPDATE Users SET usd_balance=%f WHERE ID='%s'", user_balance, user_id);
    rc = sqlite3_exec(db, sql, nullptr, nullptr, &zErrMsg);
    if (rc != SQLITE_OK) { handle_SQL_error(socket, zErrMsg); return -1; }

    // update the Stocks table
    double shares_owned = -1;
    snprintf(sql, sizeof(sql),
             "SELECT stock_balance FROM Stocks WHERE stock_symbol='%s' AND user_id='%s'",
             stock_symbol, user_id);
    rc = sqlite3_exec(db, sql, getBalance_callback, &shares_owned, &zErrMsg);
    if (rc != SQLITE_OK) { handle_SQL_error(socket, zErrMsg); return -1; }

    if (shares_owned < 0) {  // indicates this stock is not in the database
        shares_owned = 0;
        snprintf(sql, sizeof(sql),
                 "INSERT INTO Stocks (stock_symbol, stock_name, stock_balance, user_id) VALUES ('%s', '%s', %f, '%s');",
                 stock_symbol, stock_symbol, num_shares_to_buy, user_id);
        rc = sqlite3_exec(db, sql, nullptr, nullptr, &zErrMsg);
        if (rc != SQLITE_OK) { handle_SQL_error(socket, zErrMsg); return -1; }
    } else {
        snprintf(sql, sizeof(sql),
                 "UPDATE Stocks SET stock_balance=%f WHERE stock_symbol='%s' AND user_id='%s'",
                 shares_owned + num_shares_to_buy, stock_symbol, user_id);
        rc = sqlite3_exec(db, sql, nullptr, nullptr, &zErrMsg);
        if (rc != SQLITE_OK) { handle_SQL_error(socket, zErrMsg); return -1; }
    }

    snprintf(response, sizeof(response),
             "200 OK\nBOUGHT: New balance: %.2f %s. USD balance $%.2f\n",
             shares_owned + num_shares_to_buy, stock_symbol, user_balance);
    send(socket, response, strlen(response), 0);
    return 0;
}

/** Sells an amount of stock and responds to the client with the new balance.
 *  user_id is a TEXT string matching the Users.ID primary key.
 */
int sell_command(int socket, char* request, sqlite3* db) {
    // parse input string
    char *saveptr = nullptr;
    char *sell = strtok_r(request, " ", &saveptr);
    char *stock_symbol = strtok_r(nullptr, " ", &saveptr);
    char *amount_str = strtok_r(nullptr, " ", &saveptr);
    char *price_str = strtok_r(nullptr, " ", &saveptr);
    char *user_id = strtok_r(nullptr, " ", &saveptr);

    char response[256];
    char sql[256];
    int rc;
    char *zErrMsg = 0;

    // check for format errors
    if (!sell || !stock_symbol || !amount_str || !price_str || !user_id) {
        fprintf(stderr, "SELL command received but incorrectly formatted\n");
        fprintf(stderr, "Error message sent to client\n");
        snprintf(response, sizeof(response),
                 "403 message format error\nCorrect format: SELL <stock_symbol> <amount_to_sell> <price_per_stock> <user_id>\n");
        send(socket, response, strlen(response), 0);
        return -1;
    }

    // convert numeric values from char* to numbers
    double num_shares_to_sell;
    double price_per_stock;
    try {
        num_shares_to_sell = stod(amount_str);
        price_per_stock = stod(price_str);
    } catch (const std::exception&) {
        fprintf(stderr, "Amount or price are not valid numbers\n");
        fprintf(stderr, "Error message sent to client\n");
        snprintf(response, sizeof(response),
                 "403 message format error\nAmount and price fields must be numbers.\n");
        send(socket, response, strlen(response), 0);
        return -1;
    }

    if (num_shares_to_sell <= 0 || price_per_stock <= 0) {
        fprintf(stderr, "Amount or price are invalid because they are <= 0\n");
        fprintf(stderr, "Error message sent to client\n");
        snprintf(response, sizeof(response),
                 "403 message format error\nAmount and price fields must be positive and non-zero.\n");
        send(socket, response, strlen(response), 0);
        return -1;
    }

    // calculate sale value
    double shares_sold_USD = num_shares_to_sell * price_per_stock;

    // check if the user is in the database and if so get the balance
    double user_balance = -1;
    snprintf(sql, sizeof(sql),
             "SELECT usd_balance FROM Users WHERE ID='%s'", user_id);
    rc = sqlite3_exec(db, sql, getBalance_callback, &user_balance, &zErrMsg);
    if (rc != SQLITE_OK) { handle_SQL_error(socket, zErrMsg); return -1; }
    if (user_balance < 0) {
        snprintf(response, sizeof(response), "500 user %s does not exist\n", user_id);
        fprintf(stderr, "%sError message sent to client\n", response);
        send(socket, response, strlen(response), 0);
        return -1;
    }

    // Make sure the user actually owns this stock before trying to sell it
    double shares_owned = -1;
    snprintf(sql, sizeof(sql),
             "SELECT stock_balance FROM Stocks WHERE stock_symbol='%s' AND user_id='%s'",
             stock_symbol, user_id);
    rc = sqlite3_exec(db, sql, getBalance_callback, &shares_owned, &zErrMsg);
    if (rc != SQLITE_OK) { handle_SQL_error(socket, zErrMsg); return -1; }

    if (shares_owned < 0) {
        snprintf(response, sizeof(response), "403 User does not own any %s stock\n", stock_symbol);
        fprintf(stderr, "%sError message sent to client\n", response);
        send(socket, response, strlen(response), 0);
        return -1;
    }
    if (shares_owned < num_shares_to_sell) {
        snprintf(response, sizeof(response),
                 "403 Not enough %s stock balance. You have %.2f shares but trying to sell %.2f\n",
                 stock_symbol, shares_owned, num_shares_to_sell);
        fprintf(stderr, "%sError message sent to client\n", response);
        send(socket, response, strlen(response), 0);
        return -1;
    }

    // Complete the sale: add money to user's account and remove shares from their portfolio
    user_balance += shares_sold_USD;
    snprintf(sql, sizeof(sql),
             "UPDATE Users SET usd_balance=%f WHERE ID='%s'", user_balance, user_id);
    rc = sqlite3_exec(db, sql, nullptr, nullptr, &zErrMsg);
    if (rc != SQLITE_OK) { handle_SQL_error(socket, zErrMsg); return -1; }

    // update the Stocks table
    double shares_owned_after_sale = shares_owned - num_shares_to_sell;
    snprintf(sql, sizeof(sql),
             "UPDATE Stocks SET stock_balance=%f WHERE stock_symbol='%s' AND user_id='%s'",
             shares_owned_after_sale, stock_symbol, user_id);
    rc = sqlite3_exec(db, sql, nullptr, nullptr, &zErrMsg);
    if (rc != SQLITE_OK) { handle_SQL_error(socket, zErrMsg); return -1; }

    snprintf(response, sizeof(response),
             "200 OK\nSOLD: New balance: %.2f %s. USD balance $%.2f\n",
             shares_owned_after_sale, stock_symbol, user_balance);
    send(socket, response, strlen(response), 0);
    return 0;
}

/** PA2 update: only root can shut down the server.
 *  Non-root users receive a 403. Returns -99 to signal the caller to exit.
 */
int shutdown_command(int socket, char* request, sqlite3* db, bool is_root) {
    if (!is_root) {
        const char* denied = "403 Forbidden: only root can shut down the server\n";
        fprintf(stderr, "[SHUTDOWN] Rejected: current user is not root.\n");
        send(socket, denied, strlen(denied), 0);
        return -1;
    }

    const char* response = "200 OK\n";
    send(socket, response, strlen(response), 0);
    fprintf(stdout, "SHUTDOWN command received. Shutting down server...\n");

    close(socket);
    sqlite3_close(db);

    fprintf(stdout, "Server shutdown complete.\n");
    return -99;
}

/** Callback that appends each result row as a space-separated line to the result string. */
static int list_callback(void *data, int argc, char **argv, char **azColName) {
    string* result = static_cast<string*>(data);
    for (int i = 0; i < argc; i++) {
        if (argv[i]) {
            *result += argv[i];
        } else {
            *result += "NULL";
        }
        if (i < argc - 1) *result += " ";
    }
    *result += "\n";
    return 0;
}

/** PA2 update: non-root users see only their own records.
 *  Root sees every record in the database with the owning username appended.
 */
int list_command(int socket, char* request, sqlite3* db,
                 const string& username, bool is_root)
{
    string response;
    string records;
    char sql[512];
    char *zErrMsg = 0;

    if (is_root) {
        // Root sees all stocks with the owner's username
        snprintf(sql, sizeof(sql),
            "SELECT S.ID, S.stock_symbol, S.stock_balance, S.user_id "
            "FROM Stocks S ORDER BY S.ID;");

        int rc = sqlite3_exec(db, sql, list_callback, &records, &zErrMsg);
        if (rc != SQLITE_OK) {
            response  = "403 message format error\nDatabase error: ";
            response += zErrMsg;
            response += "\n";
            sqlite3_free(zErrMsg);
        } else {
            response  = "200 OK\n";
            response += "The list of records in the Stock database:\n";
            response += records.empty() ? "(No stocks found)\n" : records;
        }
    } else {
        // Non-root sees only their own stocks, filtered by their username
        snprintf(sql, sizeof(sql),
            "SELECT ID, stock_symbol, stock_balance "
            "FROM Stocks WHERE LOWER(user_id) = LOWER('%s') ORDER BY ID;",
            username.c_str());

        int rc = sqlite3_exec(db, sql, list_callback, &records, &zErrMsg);
        if (rc != SQLITE_OK) {
            response  = "403 message format error\nDatabase error: ";
            response += zErrMsg;
            response += "\n";
            sqlite3_free(zErrMsg);
        } else {
            response  = "200 OK\n";
            response += "The list of records in the Stock database for ";
            response += username;
            response += ":\n";
            response += records.empty() ? "(No stocks found)\n" : records;
        }
    }

    send(socket, response.c_str(), response.length(), 0);
    return 0;
}

/** PA2 update: shows balance for the currently logged-in user.
 *  Queries by username (TEXT primary key) instead of a numeric ID.
 */
int balance_command(int socket, char* request, sqlite3* db, const string& username) {
    string response;
    char sql[256];

    snprintf(sql, sizeof(sql),
             "SELECT first_name, last_name, usd_balance FROM Users WHERE LOWER(ID) = LOWER('%s');",
             username.c_str());

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    if (rc != SQLITE_OK) {
        response  = "403 message format error\n";
        response += "Database error: ";
        response += sqlite3_errmsg(db);
        response += "\n";
        send(socket, response.c_str(), response.length(), 0);
        return -1;
    }

    rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW) {
        /* user found */
        const char* first_name  = (const char*)sqlite3_column_text(stmt, 0);
        const char* last_name   = (const char*)sqlite3_column_text(stmt, 1);
        double      usd_balance = sqlite3_column_double(stmt, 2);

        response  = "200 OK\n";
        response += "Balance for user ";
        if (first_name) response += first_name;
        response += " ";
        if (last_name)  response += last_name;

        char balance_str[50];
        snprintf(balance_str, sizeof(balance_str), ": $%.2f\n", usd_balance);
        response += balance_str;

    } else if (rc == SQLITE_DONE) {
        /* user not found */
        response  = "403 message format error\n";
        response += "User " + username + " doesn't exist\n";
    } else {
        /* database error */
        response  = "403 message format error\n";
        response += "Database error: ";
        response += sqlite3_errmsg(db);
        response += "\n";
    }

    sqlite3_finalize(stmt);
    send(socket, response.c_str(), response.length(), 0);
    return 0;
}

/** Terminates the client connection. */
int quit_command(int socket, char* request, sqlite3* db) {
    /* acknowledge quit */
    const char* response = "200 OK\n";
    send(socket, response, strlen(response), 0);

    /* signal server to close connection */
    return 1;
}