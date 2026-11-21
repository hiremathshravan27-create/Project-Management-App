
#ifndef PROJECT_H
#define PROJECT_H

#include <libpq-fe.h>

int createProject(PGconn *db,
                  const char *name,
                  const char *description,
                  int userId,
                  int *projectId);
                  int getAllProjects(PGconn *db, int userId, char *output, int outputSize);
                  int deleteProject(PGconn *db, int projectId, int userId);
                  int updateProject(PGconn *db,
                  int projectId,
                  const char *name,
                  const char *description,
                  int userId);
#endif
