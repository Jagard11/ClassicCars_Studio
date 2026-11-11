import { el } from './dom.js';

const RESERVED_FOLDER_NAMES = new Set(['slides', 'thumbs', 'res', 'album', 'temphidden']);
const ALLOWED_FILE_EXT_RE = /\.(html|htm|shtml|php|txt|json|css|js|xml|csv|md|jpe?g|png|gif|webp|svg|bmp|tiff?|ico|avif|pdf|mp4|m4v|webm|mov|mp3|wav|swf|rm|docx?|rtf)$/i;

export function validateTreeRenameInput(kind, rawValue) {
    const value = String(rawValue ?? '').trim();
    if (!value) {
        return kind === 'folder' ? 'Enter a folder name.' : 'Enter a file name.';
    }
    if (value.length > 255) {
        return 'Name is too long.';
    }
    if (value === '.' || value === '..' || value.includes('..')) {
        return 'Name cannot contain "..".';
    }
    if (value.includes('/') || value.includes('\\')) {
        return 'Name cannot contain slashes.';
    }
    if (value.startsWith('.') || value.endsWith('.')) {
        return 'Name cannot start or end with a dot.';
    }
    if (/[?#\x00-\x1f\x7f]/.test(value)) {
        return 'Name contains disallowed characters.';
    }
    if (kind === 'folder') {
        if (RESERVED_FOLDER_NAMES.has(value.toLowerCase())) {
            return `"${value}" is reserved for gallery assets.`;
        }
        return '';
    }
    const dot = value.lastIndexOf('.');
    if (dot <= 0 || dot === value.length - 1) {
        return 'Filename must include an extension (e.g. .html, .php).';
    }
    if (!ALLOWED_FILE_EXT_RE.test(value)) {
        return 'Unsupported file extension.';
    }
    return '';
}

export function createTreeRename(container, handlers) {
    const menu = el('div', { class: 'doc-tab-menu tree-context-menu', role: 'menu', hidden: true });
    document.body.append(menu);

    let menuRow = null;
    let activeSession = null;

    document.addEventListener('pointerdown', (event) => {
        if (!menu.hidden && !menu.contains(event.target)) {
            closeMenu();
        }
    }, true);

    document.addEventListener('keydown', (event) => {
        if (event.key === 'Escape' && !menu.hidden) {
            event.preventDefault();
            event.stopPropagation();
            const targetRow = menuRow;
            closeMenu();
            targetRow?.focus?.();
        }
    }, true);

    container.addEventListener('scroll', () => {
        if (!menu.hidden) closeMenu();
    }, { passive: true });

    function closeMenu() {
        menu.hidden = true;
        menuRow = null;
    }

    function openContextMenu(row, x, y) {
        if (!row || activeSession?.row === row) return;
        if (activeSession) cancelRename({ restoreFocus: false });
        closeMenu();
        menuRow = row;

        const kind = row.dataset.kind || '';
        const path = row.dataset.path || '';
        const folderPath = row.dataset.folderPath || '';
        const isOpen = row.getAttribute('aria-expanded') === 'true';
        const items = [];

        if (kind === 'file') {
            items.push(
                menuButton('Open', 'Open file in editor', false, () => {
                    if (path) handlers.onOpen?.(path, { pin: false });
                }),
                menuButton('Keep Open', 'Open and pin tab in editor', false, () => {
                    if (path) handlers.onOpen?.(path, { pin: true });
                }),
                menuSeparator(),
                menuButton('Rename', 'Rename file (F2)', false, () => {
                    startRename(row);
                }, 'F2'),
                menuButton('Copy Path', 'Copy site path to clipboard', !path, () => {
                    copyPath(path);
                }),
            );
        } else if (kind === 'folder') {
            items.push(
                menuButton('Open Primary Page', 'Open this folder’s main page', !path, () => {
                    if (path) handlers.onOpen?.(path, { pin: true });
                }),
                menuButton(isOpen ? 'Collapse Folder' : 'Expand Folder', 'Toggle folder files', false, () => {
                    handlers.onToggleRow?.(row);
                }),
                menuSeparator(),
                menuButton('Rename Folder', 'Rename folder (F2)', false, () => {
                    startRename(row);
                }, 'F2'),
                menuButton('Copy Path', 'Copy folder path to clipboard', !(folderPath || path), () => {
                    copyPath(folderPath || path);
                }),
            );
        } else if (kind === 'section') {
            items.push(
                menuButton(isOpen ? 'Collapse Section' : 'Expand Section', 'Toggle section', false, () => {
                    handlers.onToggleRow?.(row);
                }),
                menuSeparator(),
                menuButton('Rename', 'Top-level website sections cannot be renamed', true, () => {}, 'F2'),
            );
        }

        if (!items.length) return;
        menu.replaceChildren(...items);
        menu.hidden = false;
        const width = menu.offsetWidth || 188;
        const height = menu.offsetHeight || 140;
        const left = Math.max(8, Math.min(x, window.innerWidth - width - 8));
        const top = Math.max(8, Math.min(y, window.innerHeight - height - 8));
        menu.style.left = `${left}px`;
        menu.style.top = `${top}px`;
    }

    function menuButton(label, title, disabled, onClick, shortcut = '') {
        const btn = el('button', {
            class: 'doc-tab-menu-item tree-context-item',
            type: 'button',
            role: 'menuitem',
            title,
            disabled: disabled || undefined,
        }, [
            el('span', { class: 'tree-context-label', text: label }),
            shortcut ? el('kbd', { class: 'tree-context-kbd', text: shortcut }) : null,
        ]);
        btn.addEventListener('click', (event) => {
            event.preventDefault();
            event.stopPropagation();
            closeMenu();
            onClick();
        });
        return btn;
    }

    function menuSeparator() {
        return el('div', { class: 'tree-context-sep', role: 'separator' });
    }

    function copyPath(targetPath) {
        if (!targetPath) return;
        try {
            navigator.clipboard?.writeText?.(targetPath)?.catch?.(() => {});
        } catch { /* ignore */ }
        handlers.onAnnounce?.(`Copied ${targetPath}`);
    }

    function startRename(row) {
        closeMenu();
        if (!row || !container.contains(row)) return false;
        const kind = row.dataset.kind;
        if (kind === 'section') {
            handlers.onAnnounce?.('Top-level website sections cannot be renamed.');
            return false;
        }
        if (kind !== 'file' && kind !== 'folder') return false;

        if (activeSession) {
            if (activeSession.row === row) {
                activeSession.input?.focus();
                return true;
            }
            cancelRename({ restoreFocus: false });
        }

        const currentName = String(row.dataset.name || '').trim();
        if (!currentName) return false;

        const nameNode = row.querySelector('.tree-name');
        if (!nameNode) return false;

        const input = el('input', {
            class: 'tree-rename-input',
            type: 'text',
            value: currentName,
            spellcheck: 'false',
            autocomplete: 'off',
            'aria-label': kind === 'folder' ? `Rename folder ${currentName}` : `Rename file ${currentName}`,
        });
        const errorBox = el('div', {
            class: 'tree-rename-error',
            role: 'alert',
            hidden: true,
        });
        const box = el('span', { class: 'tree-rename-box' }, [input, errorBox]);

        row.classList.add('is-renaming');
        nameNode.after(box);

        const session = {
            row,
            kind,
            currentName,
            section: row.dataset.section || '',
            slug: row.dataset.slug || '',
            path: row.dataset.path || '',
            folderPath: row.dataset.folderPath || '',
            treeKey: row.dataset.treeKey || '',
            input,
            errorBox,
            box,
            submitting: false,
            done: false,
        };
        activeSession = session;

        const setError = (msg) => {
            if (!msg) {
                errorBox.hidden = true;
                errorBox.textContent = '';
                input.classList.remove('is-invalid');
                return;
            }
            errorBox.textContent = msg;
            errorBox.hidden = false;
            input.classList.add('is-invalid');
        };

        const stopBubble = (event) => {
            event.stopPropagation();
        };

        input.addEventListener('click', stopBubble);
        input.addEventListener('dblclick', stopBubble);
        input.addEventListener('mousedown', stopBubble);
        input.addEventListener('pointerdown', stopBubble);

        input.addEventListener('input', () => {
            const trimmed = input.value.trim();
            if (!trimmed || trimmed === currentName) {
                setError('');
                return;
            }
            setError(validateTreeRenameInput(kind, trimmed));
        });

        input.addEventListener('keydown', (event) => {
            event.stopPropagation();
            if (event.key === 'Escape') {
                event.preventDefault();
                cancelRename({ restoreFocus: true });
                return;
            }
            if (event.key === 'Enter') {
                event.preventDefault();
                void submitSession(session, { fromBlur: false, setError });
            }
        });

        input.addEventListener('blur', () => {
            if (session.done || session.submitting || activeSession !== session) return;
            const nextName = input.value.trim();
            if (!nextName || nextName === currentName) {
                cancelRename({ restoreFocus: false });
                return;
            }
            const err = validateTreeRenameInput(kind, nextName);
            if (err) {
                cancelRename({ restoreFocus: false });
                return;
            }
            void submitSession(session, { fromBlur: true, setError });
        });

        input.focus({ preventScroll: true });
        if (kind === 'file') {
            const dot = currentName.lastIndexOf('.');
            if (dot > 0) input.setSelectionRange(0, dot);
            else input.select();
        } else {
            input.setSelectionRange(0, currentName.length);
        }

        return true;
    }

    async function submitSession(session, { fromBlur, setError }) {
        if (session.done || session.submitting) return;
        const nextName = session.input.value.trim();
        if (nextName === session.currentName) {
            cancelRename({ restoreFocus: !fromBlur });
            return;
        }
        const validationError = validateTreeRenameInput(session.kind, nextName);
        if (validationError) {
            setError(validationError);
            handlers.onAnnounce?.(validationError);
            if (fromBlur) cancelRename({ restoreFocus: false });
            else session.input.focus();
            return;
        }

        session.submitting = true;
        session.input.disabled = true;
        setError('');

        try {
            await handlers.onRename?.({
                kind: session.kind,
                section: session.section,
                slug: session.slug,
                path: session.kind === 'folder' ? (session.folderPath || `/${session.section}/${session.slug}`) : session.path,
                primaryFile: session.path,
                oldName: session.currentName,
                newName: nextName,
            });
            finishSession(session, { restoreFocus: false });
        } catch (error) {
            session.submitting = false;
            if (session.done || activeSession !== session) return;
            session.input.disabled = false;
            const message = error?.message || 'Could not rename item.';
            setError(message);
            handlers.onAnnounce?.(message);
            if (fromBlur && !container.contains(session.row)) {
                finishSession(session, { restoreFocus: false });
            } else {
                session.input.focus();
                session.input.select();
            }
        }
    }

    function finishSession(session, { restoreFocus = false } = {}) {
        if (!session || session.done) return;
        session.done = true;
        if (activeSession === session) activeSession = null;
        session.box.remove();
        session.row.classList.remove('is-renaming');
        if (restoreFocus && container.contains(session.row)) {
            session.row.focus({ preventScroll: true });
        }
    }

    function cancelRename({ restoreFocus = true } = {}) {
        if (!activeSession) return;
        finishSession(activeSession, { restoreFocus });
    }

    function isRenaming() {
        return Boolean(activeSession && !activeSession.done);
    }

    return {
        openContextMenu,
        closeMenu,
        startRename,
        cancelRename,
        isRenaming,
    };
}
