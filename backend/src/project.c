
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpq-fe.h>
#include "project.h"

// Helper: safely add a JSON-escaped string to output
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
        char singleChar[2] = { value[i], '\0' };

        switch (value[i]) {
            case '"':  escaped = "\\\""; break;
            case '\\': escaped = "\\\\"; break;
            case '\n': escaped = "\\n"; break;
            case '\r': escaped = "\\r"; break;
            case '\t': escaped = "\\t"; break;
            case '\b': escaped = "\\b"; break;
            case '\f': escaped = "\\f"; break;
            default:   escaped = singleChar; break;
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
    output[*position] = '\0';

    return 1;
}

// CREATE PROJECT
int createProject(
    PGconn *db,
    const char *name,
    const char *description,
    int userId,
    int *projectId
) {
    const char *query =
        "INSERT INTO projects (name, description, created_by) "
        "VALUES ($1, $2, $3) "
        "RETURNING id;";

    char userIdText[20];

    snprintf(userIdText, sizeof(userIdText), "%d", userId);

    const char *params[3] = {
        name,
        description,
        userIdText
    };

    PGresult *result = PQexecParams(
        db,
        query,
        3,
        NULL,
        params,
        NULL,
        NULL,
        0
    );

    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        printf("Project creation failed: %s\n", PQerrorMessage(db));
        PQclear(result);
        return 0;
    }

    *projectId = atoi(PQgetvalue(result, 0, 0));

    PQclear(result);

    return 1;
}

// GET ALL PROJECTS FOR A USER
int getAllProjects(
    PGconn *db,
    int userId,
    char *output,
    int outputSize
) {
    const char *query =
        "SELECT id, name, description "
        "FROM projects "
        "WHERE created_by = $1 "
        "ORDER BY id DESC;";

    char userIdText[20];

    snprintf(userIdText, sizeof(userIdText), "%d", userId);

    const char *params[1] = {
        userIdText
    };

    PGresult *result = PQexecParams(
        db,
        query,
        1,
        NULL,
        params,
        NULL,
        NULL,
        0
    );

    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        printf("Fetching projects failed: %s\n", PQerrorMessage(db));
        PQclear(result);
        return 0;
    }

    size_t position = 0;

    if (outputSize < 3) {
        PQclear(result);
        return 0;
    }

    output[position++] = '[';
    output[position] = '\0';

    int rows = PQntuples(result);

    for (int i = 0; i < rows; i++) {

        if (i > 0) {
            if (position + 1 >= (size_t)outputSize) {
                PQclear(result);
                return 0;
            }

            output[position++] = ',';
            output[position] = '\0';
        }

        char *id = PQgetvalue(result, i, 0);
        char *name = PQgetvalue(result, i, 1);

        char *description =
            PQgetisnull(result, i, 2)
            ? ""
            : PQgetvalue(result, i, 2);

        int needed = snprintf(
            output + position,
            outputSize - position,
            "{\"id\":%s,\"name\":",
            id
        );

        if (needed < 0 ||
            (size_t)needed >= (size_t)outputSize - position) {
            PQclear(result);
            return 0;
        }

        position += needed;

        if (!appendJsonString(
            output,
            outputSize,
            &position,
            name
        )) {
            PQclear(result);
            return 0;
        }

        needed = snprintf(
            output + position,
            outputSize - position,
            ",\"description\":"
        );

        if (needed < 0 ||
            (size_t)needed >= (size_t)outputSize - position) {
            PQclear(result);
            return 0;
        }

        position += needed;

        if (!appendJsonString(
            output,
            outputSize,
            &position,
            description
        )) {
            PQclear(result);
            return 0;
        }

        if (position + 2 >= (size_t)outputSize) {
            PQclear(result);
            return 0;
        }

        output[position++] = '}';
        output[position] = '\0';
    }

    if (position + 2 > (size_t)outputSize) {
        PQclear(result);
        return 0;
    }

    output[position++] = ']';
    output[position] = '\0';

    PQclear(result);

    return 1;
}

// DELETE PROJECT - ONLY THE OWNER CAN DELETE
int deleteProject(
    PGconn *db,
    int projectId,
    int userId
) {
    const char *query =
        "DELETE FROM projects "
        "WHERE id = $1 AND created_by = $2 "
        "RETURNING id;";

    char projectIdText[20];
    char userIdText[20];

    snprintf(
        projectIdText,
        sizeof(projectIdText),
        "%d",
        projectId
    );

    snprintf(
        userIdText,
        sizeof(userIdText),
        "%d",
        userId
    );

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
        printf("Delete project error: %s\n", PQerrorMessage(db));
        PQclear(result);
        return 0;
    }

    int deleted = PQntuples(result);

    PQclear(result);

    return deleted == 1;
}

// UPDATE PROJECT - ONLY THE OWNER CAN UPDATE
int updateProject(
    PGconn *db,
    int projectId,
    const char *name,
    const char *description,
    int userId
) {
    const char *query =
        "UPDATE projects "
        "SET name = $1, description = $2 "
        "WHERE id = $3 AND created_by = $4 "
        "RETURNING id;";

    char projectIdText[20];
    char userIdText[20];

    snprintf(
        projectIdText,
        sizeof(projectIdText),
        "%d",
        projectId
    );

    snprintf(
        userIdText,
        sizeof(userIdText),
        "%d",
        userId
    );

    const char *params[4] = {
        name,
        description,
        projectIdText,
        userIdText
    };

    PGresult *result = PQexecParams(
        db,
        query,
        4,
        NULL,
        params,
        NULL,
        NULL,
        0
    );

    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        printf("Update project error: %s\n", PQerrorMessage(db));
        PQclear(result);
        return 0;
    }

    int updated = PQntuples(result);

    PQclear(result);

    return updated == 1;
}
