import { computeLineDiff } from './line-diff.js';

const MAX_ENTRIES = 150;
const DEFAULT_COALESCE_MS = 650;
const MAX_BURST_MS = 5000;

export function createUndoStack({ onChange = null } = {}) {
    const timelines = new Map();

    function keyOf(path) {
        let text = String(path || '').trim();
        if (!text) return '';
        text = text.replace(/\\/g, '/');
        if (!text.startsWith('/')) text = `/${text}`;
        text = text.replace(/\/{2,}/g, '/');
        if (text.length > 1 && text.endsWith('/')) text = text.slice(0, -1);
        return text;
    }

    function notify(path) {
        if (typeof onChange === 'function') {
            onChange(keyOf(path));
        }
    }

    function initDocument(path, snapshot, { label = 'Initial state (Opened)', reset = false, saved = true } = {}) {
        const key = keyOf(path);
        if (!key) return null;
        const existing = timelines.get(key);
        if (existing && existing.entries.length > 0 && !reset) {
            return existing.entries[existing.index] || null;
        }
        const norm = normalizeSnapshot(snapshot);
        const now = Date.now();
        const entry = {
            id: 1,
            kind: 'open',
            label,
            timestamp: now,
            startedAt: now,
            saved: Boolean(saved),
            milestone: false,
            coalesceKey: null,
            line: lineAtOffset(norm.content, norm.caret.selectionStart),
            ...norm,
            delta: {
                addedLines: 0,
                deletedLines: 0,
                charDelta: 0,
                changedGallery: false,
                changedDetails: false,
                summary: 'Baseline',
            },
        };
        const timeline = {
            path: key,
            entries: [entry],
            index: 0,
            nextId: 2,
        };
        timelines.set(key, timeline);
        notify(key);
        return cloneEntry(entry, 0);
    }

    function record(path, snapshot, meta = {}) {
        const key = keyOf(path);
        if (!key) return null;
        let timeline = timelines.get(key);
        if (!timeline || !timeline.entries.length) {
            return initDocument(key, snapshot, {
                label: meta.label || 'Initial state (Opened)',
                saved: Boolean(meta.saved),
            });
        }

        const norm = normalizeSnapshot(snapshot);
        const curr = timeline.entries[timeline.index];
        const now = Date.now();
        const kind = String(meta.kind || 'code');
        const label = String(meta.label || defaultLabelForKind(kind, norm));
        const sameDocState = isSameState(curr, norm);

        if (kind === 'save') {
            markSavedAcrossTimeline(timeline, norm);
            if (sameDocState && curr.kind === 'save') {
                curr.timestamp = now;
                curr.saved = true;
                notify(key);
                return cloneEntry(curr, timeline.index);
            }
            const prevForDelta = timeline.entries[timeline.index];
            const entry = {
                id: timeline.nextId++,
                kind: 'save',
                label: meta.label || 'Saved to disk',
                timestamp: now,
                startedAt: now,
                saved: true,
                milestone: sameDocState,
                coalesceKey: null,
                line: lineAtOffset(norm.content, norm.caret.selectionStart),
                ...norm,
                delta: computeEntryDelta(prevForDelta, norm),
            };
            if (timeline.index === timeline.entries.length - 1) {
                timeline.entries.push(entry);
                timeline.index = timeline.entries.length - 1;
                trimTimeline(timeline);
            } else {
                curr.saved = true;
            }
            notify(key);
            return cloneEntry(timeline.entries[timeline.index], timeline.index);
        }

        if (sameDocState && kind !== 'revert' && !meta.force) {
            curr.caret = cloneCaret(norm.caret);
            curr.mode = norm.mode;
            return cloneEntry(curr, timeline.index);
        }

        const canCoalesce = Boolean(
            meta.coalesce
            && meta.coalesceKey
            && timeline.index === timeline.entries.length - 1
            && curr.coalesceKey === meta.coalesceKey
            && !curr.saved
            && !curr.milestone
            && curr.kind !== 'open'
            && (now - curr.timestamp) <= (Number(meta.coalesceWindow) || DEFAULT_COALESCE_MS)
            && (now - (curr.startedAt || curr.timestamp)) <= MAX_BURST_MS
            && (kind !== 'code' || Math.abs(norm.content.length - curr.content.length) <= 48)
        );

        if (canCoalesce) {
            const prevBase = timeline.entries[Math.max(0, timeline.index - 1)];
            curr.content = norm.content;
            curr.sourceLoaded = norm.sourceLoaded;
            curr.caret = cloneCaret(norm.caret);
            curr.images = norm.images;
            curr.unlistedSlides = norm.unlistedSlides;
            curr.imagesSig = norm.imagesSig;
            curr.details = norm.details;
            curr.detailsSig = norm.detailsSig;
            curr.mode = norm.mode;
            curr.timestamp = now;
            curr.line = Number(meta.line) || lineAtOffset(norm.content, norm.caret.selectionStart);
            if (meta.label) curr.label = String(meta.label);
            curr.delta = computeEntryDelta(prevBase, norm);
            refreshSavedMatches(timeline);
            notify(key);
            return cloneEntry(curr, timeline.index);
        }

        if (timeline.index < timeline.entries.length - 1) {
            timeline.entries.splice(timeline.index + 1);
        }

        const prevEntry = timeline.entries[timeline.index];
        const entry = {
            id: timeline.nextId++,
            kind,
            label,
            timestamp: now,
            startedAt: now,
            saved: Boolean(meta.saved),
            milestone: false,
            coalesceKey: meta.coalesce && meta.coalesceKey ? String(meta.coalesceKey) : null,
            line: Number(meta.line) || lineAtOffset(norm.content, norm.caret.selectionStart),
            ...norm,
            delta: computeEntryDelta(prevEntry, norm),
        };

        if (meta.saved) {
            markSavedAcrossTimeline(timeline, norm);
            entry.saved = true;
        } else {
            entry.saved = matchesAnySavedEntry(timeline, norm);
        }

        timeline.entries.push(entry);
        timeline.index = timeline.entries.length - 1;
        trimTimeline(timeline);
        notify(key);
        return cloneEntry(entry, timeline.index);
    }

    function updateCaret(path, caret, mode) {
        const timeline = timelines.get(keyOf(path));
        if (!timeline || !timeline.entries.length) return;
        const curr = timeline.entries[timeline.index];
        if (!curr) return;
        if (caret) curr.caret = cloneCaret(caret);
        if (mode === 'code' || mode === 'gallery') curr.mode = mode;
    }

    function prevUndoIndex(timeline) {
        if (!timeline || timeline.index <= 0) return -1;
        let target = timeline.index - 1;
        const curr = timeline.entries[timeline.index];
        if (curr?.milestone && target > 0 && isSameState(curr, timeline.entries[target])) {
            target -= 1;
        }
        return target;
    }

    function nextRedoIndex(timeline) {
        if (!timeline || timeline.index >= timeline.entries.length - 1) return -1;
        let target = timeline.index + 1;
        const next = timeline.entries[target];
        const after = timeline.entries[target + 1];
        if (next && after && after.milestone && isSameState(next, after)) {
            target += 1;
        }
        return target;
    }

    function canUndo(path) {
        const timeline = timelines.get(keyOf(path));
        return prevUndoIndex(timeline) >= 0;
    }

    function canRedo(path) {
        const timeline = timelines.get(keyOf(path));
        return nextRedoIndex(timeline) >= 0;
    }

    function undo(path) {
        const key = keyOf(path);
        const timeline = timelines.get(key);
        const target = prevUndoIndex(timeline);
        if (target < 0) return null;
        timeline.index = target;
        notify(key);
        return cloneEntry(timeline.entries[target], target);
    }

    function redo(path) {
        const key = keyOf(path);
        const timeline = timelines.get(key);
        const target = nextRedoIndex(timeline);
        if (target < 0) return null;
        timeline.index = target;
        notify(key);
        return cloneEntry(timeline.entries[target], target);
    }

    function jumpTo(path, targetIndex) {
        const key = keyOf(path);
        const timeline = timelines.get(key);
        if (!timeline || !timeline.entries.length) return null;
        const idx = Math.max(0, Math.min(timeline.entries.length - 1, Number(targetIndex)));
        if (!Number.isFinite(idx)) return null;
        timeline.index = idx;
        notify(key);
        return cloneEntry(timeline.entries[idx], idx);
    }

    function jumpToId(path, entryId) {
        const key = keyOf(path);
        const timeline = timelines.get(key);
        if (!timeline) return null;
        const idx = timeline.entries.findIndex((item) => item.id === Number(entryId));
        if (idx < 0) return null;
        return jumpTo(key, idx);
    }

    function clearHistory(path) {
        const key = keyOf(path);
        const timeline = timelines.get(key);
        if (!timeline || !timeline.entries.length) return false;
        const curr = timeline.entries[timeline.index];
        const now = Date.now();
        const resetEntry = {
            ...curr,
            id: 1,
            kind: 'open',
            label: 'Initial state (History cleared)',
            timestamp: now,
            startedAt: now,
            milestone: false,
            coalesceKey: null,
            delta: {
                addedLines: 0,
                deletedLines: 0,
                charDelta: 0,
                changedGallery: false,
                changedDetails: false,
                summary: 'Baseline',
            },
        };
        timeline.entries = [resetEntry];
        timeline.index = 0;
        timeline.nextId = 2;
        notify(key);
        return true;
    }

    function removeDocument(path) {
        const key = keyOf(path);
        if (!key || !timelines.has(key)) return false;
        timelines.delete(key);
        notify(key);
        return true;
    }

    function renamePath(oldPath, newPath, { isFolder = false } = {}) {
        const oldKey = keyOf(oldPath);
        const newKey = keyOf(newPath);
        if (!oldKey || !newKey || oldKey === newKey) return false;
        let moved = false;
        const prefix = `${oldKey}/`;
        for (const [key, timeline] of [...timelines.entries()]) {
            if (key === oldKey) {
                timelines.delete(key);
                timeline.path = newKey;
                timelines.set(newKey, timeline);
                notify(newKey);
                moved = true;
            } else if ((isFolder || key.startsWith(prefix)) && key.startsWith(prefix)) {
                const nextKey = `${newKey}${key.slice(oldKey.length)}`;
                timelines.delete(key);
                timeline.path = nextKey;
                timelines.set(nextKey, timeline);
                notify(nextKey);
                moved = true;
            }
        }
        return moved;
    }

    function getTimeline(path) {
        const key = keyOf(path);
        const timeline = timelines.get(key);
        if (!timeline) {
            return {
                path: key,
                index: -1,
                canUndo: false,
                canRedo: false,
                entries: [],
            };
        }
        return {
            path: timeline.path,
            index: timeline.index,
            canUndo: prevUndoIndex(timeline) >= 0,
            canRedo: nextRedoIndex(timeline) >= 0,
            entries: timeline.entries.map((entry, idx) => ({
                id: entry.id,
                index: idx,
                kind: entry.kind,
                label: entry.label,
                timestamp: entry.timestamp,
                saved: Boolean(entry.saved),
                milestone: Boolean(entry.milestone),
                isCurrent: idx === timeline.index,
                isFuture: idx > timeline.index,
                line: entry.line || 1,
                caret: cloneCaret(entry.caret),
                mode: entry.mode,
                delta: { ...entry.delta },
                photoCount: Array.isArray(entry.images) ? entry.images.length : 0,
            })),
        };
    }

    function listDocumentPaths() {
        return [...timelines.keys()];
    }

    return {
        initDocument,
        record,
        updateCaret,
        canUndo,
        canRedo,
        undo,
        redo,
        jumpTo,
        jumpToId,
        clearHistory,
        removeDocument,
        renamePath,
        getTimeline,
        listDocumentPaths,
    };
}

function trimTimeline(timeline) {
    while (timeline.entries.length > MAX_ENTRIES) {
        timeline.entries.shift();
        timeline.index = Math.max(0, timeline.index - 1);
    }
}

function markSavedAcrossTimeline(timeline, savedNorm) {
    for (const item of timeline.entries) {
        item.saved = isSameState(item, savedNorm);
    }
}

function refreshSavedMatches(timeline) {
    const savedRef = timeline.entries.find((item) => item.saved);
    if (!savedRef) return;
    for (const item of timeline.entries) {
        item.saved = isSameState(item, savedRef);
    }
}

function matchesAnySavedEntry(timeline, norm) {
    const savedRef = timeline.entries.find((item) => item.saved);
    if (!savedRef) return false;
    return isSameState(savedRef, norm);
}

function isSameState(a, b) {
    if (!a || !b) return false;
    return (
        String(a.content ?? '') === String(b.content ?? '')
        && String(a.imagesSig ?? '') === String(b.imagesSig ?? '')
        && String(a.detailsSig ?? '') === String(b.detailsSig ?? '')
    );
}

function normalizeSnapshot(snapshot = {}) {
    const content = typeof snapshot.content === 'string' ? snapshot.content : '';
    const sourceLoaded = snapshot.sourceLoaded !== false;
    const caret = cloneCaret(snapshot.caret);
    const images = cloneImages(snapshot.images);
    const unlistedSlides = cloneImages(snapshot.unlistedSlides);
    const details = cloneDetails(snapshot.details);
    return {
        content,
        sourceLoaded,
        caret,
        images,
        unlistedSlides,
        imagesSig: computeImagesSig(images),
        details,
        detailsSig: JSON.stringify(details),
        mode: snapshot.mode === 'gallery' ? 'gallery' : 'code',
    };
}

function computeEntryDelta(prev, next) {
    if (!prev) {
        return {
            addedLines: 0,
            deletedLines: 0,
            charDelta: 0,
            changedGallery: false,
            changedDetails: false,
            summary: 'Baseline',
        };
    }
    const prevContent = String(prev.content ?? '');
    const nextContent = String(next.content ?? '');
    const charDelta = nextContent.length - prevContent.length;
    const changedGallery = String(prev.imagesSig ?? '') !== String(next.imagesSig ?? '');
    const changedDetails = String(prev.detailsSig ?? '') !== String(next.detailsSig ?? '');

    let addedLines = 0;
    let deletedLines = 0;
    if (prevContent !== nextContent) {
        const diff = computeLineDiff(prevContent, nextContent);
        addedLines = (diff.addedCount || 0) + (diff.modifiedCount || 0);
        deletedLines = (diff.deletedCount || 0) + (diff.modifiedCount || 0);
    }

    const parts = [];
    if (addedLines > 0 || deletedLines > 0) {
        const lineBits = [];
        if (addedLines > 0) lineBits.push(`+${addedLines}`);
        if (deletedLines > 0) lineBits.push(`\u2212${deletedLines}`);
        const total = addedLines + deletedLines;
        parts.push(`${lineBits.join(' ')} ${total === 1 ? 'line' : 'lines'}`);
    } else if (charDelta !== 0) {
        const sign = charDelta > 0 ? '+' : '\u2212';
        const abs = Math.abs(charDelta);
        parts.push(`${sign}${abs} ${abs === 1 ? 'char' : 'chars'}`);
    }
    if (changedGallery) {
        const prevCount = Array.isArray(prev.images) ? prev.images.length : 0;
        const nextCount = Array.isArray(next.images) ? next.images.length : 0;
        if (nextCount !== prevCount) {
            const diffCount = nextCount - prevCount;
            const sign = diffCount > 0 ? '+' : '\u2212';
            parts.push(`${sign}${Math.abs(diffCount)} ${Math.abs(diffCount) === 1 ? 'slide' : 'slides'}`);
        } else {
            parts.push('gallery');
        }
    }
    if (!parts.length && changedDetails) {
        parts.push('details');
    }
    return {
        addedLines,
        deletedLines,
        charDelta,
        changedGallery,
        changedDetails,
        summary: parts.length ? parts.join(' \u00b7 ') : 'No diff',
    };
}

function defaultLabelForKind(kind, norm) {
    const line = lineAtOffset(norm.content, norm.caret?.selectionStart || 0);
    switch (kind) {
        case 'open': return 'Initial state (Opened)';
        case 'comment': return 'Toggled comment';
        case 'snippet': return 'Inserted snippet';
        case 'indent': return 'Indented code';
        case 'widget': return 'Applied widget';
        case 'inline': return 'Preview inline edit';
        case 'convert': return 'Converted jAlbum page';
        case 'details': return 'Edited vehicle/page details';
        case 'gallery': return 'Reordered / edited gallery slides';
        case 'save': return 'Saved to disk';
        case 'revert': return 'Reverted to saved';
        default: return `Typed at Ln ${line}`;
    }
}

function lineAtOffset(content, offset) {
    const text = String(content ?? '');
    const clamped = Math.max(0, Math.min(text.length, Number(offset) || 0));
    let lines = 1;
    for (let i = 0; i < clamped; i += 1) {
        if (text.charCodeAt(i) === 10) lines += 1;
    }
    return lines;
}

function cloneCaret(caret = {}) {
    return {
        selectionStart: Math.max(0, Number(caret?.selectionStart) || 0),
        selectionEnd: Math.max(0, Number(caret?.selectionEnd) || 0),
        scrollTop: Math.max(0, Number(caret?.scrollTop) || 0),
        scrollLeft: Math.max(0, Number(caret?.scrollLeft) || 0),
    };
}

function cloneImages(list) {
    if (!Array.isArray(list)) return [];
    return list.map((item) => (item && typeof item === 'object' ? { ...item } : item)).filter(Boolean);
}

function cloneDetails(details) {
    if (!details || typeof details !== 'object') return {};
    return { ...details };
}

function computeImagesSig(images) {
    if (!Array.isArray(images)) return '';
    return images.map((img) => `${img?.name || ''}\u0000${img?.caption ?? ''}`).join('\n');
}

function cloneEntry(entry, index = 0) {
    if (!entry) return null;
    return {
        ...entry,
        index,
        caret: cloneCaret(entry.caret),
        images: cloneImages(entry.images),
        unlistedSlides: cloneImages(entry.unlistedSlides),
        details: cloneDetails(entry.details),
        delta: { ...(entry.delta || {}) },
    };
}
