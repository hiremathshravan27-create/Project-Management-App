import { useMemo, useState } from 'react';
import './App.css';

const API_URL = import.meta.env.VITE_API_URL || 'https://project-management-app-ufsx.onrender.com';

async function api(path, token, options = {}) {
  const headers = { ...(options.body ? { 'Content-Type': 'application/json' } : {}), ...(token ? { Authorization: `Bearer ${token}` } : {}), ...options.headers };
  const response = await fetch(`${API_URL}${path}`, { ...options, headers });
  const raw = await response.text();
  let data;
  try { data = raw ? JSON.parse(raw) : {}; } catch { throw new Error(`Invalid server response (${response.status})`); }
  if (!response.ok) throw new Error(data.error || data.message || `Request failed (${response.status})`);
  return data;
}

const emptyIssue = { title: '', description: '', issue_type: 'Task', priority: 'Medium' };

export default function App() {
  const [mode, setMode] = useState('login');
  const [username, setUsername] = useState('');
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [token, setToken] = useState('');
  const [userId, setUserId] = useState(null);
  const [authMessage, setAuthMessage] = useState('');
  const [busy, setBusy] = useState(false);

  const [projects, setProjects] = useState([]);
  const [projectName, setProjectName] = useState('');
  const [projectDescription, setProjectDescription] = useState('');
  const [selectedProjectId, setSelectedProjectId] = useState(null);
  const [projectEditId, setProjectEditId] = useState(null);
  const [projectEdit, setProjectEdit] = useState({ name: '', description: '' });
  const [projectMessage, setProjectMessage] = useState('');

  const [issues, setIssues] = useState([]);
  const [issueDraft, setIssueDraft] = useState(emptyIssue);
  const [issueEditId, setIssueEditId] = useState(null);
  const [issueEdit, setIssueEdit] = useState({ ...emptyIssue, status: 'To Do' });
  const [issueMessage, setIssueMessage] = useState('');
  const [commentsIssueId, setCommentsIssueId] = useState(null);
  const [comments, setComments] = useState([]);
  const [commentDraft, setCommentDraft] = useState('');

  const selectedProject = projects.find((p) => p.id === selectedProjectId) || null;
  const stats = useMemo(() => ({
    projects: projects.length,
    issues: issues.length,
    done: issues.filter((i) => i.status === 'Done').length,
    active: issues.filter((i) => i.status === 'In Progress').length,
  }), [projects, issues]);

  async function loadProjects(authToken = token) {
    const rows = await api('/api/projects', authToken);
    setProjects(rows);
    if (!rows.length) { setSelectedProjectId(null); setIssues([]); }
  }

  async function loadIssues(projectId, authToken = token) {
    const rows = await api(`/api/issues?project_id=${projectId}`, authToken);
    setIssues(rows);
    setIssueMessage(`${rows.length} issue${rows.length === 1 ? '' : 's'} in this project`);
  }



  async function handleAuth(e) {
    e.preventDefault(); setBusy(true); setAuthMessage('');
    try {
      const payload = mode === 'register' ? { username, email, password } : { email, password };
      const data = await api(mode === 'register' ? '/api/register' : '/api/login', '', { method: 'POST', body: JSON.stringify(payload) });
      if (mode === 'register') { setMode('login'); setAuthMessage('Account created. Please log in.'); setPassword(''); }
      else {
        setToken(data.token); setUserId(data.userId); setAuthMessage(''); setPassword('');
        const rows = await api('/api/projects', data.token);
        setProjects(rows);
        const firstId = rows[0]?.id ?? null;
        setSelectedProjectId(firstId);
        if (firstId !== null) { const issueRows = await api(`/api/issues?project_id=${firstId}`, data.token); setIssues(issueRows); }
      }
    } catch (e2) { setAuthMessage(e2.message); }
    finally { setBusy(false); }
  }

  async function createProject(e) {
    e.preventDefault(); setProjectMessage('Creating project...');
    try {
      const created = await api('/api/projects', token, { method: 'POST', body: JSON.stringify({ name: projectName.trim(), description: projectDescription.trim() }) });
      setProjectName(''); setProjectDescription(''); setProjectMessage('Project created.'); await loadProjects();
      if (created.projectId) { setSelectedProjectId(created.projectId); await loadIssues(created.projectId); }
    } catch (e) { setProjectMessage(e.message); }
  }

  async function saveProject(e) {
    e.preventDefault();
    try {
      await api(`/api/projects/${projectEditId}`, token, { method: 'PUT', body: JSON.stringify(projectEdit) });
      setProjects((old) => old.map((p) => p.id === projectEditId ? { ...p, ...projectEdit } : p));
      setProjectEditId(null); setProjectMessage('Project updated.');
    } catch (e) { setProjectMessage(e.message); }
  }

  async function removeProject(id) {
    if (!window.confirm('Delete this project and all its issues?')) return;
    try {
      await api(`/api/projects/${id}`, token, { method: 'DELETE' });
      const next = projects.filter((p) => p.id !== id); setProjects(next);
      if (selectedProjectId === id) {
        const nextId = next[0]?.id ?? null; setSelectedProjectId(nextId);
        if (nextId !== null) await loadIssues(nextId); else setIssues([]);
      }
      setProjectMessage('Project deleted.');
    } catch (e) { setProjectMessage(e.message); }
  }

  async function chooseProject(projectId) {
    setSelectedProjectId(projectId);
    setCommentsIssueId(null); setComments([]);
    try { await loadIssues(projectId); } catch (e) { setIssueMessage(e.message); }
  }

  async function createIssue(e) {
    e.preventDefault(); if (!selectedProjectId) return;
    try {
      await api('/api/issues', token, { method: 'POST', body: JSON.stringify({ project_id: String(selectedProjectId), ...issueDraft }) });
      setIssueDraft(emptyIssue); setIssueMessage('Issue created successfully.'); await loadIssues(selectedProjectId);
    } catch (e) { setIssueMessage(e.message); }
  }

  function startIssueEdit(issue) {
    setIssueEditId(issue.id);
    setIssueEdit({ title: issue.title, description: issue.description || '', issue_type: issue.issue_type, status: issue.status, priority: issue.priority });
  }

  async function saveIssue(e) {
    e.preventDefault();
    try {
      await api(`/api/issues/${issueEditId}`, token, { method: 'PUT', body: JSON.stringify(issueEdit) });
      setIssueEditId(null); await loadIssues(selectedProjectId); setIssueMessage('Issue updated.');
    } catch (e2) { setIssueMessage(e2.message); }
  }

  async function removeIssue(id) {
    if (!window.confirm('Delete this issue and its comments?')) return;
    try {
      await api(`/api/issues/${id}`, token, { method: 'DELETE' });
      setIssues((old) => old.filter((i) => i.id !== id));
      if (commentsIssueId === id) { setCommentsIssueId(null); setComments([]); }
      setIssueMessage('Issue deleted.');
    } catch (e) { setIssueMessage(e.message); }
  }

  async function openComments(issueId) {
    if (commentsIssueId === issueId) { setCommentsIssueId(null); return; }
    try {
      const rows = await api(`/api/comments?issue_id=${issueId}`, token);
      setComments(rows); setCommentsIssueId(issueId);
    } catch (e) { setIssueMessage(e.message); }
  }

  async function addComment(e) {
    e.preventDefault();
    try {
      await api('/api/comments', token, { method: 'POST', body: JSON.stringify({ issue_id: String(commentsIssueId), comment_text: commentDraft.trim() }) });
      setCommentDraft('');
      const rows = await api(`/api/comments?issue_id=${commentsIssueId}`, token); setComments(rows);
    } catch (e) { setIssueMessage(e.message); }
  }

  async function removeComment(commentId) {
    try {
      await api(`/api/comments/${commentId}`, token, { method: 'DELETE' });
      setComments((old) => old.filter((c) => c.id !== commentId));
    } catch (e) { setIssueMessage(e.message); }
  }

  function logout() {
    setToken(''); setUserId(null); setProjects([]); setIssues([]); setComments([]);
    setSelectedProjectId(null); setCommentsIssueId(null); setMode('login');
    setAuthMessage('You have been logged out.');
  }

  if (!token) return (
    <main className="auth-shell">
      <section className="auth-card">
        <div className="brand-mark">PM</div>
        <p className="eyebrow">WORKSPACE MANAGEMENT</p>
        <h1>{mode === 'login' ? 'Welcome back' : 'Create your account'}</h1>
        <p className="muted">Plan projects, track issues, and keep work moving.</p>
        <div className="auth-tabs">
          <button className={mode === 'login' ? 'active' : ''} onClick={() => { setMode('login'); setAuthMessage(''); }}>Login</button>
          <button className={mode === 'register' ? 'active' : ''} onClick={() => { setMode('register'); setAuthMessage(''); }}>Register</button>
        </div>
        <form onSubmit={handleAuth} className="stack-form">
          {mode === 'register' && <label>Username<input value={username} onChange={(e) => setUsername(e.target.value)} required maxLength={100} placeholder="Your name" /></label>}
          <label>Email address<input type="email" value={email} onChange={(e) => setEmail(e.target.value)} required placeholder="you@example.com" /></label>
          <label>Password<input type="password" value={password} onChange={(e) => setPassword(e.target.value)} required minLength={mode === 'register' ? 8 : undefined} placeholder="At least 8 characters" /></label>
          <button className="primary full" disabled={busy}>{busy ? 'Please wait…' : mode === 'login' ? 'Sign in' : 'Create account'}</button>
        </form>
        {authMessage && <p className="notice">{authMessage}</p>}
      </section>
    </main>
  );

  return (
    <div className="app-shell">
      <header className="topbar">
        <div className="brand"><span className="brand-mark small">PM</span><span>Project<span className="brand-light">Flow</span></span></div>
        <div className="top-actions"><span className="user-chip">User #{userId}</span><button className="ghost" onClick={logout}>Log out</button></div>
      </header>
      <main className="main-content">
        <section className="welcome-row"><div><p className="eyebrow">YOUR WORKSPACE</p><h1>Project dashboard</h1><p className="muted">A clear view of your projects and the work behind them.</p></div><div className="date-pill">Workspace overview</div></section>
        <section className="stats-grid">
          <Stat label="Projects" value={stats.projects} detail="Your active workspaces" />
          <Stat label="Issues" value={stats.issues} detail="In selected project" />
          <Stat label="In progress" value={stats.active} detail="Currently being worked on" />
          <Stat label="Completed" value={stats.done} detail="Finished issues" />
        </section>
        <section className="workspace-grid">
          <aside className="panel project-panel">
            <div className="panel-heading"><div><p className="eyebrow">WORKSPACES</p><h2>My projects</h2></div><span className="count-pill">{projects.length}</span></div>
            <form className="compact-form" onSubmit={createProject}>
              <input value={projectName} onChange={(e) => setProjectName(e.target.value)} placeholder="New project name" maxLength={150} required />
              <textarea value={projectDescription} onChange={(e) => setProjectDescription(e.target.value)} placeholder="Short description (optional)" rows="2" maxLength={500} />
              <button className="primary full">＋ Create project</button>
            </form>
            {projectMessage && <p className="inline-message">{projectMessage}</p>}
            <div className="project-list">
              {projects.map((project) => <article key={project.id} className={`project-item ${selectedProjectId === project.id ? 'selected' : ''}`}>
                {projectEditId === project.id ? <form onSubmit={saveProject} className="compact-form"><input value={projectEdit.name} onChange={(e) => setProjectEdit({ ...projectEdit, name: e.target.value })} required /><textarea value={projectEdit.description} onChange={(e) => setProjectEdit({ ...projectEdit, description: e.target.value })} rows="2" /><div className="button-row"><button className="primary">Save</button><button type="button" className="ghost" onClick={() => setProjectEditId(null)}>Cancel</button></div></form> : <>
                  <button className="project-select" onClick={() => chooseProject(project.id)}><strong>{project.name}</strong><small>Project #{project.id}</small><span>{project.description || 'No description yet.'}</span></button>
                  <div className="project-tools"><button onClick={() => { setProjectEditId(project.id); setProjectEdit({ name: project.name, description: project.description || '' }); }}>Edit</button><button className="danger-text" onClick={() => removeProject(project.id)}>Delete</button></div>
                </>}
              </article>)}
              {!projects.length && <div className="empty-state">No projects yet. Create your first project above.</div>}
            </div>
          </aside>
          <section className="panel issue-panel">
            {selectedProject ? <>
              <div className="panel-heading issue-heading"><div><p className="eyebrow">PROJECT #{selectedProject.id}</p><h2>{selectedProject.name}</h2><p className="muted">{selectedProject.description || 'Manage tasks and track progress.'}</p></div><span className="count-pill">{issues.length} issues</span></div>
              <form className="issue-create" onSubmit={createIssue}>
                <h3>Create an issue</h3>
                <div className="form-grid"><label>Title<input value={issueDraft.title} onChange={(e) => setIssueDraft({ ...issueDraft, title: e.target.value })} required maxLength={200} placeholder="e.g. Fix login validation" /></label><label>Type<select value={issueDraft.issue_type} onChange={(e) => setIssueDraft({ ...issueDraft, issue_type: e.target.value })}><option>Task</option><option>Bug</option><option>Feature</option></select></label><label>Priority<select value={issueDraft.priority} onChange={(e) => setIssueDraft({ ...issueDraft, priority: e.target.value })}><option>Low</option><option>Medium</option><option>High</option></select></label></div>
                <label>Description<textarea value={issueDraft.description} onChange={(e) => setIssueDraft({ ...issueDraft, description: e.target.value })} rows="2" maxLength={500} placeholder="What needs to be done?" /></label>
                <button className="primary">＋ Add issue</button>
              </form>
              {issueMessage && <p className="inline-message">{issueMessage}</p>}
              <div className="issue-list">
                {issues.map((issue) => <article className="issue-card" key={issue.id}>
                  {issueEditId === issue.id ? <form onSubmit={saveIssue} className="compact-form">
                    <label>Title<input value={issueEdit.title} onChange={(e) => setIssueEdit({ ...issueEdit, title: e.target.value })} required /></label>
                    <label>Description<textarea value={issueEdit.description} onChange={(e) => setIssueEdit({ ...issueEdit, description: e.target.value })} rows="2" /></label>
                    <div className="form-grid"><label>Type<select value={issueEdit.issue_type} onChange={(e) => setIssueEdit({ ...issueEdit, issue_type: e.target.value })}><option>Task</option><option>Bug</option><option>Feature</option></select></label><label>Status<select value={issueEdit.status} onChange={(e) => setIssueEdit({ ...issueEdit, status: e.target.value })}><option>To Do</option><option>In Progress</option><option>Done</option></select></label><label>Priority<select value={issueEdit.priority} onChange={(e) => setIssueEdit({ ...issueEdit, priority: e.target.value })}><option>Low</option><option>Medium</option><option>High</option></select></label></div>
                    <div className="button-row"><button className="primary">Save issue</button><button type="button" className="ghost" onClick={() => setIssueEditId(null)}>Cancel</button></div>
                  </form> : <>
                    <div className="issue-top"><div><span className="issue-id">ISSUE-{issue.id}</span><h3>{issue.title}</h3></div><span className={`status status-${issue.status.toLowerCase().replaceAll(' ', '-')}`}>{issue.status}</span></div>
                    <p className="issue-description">{issue.description || 'No description provided.'}</p>
                    <div className="issue-meta"><span className="type-tag">{issue.issue_type}</span><span className={`priority priority-${issue.priority.toLowerCase()}`}>{issue.priority} priority</span></div>
                    <div className="issue-actions"><button onClick={() => startIssueEdit(issue)}>Edit</button><button onClick={() => openComments(issue.id)}>{commentsIssueId === issue.id ? 'Hide comments' : 'Comments'}</button><button className="danger-text" onClick={() => removeIssue(issue.id)}>Delete</button></div>
                    {commentsIssueId === issue.id && <div className="comments-box"><h4>Discussion</h4><form onSubmit={addComment} className="comment-form"><input value={commentDraft} onChange={(e) => setCommentDraft(e.target.value)} required maxLength={1000} placeholder="Write a comment…" /><button className="primary">Send</button></form>{comments.map((comment) => <div className="comment" key={comment.id}><div><strong>User #{comment.user_id}</strong><small>{comment.created_at}</small></div><p>{comment.comment_text}</p>{comment.user_id === userId && <button className="danger-text" onClick={() => removeComment(comment.id)}>Delete</button>}</div>)}{!comments.length && <p className="muted">No comments yet.</p>}</div>}
                  </>}
                </article>)}
                {!issues.length && <div className="empty-state large">No issues in this project yet. Add the first one using the form above.</div>}
              </div>
            </> : <div className="empty-state large">Create a project or select one to manage its issues.</div>}
          </section>
        </section>
        <footer className="footer-note">ProjectFlow · C backend · PostgreSQL · React</footer>
      </main>
    </div>
  );
}

function Stat({ label, value, detail }) {
  return <article className="stat-card"><span>{label}</span><strong>{value}</strong><small>{detail}</small></article>;
}
