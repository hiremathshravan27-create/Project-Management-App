
#include <stdio.h>
#include <stdlib.h>
#include <libpq-fe.h>
#include "auth.h"

int registerUser(PGconn *db, const char *username,
                 const char *email, const char *password) {

    const char *query =
        "INSERT INTO users (username, email, password_hash) "
        "VALUES ($1, $2, crypt($3, gen_salt('bf'))) "
        "RETURNING id;";

    const char *params[3] = {
        username,
        email,
        password
    };

    PGresult *result = PQexecParams(
        db, query, 3, NULL, params, NULL, NULL, 0
    );

    if (result == NULL) {
        printf("Registration query failed.\n");
        return 0;
    }

    if (PQresultStatus(result) == PGRES_TUPLES_OK) {
        printf("User registered successfully! ID: %s\n",
               PQgetvalue(result, 0, 0));

        PQclear(result);
        return 1;
    }

    printf("Registration failed: %s\n", PQerrorMessage(db));

    PQclear(result);
    return 0;
}


int loginUser(PGconn *db, const char *email,
              const char *password, int *userId) {

    if (db == NULL || email == NULL ||
        password == NULL || userId == NULL) {
        return 0;
    }

    const char *query =
        "SELECT id FROM users "
        "WHERE email = $1 "
        "AND password_hash = crypt($2, password_hash);";

    const char *params[2] = {
        email,
        password
    };

    PGresult *result = PQexecParams(
        db, query, 2, NULL, params, NULL, NULL, 0
    );

    if (result == NULL) {
        printf("Login query failed.\n");
        return 0;
    }

    if (PQresultStatus(result) == PGRES_TUPLES_OK &&
        PQntuples(result) == 1) {

        *userId = atoi(PQgetvalue(result, 0, 0));

        printf("Login successful! User ID: %d\n", *userId);

        PQclear(result);
        return 1;
    }

    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        printf("Login database error: %s\n", PQerrorMessage(db));
    }

    PQclear(result);
    return 0;
}
