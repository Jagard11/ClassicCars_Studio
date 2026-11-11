import { el } from './dom.js';
import { renderCommitGraphList } from './git-graph.js';

export function mountGitPanel(parent, {
    apiSend,
    confirmDialog,
    announce,
    onOpenFile,
    onFilesChanged,
    onStatusChange,
} = {}) {
    if (!parent) return { refresh: async () => {}, applyStatus() {}, focusCommit() {} };

    const COMMITS_OPEN_KEY = 'studio.gitCommitsOpen';
    const COMMITS_HEIGHT_KEY = 'studio.gitCommitsHeight';

    let statusData = null;
    let busy = false;
    let stagedOpen = true;
    let changesOpen = true;
    let commitsOpen = false;
    let sheetHeight = 0;
    let lastOpenHeight = 0;
    try {
        commitsOpen = localStorage.getItem(COMMITS_OPEN_KEY) === '1';
        const savedH = Number(localStorage.getItem(COMMITS_HEIGHT_KEY));
        if (Number.isFinite(savedH) && savedH > 40) {
            sheetHeight = Math.round(savedH);
            lastOpenHeight = sheetHeight;
        }
    } catch { /* private mode */ }
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

    /* Recent commits: bottom drawer. Collapsed to the handle until pulled up. */
    const commitsSheet = el('section', {
        class: 'git-commits-sheet',
        'aria-label': 'Recent commits',
        hidden: true,
    });
    const commitsTwist = el('span', { class: 'ps-twist', 'aria-hidden': 'true', text: '▸' });
    const commitsCount = el('span', { class: 'ps-file-count', text: '0' });
    const commitsHandleRow = el('span', { class: 'git-commits-handle-row' });
    commitsHandleRow.append(
        commitsTwist,
        el('span', { class: 'git-section-title', text: 'Recent Commits' }),
        commitsCount,
    );
    const commitsToggle = el('button', {
        type: 'button',
        class: 'git-commits-handle',
        'aria-expanded': 'false',
        'aria-controls': 'git-recent-commits',
        'aria-valuemin': '36',
        'aria-valuemax': '400',
        'aria-valuenow': '36',
        title: 'Drag to resize Recent Commits shelf height, or click to toggle',
    });
    commitsToggle.append(
        el('span', { class: 'git-commits-grip', 'aria-hidden': 'true' }),
        commitsHandleRow,
    );
    const commitsList = el('div', { id: 'git-recent-commits', class: 'git-commit-list' });
    commitsSheet.append(commitsToggle, commitsList);

    root.append(header, commitBox, bodyScroll, commitsSheet);
    parent.append(root);

    let sheetDrag = null;
    let suppressHandleClick = false;
    let sheetPeek = 36;
    let sheetMax = 400;
    let sheetDefault = 200;
    const COLLAPSE_THRESHOLD = 8;

    function persistCommitsSheetState() {
        try {
            localStorage.setItem(COMMITS_OPEN_KEY, commitsOpen ? '1' : '0');
            if (lastOpenHeight > sheetPeek + COLLAPSE_THRESHOLD) {
                localStorage.setItem(COMMITS_HEIGHT_KEY, String(Math.round(lastOpenHeight)));
            }
        } catch { /* private mode */ }
    }

    function syncCommitsChrome(currentPx = sheetHeight || sheetPeek) {
        commitsSheet.classList.toggle('is-open', commitsOpen);
        commitsToggle.setAttribute('aria-expanded', commitsOpen ? 'true' : 'false');
        commitsToggle.setAttribute('aria-valuenow', String(Math.round(currentPx)));
        commitsTwist.textContent = commitsOpen ? '▾' : '▸';
        commitsToggle.title = commitsOpen
            ? 'Drag to resize Recent Commits height, or click to collapse'
            : 'Drag up to resize Recent Commits height, or click to expand';
        commitsList.inert = !commitsOpen;
    }

    function setCommitsSheetVisible(visible) {
        commitsSheet.hidden = !visible;
    }

    function updateSheetMetrics() {
        if (commitsSheet.hidden || root.clientHeight <= 0) return false;
        const peek = commitsToggle.offsetHeight || 36;
        if (peek <= 0) return false;
        const headerH = header.offsetHeight || 32;
        const max = Math.max(peek + 48, root.clientHeight - headerH - 36);
        const defaultOpen = Math.max(peek + 64, Math.min(max, Math.round(root.clientHeight * 0.45)));
        sheetPeek = peek;
        sheetMax = max;
        sheetDefault = defaultOpen;
        commitsToggle.setAttribute('aria-valuemin', String(sheetPeek));
        commitsToggle.setAttribute('aria-valuemax', String(sheetMax));
        return true;
    }

    function setSheetHeight(px, animate) {
        const rounded = Math.round(px);
        const height = `${rounded}px`;
        commitsToggle.setAttribute('aria-valuenow', String(rounded));
        if (animate) {
            commitsSheet.classList.remove('is-dragging');
            commitsSheet.classList.add('is-animating');
            void commitsSheet.offsetHeight;
            commitsSheet.style.height = height;
            return;
        }
        commitsSheet.classList.remove('is-animating');
        commitsSheet.classList.add('is-dragging');
        commitsSheet.style.height = height;
        void commitsSheet.offsetHeight;
        commitsSheet.classList.remove('is-dragging');
    }

    function layoutCommitsSheet(animate) {
        if (sheetDrag || commitsSheet.hidden) {
            syncCommitsChrome(sheetDrag ? sheetDrag.height : (sheetHeight || sheetPeek));
            return;
        }
        if (!updateSheetMetrics()) {
            syncCommitsChrome(sheetHeight || sheetPeek);
            return;
        }
        if (!commitsOpen) {
            sheetHeight = sheetPeek;
            syncCommitsChrome(sheetPeek);
            setSheetHeight(sheetPeek, animate);
            return;
        }
        const desired = sheetHeight > sheetPeek + COLLAPSE_THRESHOLD
            ? sheetHeight
            : (lastOpenHeight > sheetPeek + COLLAPSE_THRESHOLD ? lastOpenHeight : sheetDefault);
        const clamped = Math.min(sheetMax, Math.max(sheetPeek + COLLAPSE_THRESHOLD + 1, desired));
        sheetHeight = clamped;
        lastOpenHeight = clamped;
        syncCommitsChrome(clamped);
        setSheetHeight(clamped, animate);
    }

    function renderCommits(commits) {
        commitsCount.textContent = String(commits.length);
        renderCommitGraphList(commitsList, commits, {
            defaultBranch: statusData?.branch || '',
            onSelectCommit(c) {
                void openDiffModal({
                    commit: c.hash || c.shortHash,
                    title: `Commit ${c.shortHash}: ${c.subject}`,
                    subtitle: `${c.author} · ${c.relativeDate}`,
                });
            },
        });
        setCommitsSheetVisible(true);
        layoutCommitsSheet(false);
    }

    function commitDraggedHeight(finalPx) {
        const clamped = Math.min(sheetMax, Math.max(sheetPeek, Math.round(finalPx)));
        if (clamped <= sheetPeek + COLLAPSE_THRESHOLD) {
            commitsOpen = false;
            sheetHeight = sheetPeek;
            syncCommitsChrome(sheetPeek);
            setSheetHeight(sheetPeek, false);
        } else {
            commitsOpen = true;
            sheetHeight = clamped;
            lastOpenHeight = clamped;
            syncCommitsChrome(clamped);
            setSheetHeight(clamped, false);
        }
        persistCommitsSheetState();
    }

    commitsToggle.addEventListener('pointerdown', (event) => {
        if (event.button !== 0 || commitsSheet.hidden) return;
        updateSheetMetrics();
        const startHeight = commitsSheet.offsetHeight || (commitsOpen ? (sheetHeight || sheetDefault) : sheetPeek);
        sheetDrag = {
            pointerId: event.pointerId,
            startY: event.clientY,
            startHeight,
            height: startHeight,
            startedOpen: commitsOpen,
            moved: false,
        };
        document.documentElement.classList.add('is-resizing', 'is-resizing-row');
        commitsToggle.setPointerCapture(event.pointerId);
    });

    commitsToggle.addEventListener('pointermove', (event) => {
        if (!sheetDrag || event.pointerId !== sheetDrag.pointerId) return;
        const dy = event.clientY - sheetDrag.startY;
        if (Math.abs(dy) > 2) sheetDrag.moved = true;
        if (!sheetDrag.moved) return;
        updateSheetMetrics();
        const next = Math.min(sheetMax, Math.max(sheetPeek, Math.round(sheetDrag.startHeight - dy)));
        sheetDrag.height = next;
        commitsOpen = next > sheetPeek + COLLAPSE_THRESHOLD;
        sheetHeight = next;
        if (commitsOpen) lastOpenHeight = next;
        commitsSheet.classList.add('is-dragging');
        commitsSheet.classList.remove('is-animating');
        commitsSheet.style.height = `${next}px`;
        syncCommitsChrome(next);
    });

    commitsToggle.addEventListener('pointerup', (event) => {
        if (!sheetDrag || event.pointerId !== sheetDrag.pointerId) return;
        const drag = sheetDrag;
        sheetDrag = null;
        document.documentElement.classList.remove('is-resizing', 'is-resizing-row');
        if (commitsToggle.hasPointerCapture(event.pointerId)) {
            commitsToggle.releasePointerCapture(event.pointerId);
        }
        if (!drag.moved) return;
        suppressHandleClick = true;
        setTimeout(() => { suppressHandleClick = false; }, 0);
        commitDraggedHeight(drag.height);
    });

    commitsToggle.addEventListener('pointercancel', (event) => {
        if (!sheetDrag || event.pointerId !== sheetDrag.pointerId) return;
        const drag = sheetDrag;
        sheetDrag = null;
        document.documentElement.classList.remove('is-resizing', 'is-resizing-row');
        commitDraggedHeight(drag.height);
    });

    commitsToggle.addEventListener('click', () => {
        if (suppressHandleClick) {
            suppressHandleClick = false;
            return;
        }
        updateSheetMetrics();
        if (commitsOpen) {
            if (sheetHeight > sheetPeek + COLLAPSE_THRESHOLD) {
                lastOpenHeight = sheetHeight;
            }
            commitsOpen = false;
            sheetHeight = sheetPeek;
        } else {
            commitsOpen = true;
            sheetHeight = lastOpenHeight > sheetPeek + COLLAPSE_THRESHOLD ? lastOpenHeight : sheetDefault;
            lastOpenHeight = sheetHeight;
        }
        persistCommitsSheetState();
        layoutCommitsSheet(true);
    });

    commitsToggle.addEventListener('keydown', (event) => {
        if (event.key !== 'ArrowUp' && event.key !== 'ArrowDown') return;
        event.preventDefault();
        updateSheetMetrics();
        const step = event.shiftKey ? 48 : 16;
        const current = commitsOpen ? (sheetHeight || commitsSheet.offsetHeight || sheetDefault) : sheetPeek;
        const next = current + (event.key === 'ArrowUp' ? step : -step);
        commitDraggedHeight(next);
    });

    const commitsSheetObserver = new ResizeObserver(() => {
        if (sheetDrag || commitsSheet.hidden) return;
        layoutCommitsSheet(false);
    });
    commitsSheetObserver.observe(root);

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
            setCommitsSheetVisible(false);
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
            setCommitsSheetVisible(false);
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

        renderCommits(commits);
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
        applyStatus,
        focusCommit() {
            commitInput.focus();
        },
    };
}
