
#ifndef AUTH_H
#define AUTH_H

#include <libpq-fe.h>

int registerUser(PGconn *db, const char *username,
                 const char *email, const char *password);

int loginUser(PGconn *db, const char *email,
              const char *password, int *userId);

#endif
