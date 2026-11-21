
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpq-fe.h>
#include "issue.h"

/* Safely append a JSON-escaped string */
static int appendJsonString(
    char *output,
    size_t outputSize,
    size_t *position,
    const char *value
) {
    if (*position >= outputSize)
        return 0;

    output[(*position)++] = '"';

    for (size_t i = 0; value[i] != '\0'; i++) {

        const char *escaped = NULL;
        char character[2] = { value[i], '\0' };

        switch (value[i]) {
            case '"':  escaped = "\\\""; break;
            case '\\': escaped = "\\\\"; break;
            case '\n': escaped = "\\n";  break;
            case '\r': escaped = "\\r";  break;
            case '\t': escaped = "\\t";  break;
            case '\b': escaped = "\\b";  break;
            case '\f': escaped = "\\f";  break;
            default:   escaped = character;
        }

        size_t length = strlen(escaped);

        if (*position + length >= outputSize)
            return 0;

        memcpy(output + *position, escaped, length);
        *position += length;
    }

    if (*position >= outputSize)
        return 0;

    output[(*position)++] = '"';

    return 1;
}


/* Create a new issue */
int createIssue(
    PGconn *db,
    int projectId,
    const char *title,
    const char *description,
    const char *issueType,
    const char *priority,
    int userId,
    int *issueId
) {
    const char *query =
        "INSERT INTO issues "
        "(project_id, title, description, issue_type, priority, created_by) "
        "SELECT $1, $2, $3, $4, $5, $6 "
        "WHERE EXISTS ("
        "SELECT 1 FROM projects WHERE id = $1 AND created_by = $6"
        ") "
        "RETURNING id;";

    char projectIdText[20];
    char userIdText[20];

    snprintf(projectIdText, sizeof(projectIdText), "%d", projectId);
    snprintf(userIdText, sizeof(userIdText), "%d", userId);

    const char *params[6] = {
        projectIdText,
        title,
        description,
        issueType,
        priority,
        userIdText
    };

    PGresult *result = PQexecParams(
        db,
        query,
        6,
        NULL,
        params,
        NULL,
        NULL,
        0
    );

    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        printf("Create issue error: %s\n", PQerrorMessage(db));
        PQclear(result);
        return 0;
    }

    if (PQntuples(result) != 1) {
        PQclear(result);
        return 0;
    }

    *issueId = atoi(PQgetvalue(result, 0, 0));

    PQclear(result);

    return 1;
}


/* Get all issues belonging to a project */
int getAllIssues(
    PGconn *db,
    int projectId,
    int userId,
    char *output,
    int outputSize
) {
    const char *query =
        "SELECT i.id, i.title, i.description, i.issue_type, "
        "i.status, i.priority, i.assigned_to "
        "FROM issues i "
        "JOIN projects p ON i.project_id = p.id "
        "WHERE i.project_id = $1 AND p.created_by = $2 "
        "ORDER BY i.id DESC;";

    char projectIdText[20];
    char userIdText[20];

    snprintf(projectIdText, sizeof(projectIdText), "%d", projectId);
    snprintf(userIdText, sizeof(userIdText), "%d", userId);

    const char *params[2] = {
        projectIdText,
        userIdText
    };

    PGresult *result = PQexecParams(
        db,
        query,
        2,
        NULL,
        params,
        NULL,
        NULL,
        0
    );

    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        printf("Get issues error: %s\n", PQerrorMessage(db));
        PQclear(result);
        return 0;
    }

    size_t position = 0;
    size_t capacity = (size_t)outputSize;

    if (capacity < 3) {
        PQclear(result);
        return 0;
    }

    output[0] = '\0';

    if (snprintf(output, capacity, "[") >= (int)capacity) {
        PQclear(result);
        return 0;
    }

    position = 1;

    for (int i = 0; i < PQntuples(result); i++) {

        if (i > 0) {
            if (position + 1 >= capacity) {
                PQclear(result);
                return 0;
            }

            output[position++] = ',';
        }

        int written = snprintf(
            output + position,
            capacity - position,
            "{\"id\":%s,\"title\":",
            PQgetvalue(result, i, 0)
        );

        if (written < 0 || (size_t)written >= capacity - position) {
            PQclear(result);
            return 0;
        }

        position += (size_t)written;

        if (!appendJsonString(
            output, capacity, &position,
            PQgetvalue(result, i, 1)
        )) {
            PQclear(result);
            return 0;
        }

        written = snprintf(
            output + position,
            capacity - position,
            ",\"description\":"
        );

        if (written < 0 || (size_t)written >= capacity - position) {
            PQclear(result);
            return 0;
        }

        position += (size_t)written;

        if (!appendJsonString(
            output,
            capacity,
            &position,
            PQgetisnull(result, i, 2)
                ? ""
                : PQgetvalue(result, i, 2)
        )) {
            PQclear(result);
            return 0;
        }

        const char *fieldNames[] = {
            "issue_type",
            "status",
            "priority"
        };

        for (int j = 0; j < 3; j++) {

            written = snprintf(
                output + position,
                capacity - position,
                ",\"%s\":",
                fieldNames[j]
            );

            if (written < 0 || (size_t)written >= capacity - position) {
                PQclear(result);
                return 0;
            }

            position += (size_t)written;

            if (!appendJsonString(
                output,
                capacity,
                &position,
                PQgetisnull(result, i, j + 3)
                    ? ""
                    : PQgetvalue(result, i, j + 3)
            )) {
                PQclear(result);
                return 0;
            }
        }

        written = snprintf(
            output + position,
            capacity - position,
            ",\"assigned_to\":%s}",
            PQgetisnull(result, i, 6)
                ? "null"
                : PQgetvalue(result, i, 6)
        );

        if (written < 0 || (size_t)written >= capacity - position) {
            PQclear(result);
            return 0;
        }

        position += (size_t)written;
    }

    if (position + 2 > capacity) {
        PQclear(result);
        return 0;
    }

    output[position++] = ']';
    output[position] = '\0';

    PQclear(result);

    return 1;
}

/* Update an issue owned through one of the user's projects. */
int updateIssue(
    PGconn *db, int issueId, int userId,
    const char *title, const char *description,
    const char *issueType, const char *status, const char *priority
) {
    const char *query =
        "UPDATE issues i SET title=$1, description=$2, issue_type=$3, status=$4, priority=$5 "
        "FROM projects p WHERE i.project_id=p.id AND i.id=$6 AND p.created_by=$7 RETURNING i.id;";
    char issueIdText[20], userIdText[20];
    snprintf(issueIdText, sizeof(issueIdText), "%d", issueId);
    snprintf(userIdText, sizeof(userIdText), "%d", userId);
    const char *params[7] = { title, description, issueType, status, priority, issueIdText, userIdText };
    PGresult *result = PQexecParams(db, query, 7, NULL, params, NULL, NULL, 0);
    int ok = PQresultStatus(result) == PGRES_TUPLES_OK && PQntuples(result) == 1;
    if (!ok) fprintf(stderr, "Update issue error: %s\n", PQerrorMessage(db));
    PQclear(result);
    return ok;
}

int deleteIssue(PGconn *db, int issueId, int userId) {
    const char *query =
        "DELETE FROM issues i USING projects p "
        "WHERE i.project_id=p.id AND i.id=$1 AND p.created_by=$2 RETURNING i.id;";
    char issueIdText[20], userIdText[20];
    snprintf(issueIdText, sizeof(issueIdText), "%d", issueId);
    snprintf(userIdText, sizeof(userIdText), "%d", userId);
    const char *params[2] = { issueIdText, userIdText };
    PGresult *result = PQexecParams(db, query, 2, NULL, params, NULL, NULL, 0);
    int ok = PQresultStatus(result) == PGRES_TUPLES_OK && PQntuples(result) == 1;
    if (!ok) fprintf(stderr, "Delete issue error: %s\n", PQerrorMessage(db));
    PQclear(result);
    return ok;
}

int createIssueComment(PGconn *db, int issueId, int userId, const char *commentText, int *commentId) {
    const char *query =
        "INSERT INTO comments (issue_id,user_id,comment_text) "
        "SELECT $1,$2,$3 WHERE EXISTS ("
        "SELECT 1 FROM issues i JOIN projects p ON p.id=i.project_id "
        "WHERE i.id=$1 AND p.created_by=$2) RETURNING id;";
    char issueIdText[20], userIdText[20];
    snprintf(issueIdText, sizeof(issueIdText), "%d", issueId);
    snprintf(userIdText, sizeof(userIdText), "%d", userId);
    const char *params[3] = { issueIdText, userIdText, commentText };
    PGresult *result = PQexecParams(db, query, 3, NULL, params, NULL, NULL, 0);
    int ok = PQresultStatus(result) == PGRES_TUPLES_OK && PQntuples(result) == 1;
    if (ok) *commentId = atoi(PQgetvalue(result, 0, 0));
    else fprintf(stderr, "Create comment error: %s\n", PQerrorMessage(db));
    PQclear(result);
    return ok;
}

int getIssueComments(PGconn *db, int issueId, int userId, char *output, int outputSize) {
    const char *query =
        "SELECT c.id,c.user_id,c.comment_text,to_char(c.created_at,'YYYY-MM-DD HH24:MI:SS') "
        "FROM comments c JOIN issues i ON i.id=c.issue_id "
        "JOIN projects p ON p.id=i.project_id "
        "WHERE c.issue_id=$1 AND p.created_by=$2 ORDER BY c.id ASC;";
    char issueIdText[20], userIdText[20];
    snprintf(issueIdText, sizeof(issueIdText), "%d", issueId);
    snprintf(userIdText, sizeof(userIdText), "%d", userId);
    const char *params[2] = { issueIdText, userIdText };
    PGresult *result = PQexecParams(db, query, 2, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        fprintf(stderr, "Get comments error: %s\n", PQerrorMessage(db));
        PQclear(result); return 0;
    }
    size_t pos = 0, cap = (size_t)outputSize;
    if (cap < 3) { PQclear(result); return 0; }
    output[pos++] = '[';
    for (int i = 0; i < PQntuples(result); i++) {
        if (i && pos + 1 >= cap) { PQclear(result); return 0; }
        if (i) output[pos++] = ',';
        int n = snprintf(output + pos, cap - pos, "{\"id\":%s,\"user_id\":%s,\"comment_text\":",
                         PQgetvalue(result,i,0), PQgetvalue(result,i,1));
        if (n < 0 || (size_t)n >= cap-pos) { PQclear(result); return 0; }
        pos += (size_t)n;
        if (!appendJsonString(output, cap, &pos, PQgetvalue(result,i,2))) { PQclear(result); return 0; }
        n = snprintf(output + pos, cap-pos, ",\"created_at\":");
        if (n < 0 || (size_t)n >= cap-pos) { PQclear(result); return 0; }
        pos += (size_t)n;
        if (!appendJsonString(output, cap, &pos, PQgetvalue(result,i,3))) { PQclear(result); return 0; }
        if (pos + 2 >= cap) { PQclear(result); return 0; }
        output[pos++] = '}';
    }
    if (pos + 2 > cap) { PQclear(result); return 0; }
    output[pos++] = ']'; output[pos] = '\0';
    PQclear(result); return 1;
}

int deleteIssueComment(PGconn *db, int commentId, int userId) {
    const char *query =
        "DELETE FROM comments c USING issues i, projects p "
        "WHERE c.issue_id=i.id AND i.project_id=p.id AND c.id=$1 "
        "AND c.user_id=$2 AND p.created_by=$2 RETURNING c.id;";
    char commentIdText[20], userIdText[20];
    snprintf(commentIdText, sizeof(commentIdText), "%d", commentId);
    snprintf(userIdText, sizeof(userIdText), "%d", userId);
    const char *params[2] = { commentIdText, userIdText };
    PGresult *result = PQexecParams(db, query, 2, NULL, params, NULL, NULL, 0);
    int ok = PQresultStatus(result) == PGRES_TUPLES_OK && PQntuples(result) == 1;
    if (!ok) fprintf(stderr, "Delete comment error: %s\n", PQerrorMessage(db));
    PQclear(result); return ok;
}
