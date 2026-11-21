#include <stdio.h>
#include <libpq-fe.h>
#include "database.h"

/* libpq reads PGHOST, PGPORT, PGDATABASE, PGUSER and PGPASSWORD from
   the process environment. Keep credentials out of source control. */
PGconn *connectDatabase(void) {
    PGconn *conn = PQconnectdb("");
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Database connection failed: %s\n", PQerrorMessage(conn));
        PQfinish(conn);
        return NULL;
    }
    printf("PostgreSQL connected successfully!\n");
    return conn;
}
