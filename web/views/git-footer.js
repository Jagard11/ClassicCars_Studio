import { el } from './dom.js';

const DEFAULT_INTERVAL_SEC = 180;

export function mountGitFooter(parent, {
    apiSend,
    announce,
    onOpenGitPanel,
    onStatusUpdated,
    onFilesChanged,
} = {}) {
    if (!parent) {
        return {
            applyStatus() {},
            applyAppConfig() {},
            getAppConfig: () => ({ gitAutoFetch: false, gitAutoFetchPrompted: false, gitAutoFetchIntervalSec: DEFAULT_INTERVAL_SEC }),
            openPeriodicCheckModal() {},
            refresh: async () => {},
        };
    }

    let statusData = null;
    let busyAction = '';
    let pollTimer = 0;
    let modal = null;
    let modalDesc = null;
    let modalIntervalSelect = null;
    let modalRememberCheck = null;
    let modalEnableBtn = null;
    let modalDisableBtn = null;
    let modalCancelBtn = null;
    let pendingModalAction = null;

    const appConfig = {
        gitAutoFetch: false,
        gitAutoFetchPrompted: false,
        gitAutoFetchIntervalSec: DEFAULT_INTERVAL_SEC,
    };

    parent.classList.add('git-footer');
    parent.replaceChildren();

    /* One window-footer row. Order matches the previous shelf footer: repo, branch, pull, push, refresh, auto. */
    const row = el('div', { class: 'git-footer-row' });

    const repoBtn = el('button', {
        type: 'button',
        class: 'git-footer-chip git-footer-repo',
        id: 'git-footer-repo',
        'data-git-footer': 'repo',
        title: 'Git repository — click to open Git version control panel (Ctrl+Shift+G)',
    });
    const repoIcon = el('span', { class: 'git-footer-repo-icon', 'aria-hidden': 'true', text: '◈' });
    const repoNameEl = el('span', { class: 'git-footer-repo-name', id: 'git-footer-repo-name', text: 'Repository' });
    repoBtn.append(repoIcon, repoNameEl);

    const branchBtn = el('button', {
        type: 'button',
        class: 'git-footer-chip git-footer-branch',
        id: 'git-footer-branch',
        'data-git-footer': 'branch',
        title: 'Current Git branch — click to open Git version control panel (Ctrl+Shift+G)',
    });
    const branchIcon = el('span', { class: 'git-footer-branch-icon', 'aria-hidden': 'true', text: '⎇' });
    const branchNameEl = el('span', { class: 'git-footer-branch-name', id: 'git-footer-branch-name', text: '—' });
    branchBtn.append(branchIcon, branchNameEl);

    const pullBtn = el('button', {
        type: 'button',
        class: 'git-footer-widget is-pull',
        id: 'git-footer-pull',
        'data-git-footer': 'pull',
        'data-pending': 'false',
        title: 'Pending Git pulls (incoming commits) — click to pull from remote',
    });
    const pullArrow = el('span', { class: 'git-footer-arrow', 'aria-hidden': 'true', text: '↓' });
    const pullCountEl = el('span', { class: 'git-footer-count', id: 'git-footer-pull-count', text: '0' });
    const pullLabelEl = el('span', { class: 'git-footer-wlabel', text: 'Pull' });
    pullBtn.append(pullArrow, pullCountEl, pullLabelEl);

    const pushBtn = el('button', {
        type: 'button',
        class: 'git-footer-widget is-push',
        id: 'git-footer-push',
        'data-git-footer': 'push',
        'data-pending': 'false',
        title: 'Pending Git pushes (outgoing commits) — click to push to remote',
    });
    const pushArrow = el('span', { class: 'git-footer-arrow', 'aria-hidden': 'true', text: '↑' });
    const pushCountEl = el('span', { class: 'git-footer-count', id: 'git-footer-push-count', text: '0' });
    const pushLabelEl = el('span', { class: 'git-footer-wlabel', text: 'Push' });
    pushBtn.append(pushArrow, pushCountEl, pushLabelEl);

    const actionsGroup = el('div', { class: 'git-footer-actions' });

    const fetchBtn = el('button', {
        type: 'button',
        class: 'git-footer-icon-btn',
        id: 'git-footer-fetch',
        'data-git-footer': 'fetch',
        text: '↻',
        'aria-label': 'Fetch remote Git updates now',
        title: 'Ping remote repository now (git fetch) to check for pending pulls and pushes',
    });

    const autoCfgBtn = el('button', {
        type: 'button',
        class: 'git-footer-icon-btn git-footer-auto-btn',
        id: 'git-footer-auto-cfg',
        'data-git-footer': 'auto-config',
        'data-auto-fetch': 'off',
        'aria-label': 'Configure periodic Git remote checks',
        title: 'Periodic Git remote check: Disabled (default) — click to configure app-wide setting',
    });
    const autoDot = el('span', { class: 'git-footer-auto-dot', 'aria-hidden': 'true' });
    const autoLabel = el('span', { class: 'git-footer-auto-text', text: 'Auto' });
    autoCfgBtn.append(autoDot, autoLabel);

    actionsGroup.append(fetchBtn, autoCfgBtn);
    row.append(repoBtn, branchBtn, pullBtn, pushBtn, actionsGroup);

    parent.append(row);

    repoBtn.addEventListener('click', () => {
        onOpenGitPanel?.();
    });
    branchBtn.addEventListener('click', () => {
        onOpenGitPanel?.();
    });

    pullBtn.addEventListener('click', () => {
        if (busyAction) return;
        handleSyncWidgetClick('pull');
    });

    pushBtn.addEventListener('click', () => {
        if (busyAction) return;
        handleSyncWidgetClick('push');
    });

    fetchBtn.addEventListener('click', () => {
        if (busyAction) return;
        void runFetch({ background: false });
    });

    autoCfgBtn.addEventListener('click', () => {
        openPeriodicCheckModal({ pendingAction: null });
    });

    function handleSyncWidgetClick(action) {
        if (!statusData || !statusData.isRepo) {
            announce?.('Current folder is not a Git repository.');
            return;
        }
        if (!appConfig.gitAutoFetchPrompted) {
            openPeriodicCheckModal({ pendingAction: action });
            return;
        }
        void executeSyncAction(action);
    }

    function ensureModal() {
        if (modal) return modal;
        modal = el('dialog', {
            class: 'git-sync-modal',
            id: 'git-periodic-modal',
            'aria-labelledby': 'git-periodic-modal-title',
        });

        const head = el('div', { class: 'dialog-head git-sync-modal-head' });
        const title = el('h2', {
            id: 'git-periodic-modal-title',
            text: 'Periodic Git Remote Checks',
        });
        const closeX = el('button', {
            type: 'button',
            class: 'btn',
            text: 'Close',
            title: 'Close dialog (Esc)',
        });
        closeX.addEventListener('click', () => {
            pendingModalAction = null;
            modal.close();
        });
        head.append(title, closeX);

        const body = el('div', { class: 'dialog-body git-sync-modal-body' });
        modalDesc = el('p', {
            class: 'git-sync-modal-desc',
            text: 'Would you like ClassicCars Studio to periodically ping the remote repository in the background to check for pending pulls and pushes?',
        });

        const scopeNote = el('p', {
            class: 'git-sync-modal-note',
            text: 'Saved in the app-wide config (state/app-config.json) rather than project config, so your preference applies across all projects.',
        });

        const intervalRow = el('label', { class: 'git-sync-modal-field' });
        intervalRow.append(el('span', { class: 'git-sync-modal-label', text: 'Ping frequency when enabled:' }));
        modalIntervalSelect = el('select', {
            class: 'git-sync-modal-select',
            id: 'git-periodic-interval',
            'aria-label': 'Periodic check interval',
        });
        for (const [sec, label] of [
            [60, 'Every 1 minute'],
            [180, 'Every 3 minutes (Recommended)'],
            [300, 'Every 5 minutes'],
            [600, 'Every 10 minutes'],
        ]) {
            modalIntervalSelect.append(el('option', { value: String(sec), text: label }));
        }
        intervalRow.append(modalIntervalSelect);

        const rememberRow = el('label', { class: 'git-sync-modal-check' });
        modalRememberCheck = el('input', {
            type: 'checkbox',
            id: 'git-periodic-remember',
            checked: true,
        });
        rememberRow.append(
            modalRememberCheck,
            el('span', { text: 'Remember my choice in app-wide config and do not ask again when clicking Pull or Push' }),
        );

        const foot = el('div', { class: 'git-sync-modal-actions' });
        modalEnableBtn = el('button', {
            type: 'button',
            class: 'btn btn-primary',
            id: 'git-periodic-enable-btn',
            text: 'Enable Periodic Checks',
        });
        modalDisableBtn = el('button', {
            type: 'button',
            class: 'btn',
            id: 'git-periodic-disable-btn',
            text: 'Keep Disabled (Default)',
        });
        modalCancelBtn = el('button', {
            type: 'button',
            class: 'btn',
            id: 'git-periodic-cancel-btn',
            text: 'Cancel',
        });

        modalEnableBtn.addEventListener('click', () => {
            void submitModalChoice(true);
        });
        modalDisableBtn.addEventListener('click', () => {
            void submitModalChoice(false);
        });
        modalCancelBtn.addEventListener('click', () => {
            pendingModalAction = null;
            modal.close();
        });

        foot.append(modalEnableBtn, modalDisableBtn, modalCancelBtn);
        body.append(modalDesc, scopeNote, intervalRow, rememberRow, foot);
        modal.append(head, body);
        document.body.append(modal);
        return modal;
    }

    function openPeriodicCheckModal({ pendingAction = null } = {}) {
        const dlg = ensureModal();
        pendingModalAction = pendingAction;
        modalIntervalSelect.value = String(appConfig.gitAutoFetchIntervalSec || DEFAULT_INTERVAL_SEC);
        if (![...modalIntervalSelect.options].some((o) => o.value === modalIntervalSelect.value)) {
            modalIntervalSelect.value = String(DEFAULT_INTERVAL_SEC);
        }
        modalRememberCheck.checked = true;

        if (pendingAction === 'pull' || pendingAction === 'push') {
            const verb = pendingAction === 'pull' ? 'Pull' : 'Push';
            modalDesc.textContent =
                `You clicked ${verb}. Would you like ClassicCars Studio to periodically ping the remote repository in the background to check for pending pulls and pushes?`;
            modalEnableBtn.textContent = `Enable Periodic Checks & ${verb}`;
            modalDisableBtn.textContent = `Keep Disabled & ${verb}`;
        } else {
            const curState = appConfig.gitAutoFetch ? 'currently ENABLED' : 'currently DISABLED (default)';
            modalDesc.textContent =
                `Configure whether ClassicCars Studio periodically pings the remote Git repository for pending pulls and pushes (${curState}).`;
            modalEnableBtn.textContent = 'Enable Periodic Checks';
            modalDisableBtn.textContent = 'Keep Disabled (Manual Only)';
        }

        if (!dlg.open) dlg.showModal();
        modalEnableBtn.focus();
    }

    async function submitModalChoice(enableAutoFetch) {
        const actionToRun = pendingModalAction;
        pendingModalAction = null;
        const intervalSec = Number(modalIntervalSelect?.value) || DEFAULT_INTERVAL_SEC;
        const remember = Boolean(modalRememberCheck?.checked);

        if (modal?.open) modal.close();

        await saveAppConfig({
            gitAutoFetch: Boolean(enableAutoFetch),
            gitAutoFetchPrompted: remember,
            gitAutoFetchIntervalSec: intervalSec,
        });

        if (enableAutoFetch) {
            announce?.(`Enabled periodic Git remote checks (every ${Math.round(intervalSec / 60)}m) in app-wide config.`);
        } else {
            announce?.('Periodic Git remote checks kept disabled in app-wide config.');
        }

        if (actionToRun === 'pull' || actionToRun === 'push') {
            await executeSyncAction(actionToRun);
        } else if (enableAutoFetch) {
            void runFetch({ background: true });
        }
    }

    async function saveAppConfig(patch) {
        applyAppConfig(patch);
        if (typeof apiSend !== 'function') return;
        try {
            const res = await apiSend('POST', '/__studio/api/app-config', {
                gitAutoFetch: appConfig.gitAutoFetch,
                gitAutoFetchPrompted: appConfig.gitAutoFetchPrompted,
                gitAutoFetchIntervalSec: appConfig.gitAutoFetchIntervalSec,
            });
            if (res?.appConfig) {
                applyAppConfig(res.appConfig);
            }
        } catch (error) {
            announce?.(error.message || 'Could not save app-wide config.');
        }
    }

    function applyAppConfig(raw) {
        if (!raw || typeof raw !== 'object') return;
        const src = raw.appConfig && typeof raw.appConfig === 'object' ? raw.appConfig : raw;
        if (typeof src.gitAutoFetch === 'boolean') {
            appConfig.gitAutoFetch = src.gitAutoFetch;
        }
        if (typeof src.gitAutoFetchPrompted === 'boolean') {
            appConfig.gitAutoFetchPrompted = src.gitAutoFetchPrompted;
        }
        if (typeof src.gitAutoFetchIntervalSec === 'number' && Number.isFinite(src.gitAutoFetchIntervalSec)) {
            appConfig.gitAutoFetchIntervalSec = Math.max(30, Math.min(3600, Math.round(src.gitAutoFetchIntervalSec)));
        }
        updateAutoFetchUi();
        restartPollTimer();
    }

    function updateAutoFetchUi() {
        const on = Boolean(appConfig.gitAutoFetch);
        autoCfgBtn.dataset.autoFetch = on ? 'on' : 'off';
        const mins = Math.max(1, Math.round((appConfig.gitAutoFetchIntervalSec || DEFAULT_INTERVAL_SEC) / 60));
        autoCfgBtn.title = on
            ? `Periodic Git remote check: Enabled (every ${mins}m) — click to configure app-wide setting`
            : 'Periodic Git remote check: Disabled by default — click to configure app-wide setting';
    }

    function restartPollTimer() {
        window.clearInterval(pollTimer);
        pollTimer = 0;
        if (!appConfig.gitAutoFetch) return;
        const intervalMs = Math.max(30000, (appConfig.gitAutoFetchIntervalSec || DEFAULT_INTERVAL_SEC) * 1000);
        pollTimer = window.setInterval(() => {
            if (document.hidden || busyAction || !statusData?.isRepo) return;
            void runFetch({ background: true });
        }, intervalMs);
    }

    function setBusy(action) {
        busyAction = action || '';
        const isBusy = Boolean(busyAction);
        pullBtn.disabled = isBusy || !statusData?.isRepo;
        pushBtn.disabled = isBusy || !statusData?.isRepo;
        fetchBtn.disabled = isBusy || !statusData?.isRepo;
        fetchBtn.classList.toggle('is-spinning', isBusy);
        pullBtn.classList.toggle('is-busy', busyAction === 'pull');
        pushBtn.classList.toggle('is-busy', busyAction === 'push');
    }

    async function runFetch({ background = false } = {}) {
        if (busyAction || typeof apiSend !== 'function') return;
        setBusy('fetch');
        try {
            const data = await apiSend('POST', '/__studio/api/git/fetch', { background });
            applyStatus(data);
            onStatusUpdated?.(data);
            if (!background) {
                if (data?.noRemote) {
                    announce?.('No Git remote is configured for this repository.');
                } else {
                    const behind = Number(data?.behind) || 0;
                    const ahead = Number(data?.ahead) || 0;
                    announce?.(`Fetched from remote (${behind} to pull, ${ahead} to push).`);
                }
            }
        } catch (error) {
            if (!background) {
                announce?.(error.message || 'Git fetch failed.');
            }
        } finally {
            setBusy('');
        }
    }

    async function executeSyncAction(action) {
        if (busyAction || typeof apiSend !== 'function') return;
        const isPull = action === 'pull';
        setBusy(action);
        announce?.(isPull ? 'Pulling from Git remote…' : 'Pushing commits to Git remote…');
        try {
            const endpoint = isPull ? '/__studio/api/git/pull' : '/__studio/api/git/push';
            const data = await apiSend('POST', endpoint, {});
            applyStatus(data);
            onStatusUpdated?.(data);
            if (isPull) {
                announce?.('Git pull completed.');
                if (typeof onFilesChanged === 'function') {
                    await onFilesChanged([]);
                }
            } else {
                announce?.('Git push completed.');
            }
        } catch (error) {
            announce?.(error.message || `Git ${action} failed.`);
        } finally {
            setBusy('');
        }
    }

    function applyStatus(data) {
        statusData = data && typeof data === 'object' ? data : null;
        render();
    }

    function render() {
        if (!statusData || !statusData.isRepo) {
            repoNameEl.textContent = statusData?.repoName || 'No Git Repo';
            branchNameEl.textContent = '—';
            pullCountEl.textContent = '0';
            pushCountEl.textContent = '0';
            pullBtn.dataset.pending = 'false';
            pushBtn.dataset.pending = 'false';
            pullBtn.disabled = true;
            pushBtn.disabled = true;
            fetchBtn.disabled = true;
            parent.dataset.isRepo = 'false';
            return;
        }

        parent.dataset.isRepo = 'true';
        const repoName = statusData.repoName || 'Repository';
        const branch = statusData.branch || 'HEAD';
        const upstream = statusData.upstream || '';
        const remoteUrl = statusData.remoteUrl || '';
        const behind = Math.max(0, Number(statusData.behind) || 0);
        const ahead = Math.max(0, Number(statusData.ahead) || 0);

        repoNameEl.textContent = repoName;
        repoBtn.title = remoteUrl
            ? `Repository: ${repoName}\nRemote: ${remoteUrl}\nClick to open Git panel`
            : `Repository: ${repoName} — Click to open Git panel`;

        branchNameEl.textContent = branch;
        branchBtn.title = upstream
            ? `Branch: ${branch} (tracking ${upstream}) — Click to open Git panel`
            : `Branch: ${branch} — Click to open Git panel`;

        pullCountEl.textContent = String(behind);
        pullBtn.dataset.pending = behind > 0 ? 'true' : 'false';
        pullBtn.title = behind > 0
            ? `${behind} pending pull${behind === 1 ? '' : 's'} from ${upstream || 'remote'} — Click to pull`
            : `0 pending pulls from ${upstream || 'remote'} — Click to pull`;

        pushCountEl.textContent = String(ahead);
        pushBtn.dataset.pending = ahead > 0 ? 'true' : 'false';
        pushBtn.title = ahead > 0
            ? `${ahead} pending push${ahead === 1 ? '' : 'es'} to ${upstream || 'remote'} — Click to push`
            : `0 pending pushes to ${upstream || 'remote'} — Click to push`;

        const isBusy = Boolean(busyAction);
        pullBtn.disabled = isBusy;
        pushBtn.disabled = isBusy;
        fetchBtn.disabled = isBusy;
    }

    async function refresh() {
        if (typeof apiSend !== 'function') return;
        try {
            const data = await apiSend('GET', '/__studio/api/git/status');
            applyStatus(data);
        } catch {
            /* ignore offline during startup */
        }
    }

    updateAutoFetchUi();
    render();

    return {
        applyStatus,
        applyAppConfig,
        getAppConfig: () => ({ ...appConfig }),
        openPeriodicCheckModal,
        refresh,
    };
}
