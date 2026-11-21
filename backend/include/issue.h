
#ifndef ISSUE_H
#define ISSUE_H

#include <libpq-fe.h>

int createIssue(
    PGconn *db,
    int projectId,
    const char *title,
    const char *description,
    const char *issueType,
    const char *priority,
    int userId,
    int *issueId
);

int getAllIssues(
    PGconn *db,
    int projectId,
    int userId,
    char *output,
    int outputSize
);

int updateIssue(PGconn *db, int issueId, int userId,
    const char *title, const char *description,
    const char *issueType, const char *status, const char *priority);
int deleteIssue(PGconn *db, int issueId, int userId);
int createIssueComment(PGconn *db, int issueId, int userId, const char *commentText, int *commentId);
int getIssueComments(PGconn *db, int issueId, int userId, char *output, int outputSize);
int deleteIssueComment(PGconn *db, int commentId, int userId);

#endif
