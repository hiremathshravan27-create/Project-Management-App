
#ifndef JWT_H
#define JWT_H

int generateJWT(int userId, char *token, int tokenSize);

int verifyJWT(const char *token, int *userId);

#endif
