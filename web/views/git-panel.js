import { el } from './dom.js';

export function mountGitPanel(parent, {
    apiSend,
    confirmDialog,
    announce,
    onOpenFile,
    onFilesChanged,
    onStatusChange,
} = {}) {
    if (!parent) return { refresh: async () => {}, focusCommit() {} };

    let statusData = null;
    let busy = false;
    let stagedOpen = true;
    let changesOpen = true;
    let commitsOpen = false;
    let diffDialog = null;
    let diffTitle = null;
    let diffSub = null;
    let diffBody = null;
    let diffOpenFileBtn = null;
    let diffTargetFile = '';

    const root = el('div', { class: 'git-panel' });

    /* Branch & toolbar header */
    const header = el('div', { class: 'git-header' });
    const branchInfo = el('div', { class: 'git-branch-info' });
    const branchIcon = el('span', { class: 'git-branch-icon', 'aria-hidden': 'true', text: '⎇' });
    const branchName = el('span', { class: 'git-branch-name', text: 'Checking Git…' });
    const syncPill = el('span', { class: 'git-sync-pill', hidden: true });
    branchInfo.append(branchIcon, branchName, syncPill);

    const refreshBtn = el('button', {
        type: 'button',
        class: 'ps-mini-btn',
        text: '↻',
        title: 'Refresh Git status and recent commits',
        'aria-label': 'Refresh Git status',
    });
    header.append(branchInfo, refreshBtn);

    /* Commit composer box */
    const commitBox = el('div', { class: 'git-commit-box' });
    const commitInput = el('textarea', {
        id: 'git-commit-message',
        class: 'git-commit-input',
        rows: '2',
        placeholder: 'Commit message (Ctrl+Enter to commit)',
        'aria-label': 'Commit message',
        title: 'Enter a commit summary message. Press Ctrl+Enter to commit.',
    });
    const commitActions = el('div', { class: 'git-commit-actions' });
    const commitBtn = el('button', {
        type: 'button',
        class: 'btn btn-primary git-commit-btn',
        text: 'Commit',
        disabled: true,
        title: 'Commit changes to the repository',
    });
    commitActions.append(commitBtn);
    commitBox.append(commitInput, commitActions);

    /* Scrollable sections container */
    const bodyScroll = el('div', { class: 'git-sections' });

    root.append(header, commitBox, bodyScroll);
    parent.append(root);

    refreshBtn.addEventListener('click', () => { void refresh(); });

    commitInput.addEventListener('input', () => {
        updateCommitButtonState();
    });
    commitInput.addEventListener('keydown', (event) => {
        if (event.key === 'Enter' && (event.ctrlKey || event.metaKey)) {
            event.preventDefault();
            if (!commitBtn.disabled) void handleCommit();
        }
    });
    commitBtn.addEventListener('click', () => { void handleCommit(); });

    function updateCommitButtonState() {
        if (!statusData || !statusData.isRepo) {
            commitBtn.disabled = true;
            commitBtn.textContent = 'Commit';
            return;
        }
        const stagedCount = Number(statusData.stagedCount) || 0;
        const unstagedCount = Number(statusData.unstagedCount) || 0;
        const hasMsg = commitInput.value.trim().length > 0;
        if (stagedCount > 0) {
            commitBtn.textContent = `Commit Staged (${stagedCount})`;
            commitBtn.title = `Commit ${stagedCount} staged ${stagedCount === 1 ? 'file' : 'files'} to ${statusData.branch || 'HEAD'}`;
            commitBtn.disabled = busy || !hasMsg;
        } else if (unstagedCount > 0) {
            commitBtn.textContent = `Stage All & Commit (${unstagedCount})`;
            commitBtn.title = `Stage all ${unstagedCount} changed ${unstagedCount === 1 ? 'file' : 'files'} and commit to ${statusData.branch || 'HEAD'}`;
            commitBtn.disabled = busy || !hasMsg;
        } else {
            commitBtn.textContent = 'Commit';
            commitBtn.title = 'Working tree is clean — no changes to commit';
            commitBtn.disabled = true;
        }
    }

    async function refresh() {
        try {
            const data = await apiSend('GET', '/__studio/api/git/status');
            applyStatus(data);
        } catch (error) {
            branchName.textContent = 'Git unavailable';
            bodyScroll.replaceChildren(
                el('p', { class: 'ps-empty ps-error', text: error.message || 'Could not load Git status.' }),
            );
        }
    }

    function applyStatus(data) {
        statusData = data && typeof data === 'object' ? data : null;
        const totalChanged = Number(statusData?.totalChanged) || 0;
        onStatusChange?.(totalChanged, statusData);
        render();
    }

    function splitFileLabel(path) {
        const clean = String(path || '').replace(/^\/+/, '');
        const slash = clean.lastIndexOf('/');
        if (slash < 0) return { name: clean || path, dir: '' };
        return {
            name: clean.slice(slash + 1),
            dir: clean.slice(0, slash),
        };
    }

    function render() {
        bodyScroll.replaceChildren();
        updateCommitButtonState();

        if (!statusData || !statusData.isRepo) {
            branchName.textContent = 'Not a Git repository';
            syncPill.hidden = true;
            commitBox.hidden = true;
            bodyScroll.append(
                el('p', { class: 'ps-empty', text: 'The current project folder is not inside a Git working tree.' }),
            );
            return;
        }

        commitBox.hidden = false;
        branchName.textContent = statusData.branch || 'HEAD';
        branchName.title = statusData.upstream
            ? `Branch ${statusData.branch} tracking ${statusData.upstream}`
            : `Current Git branch: ${statusData.branch || 'HEAD'}`;

        const ahead = Number(statusData.ahead) || 0;
        const behind = Number(statusData.behind) || 0;
        if (ahead > 0 || behind > 0) {
            syncPill.hidden = false;
            syncPill.textContent = `${ahead ? `↑${ahead}` : ''}${ahead && behind ? ' ' : ''}${behind ? `↓${behind}` : ''}`;
            syncPill.title = `${ahead} commit(s) ahead, ${behind} commit(s) behind ${statusData.upstream || 'upstream'}`;
        } else {
            syncPill.hidden = true;
        }

        const staged = Array.isArray(statusData.staged) ? statusData.staged : [];
        const changes = Array.isArray(statusData.changes) ? statusData.changes : [];
        const commits = Array.isArray(statusData.commits) ? statusData.commits : [];

        /* Staged Changes section */
        bodyScroll.append(buildFileSection({
            title: 'Staged Changes',
            items: staged,
            open: stagedOpen,
            onToggle: () => { stagedOpen = !stagedOpen; render(); },
            bulkLabel: '− All',
            bulkTitle: 'Unstage all staged files',
            onBulk: staged.length ? () => runGitAction('/__studio/api/git/unstage', { all: true }, 'Unstaged all files.') : null,
            isStaged: true,
            emptyText: 'No staged changes.',
        }));

        /* Unstaged Changes section */
        bodyScroll.append(buildFileSection({
            title: 'Changes',
            items: changes,
            open: changesOpen,
            onToggle: () => { changesOpen = !changesOpen; render(); },
            bulkLabel: '+ All',
            bulkTitle: 'Stage all changed and untracked files',
            onBulk: changes.length ? () => runGitAction('/__studio/api/git/stage', { all: true }, 'Staged all changes.') : null,
            isStaged: false,
            emptyText: staged.length ? 'All modified files are staged.' : 'Working tree is clean.',
        }));

        /* Recent Commits section */
        const commitsSec = el('section', { class: `git-section${commitsOpen ? '' : ' is-collapsed'}` });
        const commitsHead = el('div', { class: 'git-section-head' });
        const commitsToggle = el('button', {
            type: 'button',
            class: 'git-section-toggle',
            'aria-expanded': commitsOpen ? 'true' : 'false',
            title: 'Show or hide recent Git commits',
        });
        commitsToggle.append(
            el('span', { class: 'ps-twist', 'aria-hidden': 'true', text: commitsOpen ? '▾' : '▸' }),
            el('span', { class: 'git-section-title', text: 'Recent Commits' }),
        );
        commitsToggle.addEventListener('click', () => {
            commitsOpen = !commitsOpen;
            render();
        });
        const commitsCount = el('span', { class: 'ps-file-count', text: String(commits.length) });
        commitsHead.append(commitsToggle, commitsCount);
        commitsSec.append(commitsHead);

        if (commitsOpen) {
            const list = el('div', { class: 'git-commit-list' });
            if (!commits.length) {
                list.append(el('p', { class: 'git-empty-note', text: 'No commits yet.' }));
            } else {
                for (const c of commits) {
                    const btn = el('button', {
                        type: 'button',
                        class: 'git-commit-row',
                        title: `${c.shortHash} — ${c.subject}\n${c.author} (${c.relativeDate})\nClick to view commit diff`,
                    });
                    btn.append(
                        el('code', { class: 'git-commit-hash', text: c.shortHash }),
                        el('span', { class: 'git-commit-subj', text: c.subject }),
                        el('span', { class: 'git-commit-time', text: c.relativeDate }),
                    );
                    btn.addEventListener('click', () => {
                        void openDiffModal({
                            commit: c.hash || c.shortHash,
                            title: `Commit ${c.shortHash}: ${c.subject}`,
                            subtitle: `${c.author} · ${c.relativeDate}`,
                        });
                    });
                    list.append(btn);
                }
            }
            commitsSec.append(list);
        }
        bodyScroll.append(commitsSec);
    }

    function buildFileSection({
        title,
        items,
        open,
        onToggle,
        bulkLabel,
        bulkTitle,
        onBulk,
        isStaged,
        emptyText,
    }) {
        const sec = el('section', { class: `git-section${open ? '' : ' is-collapsed'}` });
        const head = el('div', { class: 'git-section-head' });
        const toggleBtn = el('button', {
            type: 'button',
            class: 'git-section-toggle',
            'aria-expanded': open ? 'true' : 'false',
            title: `${title} (${items.length}) — click to expand or collapse`,
        });
        toggleBtn.append(
            el('span', { class: 'ps-twist', 'aria-hidden': 'true', text: open ? '▾' : '▸' }),
            el('span', { class: 'git-section-title', text: title }),
        );
        toggleBtn.addEventListener('click', onToggle);

        const countBadge = el('span', { class: 'ps-file-count', text: String(items.length) });
        head.append(toggleBtn, countBadge);

        if (onBulk) {
            const bulkBtn = el('button', {
                type: 'button',
                class: 'ps-mini-btn',
                text: bulkLabel,
                disabled: busy,
                title: bulkTitle,
            });
            bulkBtn.addEventListener('click', (event) => {
                event.stopPropagation();
                void onBulk();
            });
            head.append(bulkBtn);
        }
        sec.append(head);

        if (open) {
            const list = el('div', { class: 'git-file-list' });
            if (!items.length) {
                list.append(el('p', { class: 'git-empty-note', text: emptyText }));
            } else {
                for (const item of items) {
                    const { name, dir } = splitFileLabel(item.path);
                    const row = el('div', { class: 'git-file-row' });
                    const openBtn = el('button', {
                        type: 'button',
                        class: 'git-file-main',
                        title: `${item.path} (${item.statusLabel || item.status}) — Click to open in editor`,
                    });
                    const statusBadge = el('span', {
                        class: `git-status-badge is-${String(item.status || 'M').toLowerCase()}`,
                        text: item.status || 'M',
                        title: item.statusLabel || item.status,
                    });
                    openBtn.append(
                        statusBadge,
                        el('span', { class: 'git-file-name', text: name }),
                        dir ? el('span', { class: 'git-file-dir', text: dir }) : null,
                    );
                    openBtn.addEventListener('click', () => {
                        if (item.status === 'D') {
                            void openDiffModal({
                                path: item.path,
                                staged: isStaged,
                                title: `${item.path} (${isStaged ? 'Staged' : 'Working Tree'})`,
                                subtitle: item.statusLabel || 'Deleted',
                            });
                            return;
                        }
                        onOpenFile?.(item.path);
                    });

                    const actions = el('div', { class: 'git-file-actions' });
                    const diffBtn = el('button', {
                        type: 'button',
                        class: 'ps-action-btn',
                        text: '±',
                        title: `View ${isStaged ? 'staged' : 'unstaged'} diff for ${item.path}`,
                        'aria-label': `View diff for ${name}`,
                    });
                    diffBtn.addEventListener('click', (event) => {
                        event.stopPropagation();
                        void openDiffModal({
                            path: item.path,
                            staged: isStaged,
                            title: `${item.path} (${isStaged ? 'Staged Diff' : 'Working Tree Diff'})`,
                            subtitle: item.statusLabel || '',
                        });
                    });
                    actions.append(diffBtn);

                    if (isStaged) {
                        const unstageBtn = el('button', {
                            type: 'button',
                            class: 'ps-action-btn',
                            text: '−',
                            disabled: busy,
                            title: `Unstage ${item.path}`,
                            'aria-label': `Unstage ${name}`,
                        });
                        unstageBtn.addEventListener('click', (event) => {
                            event.stopPropagation();
                            void runGitAction('/__studio/api/git/unstage', { path: item.path }, `Unstaged ${name}.`);
                        });
                        actions.append(unstageBtn);
                    } else {
                        const discardBtn = el('button', {
                            type: 'button',
                            class: 'ps-action-btn git-discard-btn',
                            text: '↺',
                            disabled: busy,
                            title: item.untracked
                                ? `Delete untracked file ${item.path}`
                                : `Discard uncommitted changes in ${item.path}`,
                            'aria-label': `Discard changes in ${name}`,
                        });
                        discardBtn.addEventListener('click', async (event) => {
                            event.stopPropagation();
                            const ok = await confirmDialog(
                                item.untracked ? 'Delete untracked file?' : 'Discard changes?',
                                item.untracked
                                    ? `Delete untracked file "${item.path}" from disk? This cannot be undone.`
                                    : `Discard uncommitted changes in "${item.path}" and restore it to HEAD?`,
                                item.untracked ? 'Delete File' : 'Discard Changes',
                            );
                            if (!ok) return;
                            await runGitAction(
                                '/__studio/api/git/discard',
                                { path: item.path },
                                `Discarded changes in ${name}.`,
                                [item.path],
                            );
                        });

                        const stageBtn = el('button', {
                            type: 'button',
                            class: 'ps-action-btn',
                            text: '+',
                            disabled: busy,
                            title: `Stage ${item.path}`,
                            'aria-label': `Stage ${name}`,
                        });
                        stageBtn.addEventListener('click', (event) => {
                            event.stopPropagation();
                            void runGitAction('/__studio/api/git/stage', { path: item.path }, `Staged ${name}.`);
                        });
                        actions.append(discardBtn, stageBtn);
                    }

                    row.append(openBtn, actions);
                    list.append(row);
                }
            }
            sec.append(list);
        }
        return sec;
    }

    async function runGitAction(endpoint, body, message, reloadPaths = []) {
        if (busy) return;
        busy = true;
        updateCommitButtonState();
        try {
            const data = await apiSend('POST', endpoint, body);
            applyStatus(data);
            if (message) announce?.(message);
            if (typeof onFilesChanged === 'function') {
                await onFilesChanged(reloadPaths);
            }
        } catch (error) {
            announce?.(error.message || 'Git operation failed.');
        } finally {
            busy = false;
            updateCommitButtonState();
        }
    }

    async function handleCommit() {
        const message = commitInput.value.trim();
        if (!message || busy || !statusData?.isRepo) return;
        const stagedCount = Number(statusData.stagedCount) || 0;
        const unstagedCount = Number(statusData.unstagedCount) || 0;
        const stageAll = stagedCount === 0 && unstagedCount > 0;
        busy = true;
        updateCommitButtonState();
        try {
            const data = await apiSend('POST', '/__studio/api/git/commit', {
                message,
                stageAll,
            });
            commitInput.value = '';
            applyStatus(data);
            announce?.(`Committed to ${data.branch || 'repository'}.`);
            if (typeof onFilesChanged === 'function') {
                await onFilesChanged([]);
            }
        } catch (error) {
            announce?.(error.message || 'Git commit failed.');
        } finally {
            busy = false;
            updateCommitButtonState();
        }
    }

    function ensureDiffDialog() {
        if (diffDialog) return diffDialog;
        diffDialog = el('dialog', { class: 'git-diff-dialog', 'aria-label': 'Git diff preview' });
        const head = el('div', { class: 'git-diff-head' });
        const titles = el('div', { class: 'git-diff-titles' });
        diffTitle = el('h2', { class: 'git-diff-title', text: 'Diff' });
        diffSub = el('p', { class: 'git-diff-sub', text: '' });
        titles.append(diffTitle, diffSub);

        const actions = el('div', { class: 'git-diff-actions' });
        diffOpenFileBtn = el('button', {
            type: 'button',
            class: 'btn',
            text: 'Open in Editor',
            hidden: true,
            title: 'Open this file in the code editor',
        });
        diffOpenFileBtn.addEventListener('click', () => {
            const target = diffTargetFile;
            diffDialog.close();
            if (target) onOpenFile?.(target);
        });
        const closeBtn = el('button', {
            type: 'button',
            class: 'btn',
            text: 'Close',
            title: 'Close diff preview (Esc)',
        });
        closeBtn.addEventListener('click', () => diffDialog.close());
        actions.append(diffOpenFileBtn, closeBtn);
        head.append(titles, actions);

        diffBody = el('pre', { class: 'git-diff-body' });
        diffDialog.append(head, diffBody);
        document.body.append(diffDialog);
        return diffDialog;
    }

    async function openDiffModal({ path = '', staged = false, commit = '', title = 'Git Diff', subtitle = '' }) {
        const dlg = ensureDiffDialog();
        diffTitle.textContent = title;
        diffSub.textContent = subtitle;
        diffTargetFile = path || '';
        diffOpenFileBtn.hidden = !path;
        diffBody.replaceChildren(el('span', { class: 'git-diff-ctx', text: 'Loading diff…' }));
        if (!dlg.open) dlg.showModal();

        try {
            const params = new URLSearchParams();
            if (commit) params.set('commit', commit);
            if (path) params.set('path', path);
            if (staged) params.set('staged', '1');
            const data = await apiSend('GET', `/__studio/api/git/diff?${params.toString()}`);
            renderUnifiedDiff(diffBody, data?.diff || '');
        } catch (error) {
            diffBody.replaceChildren(el('span', { class: 'git-diff-del', text: error.message || 'Could not load diff.' }));
        }
    }

    function renderUnifiedDiff(container, rawDiff) {
        container.replaceChildren();
        const text = String(rawDiff || '');
        if (!text.trim()) {
            container.append(el('span', { class: 'git-diff-ctx', text: 'No text differences.' }));
            return;
        }
        const lines = text.split('\n');
        for (const line of lines) {
            let cls = 'git-diff-ctx';
            if (line.startsWith('+++') || line.startsWith('---') || line.startsWith('diff --git')) {
                cls = 'git-diff-meta';
            } else if (line.startsWith('@@')) {
                cls = 'git-diff-hunk';
            } else if (line.startsWith('+')) {
                cls = 'git-diff-add';
            } else if (line.startsWith('-')) {
                cls = 'git-diff-del';
            }
            container.append(el('span', { class: `git-diff-line ${cls}`, text: `${line}\n` }));
        }
    }

    return {
        refresh,
        focusCommit() {
            commitInput.focus();
        },
    };
}
