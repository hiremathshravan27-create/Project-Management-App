
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <winsock2.h>

#include "database.h"
#include "auth.h"
#include "jwt.h"
#include "project.h"
#include "issue.h"

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define BUFFER_SIZE 8192

// Extract a simple string value from JSON
int getJsonValue(
    const char *json,
    const char *key,
    char *output,
    size_t outputSize
) {
    if (!json || !key || !output || outputSize == 0) return 0;
    char searchKey[100];
    if (snprintf(searchKey, sizeof(searchKey), "\"%s\"", key) >= (int)sizeof(searchKey)) return 0;
    const char *position = strstr(json, searchKey);
    if (!position) return 0;
    position = strchr(position, ':');
    if (!position) return 0;
    position++;
    while (*position == ' ' || *position == '\t' || *position == '\r' || *position == '\n') position++;
    if (*position != '"') return 0;
    position++;
    size_t i = 0;
    while (*position && *position != '"') {
        char ch = *position++;
        if (ch == '\\') {
            ch = *position++;
            if (!ch) return 0;
            switch (ch) {
                case '"': ch = '"'; break;
                case '\\': ch = '\\'; break;
                case '/': ch = '/'; break;
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case 'b': ch = '\b'; break;
                case 'f': ch = '\f'; break;
                default: return 0; /* Unicode escapes are not decoded by this small parser. */
            }
        }
        if (i + 1 >= outputSize) return 0;
        output[i++] = ch;
    }
    if (*position != '"') return 0;
    output[i] = '\0';
    return 1;
}

// Extract JWT token from Authorization header
int getAuthUserId(const char *request, int *userId) {

    char *authHeader = strstr(
        request,
        "Authorization: Bearer "
    );

    if (authHeader == NULL)
        return 0;

    authHeader += strlen("Authorization: Bearer ");

    char token[2048];

    int i = 0;

    while (
        authHeader[i] != '\r' &&
        authHeader[i] != '\n' &&
        authHeader[i] != '\0' &&
        i < (int)sizeof(token) - 1
    ) {
        token[i] = authHeader[i];
        i++;
    }

    token[i] = '\0';

    return verifyJWT(token, userId);
}


// Send HTTP response
void sendResponse(
    SOCKET client,
    int status,
    const char *statusText,
    const char *body
) {
    char response[20000];

    int length = snprintf(
        response,
        sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: http://localhost:5173\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        status,
        statusText,
        (int)strlen(body),
        body
    );

    if (length < 0 || length >= (int)sizeof(response))
        return;

    int sent = 0;

    while (sent < length) {

        int result = send(
            client,
            response + sent,
            length - sent,
            0
        );

        if (result == SOCKET_ERROR || result == 0)
            break;

        sent += result;
    }
}


// Main server
int main() {

    // Connect to PostgreSQL
    PGconn *db = connectDatabase();

    if (db == NULL) {
        printf("Unable to start server without database!\n");
        return 1;
    }

    WSADATA wsa;
    SOCKET server, client;

    struct sockaddr_in address;

    char request[BUFFER_SIZE];

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {

        printf("Winsock initialization failed!\n");

        PQfinish(db);
        return 1;
    }

    // Create socket
    server = socket(AF_INET, SOCK_STREAM, 0);

    if (server == INVALID_SOCKET) {

        printf("Socket creation failed!\n");

        WSACleanup();
        PQfinish(db);
        return 1;
    }

    // Configure server address
    memset(&address, 0, sizeof(address));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind socket
    if (bind(
        server,
        (struct sockaddr *)&address,
        sizeof(address)
    ) == SOCKET_ERROR) {

        printf("Binding failed: %d\n", WSAGetLastError());

        closesocket(server);
        WSACleanup();
        PQfinish(db);

        return 1;
    }

    // Start listening
    if (listen(server, 5) == SOCKET_ERROR) {

        printf("Listening failed!\n");

        closesocket(server);
        WSACleanup();
        PQfinish(db);

        return 1;
    }

    printf("C Backend running on port %d...\n", PORT);

    while (1) {

        int address_length = sizeof(address);

        client = accept(
            server,
            (struct sockaddr *)&address,
            &address_length
        );

        if (client == INVALID_SOCKET) {

            printf("Accept failed!\n");
            continue;
        }

        memset(request, 0, sizeof(request));

        // Read HTTP request
        int totalReceived = 0;
        int contentLength = 0;
        int headerFound = 0;

        while (totalReceived < BUFFER_SIZE - 1) {

            int received = recv(
                client,
                request + totalReceived,
                BUFFER_SIZE - 1 - totalReceived,
                0
            );

            if (received <= 0)
                break;

            totalReceived += received;
            request[totalReceived] = '\0';

            char *headerEnd = strstr(request, "\r\n\r\n");

            if (headerEnd != NULL && !headerFound) {

                headerFound = 1;

                char *contentHeader =
                    strstr(request, "Content-Length:");

                if (contentHeader != NULL) {
                    contentLength = atoi(contentHeader + 15);
                }
            }

            if (headerFound) {

                char *bodyStart =
                    strstr(request, "\r\n\r\n") + 4;

                int receivedBody =
                    totalReceived -
                    (int)(bodyStart - request);

                if (receivedBody >= contentLength)
                    break;
            }
        }

        if (!headerFound) {

            sendResponse(
                client,
                400,
                "Bad Request",
                "{\"error\":\"Invalid HTTP request\"}"
            );

            closesocket(client);
            continue;
        }

        printf("HTTP request received.\n");

        // Handle CORS preflight
        if (strncmp(request, "OPTIONS ", 8) == 0) {

            sendResponse(
                client,
                204,
                "No Content",
                ""
            );
        }


        // USER REGISTRATION API
        else if (
            strncmp(request, "POST /api/register ", 19) == 0
        ) {

            char *body = strstr(request, "\r\n\r\n") + 4;

            char username[101];
            char email[151];
            char password[256];

            int validUsername = getJsonValue(
                body, "username", username, sizeof(username)
            );

            int validEmail = getJsonValue(
                body, "email", email, sizeof(email)
            );

            int validPassword = getJsonValue(
                body, "password", password, sizeof(password)
            );

            if (
                !validUsername ||
                !validEmail ||
                !validPassword ||
                strlen(username) == 0 ||
                strlen(email) == 0 ||
                strlen(password) == 0
            ) {

                sendResponse(
                    client,
                    400,
                    "Bad Request",
                    "{\"error\":\"Username, email and password are required\"}"
                );
            }

            else if (strlen(password) < 8) {

                sendResponse(
                    client,
                    400,
                    "Bad Request",
                    "{\"error\":\"Password must contain at least 8 characters\"}"
                );
            }

            else {

                int success = registerUser(
                    db, username, email, password
                );

                if (success) {

                    sendResponse(
                        client,
                        201,
                        "Created",
                        "{\"message\":\"User registered successfully\"}"
                    );

                } else {

                    sendResponse(
                        client,
                        409,
                        "Conflict",
                        "{\"error\":\"Registration failed. Email may already exist.\"}"
                    );
                }
            }
        }


        // USER LOGIN API WITH JWT
        else if (
            strncmp(request, "POST /api/login ", 16) == 0
        ) {

            char *body = strstr(request, "\r\n\r\n") + 4;

            char email[151];
            char password[256];

            int validEmail = getJsonValue(
                body,
                "email",
                email,
                sizeof(email)
            );

            int validPassword = getJsonValue(
                body,
                "password",
                password,
                sizeof(password)
            );

            if (
                !validEmail ||
                !validPassword ||
                strlen(email) == 0 ||
                strlen(password) == 0
            ) {

                sendResponse(
                    client,
                    400,
                    "Bad Request",
                    "{\"error\":\"Email and password are required\"}"
                );
            }

            else {

                int userId = 0;

                int success = loginUser(
                    db,
                    email,
                    password,
                    &userId
                );

                if (success) {

                    char token[2048];

                    if (!generateJWT(
                        userId,
                        token,
                        sizeof(token)
                    )) {

                        sendResponse(
                            client,
                            500,
                            "Internal Server Error",
                            "{\"error\":\"Unable to generate authentication token\"}"
                        );
                    }

                    else {

                        char responseBody[2300];

                        snprintf(
                            responseBody,
                            sizeof(responseBody),
                            "{\"message\":\"Login successful\",\"userId\":%d,\"token\":\"%s\"}",
                            userId,
                            token
                        );

                        sendResponse(
                            client,
                            200,
                            "OK",
                            responseBody
                        );
                    }

                } else {

                    sendResponse(
                        client,
                        401,
                        "Unauthorized",
                        "{\"error\":\"Invalid email or password\"}"
                    );
                }
            }
        }


        // PROTECTED USER PROFILE API
        else if (
            strncmp(request, "GET /api/me ", 12) == 0
        ) {

            int userId = 0;

            if (getAuthUserId(request, &userId)) {

                char responseBody[200];

                snprintf(
                    responseBody,
                    sizeof(responseBody),
                    "{\"message\":\"Authentication successful\",\"userId\":%d}",
                    userId
                );

                sendResponse(
                    client,
                    200,
                    "OK",
                    responseBody
                );
            }

            else {

                sendResponse(
                    client,
                    401,
                    "Unauthorized",
                    "{\"error\":\"Invalid or missing authentication token\"}"
                );
            }
        }


        // CREATE PROJECT API (JWT PROTECTED)
        else if (
            strncmp(request, "POST /api/projects ", 19) == 0
        ) {

            int userId = 0;

            if (!getAuthUserId(request, &userId)) {

                sendResponse(
                    client,
                    401,
                    "Unauthorized",
                    "{\"error\":\"Invalid or missing authentication token\"}"
                );
            }

            else {

                char *body = strstr(request, "\r\n\r\n") + 4;

                char name[151];
                char description[501];

                int validName = getJsonValue(
                    body, "name", name, sizeof(name)
                );

                int validDescription = getJsonValue(
                    body, "description", description,
                    sizeof(description)
                );

                if (
                    !validName ||
                    strlen(name) == 0 ||
                    (validDescription && strlen(description) > 500)
                ) {

                    sendResponse(
                        client,
                        400,
                        "Bad Request",
                        "{\"error\":\"A valid project name is required\"}"
                    );
                }

                else {

                    if (!validDescription)
                        description[0] = '\0';

                    int projectId = 0;

                    int success = createProject(
                        db,
                        name,
                        description,
                        userId,
                        &projectId
                    );

                    if (success) {

                        char responseBody[256];

                        snprintf(
                            responseBody,
                            sizeof(responseBody),
                            "{\"message\":\"Project created successfully\",\"projectId\":%d}",
                            projectId
                        );

                        sendResponse(
                            client,
                            201,
                            "Created",
                            responseBody
                        );
                    }

                    else {

                        sendResponse(
                            client,
                            500,
                            "Internal Server Error",
                            "{\"error\":\"Unable to create project\"}"
                        );
                    }
                }
            }
        }


        // GET ALL PROJECTS API (JWT PROTECTED)
        else if (
            strncmp(request, "GET /api/projects ", 18) == 0
        ) {

            int userId = 0;

            if (!getAuthUserId(request, &userId)) {

                sendResponse(
                    client,
                    401,
                    "Unauthorized",
                    "{\"error\":\"Invalid or missing authentication token\"}"
                );
            }

            else {

                char responseBody[6000];

                int success = getAllProjects(
                    db,
                    userId,
                    responseBody,
                    sizeof(responseBody)
                );

                if (success) {

                    sendResponse(
                        client,
                        200,
                        "OK",
                        responseBody
                    );
                }

                else {

                    sendResponse(
                        client,
                        500,
                        "Internal Server Error",
                        "{\"error\":\"Unable to retrieve projects\"}"
                    );
                }
            }
        }


        // UPDATE PROJECT API (JWT PROTECTED)
        else if (
            strncmp(request, "PUT /api/projects/", 18) == 0
        ) {

            char *idStart = request + 18;
            char *idEnd;

            long projectIdLong = strtol(idStart, &idEnd, 10);

            if (
                idEnd == idStart ||
                projectIdLong <= 0 ||
                projectIdLong > INT_MAX ||
                *idEnd != ' '
            ) {

                sendResponse(
                    client,
                    400,
                    "Bad Request",
                    "{\"error\":\"Invalid project ID\"}"
                );
            }

            else {

                int projectId = (int)projectIdLong;
                int userId = 0;

                if (!getAuthUserId(request, &userId)) {

                    sendResponse(
                        client,
                        401,
                        "Unauthorized",
                        "{\"error\":\"Invalid or missing authentication token\"}"
                    );
                }

                else {

                    char *body = strstr(request, "\r\n\r\n") + 4;

                    char name[151];
                    char description[501];

                    int validName = getJsonValue(
                        body, "name", name, sizeof(name)
                    );

                    int validDescription = getJsonValue(
                        body, "description", description,
                        sizeof(description)
                    );

                    if (
                        !validName ||
                        strlen(name) == 0 ||
                        (validDescription && strlen(description) > 500)
                    ) {

                        sendResponse(
                            client,
                            400,
                            "Bad Request",
                            "{\"error\":\"A valid project name is required\"}"
                        );
                    }

                    else {

                        if (!validDescription)
                            description[0] = '\0';

                        int success = updateProject(
                            db,
                            projectId,
                            name,
                            description,
                            userId
                        );

                        if (success) {

                            sendResponse(
                                client,
                                200,
                                "OK",
                                "{\"message\":\"Project updated successfully\"}"
                            );
                        }

                        else {

                            sendResponse(
                                client,
                                404,
                                "Not Found",
                                "{\"error\":\"Project not found or you do not have permission to update it\"}"
                            );
                        }
                    }
                }
            }
        }


        // DELETE PROJECT API (JWT PROTECTED)
        else if (
            strncmp(request, "DELETE /api/projects/", 21) == 0
        ) {

            char *idStart = request + 21;
            char *idEnd;

            long projectIdLong = strtol(idStart, &idEnd, 10);

            if (
                idEnd == idStart ||
                projectIdLong <= 0 ||
                projectIdLong > INT_MAX ||
                *idEnd != ' '
            ) {

                sendResponse(
                    client,
                    400,
                    "Bad Request",
                    "{\"error\":\"Invalid project ID\"}"
                );
            }

            else {

                int projectId = (int)projectIdLong;
                int userId = 0;

                if (!getAuthUserId(request, &userId)) {

                    sendResponse(
                        client,
                        401,
                        "Unauthorized",
                        "{\"error\":\"Invalid or missing authentication token\"}"
                    );
                }

                else {

                    int success = deleteProject(
                        db,
                        projectId,
                        userId
                    );

                    if (success) {

                        sendResponse(
                            client,
                            200,
                            "OK",
                            "{\"message\":\"Project deleted successfully\"}"
                        );
                    }

                    else {

                        sendResponse(
                            client,
                            404,
                            "Not Found",
                            "{\"error\":\"Project not found or you do not have permission to delete it\"}"
                        );
                    }
                }
            }
        }


        // CREATE ISSUE API (JWT PROTECTED)
        else if (
            strncmp(request, "POST /api/issues ", 16) == 0
        ) {

            int userId = 0;

            if (!getAuthUserId(request, &userId)) {

                sendResponse(
                    client,
                    401,
                    "Unauthorized",
                    "{\"error\":\"Invalid or missing authentication token\"}"
                );
            }

            else {

                char *body = strstr(request, "\r\n\r\n") + 4;

                char projectIdText[20];
                char title[201];
                char description[501];
                char issueType[31];
                char priority[21];

                int validProjectId = getJsonValue(
                    body,
                    "project_id",
                    projectIdText,
                    sizeof(projectIdText)
                );

                int validTitle = getJsonValue(
                    body,
                    "title",
                    title,
                    sizeof(title)
                );

                int validDescription = getJsonValue(
                    body,
                    "description",
                    description,
                    sizeof(description)
                );

                int validIssueType = getJsonValue(
                    body,
                    "issue_type",
                    issueType,
                    sizeof(issueType)
                );

                int validPriority = getJsonValue(
                    body,
                    "priority",
                    priority,
                    sizeof(priority)
                );

                char *idEnd = NULL;

                long projectIdLong = validProjectId
                    ? strtol(projectIdText, &idEnd, 10)
                    : 0;

                if (
                    !validProjectId ||
                    idEnd == projectIdText ||
                    *idEnd != '\0' ||
                    projectIdLong <= 0 ||
                    projectIdLong > INT_MAX ||
                    !validTitle ||
                    strlen(title) == 0 ||
                    (validDescription && strlen(description) > 500) ||
                    (validIssueType && strlen(issueType) > 30) ||
                    (validPriority && strlen(priority) > 20)
                ) {

                    sendResponse(
                        client,
                        400,
                        "Bad Request",
                        "{\"error\":\"Valid project_id and title are required\"}"
                    );
                }

                else {

                    if (!validDescription)
                        description[0] = '\0';

                    if (!validIssueType)
                        strcpy(issueType, "Task");

                    if (!validPriority)
                        strcpy(priority, "Medium");

                    int issueId = 0;

                    int success = createIssue(
                        db,
                        (int)projectIdLong,
                        title,
                        description,
                        issueType,
                        priority,
                        userId,
                        &issueId
                    );

                    if (success) {

                        char responseBody[256];

                        snprintf(
                            responseBody,
                            sizeof(responseBody),
                            "{\"message\":\"Issue created successfully\",\"issueId\":%d}",
                            issueId
                        );

                        sendResponse(
                            client,
                            201,
                            "Created",
                            responseBody
                        );
                    }

                    else {

                        sendResponse(
                            client,
                            404,
                            "Not Found",
                            "{\"error\":\"Project not found or you do not have permission to create an issue\"}"
                        );
                    }
                }
            }
        }


        // GET ALL ISSUES API (JWT PROTECTED)
        else if (
            strncmp(
                request,
                "GET /api/issues?project_id=",
                sizeof("GET /api/issues?project_id=") - 1
            ) == 0
        ) {

            char *idStart =
                request + sizeof("GET /api/issues?project_id=") - 1;

            char *idEnd;

            long projectIdLong = strtol(idStart, &idEnd, 10);

            if (
                idEnd == idStart ||
                projectIdLong <= 0 ||
                projectIdLong > INT_MAX ||
                *idEnd != ' '
            ) {

                sendResponse(
                    client,
                    400,
                    "Bad Request",
                    "{\"error\":\"Invalid project ID\"}"
                );
            }

            else {

                int projectId = (int)projectIdLong;
                int userId = 0;

                if (!getAuthUserId(request, &userId)) {

                    sendResponse(
                        client,
                        401,
                        "Unauthorized",
                        "{\"error\":\"Invalid or missing authentication token\"}"
                    );
                }

                else {

                    char responseBody[6000];

                    int success = getAllIssues(
                        db,
                        projectId,
                        userId,
                        responseBody,
                        sizeof(responseBody)
                    );

                    if (success) {

                        sendResponse(
                            client,
                            200,
                            "OK",
                            responseBody
                        );
                    }

                    else {

                        sendResponse(
                            client,
                            500,
                            "Internal Server Error",
                            "{\"error\":\"Unable to retrieve issues\"}"
                        );
                    }
                }
            }
        }


        // UPDATE ISSUE API (JWT PROTECTED)
        else if (strncmp(request, "PUT /api/issues/", 16) == 0) {
            char *idStart = request + 16;
            char *idEnd = NULL;
            long issueIdLong = strtol(idStart, &idEnd, 10);
            int userId = 0;
            if (idEnd == idStart || issueIdLong <= 0 || issueIdLong > INT_MAX || *idEnd != ' ') {
                sendResponse(client, 400, "Bad Request", "{\"error\":\"Invalid issue ID\"}");
            } else if (!getAuthUserId(request, &userId)) {
                sendResponse(client, 401, "Unauthorized", "{\"error\":\"Invalid or missing authentication token\"}");
            } else {
                char *body = strstr(request, "\r\n\r\n") + 4;
                char title[201], description[501], issueType[31], status[31], priority[21];
                int a=getJsonValue(body,"title",title,sizeof(title));
                int b=getJsonValue(body,"description",description,sizeof(description));
                int c=getJsonValue(body,"issue_type",issueType,sizeof(issueType));
                int d=getJsonValue(body,"status",status,sizeof(status));
                int e=getJsonValue(body,"priority",priority,sizeof(priority));
                int validType = c && (!strcmp(issueType,"Task") || !strcmp(issueType,"Bug") || !strcmp(issueType,"Feature"));
                int validStatus = d && (!strcmp(status,"To Do") || !strcmp(status,"In Progress") || !strcmp(status,"Done"));
                int validPriority = e && (!strcmp(priority,"Low") || !strcmp(priority,"Medium") || !strcmp(priority,"High"));
                if (!a || !b || !c || !d || !e || !title[0] || !validType || !validStatus || !validPriority) {
                    sendResponse(client, 400, "Bad Request", "{\"error\":\"Provide title, description, valid issue_type, status and priority\"}");
                } else if (updateIssue(db,(int)issueIdLong,userId,title,description,issueType,status,priority)) {
                    sendResponse(client,200,"OK","{\"message\":\"Issue updated successfully\"}");
                } else {
                    sendResponse(client,404,"Not Found","{\"error\":\"Issue not found or access denied\"}");
                }
            }
        }

        // DELETE ISSUE API (JWT PROTECTED)
        else if (strncmp(request, "DELETE /api/issues/", 19) == 0) {
            char *idStart = request + 19;
            char *idEnd = NULL;
            long issueIdLong = strtol(idStart, &idEnd, 10);
            int userId = 0;
            if (idEnd == idStart || issueIdLong <= 0 || issueIdLong > INT_MAX || *idEnd != ' ') {
                sendResponse(client,400,"Bad Request","{\"error\":\"Invalid issue ID\"}");
            } else if (!getAuthUserId(request,&userId)) {
                sendResponse(client,401,"Unauthorized","{\"error\":\"Invalid or missing authentication token\"}");
            } else if (deleteIssue(db,(int)issueIdLong,userId)) {
                sendResponse(client,200,"OK","{\"message\":\"Issue deleted successfully\"}");
            } else {
                sendResponse(client,404,"Not Found","{\"error\":\"Issue not found or access denied\"}");
            }
        }

        // CREATE COMMENT API (JWT PROTECTED)
        else if (strncmp(request, "POST /api/comments ", 19) == 0) {
            int userId = 0;
            if (!getAuthUserId(request,&userId)) {
                sendResponse(client,401,"Unauthorized","{\"error\":\"Invalid or missing authentication token\"}");
            } else {
                char *body = strstr(request,"\r\n\r\n") + 4;
                char issueIdText[20], commentText[1001];
                int a=getJsonValue(body,"issue_id",issueIdText,sizeof(issueIdText));
                int b=getJsonValue(body,"comment_text",commentText,sizeof(commentText));
                char *end=NULL; long issueIdLong=a?strtol(issueIdText,&end,10):0;
                if (!a || end==issueIdText || *end!='\0' || issueIdLong<=0 || issueIdLong>INT_MAX || !b || !commentText[0]) {
                    sendResponse(client,400,"Bad Request","{\"error\":\"Valid issue_id and comment_text are required\"}");
                } else {
                    int commentId=0;
                    if (createIssueComment(db,(int)issueIdLong,userId,commentText,&commentId)) {
                        char responseBody[160];
                        snprintf(responseBody,sizeof(responseBody),"{\"message\":\"Comment added\",\"commentId\":%d}",commentId);
                        sendResponse(client,201,"Created",responseBody);
                    } else sendResponse(client,404,"Not Found","{\"error\":\"Issue not found or access denied\"}");
                }
            }
        }

        // GET COMMENTS FOR AN ISSUE (JWT PROTECTED)
        else if (strncmp(request,"GET /api/comments?issue_id=",sizeof("GET /api/comments?issue_id=")-1)==0) {
            char *idStart=request+sizeof("GET /api/comments?issue_id=")-1;
            char *idEnd=NULL; long issueIdLong=strtol(idStart,&idEnd,10); int userId=0;
            if (idEnd==idStart || issueIdLong<=0 || issueIdLong>INT_MAX || *idEnd!=' ') {
                sendResponse(client,400,"Bad Request","{\"error\":\"Invalid issue ID\"}");
            } else if (!getAuthUserId(request,&userId)) {
                sendResponse(client,401,"Unauthorized","{\"error\":\"Invalid or missing authentication token\"}");
            } else {
                char responseBody[12000];
                if (getIssueComments(db,(int)issueIdLong,userId,responseBody,sizeof(responseBody))) sendResponse(client,200,"OK",responseBody);
                else sendResponse(client,500,"Internal Server Error","{\"error\":\"Unable to retrieve comments\"}");
            }
        }

        // DELETE OWN COMMENT API (JWT PROTECTED)
        else if (strncmp(request,"DELETE /api/comments/",21)==0) {
            char *idStart=request+21; char *idEnd=NULL; long commentIdLong=strtol(idStart,&idEnd,10); int userId=0;
            if (idEnd==idStart || commentIdLong<=0 || commentIdLong>INT_MAX || *idEnd!=' ') {
                sendResponse(client,400,"Bad Request","{\"error\":\"Invalid comment ID\"}");
            } else if (!getAuthUserId(request,&userId)) {
                sendResponse(client,401,"Unauthorized","{\"error\":\"Invalid or missing authentication token\"}");
            } else if (deleteIssueComment(db,(int)commentIdLong,userId)) {
                sendResponse(client,200,"OK","{\"message\":\"Comment deleted\"}");
            } else sendResponse(client,404,"Not Found","{\"error\":\"Comment not found or access denied\"}");
        }

        // UNKNOWN ROUTE
        else {

            sendResponse(
                client,
                404,
                "Not Found",
                "{\"error\":\"Route not found\"}"
            );
        }

        closesocket(client);
    }

    closesocket(server);
    WSACleanup();
    PQfinish(db);

    return 0;
}
