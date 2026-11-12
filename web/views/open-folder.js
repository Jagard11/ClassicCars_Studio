import { el } from './dom.js';

const RECENT_KEY = 'studio.recentRoots';
const RECENT_LIMIT = 8;

function ensureStyles() {
    if (document.getElementById('open-folder-styles')) return;
    const style = document.createElement('style');
    style.id = 'open-folder-styles';
    style.textContent = `
.open-folder-dialog {
    width: min(720px, calc(100vw - 24px));
    max-height: calc(100vh - 24px);
    overflow: auto;
}
.open-folder-current {
    margin: 0 0 12px;
    color: var(--text-muted);
    font-size: 13px;
    overflow-wrap: anywhere;
}
.open-folder-current strong { color: var(--text-primary); font-weight: 650; }
.open-folder-path {
    display: flex;
    gap: 8px;
    align-items: center;
    margin-bottom: 12px;
}
.open-folder-path input {
    flex: 1 1 auto;
    min-width: 0;
    height: 36px;
    padding: 0 10px;
}
.open-folder-crumbs {
    display: flex;
    flex-wrap: wrap;
    gap: 4px;
    margin: 0 0 8px;
}
.open-folder-crumbs button {
    border: 0;
    background: transparent;
    color: var(--link, var(--brand-heading));
    padding: 0 2px;
    cursor: pointer;
}
.open-folder-list {
    border: var(--border-width) solid var(--border-subtle);
    border-radius: var(--radius);
    max-height: 280px;
    overflow: auto;
    margin-bottom: 12px;
}
.open-folder-row {
    display: flex;
    width: 100%;
    justify-content: space-between;
    gap: 12px;
    align-items: center;
    text-align: left;
    border: 0;
    border-bottom: var(--border-width) solid var(--border-subtle);
    background: transparent;
    color: inherit;
    padding: 8px 10px;
    cursor: pointer;
}
.open-folder-row:last-child { border-bottom: 0; }
.open-folder-row[aria-selected="true"],
.open-folder-row:hover { background: var(--bg-hover); }
.open-folder-name { overflow-wrap: anywhere; }
.open-folder-badge {
    flex: 0 0 auto;
    color: var(--text-muted);
    font-size: 11px;
    letter-spacing: 0.04em;
    text-transform: uppercase;
}
.open-folder-recent { display: flex; flex-wrap: wrap; gap: 6px; margin: 0 0 12px; }
.open-folder-recent button { max-width: 100%; overflow-wrap: anywhere; }
.open-folder-actions { display: flex; flex-wrap: wrap; gap: 8px; justify-content: flex-end; }
.open-folder-options { margin: 0 0 12px; }
`;
    document.head.append(style);
}

function readRecent() {
    try {
        const parsed = JSON.parse(localStorage.getItem(RECENT_KEY) || '[]');
        if (!Array.isArray(parsed)) return [];
        const items = [];
        for (const item of parsed) {
            if (typeof item !== 'string' || !item.trim()) continue;
            if (items.includes(item)) continue;
            items.push(item);
            if (items.length === RECENT_LIMIT) break;
        }
        return items;
    } catch {
        return [];
    }
}

function rememberRecent(path) {
    const text = String(path || '').trim();
    if (!text) return;
    const next = [text, ...readRecent().filter((item) => item !== text)].slice(0, RECENT_LIMIT);
    try { localStorage.setItem(RECENT_KEY, JSON.stringify(next)); } catch { /* private mode */ }
}

function crumbsFor(path) {
    const text = String(path || '').replace(/\\/g, '/').replace(/\/+$/, '');
    if (!text) return [];
    const drive = text.match(/^([A-Za-z]:)(\/.*)?$/);
    if (drive) {
        const items = [{ name: drive[1], path: `${drive[1]}/` }];
        let acc = drive[1];
        for (const part of (drive[2] || '').split('/').filter(Boolean)) {
            acc += `/${part}`;
            items.push({ name: part, path: acc });
        }
        return items;
    }
    if (!text.startsWith('/')) return [{ name: text, path: text }];
    const items = [{ name: '/', path: '/' }];
    let acc = '';
    for (const part of text.split('/').filter(Boolean)) {
        acc += `/${part}`;
        items.push({ name: part, path: acc });
    }
    return items;
}

export function mountOpenFolder({ apiSend, getRoot, confirmDiscard, onOpened }) {
    ensureStyles();
    const dialog = el('dialog', { class: 'open-folder-dialog', 'aria-labelledby': 'open-folder-title' });
    const head = el('div', { class: 'dialog-head' });
    const close = el('button', { class: 'btn', type: 'button', text: 'Close', title: 'Close the folder picker' });
    head.append(el('h2', { id: 'open-folder-title', text: 'Open Project Folder' }), close);
    const current = el('p', { class: 'open-folder-current' });
    const pathRow = el('div', { class: 'open-folder-path' });
    const input = el('input', {
        type: 'text',
        spellcheck: 'false',
        autocomplete: 'off',
        'aria-label': 'Folder path',
        placeholder: '/path/to/project',
        title: 'Type a folder path and press Go',
    });
    const up = el('button', { class: 'btn', type: 'button', text: '↑ Up', title: 'Go to parent folder' });
    const go = el('button', { class: 'btn', type: 'button', text: 'Go', title: 'Browse this folder path' });
    pathRow.append(input, up, go);
    const recentWrap = el('div', { class: 'open-folder-recent', hidden: true });
    const hiddenToggle = el('label', { class: 'open-folder-options field-hint' });
    const hiddenBox = el('input', { type: 'checkbox', checked: 'checked', title: 'Show hidden folders in the listing' });
    hiddenBox.checked = true;
    hiddenToggle.append(hiddenBox, ' Show hidden folders');
    const crumbs = el('div', { class: 'open-folder-crumbs' });
    const list = el('div', { class: 'open-folder-list', role: 'listbox', 'aria-label': 'Folders' });
    const hint = el('p', { class: 'field-hint', text: '' });
    const error = el('p', { class: 'field-error', hidden: true, role: 'alert' });
    const actions = el('div', { class: 'open-folder-actions' });
    const pick = el('button', { class: 'btn', type: 'button', text: 'System Folder Picker', title: 'Choose a folder with the system file picker' });
    const open = el('button', { class: 'btn btn-primary', type: 'button', text: 'Set as Project Folder', title: 'Use this folder as the Studio project root' });
    actions.append(pick, open);
    const body = el('div', { class: 'dialog-body' });
    body.append(current, pathRow, recentWrap, hiddenToggle, crumbs, list, hint, error, actions);
    dialog.append(head, body);
    document.body.append(dialog);

    let listing = '';
    let parentPath = '';
    let busy = false;

    function showError(message) {
        error.hidden = !message;
        error.textContent = message || '';
    }

    function setBusy(on, label) {
        busy = on;
        up.disabled = on || !parentPath;
        go.disabled = on;
        pick.disabled = on;
        open.disabled = on;
        input.disabled = on;
        if (on && label) open.textContent = label;
        else open.textContent = 'Set as Project Folder';
    }

    function renderRecent() {
        const items = readRecent();
        recentWrap.replaceChildren();
        recentWrap.hidden = items.length === 0;
        if (!items.length) return;
        recentWrap.append(el('span', { class: 'field-hint', text: 'Recent' }));
        for (const path of items) {
            const button = el('button', { class: 'btn', type: 'button', text: path, title: `Browse recent project ${path}` });
            button.addEventListener('click', () => { void browse(path); });
            recentWrap.append(button);
        }
    }

    function renderCrumbs(path) {
        crumbs.replaceChildren();
        for (const crumb of crumbsFor(path)) {
            const button = el('button', { type: 'button', text: crumb.name, title: `Browse ${crumb.path}` });
            button.addEventListener('click', () => { void browse(crumb.path); });
            crumbs.append(button);
        }
    }

    function renderEntries(data) {
        list.replaceChildren();
        const entries = Array.isArray(data?.entries) ? data.entries : [];
        if (!entries.length) {
            list.append(el('p', { class: 'field-hint', text: 'This folder has no subfolders.' }));
            return;
        }
        const selected = input.value;
        for (const entry of entries) {
            const name = entry.parent ? '.. (Parent Directory)' : String(entry.name || '');
            const button = el('button', {
                class: 'open-folder-row',
                type: 'button',
                role: 'option',
                'aria-selected': entry.path === selected ? 'true' : 'false',
                title: entry.parent
                    ? `Go to parent folder ${entry.path || ''}`
                    : `Select ${entry.path || name}. Double-click to open it.`,
            });
            button.append(el('span', { class: 'open-folder-name', text: name }));
            if (entry.website) {
                button.append(el('span', {
                    class: 'open-folder-badge',
                    text: 'Website',
                    title: 'This folder looks like a website',
                }));
            }
            button.addEventListener('click', () => {
                if (entry.parent) {
                    void browse(entry.path);
                    return;
                }
                input.value = entry.path || '';
                for (const row of list.querySelectorAll('.open-folder-row')) {
                    row.setAttribute('aria-selected', row === button ? 'true' : 'false');
                }
            });
            button.addEventListener('dblclick', (event) => {
                event.preventDefault();
                void browse(entry.path);
            });
            list.append(button);
        }
    }

    async function browse(path) {
        const target = String(path ?? input.value ?? '').trim();
        showError('');
        const params = new URLSearchParams();
        if (target) params.set('path', target);
        if (hiddenBox.checked) params.set('hidden', '1');
        try {
            const data = await apiSend('GET', `/__studio/api/fs/dirs?${params.toString()}`);
            listing = data.path || target;
            parentPath = data.parent || '';
            up.disabled = busy || !parentPath;
            input.value = listing;
            renderCrumbs(listing);
            renderEntries(data);
            hint.textContent = data.website
                ? 'This folder looks like a website.'
                : 'Select this folder, or open a subfolder.';
        } catch (err) {
            showError(err.message || 'Could not list that folder.');
        }
    }

    async function commit(path) {
        const target = String(path || input.value || '').trim();
        if (!target) {
            showError('Choose a folder first.');
            return;
        }
        if (confirmDiscard && !(await confirmDiscard())) return;
        setBusy(true, 'Opening…');
        showError('');
        try {
            const data = await apiSend('POST', '/__studio/api/project/open', { path: target });
            const root = data.repoRoot || target;
            rememberRecent(root);
            dialog.close();
            if (onOpened) await onOpened(data);
        } catch (err) {
            showError(err.message || 'Could not open that folder.');
        } finally {
            setBusy(false);
        }
    }

    close.addEventListener('click', () => dialog.close());
    up.addEventListener('click', () => {
        if (parentPath) {
            void browse(parentPath);
            return;
        }
        const items = crumbsFor(listing || input.value);
        if (items.length > 1) void browse(items[items.length - 2].path);
    });
    go.addEventListener('click', () => { void browse(input.value); });
    input.addEventListener('keydown', (event) => {
        if (event.key !== 'Enter') return;
        event.preventDefault();
        void browse(input.value);
    });
    hiddenBox.addEventListener('change', () => { void browse(listing || input.value); });
    open.addEventListener('click', () => { void commit(input.value); });
    pick.addEventListener('click', async () => {
        if (busy) return;
        setBusy(true, 'Waiting for the folder picker…');
        showError('');
        try {
            const data = await apiSend('POST', '/__studio/api/fs/pick-folder', {});
            setBusy(false);
            if (data?.path) await commit(data.path);
        } catch (err) {
            setBusy(false);
            showError(err.message || 'The system folder picker is not available. Use the list or paste a path.');
        }
    });

    return {
        remember(path) {
            rememberRecent(path);
        },
        open() {
            if (dialog.open) return;
            showError('');
            const root = getRoot?.() || '';
            current.replaceChildren(
                'Current project folder: ',
                el('strong', { text: root || 'Not set' }),
            );
            input.value = root;
            listing = root;
            renderRecent();
            renderCrumbs(root);
            list.replaceChildren(el('p', { class: 'field-hint', text: 'Loading folders…' }));
            hint.textContent = '';
            dialog.showModal();
            input.focus();
            input.select();
            void browse(root);
        },
        close() {
            if (dialog.open) dialog.close();
        },
    };
}
