import { el } from './dom.js';

const RESERVED_FOLDER_NAMES = new Set(['slides', 'thumbs', 'res', 'album', 'temphidden']);
const BLOCKED_SECTION_NAMES = new Set(['.git', 'studio', '__studio', 'state', 'build', 'node_modules', '__pycache__']);

export function validateTreeRenameInput(kind, rawValue) {
    const value = String(rawValue ?? '').trim();
    const isDir = kind === 'folder' || kind === 'section';
    if (!value) {
        return kind === 'section'
            ? 'Enter a section name.'
            : (kind === 'folder' ? 'Enter a folder name.' : 'Enter a file name.');
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
    if (value.endsWith('.')) {
        return 'Name cannot end with a dot.';
    }
    if (/[?#\x00-\x1f\x7f]/.test(value)) {
        return 'Name contains disallowed characters.';
    }
    const lower = value.toLowerCase();
    if (BLOCKED_SECTION_NAMES.has(lower)) {
        return `"${value}" is a reserved directory name.`;
    }
    if (isDir && RESERVED_FOLDER_NAMES.has(lower)) {
        return `"${value}" is reserved for gallery assets.`;
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

    function rowInfo(row) {
        if (!row) return null;
        const kind = row.dataset.kind || '';
        const section = row.dataset.section || row.dataset.id || '';
        const slug = row.dataset.slug || '';
        const name = row.dataset.name || (kind === 'section' ? (section === '/' ? '/' : section) : '');
        const path = row.dataset.path || '';
        const folderPath = row.dataset.folderPath
            || (kind === 'folder' && section && slug
                ? (section === '/' ? `/${slug}` : `/${section}/${slug}`)
                : (kind === 'section' ? (section === '/' ? '/' : `/${section}`) : ''));
        const itemPath = kind === 'folder' || kind === 'section' ? folderPath : path;
        return {
            kind,
            section,
            slug,
            name,
            path: itemPath,
            primaryFile: path,
            folderPath,
            treeKey: row.dataset.treeKey || '',
        };
    }

    function openContextMenu(row, x, y) {
        if (row && activeSession?.row === row) return;
        if (activeSession) cancelRename({ restoreFocus: false });
        closeMenu();
        menuRow = row || null;

        const info = row
            ? rowInfo(row)
            : { kind: 'section', section: '/', slug: '', name: '/', path: '/', folderPath: '/', treeKey: 'section:/' };
        const kind = info.kind;
        const path = row?.dataset?.path || '';
        const folderPath = info.folderPath;
        const isOpen = row?.getAttribute?.('aria-expanded') === 'true';
        const state = handlers.getOpsState?.() || {};
        const clip = state.clipboard || null;
        const canPaste = Boolean(clip && sectionCanAcceptPaste(info, clip));
        const undoText = state.canUndo && state.undoLabel ? `Undo ${state.undoLabel}` : 'Undo';
        const redoText = state.canRedo && state.redoLabel ? `Redo ${state.redoLabel}` : 'Redo';
        const items = [];

        if (kind === 'file') {
            const inRoot = !info.section || info.section === '/';
            const containerLabel = info.slug
                ? `/${info.section}/${info.slug}`
                : (inRoot ? '/' : `/${info.section}`);
            items.push(
                menuButton('Open', 'Open file in editor', false, () => {
                    if (path) handlers.onOpen?.(path, { pin: false });
                }),
                menuButton('Keep Open', 'Open and pin tab in editor', false, () => {
                    if (path) handlers.onOpen?.(path, { pin: true });
                }),
                menuSeparator(),
                menuButton('New Page / File Here…', `Create a new page or file in ${containerLabel}`, false, () => {
                    handlers.onNewPage?.(info);
                }),
                menuButton('New Folder Here…', `Create a new folder in ${containerLabel}`, false, () => {
                    handlers.onNewFolder?.(info);
                }),
                menuSeparator(),
                menuButton('Rename', 'Rename file (F2)', false, () => {
                    startRename(row);
                }, 'F2'),
                menuButton('Duplicate', 'Duplicate file (Ctrl+D)', false, () => {
                    handlers.onDuplicate?.(info);
                }, 'Ctrl+D'),
                menuSeparator(),
                menuButton('Cut', 'Cut file to move (Ctrl+X)', false, () => {
                    handlers.onCut?.(info);
                }, 'Ctrl+X'),
                menuButton('Copy', 'Copy file (Ctrl+C)', false, () => {
                    handlers.onCopy?.(info);
                }, 'Ctrl+C'),
                menuButton('Paste', pasteTitle(info, clip), !canPaste, () => {
                    handlers.onPaste?.(info);
                }, 'Ctrl+V'),
                menuSeparator(),
                menuButton('Delete…', 'Move file to trash (Delete — can be undone with Ctrl+Z)', false, () => {
                    handlers.onDelete?.(info);
                }, 'Del'),
                menuSeparator(),
                menuButton(undoText, 'Undo last file tree action (Ctrl+Z)', !state.canUndo, () => {
                    handlers.onUndo?.();
                }, 'Ctrl+Z'),
                menuButton(redoText, 'Redo file tree action (Ctrl+Y)', !state.canRedo, () => {
                    handlers.onRedo?.();
                }, 'Ctrl+Y'),
                menuSeparator(),
                menuButton('Copy Path', 'Copy site path to clipboard', !path, () => {
                    copyPath(path);
                }),
            );
        } else if (kind === 'folder') {
            items.push(
                menuButton('New Page / File Here…', `Create a new page or file inside ${folderPath}`, false, () => {
                    handlers.onNewPage?.(info);
                }),
                menuButton('New Folder Here…', `Create a new subfolder inside ${folderPath}`, false, () => {
                    handlers.onNewFolder?.(info);
                }),
                menuSeparator(),
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
                menuButton('Duplicate Folder', 'Duplicate folder and its contents (Ctrl+D)', false, () => {
                    handlers.onDuplicate?.(info);
                }, 'Ctrl+D'),
                menuSeparator(),
                menuButton('Cut Folder', 'Cut folder to move (Ctrl+X)', false, () => {
                    handlers.onCut?.(info);
                }, 'Ctrl+X'),
                menuButton('Copy Folder', 'Copy folder (Ctrl+C)', false, () => {
                    handlers.onCopy?.(info);
                }, 'Ctrl+C'),
                menuButton('Paste', pasteTitle(info, clip), !canPaste, () => {
                    handlers.onPaste?.(info);
                }, 'Ctrl+V'),
                menuSeparator(),
                menuButton('Delete Folder…', 'Move folder to trash (Delete — can be undone with Ctrl+Z)', false, () => {
                    handlers.onDelete?.(info);
                }, 'Del'),
                menuSeparator(),
                menuButton(undoText, 'Undo last file tree action (Ctrl+Z)', !state.canUndo, () => {
                    handlers.onUndo?.();
                }, 'Ctrl+Z'),
                menuButton(redoText, 'Redo file tree action (Ctrl+Y)', !state.canRedo, () => {
                    handlers.onRedo?.();
                }, 'Ctrl+Y'),
                menuSeparator(),
                menuButton('Copy Path', 'Copy folder path to clipboard', !(folderPath || path), () => {
                    copyPath(folderPath || path);
                }),
            );
        } else if (kind === 'section') {
            const isRoot = info.section === '/';
            if (isRoot) {
                items.push(
                    menuButton('New Page / File at Root…', 'Create a new page or file at the website root (/)', false, () => {
                        handlers.onNewPage?.(info);
                    }),
                    menuButton('New Top-Level Folder / Section…', 'Create a new top-level section folder', false, () => {
                        handlers.onNewFolder?.(info);
                    }),
                );
                if (row) {
                    items.push(
                        menuSeparator(),
                        menuButton(isOpen ? 'Collapse Section' : 'Expand Section', 'Toggle section', false, () => {
                            handlers.onToggleRow?.(row);
                        }),
                    );
                }
                items.push(
                    menuSeparator(),
                    menuButton('Paste at Root', pasteTitle(info, clip), !canPaste, () => {
                        handlers.onPaste?.(info);
                    }, 'Ctrl+V'),
                    menuSeparator(),
                    menuButton(undoText, 'Undo last file tree action (Ctrl+Z)', !state.canUndo, () => {
                        handlers.onUndo?.();
                    }, 'Ctrl+Z'),
                    menuButton(redoText, 'Redo file tree action (Ctrl+Y)', !state.canRedo, () => {
                        handlers.onRedo?.();
                    }, 'Ctrl+Y'),
                );
            } else {
                items.push(
                    menuButton('New Page / File Here…', `Create a new page or file in /${info.section}`, false, () => {
                        handlers.onNewPage?.(info);
                    }),
                    menuButton('New Folder Here…', `Create a new subfolder in /${info.section}`, false, () => {
                        handlers.onNewFolder?.(info);
                    }),
                    menuButton('New Top-Level Section…', 'Create a new top-level section folder at website root', false, () => {
                        handlers.onNewFolder?.({ kind: 'section', section: '/', slug: '', name: '/', path: '/', folderPath: '/' });
                    }),
                    menuSeparator(),
                    menuButton(isOpen ? 'Collapse Section' : 'Expand Section', 'Toggle section', false, () => {
                        handlers.onToggleRow?.(row);
                    }),
                    menuSeparator(),
                    menuButton('Rename Section', 'Rename section folder (F2)', false, () => {
                        startRename(row);
                    }, 'F2'),
                    menuButton('Duplicate Section', 'Duplicate section folder and its contents (Ctrl+D)', false, () => {
                        handlers.onDuplicate?.(info);
                    }, 'Ctrl+D'),
                    menuSeparator(),
                    menuButton('Cut Section', 'Cut section folder to move (Ctrl+X)', false, () => {
                        handlers.onCut?.(info);
                    }, 'Ctrl+X'),
                    menuButton('Copy Section', 'Copy section folder (Ctrl+C)', false, () => {
                        handlers.onCopy?.(info);
                    }, 'Ctrl+C'),
                    menuButton('Paste Into Section', pasteTitle(info, clip), !canPaste, () => {
                        handlers.onPaste?.(info);
                    }, 'Ctrl+V'),
                    menuSeparator(),
                    menuButton('Delete Section…', 'Move section folder to trash (Delete — can be undone with Ctrl+Z)', false, () => {
                        handlers.onDelete?.(info);
                    }, 'Del'),
                    menuSeparator(),
                    menuButton(undoText, 'Undo last file tree action (Ctrl+Z)', !state.canUndo, () => {
                        handlers.onUndo?.();
                    }, 'Ctrl+Z'),
                    menuButton(redoText, 'Redo file tree action (Ctrl+Y)', !state.canRedo, () => {
                        handlers.onRedo?.();
                    }, 'Ctrl+Y'),
                    menuSeparator(),
                    menuButton('Copy Path', 'Copy section path to clipboard', !folderPath, () => {
                        copyPath(folderPath);
                    }),
                );
            }
        }

        if (!items.length) return;
        menu.replaceChildren(...items);
        menu.hidden = false;
        const width = menu.offsetWidth || 210;
        const height = menu.offsetHeight || 240;
        const left = Math.max(8, Math.min(x, window.innerWidth - width - 8));
        const top = Math.max(8, Math.min(y, window.innerHeight - height - 8));
        menu.style.left = `${left}px`;
        menu.style.top = `${top}px`;
    }

    function sectionCanAcceptPaste(targetInfo, clip) {
        if (!targetInfo || !clip || !clip.path) return false;
        if (!targetInfo.section) return false;
        return true;
    }

    function pasteTitle(targetInfo, clip) {
        if (!clip || !clip.path) return 'Nothing in tree clipboard to paste';
        const modeVerb = clip.mode === 'cut' ? 'Move' : 'Paste copy of';
        const destSec = targetInfo.section === '/' ? '/' : `/${targetInfo.section}`;
        if (clip.kind === 'folder' || clip.kind === 'section') {
            return `${modeVerb} folder "${clip.name || clip.slug || clip.section}" into ${destSec} (Ctrl+V)`;
        }
        const dest = targetInfo.slug
            ? (targetInfo.section === '/' ? `/${targetInfo.slug}` : `/${targetInfo.section}/${targetInfo.slug}`)
            : destSec;
        return `${modeVerb} "${clip.name}" into ${dest} (Ctrl+V)`;
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
        if (kind === 'section' && (row.dataset.section === '/' || row.dataset.id === '/')) {
            handlers.onAnnounce?.('Website root (/) cannot be renamed.');
            return false;
        }
        if (kind !== 'file' && kind !== 'folder' && kind !== 'section') return false;

        if (activeSession) {
            if (activeSession.row === row) {
                activeSession.input?.focus();
                return true;
            }
            cancelRename({ restoreFocus: false });
        }

        const currentName = String(
            row.dataset.name || (kind === 'section' ? (row.dataset.section || row.dataset.id) : '') || '',
        ).trim();
        if (!currentName || currentName === '/') return false;

        const nameNode = row.querySelector('.tree-name');
        if (!nameNode) return false;

        const input = el('input', {
            class: 'tree-rename-input',
            type: 'text',
            value: currentName,
            spellcheck: 'false',
            autocomplete: 'off',
            'aria-label': kind === 'section'
                ? `Rename section ${currentName}`
                : (kind === 'folder' ? `Rename folder ${currentName}` : `Rename file ${currentName}`),
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
            const renamePath = session.kind === 'section'
                ? (session.folderPath || `/${session.section || session.currentName}`)
                : (session.kind === 'folder'
                    ? (session.folderPath || `/${session.section}/${session.slug}`)
                    : session.path);
            await handlers.onRename?.({
                kind: session.kind,
                section: session.section || (session.kind === 'section' ? session.currentName : ''),
                slug: session.slug,
                path: renamePath,
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
        rowInfo,
    };
}
