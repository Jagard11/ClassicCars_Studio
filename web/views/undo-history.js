import { el } from './dom.js';
import { createUndoStack } from './undo-stack.js';

export { createUndoStack };

const KIND_BADGES = {
    open: { label: 'OPEN', tone: 'neutral' },
    code: { label: 'CODE', tone: 'code' },
    comment: { label: 'COMMENT', tone: 'code' },
    snippet: { label: 'SNIPPET', tone: 'code' },
    indent: { label: 'INDENT', tone: 'code' },
    widget: { label: 'WIDGET', tone: 'widget' },
    inline: { label: 'INLINE', tone: 'inline' },
    convert: { label: 'CONVERT', tone: 'convert' },
    details: { label: 'DETAILS', tone: 'details' },
    gallery: { label: 'GALLERY', tone: 'gallery' },
    save: { label: 'SAVED', tone: 'save' },
    revert: { label: 'REVERT', tone: 'revert' },
};

export function mountUndoHistory(ctx) {
    let selectedPath = '';
    let filterQuery = '';
    let focusedEntryIdx = -1;
    let relativeTimer = 0;

    const dialog = el('dialog', {
        class: 'undo-history-dialog',
        'aria-labelledby': 'undo-history-title',
    });

    const head = el('div', { class: 'dialog-head uh-head' });
    const titleGroup = el('div', { class: 'uh-title-group' });
    const title = el('h2', { id: 'undo-history-title', text: 'Undo History' });
    const summaryPill = el('span', { class: 'pill uh-summary-pill', text: '0 states' });
    titleGroup.append(title, summaryPill);

    const closeBtn = el('button', {
        class: 'btn',
        type: 'button',
        text: 'Close',
        title: 'Close Undo History (Esc)',
    });
    closeBtn.addEventListener('click', () => close());
    head.append(titleGroup, closeBtn);

    const toolbar = el('div', { class: 'uh-toolbar' });
    const fileLabel = el('label', { class: 'uh-file-label', title: 'Switch open document timeline' });
    fileLabel.append(el('span', { class: 'uh-field-kicker', text: 'Document' }));
    const fileSelect = el('select', {
        class: 'uh-file-select',
        'aria-label': 'Select open document',
        title: 'Choose which open file’s undo timeline to inspect',
    });
    fileLabel.append(fileSelect);

    const filterWrap = el('div', { class: 'uh-filter-wrap' });
    const filterInput = el('input', {
        type: 'search',
        class: 'uh-filter-input',
        placeholder: 'Filter history (e.g. widget, gallery, Ln)…',
        'aria-label': 'Filter undo history entries',
        title: 'Filter undo history entries by action, line, or type',
        autocomplete: 'off',
        spellcheck: 'false',
    });
    filterWrap.append(filterInput);

    const actions = el('div', { class: 'uh-actions' });
    const undoBtn = el('button', {
        class: 'btn',
        type: 'button',
        text: '↶ Undo',
        title: 'Undo one step in the selected file (Ctrl+Z)',
    });
    const redoBtn = el('button', {
        class: 'btn',
        type: 'button',
        text: '↷ Redo',
        title: 'Redo one step in the selected file (Ctrl+Y / Ctrl+Shift+Z)',
    });
    const clearBtn = el('button', {
        class: 'btn',
        type: 'button',
        text: 'Clear History',
        title: 'Clear prior undo/redo entries for this file and keep the current state as baseline',
    });
    actions.append(undoBtn, redoBtn, clearBtn);
    toolbar.append(fileLabel, filterWrap, actions);

    const listWrap = el('div', { class: 'uh-list-wrap' });
    const list = el('div', {
        class: 'uh-list',
        role: 'listbox',
        'aria-label': 'Document undo history timeline',
        tabindex: '0',
    });
    listWrap.append(list);

    const footer = el('div', { class: 'uh-footer' });
    const footerHint = el('span', {
        class: 'uh-footer-hint',
        text: 'Click any entry (or Jump Here) to restore that point without losing future redo entries.',
    });
    const footerShortcuts = el('div', { class: 'uh-footer-keys' }, [
        el('kbd', { text: 'Ctrl+Z' }),
        el('span', { text: 'Undo' }),
        el('kbd', { text: 'Ctrl+Y' }),
        el('span', { text: 'Redo' }),
        el('kbd', { text: 'Ctrl+Shift+H' }),
        el('span', { text: 'History' }),
    ]);
    footer.append(footerHint, footerShortcuts);

    dialog.append(head, toolbar, listWrap, footer);
    document.body.append(dialog);

    fileSelect.addEventListener('change', () => {
        selectedPath = fileSelect.value || ctx.getActivePath?.() || '';
        focusedEntryIdx = -1;
        render({ scrollToCurrent: true });
    });

    filterInput.addEventListener('input', () => {
        filterQuery = filterInput.value || '';
        render({ scrollToCurrent: false });
    });

    undoBtn.addEventListener('click', () => {
        const targetPath = effectivePath();
        if (!targetPath) return;
        ctx.onUndo?.(targetPath);
    });

    redoBtn.addEventListener('click', () => {
        const targetPath = effectivePath();
        if (!targetPath) return;
        ctx.onRedo?.(targetPath);
    });

    clearBtn.addEventListener('click', () => {
        const targetPath = effectivePath();
        if (!targetPath) return;
        ctx.onClear?.(targetPath);
        render({ scrollToCurrent: true });
    });

    dialog.addEventListener('close', () => {
        window.clearInterval(relativeTimer);
        relativeTimer = 0;
    });

    dialog.addEventListener('click', (event) => {
        if (event.target === dialog) close();
    });

    dialog.addEventListener('keydown', (event) => {
        const key = String(event.key || '').toLowerCase();
        const mod = event.ctrlKey || event.metaKey;
        if (mod && !event.altKey) {
            if (key === 'z' && !event.shiftKey) {
                event.preventDefault();
                event.stopPropagation();
                ctx.onUndo?.(effectivePath());
                return;
            }
            if ((key === 'z' && event.shiftKey) || (key === 'y' && !event.shiftKey)) {
                event.preventDefault();
                event.stopPropagation();
                ctx.onRedo?.(effectivePath());
                return;
            }
            if (key === 'h' && event.shiftKey) {
                event.preventDefault();
                event.stopPropagation();
                close();
                return;
            }
        }
        if (document.activeElement === filterInput || document.activeElement === fileSelect) {
            if (event.key === 'ArrowDown') {
                event.preventDefault();
                list.focus();
                stepFocus(1);
            }
            return;
        }
        if (event.key === 'ArrowUp') {
            event.preventDefault();
            stepFocus(-1);
        } else if (event.key === 'ArrowDown') {
            event.preventDefault();
            stepFocus(1);
        } else if (event.key === 'Home') {
            event.preventDefault();
            stepFocus(-9999);
        } else if (event.key === 'End') {
            event.preventDefault();
            stepFocus(9999);
        } else if (event.key === 'Enter' && focusedEntryIdx >= 0) {
            event.preventDefault();
            ctx.onJumpTo?.(effectivePath(), focusedEntryIdx);
        }
    });

    function effectivePath() {
        const active = ctx.getActivePath?.() || '';
        if (selectedPath && hasTimelineFor(selectedPath)) return selectedPath;
        return active;
    }

    function hasTimelineFor(path) {
        const timeline = ctx.getTimeline?.(path);
        return Boolean(timeline && Array.isArray(timeline.entries) && timeline.entries.length > 0);
    }

    function stepFocus(delta) {
        const timeline = ctx.getTimeline?.(effectivePath());
        const entries = filteredEntries(timeline?.entries || []);
        if (!entries.length) return;
        const currPos = entries.findIndex((item) => item.index === focusedEntryIdx);
        let nextPos;
        if (currPos < 0) {
            const activePos = entries.findIndex((item) => item.isCurrent);
            nextPos = activePos >= 0 ? activePos : 0;
        } else {
            nextPos = Math.max(0, Math.min(entries.length - 1, currPos + delta));
        }
        focusedEntryIdx = entries[nextPos].index;
        paintFocus();
    }

    function paintFocus() {
        for (const row of list.querySelectorAll('.uh-item')) {
            const idx = Number(row.dataset.index);
            const focused = idx === focusedEntryIdx;
            row.classList.toggle('is-focused', focused);
            if (focused) row.scrollIntoView({ block: 'nearest' });
        }
    }

    function filteredEntries(entries) {
        const q = filterQuery.trim().toLowerCase();
        if (!q) return entries;
        return entries.filter((entry) => {
            const kindMeta = KIND_BADGES[entry.kind] || KIND_BADGES.code;
            const hay = [
                entry.label,
                entry.kind,
                kindMeta.label,
                entry.delta?.summary || '',
                entry.saved ? 'saved' : '',
                entry.isCurrent ? 'current' : '',
                entry.isFuture ? 'redo future' : 'undo',
                entry.line ? `ln ${entry.line}` : '',
            ].join(' ').toLowerCase();
            return hay.includes(q);
        });
    }

    function populateFileSelect(activePath) {
        const openFiles = ctx.getOpenFiles?.() || [];
        const trackedPaths = ctx.getTrackedPaths?.() || [];
        const seen = new Set();
        const options = [];
        for (const item of openFiles) {
            if (!item?.path || seen.has(item.path)) continue;
            seen.add(item.path);
            options.push({ path: item.path, label: item.label || item.path });
        }
        for (const path of trackedPaths) {
            if (!path || seen.has(path)) continue;
            seen.add(path);
            options.push({ path, label: path });
        }
        fileSelect.replaceChildren();
        if (!options.length) {
            fileSelect.append(el('option', { value: '', text: 'No open document' }));
            fileSelect.disabled = true;
            return;
        }
        fileSelect.disabled = false;
        for (const opt of options) {
            const tl = ctx.getTimeline?.(opt.path);
            const count = tl?.entries?.length || 0;
            const suffix = count ? ` (${count} ${count === 1 ? 'state' : 'states'})` : '';
            const optionEl = el('option', {
                value: opt.path,
                text: `${opt.label}${suffix}`,
            });
            if (opt.path === activePath) optionEl.selected = true;
            fileSelect.append(optionEl);
        }
    }

    function render({ scrollToCurrent = false } = {}) {
        const path = effectivePath();
        selectedPath = path;
        populateFileSelect(path);
        const timeline = ctx.getTimeline?.(path) || { index: -1, canUndo: false, canRedo: false, entries: [] };
        const allEntries = timeline.entries || [];
        const undoSteps = Math.max(0, timeline.index);
        const redoSteps = Math.max(0, allEntries.length - 1 - Math.max(0, timeline.index));

        summaryPill.textContent = allEntries.length
            ? `${allEntries.length} ${allEntries.length === 1 ? 'state' : 'states'} · ${undoSteps} undo / ${redoSteps} redo`
            : 'No history';
        summaryPill.dataset.tone = redoSteps > 0 ? 'warn' : 'ok';

        undoBtn.disabled = !timeline.canUndo;
        redoBtn.disabled = !timeline.canRedo;
        clearBtn.disabled = allEntries.length <= 1;

        if (focusedEntryIdx < 0 || focusedEntryIdx >= allEntries.length) {
            focusedEntryIdx = timeline.index;
        }

        const shown = filteredEntries(allEntries);
        list.replaceChildren();
        if (!shown.length) {
            list.append(el('p', {
                class: 'empty uh-empty',
                text: allEntries.length
                    ? 'No history entries match your filter.'
                    : 'Open a page to view its undo history timeline.',
            }));
            return;
        }

        const frag = document.createDocumentFragment();
        for (const entry of shown) {
            frag.append(buildEntryRow(entry, path));
        }
        list.append(frag);

        if (scrollToCurrent) {
            requestAnimationFrame(() => {
                const currentRow = list.querySelector('.uh-item.is-current');
                currentRow?.scrollIntoView({ block: 'nearest' });
            });
        }
    }

    function buildEntryRow(entry, path) {
        const kindMeta = KIND_BADGES[entry.kind] || KIND_BADGES.code;
        const classes = ['uh-item'];
        if (entry.isCurrent) classes.push('is-current');
        if (entry.isFuture) classes.push('is-future');
        if (entry.saved) classes.push('is-saved');
        if (entry.index === focusedEntryIdx) classes.push('is-focused');

        const row = el('div', {
            class: classes.join(' '),
            role: 'option',
            'aria-selected': entry.isCurrent ? 'true' : 'false',
            'data-index': String(entry.index),
            'data-entry-id': String(entry.id),
            title: `${entry.label} (${formatExactTime(entry.timestamp)}) — Click to restore this state`,
        });

        const rail = el('div', { class: 'uh-rail', 'aria-hidden': 'true' });
        rail.append(
            el('span', { class: 'uh-dot' }),
            el('span', { class: 'uh-step-num', text: `#${entry.index + 1}` }),
        );

        const main = el('div', { class: 'uh-main' });
        const topLine = el('div', { class: 'uh-row-top' });
        const kindBadge = el('span', {
            class: `uh-kind-badge uh-kind-${kindMeta.tone}`,
            text: kindMeta.label,
        });
        const labelEl = el('span', { class: 'uh-label', text: entry.label });
        topLine.append(kindBadge, labelEl);

        const badges = el('div', { class: 'uh-badges' });
        if (entry.isCurrent) {
            badges.append(el('span', {
                class: 'uh-state-badge uh-badge-current',
                text: 'Current',
                title: 'Active state in the editor',
            }));
        } else if (entry.isFuture) {
            badges.append(el('span', {
                class: 'uh-state-badge uh-badge-redo',
                text: 'Redo',
                title: 'Future state — click or press Ctrl+Y to redo to this point',
            }));
        }
        if (entry.saved) {
            badges.append(el('span', {
                class: 'uh-state-badge uh-badge-saved',
                text: 'Saved',
                title: 'Matches the file saved on disk',
            }));
        }
        topLine.append(badges);

        const bottomLine = el('div', { class: 'uh-row-bottom' });
        bottomLine.append(buildDeltaNode(entry.delta));

        if (entry.line > 0 && entry.kind !== 'gallery') {
            bottomLine.append(el('span', {
                class: 'uh-meta-chip',
                text: `Ln ${entry.line}`,
                title: `Cursor at line ${entry.line}`,
            }));
        }
        if (entry.photoCount > 0 && (entry.kind === 'gallery' || entry.delta?.changedGallery)) {
            bottomLine.append(el('span', {
                class: 'uh-meta-chip',
                text: `${entry.photoCount} ${entry.photoCount === 1 ? 'photo' : 'photos'}`,
                title: `${entry.photoCount} photos in gallery`,
            }));
        }
        const timeEl = el('time', {
            class: 'uh-time',
            text: `${formatRelativeTime(entry.timestamp)} · ${formatExactTime(entry.timestamp)}`,
            title: new Date(entry.timestamp).toLocaleString(),
        });
        bottomLine.append(timeEl);

        main.append(topLine, bottomLine);

        const right = el('div', { class: 'uh-right' });
        const jumpBtn = el('button', {
            class: entry.isCurrent ? 'btn uh-jump-btn is-active-state' : 'btn uh-jump-btn',
            type: 'button',
            text: entry.isCurrent ? 'Active' : 'Jump Here',
            disabled: entry.isCurrent,
            title: entry.isCurrent
                ? 'This is the current active state'
                : `Restore "${entry.label}" without losing future redo entries`,
        });
        jumpBtn.addEventListener('click', (event) => {
            event.stopPropagation();
            focusedEntryIdx = entry.index;
            ctx.onJumpTo?.(path, entry.index);
        });
        right.append(jumpBtn);

        row.append(rail, main, right);
        row.addEventListener('click', () => {
            focusedEntryIdx = entry.index;
            if (!entry.isCurrent) {
                ctx.onJumpTo?.(path, entry.index);
            } else {
                paintFocus();
            }
        });
        return row;
    }

    function buildDeltaNode(delta = {}) {
        const wrap = el('span', { class: 'uh-delta' });
        const added = Number(delta.addedLines) || 0;
        const deleted = Number(delta.deletedLines) || 0;
        const chars = Number(delta.charDelta) || 0;
        if (added > 0 || deleted > 0) {
            if (added > 0) wrap.append(el('span', { class: 'uh-delta-add', text: `+${added}` }));
            if (deleted > 0) wrap.append(el('span', { class: 'uh-delta-del', text: `\u2212${deleted}` }));
            const total = added + deleted;
            wrap.append(el('span', { class: 'uh-delta-unit', text: total === 1 ? 'line' : 'lines' }));
            if (delta.changedGallery) {
                wrap.append(el('span', { class: 'uh-delta-unit', text: '· gallery' }));
            }
            return wrap;
        }
        if (chars !== 0) {
            const cls = chars > 0 ? 'uh-delta-add' : 'uh-delta-del';
            const sign = chars > 0 ? '+' : '\u2212';
            const abs = Math.abs(chars);
            wrap.append(
                el('span', { class: cls, text: `${sign}${abs}` }),
                el('span', { class: 'uh-delta-unit', text: abs === 1 ? 'char' : 'chars' }),
            );
            return wrap;
        }
        wrap.append(el('span', { class: 'uh-delta-neutral', text: delta.summary || 'Baseline' }));
        return wrap;
    }

    function open(path) {
        selectedPath = path || ctx.getActivePath?.() || '';
        focusedEntryIdx = -1;
        render({ scrollToCurrent: true });
        if (!dialog.open) dialog.showModal();
        window.clearInterval(relativeTimer);
        relativeTimer = window.setInterval(() => {
            if (dialog.open) render({ scrollToCurrent: false });
        }, 15000);
        list.focus();
    }

    function close() {
        if (dialog.open) dialog.close();
    }

    function toggle() {
        if (dialog.open) close();
        else open();
    }

    function refresh() {
        if (!dialog.open) return;
        const active = ctx.getActivePath?.() || '';
        if (!selectedPath || !hasTimelineFor(selectedPath)) {
            selectedPath = active;
        }
        render({ scrollToCurrent: true });
    }

    return {
        open,
        close,
        toggle,
        refresh,
        isOpen: () => Boolean(dialog.open),
    };
}

function formatRelativeTime(timestamp) {
    const diffSec = Math.max(0, Math.round((Date.now() - (Number(timestamp) || 0)) / 1000));
    if (diffSec < 5) return 'Just now';
    if (diffSec < 60) return `${diffSec}s ago`;
    const diffMin = Math.floor(diffSec / 60);
    if (diffMin < 60) return `${diffMin}m ago`;
    const diffHr = Math.floor(diffMin / 60);
    return `${diffHr}h ago`;
}

function formatExactTime(timestamp) {
    try {
        const d = new Date(Number(timestamp) || Date.now());
        return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
    } catch {
        return '';
    }
}
