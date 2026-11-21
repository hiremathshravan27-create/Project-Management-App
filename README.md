# ProjectFlow – Project Management App

A project and issue tracking application built with a **C (Winsock) backend**, **PostgreSQL**, and **React + Vite** frontend.

## Features

- User registration and login with JWT authentication
- Create, list, update, and delete projects
- Create, list, update, and delete issues
- Issue type (Task, Bug, Feature), status (To Do, In Progress, Done), and priority (Low, Medium, High)
- Add, list, and delete comments on issues
- Dashboard counts for projects and the selected project's issues
- Ownership checks on project, issue, and comment APIs

## Requirements

- Windows 10/11
- MSYS2 UCRT64 with GCC, PostgreSQL `libpq`, OpenSSL and Winsock libraries
- PostgreSQL
- Node.js (current LTS)

## 1. Database

Create/use database `project_management`, then run `database/schema.sql` with psql. Existing user/project/issue data is preserved. The schema expects existing `users`, `projects`, and `issues` tables from the earlier project setup.

## 2. Configure PostgreSQL environment variables

Do not place database passwords in source files. In the **MSYS2 UCRT64 terminal**, set the values for the current terminal session (replace the password locally; do not share it):

```bash
export PGHOST=localhost
export PGPORT=5432
export PGDATABASE=project_management
export PGUSER=postgres
read -s -p "PostgreSQL password: " PGPASSWORD
echo
export PGPASSWORD
```

The C backend uses libpq's standard `PG*` environment variables.

## 3. Configure JWT secret

Use the same private JWT secret you previously saved. It must be at least 64 characters. In MSYS2 UCRT64:

```bash
read -s -p "JWT secret: " JWT_SECRET
echo
export JWT_SECRET
```

Do not paste the secret into chat, source code, screenshots, or GitHub. If the secret changes, log in again because existing tokens will no longer validate.

## 4. Build and run backend

From **MSYS2 UCRT64**:

```bash
cd "/c/Users/Shravan M H/Project-Management-App"
gcc backend/src/main.c backend/src/database.c backend/src/auth.c backend/src/jwt.c backend/src/project.c backend/src/issue.c -Ibackend/include -o backend/server_auth2.exe $(pkg-config --cflags --libs libpq openssl) -lws2_32
./backend/server_auth2.exe
```

Expected startup messages include `PostgreSQL connected successfully!` and `C Backend running on port 8080...`.

## 5. Run frontend

Open a separate **VS Code PowerShell** terminal:

```powershell
cd "C:\Users\Shravan M H\Project-Management-App\frontend"
npm install
npm run dev
```

Open `http://localhost:5173`.

## API routes

All routes except register/login require `Authorization: Bearer <JWT>`.

| Method | Route | Purpose |
|---|---|---|
| POST | `/api/register` | Register |
| POST | `/api/login` | Login |
| GET | `/api/me` | Verify token |
| POST | `/api/projects` | Create project |
| GET | `/api/projects` | List owned projects |
| PUT | `/api/projects/:id` | Update project |
| DELETE | `/api/projects/:id` | Delete project |
| POST | `/api/issues` | Create issue |
| GET | `/api/issues?project_id=:id` | List project issues |
| PUT | `/api/issues/:id` | Update issue fields |
| DELETE | `/api/issues/:id` | Delete issue |
| POST | `/api/comments` | Add comment |
| GET | `/api/comments?issue_id=:id` | List issue comments |
| DELETE | `/api/comments/:id` | Delete your comment |

## Important notes

- This implementation follows the current single-owner project model. It does not yet implement project teams, invitations, or cross-user assignment workflows.
- The small C JSON parser supports ordinary JSON string escapes but does not decode `\\uXXXX` Unicode escape sequences.
- The server is a learning-project HTTP server, not a production-grade hardened HTTP stack. Before public deployment, add TLS/reverse proxy, request-size limits, robust HTTP parsing, rate limiting, secure secret management, and production logging.
- Never commit `PGPASSWORD`, `JWT_SECRET`, executable files, object files, or `node_modules` to GitHub.
