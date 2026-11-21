-- Run this against the existing project_management database.
-- It preserves existing rows and adds the columns/tables required by the app.

ALTER TABLE IF EXISTS issues
    ADD COLUMN IF NOT EXISTS issue_type VARCHAR(30) DEFAULT 'Task',
    ADD COLUMN IF NOT EXISTS status VARCHAR(30) DEFAULT 'To Do',
    ADD COLUMN IF NOT EXISTS priority VARCHAR(20) DEFAULT 'Medium',
    ADD COLUMN IF NOT EXISTS assigned_to INTEGER,
    ADD COLUMN IF NOT EXISTS created_by INTEGER,
    ADD COLUMN IF NOT EXISTS created_at TIMESTAMP WITHOUT TIME ZONE DEFAULT CURRENT_TIMESTAMP;

CREATE TABLE IF NOT EXISTS comments (
    id SERIAL PRIMARY KEY,
    issue_id INTEGER NOT NULL REFERENCES issues(id) ON DELETE CASCADE,
    user_id INTEGER REFERENCES users(id) ON DELETE SET NULL,
    comment_text TEXT NOT NULL,
    created_at TIMESTAMP WITHOUT TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_projects_created_by ON projects(created_by);
CREATE INDEX IF NOT EXISTS idx_issues_project_id ON issues(project_id);
CREATE INDEX IF NOT EXISTS idx_comments_issue_id ON comments(issue_id);
