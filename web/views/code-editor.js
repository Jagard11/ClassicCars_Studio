import { el } from './dom.js';
import { mountIntelliSense } from './intellisense.js';
import { highlightDocument, languageFromPath } from './syntax-highlight.js';
import { computeCommentToggle } from './code-comments.js';
import { computeLineDiff, DIFF_ADDED, DIFF_MODIFIED } from './line-diff.js';
import { mountCodeMinimap } from './code-minimap.js';

const BLOCKS = [
    { id: 'feature-single', html: featureSingle() },
    { id: 'feature-landscape', html: featurePair('articleFeaturePair--landscape', 'photo-a.jpg', 'photo-b.jpg') },
    { id: 'feature-portrait', html: featurePair('articleFeaturePair--portrait', 'photo-c.jpg', 'photo-d.jpg') },
    { id: 'gallery-section', html: gallerySection() },
];
const SNIPPETS = [
    ['.articleFeatureSingle', BLOCKS[0].html],
    ['.articleFeaturePair--landscape', BLOCKS[1].html],
    ['.articleFeaturePair--portrait', BLOCKS[2].html],
    ['.articleGallerySection', BLOCKS[3].html],
];

export function mountCodeEditor(parent, ctx) {
    const shell = el('div', { class: 'code-shell' });
    const toolbar = el('div', { class: 'code-toolbar' });
    const findButton = el('button', { class: 'btn', type: 'button', text: 'Find', title: 'Find text in code editor (Ctrl+F)' });
    const snippetMenu = el('details', { class: 'snippet-menu' });
    const snippetSummary = el('summary', { class: 'btn', text: 'Insert Snippet', title: 'Insert an HTML photo block at the cursor' });
    const snippetList = el('div', { class: 'snippet-list' });
    const snippetTips = {
        '.articleFeatureSingle': 'Insert single featured image block',
        '.articleFeaturePair--landscape': 'Insert landscape photo pair',
        '.articleFeaturePair--portrait': 'Insert portrait photo pair',
        '.articleGallerySection': 'Insert Lightbox photo gallery section',
    };
    for (const [label, snippet] of SNIPPETS) {
        const button = el('button', { type: 'button', text: label, title: snippetTips[label] || `Insert ${label}` });
        button.addEventListener('click', () => {
            insertSnippet(snippet);
            snippetMenu.open = false;
        });
        snippetList.append(button);
    }
    snippetMenu.append(snippetSummary, snippetList);
    const fileLabel = el('span', { class: 'code-file', text: 'Loading…' });
    const jumpBadge = el('span', { class: 'jump-badge', hidden: true, 'aria-live': 'polite' });
    toolbar.append(findButton, snippetMenu, jumpBadge, fileLabel);

    const findBar = el('div', { class: 'find-bar', hidden: true });
    const findInput = el('input', {
        type: 'search',
        placeholder: 'Find in file',
        'aria-label': 'Find in file',
        title: 'Find text in the code editor. Enter jumps to the next match. (Ctrl+F)',
        autocomplete: 'off',
        spellcheck: 'false',
    });
    const findCount = el('span', { class: 'find-count', 'aria-live': 'polite', title: 'Find matches in this file' });
    const findPrev = el('button', { class: 'btn', type: 'button', text: 'Previous', title: 'Previous match in this file' });
    const findNext = el('button', { class: 'btn', type: 'button', text: 'Next', title: 'Next match in this file' });
    const findClose = el('button', { class: 'btn', type: 'button', text: 'Close', title: 'Close find' });
    findBar.append(findInput, findCount, findPrev, findNext, findClose);

    const blocker = el('div', { class: 'code-blocker' });
    const blockerText = el('p', { text: 'Loading page…' });
    blocker.append(blockerText);
    const mediaCard = el('div', { class: 'media-card', hidden: true });

    const codeBody = el('div', { class: 'code-body', hidden: true });
    const gutter = el('pre', { class: 'line-gutter', 'aria-hidden': 'true', text: '1' });
    const stage = el('div', { class: 'code-stage' });
    const highlight = el('pre', { class: 'code-editor-highlight', 'aria-hidden': 'true' });
    const highlightCode = el('code', { class: 'code-editor-highlight-code' });
    highlight.append(highlightCode);
    const textarea = el('textarea', {
        class: 'code-input code-editor-textarea',
        spellcheck: 'false',
        autocomplete: 'off',
        autocapitalize: 'off',
        autocorrect: 'off',
        wrap: 'off',
        'aria-label': 'Page source',
    });
    stage.append(highlight, textarea);
    const minimap = mountCodeMinimap({
        textarea,
        onScrollTo: () => syncBackdrop(),
    });
    codeBody.append(gutter, stage, minimap.element);
    const sense = mountIntelliSense({ textarea, toolbar });

    const footer = el('div', { class: 'code-footer' });
    const cursor = el('span', { text: 'Ln 1, Col 1', title: 'Line and column at the cursor' });
    const diffBadge = el('span', { class: 'code-diff-stat', hidden: true, title: 'Line changes compared to Git HEAD / saved baseline' });
    const lines = el('span', { text: '0 lines', title: 'Number of lines in this file' });
    const language = el('span', { text: 'Text', title: 'Source language' });
    footer.append(cursor, diffBadge, el('span', { class: 'spacer' }), lines, language);

    shell.append(toolbar, findBar, blocker, mediaCard, codeBody, footer);
    parent.append(shell);

    let findHits = [];
    let findIndex = -1;
    let snippetsOn = false;
    let blocksOn = false;
    let jumpTimer = 0;
    let inlineMark = null;
    let jumpMark = null;
    let filePath = '';
    let baselineText = null;
    let baselinePath = '';
    let currentDiff = null;

    findButton.addEventListener('click', () => openFind());
    findPrev.addEventListener('click', () => runFind(-1));
    findNext.addEventListener('click', () => runFind(1));
    findClose.addEventListener('click', () => closeFind());
    findInput.addEventListener('input', () => runFind(0));
    findInput.addEventListener('keydown', (event) => {
        if (event.key === 'Escape') {
            event.preventDefault();
            event.stopPropagation();
            closeFind();
        } else if (event.key === 'Enter') {
            event.preventDefault();
            runFind(event.shiftKey ? -1 : 1);
        }
    });
    textarea.addEventListener('input', () => {
        if (!findBar.hidden) refreshFindMatches();
        refreshGutter();
        refreshHighlight();
        updateCursor();
        ctx.onEdit();
    });
    textarea.addEventListener('scroll', syncBackdrop);
    textarea.addEventListener('keyup', updateCursor);
    textarea.addEventListener('click', updateCursor);
    textarea.addEventListener('select', updateCursor);
    textarea.addEventListener('pointerup', updateCursor);
    textarea.addEventListener('keydown', (event) => {
        if (event.defaultPrevented) return;
        if ((event.ctrlKey || event.metaKey) && !event.altKey && (event.key === '/' || event.code === 'Slash')) {
            event.preventDefault();
            event.stopPropagation();
            toggleComment();
        } else if (event.key === 'Tab' && !event.ctrlKey && !event.metaKey && !event.altKey) {
            event.preventDefault();
            indentSelection(event.shiftKey);
        } else if (event.key === 'Escape' && !findBar.hidden) {
            event.preventDefault();
            event.stopPropagation();
            closeFind();
        }
    });

    function setValue(text) {
        sense.dismiss();
        const next = String(text ?? '');
        const scrollTop = textarea.scrollTop;
        const scrollLeft = textarea.scrollLeft;
        const start = textarea.selectionStart;
        const end = textarea.selectionEnd;
        if (baselineText === null) baselineText = next;
        if (textarea.value !== next) textarea.value = next;
        if (!findBar.hidden) refreshFindMatches();
        refreshGutter();
        refreshHighlight();
        const max = textarea.value.length;
        if (Number.isFinite(start)) {
            textarea.selectionStart = Math.min(start, max);
            textarea.selectionEnd = Math.min(end, max);
        }
        textarea.scrollTop = scrollTop;
        textarea.scrollLeft = scrollLeft;
        syncBackdrop();
        updateCursor();
    }

    function setContext({
        path = '',
        section = '',
        slug = '',
        editable = false,
        message = '',
        images = null,
        unlistedSlides = null,
        folders = null,
        media = null,
        baselineContent = undefined,
    } = {}) {
        sense.setContext({
            path,
            section,
            slug,
            images: Array.isArray(images) ? images : undefined,
            unlistedSlides: Array.isArray(unlistedSlides) ? unlistedSlides : undefined,
            folders: Array.isArray(folders) ? folders : undefined,
        });
        const pathChanged = path !== filePath;
        filePath = path;
        if (typeof baselineContent === 'string') {
            baselineText = baselineContent;
            baselinePath = path;
        } else if (pathChanged || baselinePath !== path) {
            baselineText = textarea.value;
            baselinePath = path;
        }
        sense.setEnabled(Boolean(editable));
        const htmlLike = /\.(html?|shtml|php)$/i.test(path);
        const htmlPage = /\.(html?|shtml)$/i.test(path);
        snippetsOn = Boolean(editable && section === 'articles' && htmlLike);
        blocksOn = Boolean(editable && htmlPage);
        fileLabel.textContent = path || (message ? 'Loading…' : 'No file');
        fileLabel.title = path ? `${path} — open file in the code editor` : 'No file is open in the code editor';
        const mediaLabel = media?.isMedia ? mediaKindLabel(path, media) : '';
        language.textContent = mediaLabel || languageFor(path);
        language.title = mediaLabel ? mediaLabel : `${languageFor(path)} source`;
        snippetMenu.hidden = !snippetsOn || Boolean(media?.isMedia);
        findButton.disabled = !editable || Boolean(media?.isMedia);
        const showingMedia = Boolean(media?.isMedia);
        codeBody.hidden = showingMedia || !editable;
        blocker.hidden = showingMedia || editable;
        mediaCard.hidden = !showingMedia;
        if (showingMedia) renderMediaCard(mediaCard, media, path);
        blockerText.textContent = message || (path ? 'Page source is not available yet.' : 'Choose a file from the tree.');
        if (!editable) {
            findBar.hidden = true;
            clearJump();
        }
        textarea.disabled = !editable;
        if (!findBar.hidden) refreshFindMatches();
        refreshGutter();
        refreshHighlight();
        updateCursor();
    }

    function openFind() {
        if (textarea.disabled) return;
        findBar.hidden = false;
        findInput.focus();
        findInput.select();
        runFind(0);
    }

    function closeFind() {
        findBar.hidden = true;
        minimap.updateFind({ open: false });
        refreshHighlight();
        if (!textarea.disabled) textarea.focus();
    }

    function collectFindHits(query) {
        if (!query) return [];
        const hay = textarea.value.toLowerCase();
        const needle = query.toLowerCase();
        const hits = [];
        let from = 0;
        while (from <= hay.length - needle.length) {
            const at = hay.indexOf(needle, from);
            if (at < 0) break;
            hits.push(at);
            from = at + Math.max(needle.length, 1);
        }
        return hits;
    }

    function refreshFindMatches() {
        const query = findInput.value;
        if (!query) {
            findHits = [];
            findIndex = -1;
            findCount.textContent = '';
            findCount.title = 'Find matches in this file';
            minimap.updateFind({ open: false });
            return;
        }
        findHits = collectFindHits(query);
        if (!findHits.length) {
            findIndex = -1;
            findCount.textContent = 'No matches';
            findCount.title = 'No matches in this file';
            minimap.updateFind({ open: false });
            return;
        }
        if (findIndex < 0 || findIndex >= findHits.length) findIndex = 0;
        findCount.textContent = `${findIndex + 1} / ${findHits.length}`;
        findCount.title = `Match ${findIndex + 1} of ${findHits.length} in this file`;
        minimap.updateFind({
            hits: findHits,
            needleLength: query.length,
            activeIndex: findIndex,
            open: !findBar.hidden,
        });
    }

    function runFind(delta) {
        const query = findInput.value;
        if (!query) {
            findHits = [];
            findIndex = -1;
            findCount.textContent = '';
            findCount.title = 'Find matches in this file';
            minimap.updateFind({ open: false });
            refreshHighlight();
            return;
        }
        const needle = query.toLowerCase();
        findHits = collectFindHits(query);
        if (!findHits.length) {
            findIndex = -1;
            findCount.textContent = 'No matches';
            findCount.title = 'No matches in this file';
            minimap.updateFind({ open: false });
            refreshHighlight();
            return;
        }
        if (delta === 0) {
            const cursorAt = textarea.selectionStart || 0;
            findIndex = findHits.findIndex((pos) => pos >= cursorAt);
            if (findIndex < 0) findIndex = 0;
        } else {
            findIndex = (findIndex + delta + findHits.length) % findHits.length;
        }
        const pos = findHits[findIndex];
        const keepInputFocus = delta === 0 && document.activeElement === findInput;
        if (!keepInputFocus) textarea.focus({ preventScroll: true });
        textarea.setSelectionRange(pos, pos + needle.length);
        const matchLine = textarea.value.slice(0, pos).split('\n').length;
        scrollLineIntoView(matchLine);
        updateCursor();
        findCount.textContent = `${findIndex + 1} / ${findHits.length}`;
        findCount.title = `Match ${findIndex + 1} of ${findHits.length} in this file`;
        minimap.updateFind({
            hits: findHits,
            needleLength: needle.length,
            activeIndex: findIndex,
            open: !findBar.hidden,
        });
        refreshHighlight();
    }

    function insertSnippet(snippet) {
        const start = textarea.selectionStart || 0;
        const end = textarea.selectionEnd || 0;
        const before = textarea.value.slice(0, start);
        const prefix = before.length && !before.endsWith('\n') ? '\n' : '';
        const chunk = `${prefix}${snippet.replace(/\s+$/, '')}\n`;
        textarea.focus();
        textarea.setRangeText(chunk, start, end, 'end');
    }

    function indentSelection(outdent) {
        const value = textarea.value;
        const start = textarea.selectionStart;
        const end = textarea.selectionEnd;
        const lineStart = value.lastIndexOf('\n', Math.max(0, start - 1)) + 1;
        if (start === end && !outdent) {
            textarea.setRangeText('    ', start, end, 'end');
            return;
        }
        let blockEnd = value.indexOf('\n', start === end ? start : end);
        if (end > start && value[end - 1] === '\n') blockEnd = end - 1;
        else if (blockEnd < 0) blockEnd = value.length;
        const block = value.slice(lineStart, blockEnd);
        const next = block.split('\n').map((line) => {
            if (!outdent) return `    ${line}`;
            if (line.startsWith('    ')) return line.slice(4);
            if (line.startsWith('\t')) return line.slice(1);
            return line.replace(/^ {1,3}/, '');
        }).join('\n');
        textarea.setRangeText(next, lineStart, Math.max(lineStart, blockEnd), 'start');
        textarea.selectionStart = lineStart;
        textarea.selectionEnd = lineStart + next.length;
    }

    function toggleComment() {
        if (textarea.disabled) return false;
        sense.dismiss();
        const edit = computeCommentToggle(
            textarea.value,
            textarea.selectionStart || 0,
            textarea.selectionEnd || 0,
            filePath,
        );
        if (!edit) return false;
        applyTextareaEdit(edit.from, edit.to, edit.insert, edit.selectionStart, edit.selectionEnd);
        return true;
    }

    function applyTextareaEdit(from, to, insert, selectionStart, selectionEnd) {
        const scrollTop = textarea.scrollTop;
        const scrollLeft = textarea.scrollLeft;
        const beforeValue = textarea.value;
        textarea.focus({ preventScroll: true });
        textarea.setSelectionRange(from, to);
        let applied = false;
        try {
            if (typeof document.execCommand === 'function') {
                applied = document.execCommand('insertText', false, insert);
            }
        } catch {
            applied = false;
        }
        const expected = `${beforeValue.slice(0, from)}${insert}${beforeValue.slice(to)}`;
        if (!applied || textarea.value !== expected) {
            textarea.setRangeText(insert, from, to, 'end');
            textarea.dispatchEvent(new InputEvent('input', { bubbles: true, inputType: 'insertText', data: insert }));
        }
        const max = textarea.value.length;
        const selStart = clampOffset(selectionStart, max);
        const selEnd = Math.max(selStart, clampOffset(selectionEnd, max));
        textarea.setSelectionRange(selStart, selEnd);
        textarea.scrollTop = scrollTop;
        textarea.scrollLeft = scrollLeft;
        syncBackdrop();
        updateCursor();
    }

    function refreshGutter() {
        const count = Math.max(1, textarea.value.split('\n').length);
        currentDiff = computeLineDiff(baselineText, textarea.value);
        const hasDiff = Boolean(currentDiff?.hasChanges);
        if (inlineMark || hasDiff) {
            const rows = [];
            for (let index = 0; index < count; index += 1) {
                const number = String(index + 1);
                const line = index + 1;
                const classes = [];
                if (inlineMark && line >= inlineMark.startLine && line <= inlineMark.endLine) {
                    classes.push('is-inline-line');
                }
                if (hasDiff) {
                    const kind = currentDiff.lineKinds[index];
                    if (kind === DIFF_ADDED) classes.push('is-diff-add');
                    else if (kind === DIFF_MODIFIED) classes.push('is-diff-mod');
                    if (currentDiff.deletedAfter[index]) classes.push('is-diff-del');
                }
                if (classes.length) rows.push(`<span class="${classes.join(' ')}">${number}</span>`);
                else rows.push(number);
            }
            gutter.innerHTML = rows.join('\n');
        } else {
            let text = '1';
            if (count > 1) {
                const nums = new Array(count);
                for (let index = 0; index < count; index += 1) nums[index] = String(index + 1);
                text = nums.join('\n');
            }
            gutter.textContent = text;
        }
        gutter.style.width = `${String(count).length + 1.5}ch`;
        lines.textContent = `${count} line${count === 1 ? '' : 's'}`;
        lines.title = count === 1 ? '1 line in this file' : `${count} lines in this file`;

        if (hasDiff) {
            const bits = [];
            const tips = [];
            if (currentDiff.addedCount) {
                bits.push(`+${currentDiff.addedCount}`);
                tips.push(`${currentDiff.addedCount} added`);
            }
            if (currentDiff.modifiedCount) {
                bits.push(`~${currentDiff.modifiedCount}`);
                tips.push(`${currentDiff.modifiedCount} modified`);
            }
            if (currentDiff.deletedCount) {
                bits.push(`-${currentDiff.deletedCount}`);
                tips.push(`${currentDiff.deletedCount} deleted`);
            }
            diffBadge.textContent = bits.join(' ');
            diffBadge.title = `Git / editor comparison: ${tips.join(', ')}`;
            diffBadge.hidden = false;
        } else {
            diffBadge.textContent = '';
            diffBadge.hidden = true;
        }

        minimap.updateDocument({
            source: textarea.value,
            lang: languageFromPath(filePath),
            diff: currentDiff,
        });
    }

    function refreshHighlight() {
        const lang = languageFromPath(filePath);
        const hasFindMarks = !findBar.hidden && findHits.length > 0 && findInput.value.length > 0;
        const enabled = (lang !== 'text' || hasFindMarks) && !textarea.disabled;
        textarea.classList.toggle('is-syntax', enabled);
        if (!enabled) {
            highlightCode.textContent = '';
            syncBackdrop();
            return;
        }
        highlightCode.innerHTML = highlightDocument(textarea.value, lang);
        if (hasFindMarks) {
            applyBackdropFindMarks(highlightCode, findHits, findInput.value.length, findIndex);
        }
        syncBackdrop();
    }

    function syncBackdrop() {
        const top = textarea.scrollTop;
        const left = textarea.scrollLeft;
        if (gutter.scrollTop !== top) gutter.scrollTop = top;
        if (highlight.scrollTop !== top) highlight.scrollTop = top;
        if (highlight.scrollLeft !== left) highlight.scrollLeft = left;
        minimap.syncScroll();
    }

    function updateCursor() {
        const point = textarea.selectionStart || 0;
        const upto = textarea.value.slice(0, point);
        const line = upto.split('\n').length;
        const col = point - upto.lastIndexOf('\n');
        cursor.textContent = `Ln ${line}, Col ${col}`;
        cursor.title = `Line ${line}, column ${col}`;
        minimap.updateSelection({
            selectionStart: textarea.selectionStart || 0,
            selectionEnd: textarea.selectionEnd || 0,
        });
    }

    function revealRange(startOffset, endOffset, label) {
        if (typeof ctx.onShowCode === 'function') ctx.onShowCode();
        if (textarea.disabled) return false;
        const max = textarea.value.length;
        let start = Math.max(0, Math.min(Number(startOffset) || 0, max));
        let end = Math.max(0, Math.min(Number(endOffset) || 0, max));
        if (end < start) {
            const swap = start;
            start = end;
            end = swap;
        }
        textarea.focus({ preventScroll: true });
        textarea.setSelectionRange(start, end);
        updateCursor();
        const startLine = textarea.value.slice(0, start).split('\n').length;
        const endIndex = Math.max(start, end - 1);
        const endLine = textarea.value.slice(0, endIndex).split('\n').length;
        jumpMark = { startLine, endLine, kind: 'jump' };
        minimap.updateTarget(inlineMark || jumpMark);
        const phrase = startLine === endLine ? `Line ${startLine}` : `Lines ${startLine}\u2013${endLine}`;
        showJump(label ? `Jumped to ${phrase} (${label})` : `Jumped to ${phrase}`);
        scrollLineIntoView(startLine);
        return true;
    }

    function highlightRange(startOffset, endOffset, label, options = {}) {
        if (options.showCode !== false && typeof ctx.onShowCode === 'function') ctx.onShowCode();
        if (textarea.disabled) return false;
        const max = textarea.value.length;
        let start = Math.max(0, Math.min(Number(startOffset) || 0, max));
        let end = Math.max(0, Math.min(Number(endOffset) || 0, max));
        if (end < start) {
            const swap = start;
            start = end;
            end = swap;
        }
        const startLine = textarea.value.slice(0, start).split('\n').length;
        const endLine = textarea.value.slice(0, Math.max(start, end - 1)).split('\n').length;
        inlineMark = { startLine, endLine, kind: 'inline' };
        minimap.updateTarget(inlineMark);
        refreshGutter();
        shell.classList.add('is-inline-target');
        const phrase = startLine === endLine ? `Line ${startLine}` : `Lines ${startLine}\u2013${endLine}`;
        showJump(label ? `Editing ${phrase} (${label})` : `Editing ${phrase}`, { sticky: true, tone: 'edit' });
        if (options.scroll !== false) scrollLineIntoView(startLine);
        return true;
    }

    function clearHighlight() {
        inlineMark = null;
        minimap.updateTarget(jumpMark);
        shell.classList.remove('is-inline-target');
        delete jumpBadge.dataset.tone;
        clearJump();
        refreshGutter();
    }

    function scrollLineIntoView(startLine) {
        const scroll = () => {
            if (textarea.clientHeight > 0) scrollBlockIntoView(startLine);
        };
        requestAnimationFrame(() => {
            scroll();
            if (textarea.clientHeight <= 0) requestAnimationFrame(scroll);
        });
    }

    function scrollBlockIntoView(startLine) {
        const style = getComputedStyle(textarea);
        const fontSize = Number.parseFloat(style.fontSize) || 13;
        const parsed = Number.parseFloat(style.lineHeight);
        const lineHeight = Number.isFinite(parsed) ? parsed : fontSize * 1.5;
        const editorHeight = textarea.clientHeight || 0;
        const top = Math.max(0, (startLine - 1) * lineHeight - editorHeight / 3);
        textarea.scrollTo({ top, behavior: 'smooth' });
        syncBackdrop();
    }

    function showJump(text, options = {}) {
        const sticky = options.sticky === true;
        window.clearTimeout(jumpTimer);
        jumpBadge.hidden = false;
        jumpBadge.textContent = text;
        jumpBadge.title = text;
        if (options.tone) jumpBadge.dataset.tone = options.tone;
        else delete jumpBadge.dataset.tone;
        jumpBadge.classList.remove('is-on');
        void jumpBadge.offsetWidth;
        if (!sticky) jumpBadge.classList.add('is-on');
        shell.classList.toggle('is-jumping', !sticky);
        if (sticky) return;
        jumpTimer = window.setTimeout(() => {
            jumpBadge.classList.remove('is-on');
            shell.classList.remove('is-jumping');
            jumpMark = null;
            minimap.updateTarget(inlineMark);
            jumpTimer = window.setTimeout(() => {
                if (!shell.classList.contains('is-inline-target')) jumpBadge.hidden = true;
            }, 280);
        }, 3400);
    }

    function clearJump() {
        window.clearTimeout(jumpTimer);
        jumpMark = null;
        minimap.updateTarget(inlineMark);
        jumpBadge.classList.remove('is-on');
        jumpBadge.hidden = true;
        shell.classList.remove('is-jumping');
    }

    if (typeof ResizeObserver === 'function') {
        const observer = new ResizeObserver(() => syncBackdrop());
        observer.observe(stage);
    }
    refreshGutter();
    refreshHighlight();
    return {
        setValue,
        getValue: () => textarea.value,
        getCaret() {
            return {
                selectionStart: textarea.selectionStart || 0,
                selectionEnd: textarea.selectionEnd || 0,
                scrollTop: textarea.scrollTop || 0,
                scrollLeft: textarea.scrollLeft || 0,
            };
        },
        setCaret(caret = {}) {
            const max = textarea.value.length;
            const start = clampOffset(caret.selectionStart, max);
            const end = Math.max(start, clampOffset(caret.selectionEnd, max));
            try {
                textarea.selectionStart = start;
                textarea.selectionEnd = end;
            } catch { /* the field can be disabled while a page is loading */ }
            const apply = () => {
                textarea.scrollTop = Number(caret.scrollTop) || 0;
                textarea.scrollLeft = Number(caret.scrollLeft) || 0;
                syncBackdrop();
            };
            apply();
            requestAnimationFrame(apply);
            updateCursor();
        },
        setContext,
        setBaseline(text) {
            baselineText = typeof text === 'string' ? text : textarea.value;
            refreshGutter();
        },
        focus: () => { if (!textarea.disabled) textarea.focus(); },
        openFind,
        closeFind,
        isFindOpen: () => !findBar.hidden,
        revealRange,
        highlightRange,
        clearHighlight,
        insertBlock,
        canInsertBlocks: () => Boolean(blocksOn && !textarea.disabled),
        toggleComment,
        canToggleComment: () => !textarea.disabled,
    };

    function insertBlock(id) {
        const block = BLOCKS.find((item) => item.id === id);
        if (!block || !blocksOn || textarea.disabled) return false;
        insertSnippet(block.html);
        textarea.focus();
        return true;
    }
}

function applyBackdropFindMarks(container, hits, needleLen, activeIdx) {
    if (!container || !hits.length || needleLen <= 0) return;
    const walker = document.createTreeWalker(container, NodeFilter.SHOW_TEXT);
    const nodes = [];
    let current = walker.nextNode();
    let offset = 0;
    while (current) {
        const text = current.nodeValue || '';
        if (text.length) {
            nodes.push({ node: current, start: offset, end: offset + text.length });
            offset += text.length;
        }
        current = walker.nextNode();
    }

    let hitIdx = 0;
    for (let n = 0; n < nodes.length && hitIdx < hits.length; n += 1) {
        let { node, start } = nodes[n];
        const end = nodes[n].end;
        while (hitIdx < hits.length && hits[hitIdx] + needleLen <= start) hitIdx += 1;
        let checkIdx = hitIdx;
        while (checkIdx < hits.length && hits[checkIdx] < end) {
            const matchStart = Math.max(start, hits[checkIdx]);
            const matchEnd = Math.min(end, hits[checkIdx] + needleLen);
            if (matchEnd > matchStart) {
                const relStart = matchStart - start;
                const relLen = matchEnd - matchStart;
                let targetNode = node;
                if (relStart > 0) {
                    targetNode = node.splitText(relStart);
                    start += relStart;
                }
                const remainder = relLen < targetNode.nodeValue.length ? targetNode.splitText(relLen) : null;
                const mark = document.createElement('span');
                mark.className = checkIdx === activeIdx ? 'tok-find-hit is-current' : 'tok-find-hit';
                targetNode.parentNode.replaceChild(mark, targetNode);
                mark.appendChild(targetNode);
                start += relLen;
                if (!remainder) break;
                node = remainder;
            }
            if (hits[checkIdx] + needleLen <= end) {
                checkIdx += 1;
                hitIdx = checkIdx;
            } else {
                break;
            }
        }
    }
}

function clampOffset(value, max) {
    const number = Number(value);
    if (!Number.isFinite(number)) return 0;
    return Math.max(0, Math.min(max, number));
}

function languageFor(path) {
    const lang = languageFromPath(path);
    if (lang === 'php') return 'PHP';
    if (lang === 'html') return 'HTML';
    if (lang === 'css') return 'CSS';
    if (lang === 'js') return 'JavaScript';
    if (lang === 'json') return 'JSON';
    return 'Text';
}

function featureSingle() {
    return `    <div class="articleFeatureSingle">
      <figure>
        <a class="articleLightboxLink" href="slides/photo.jpg" data-lightbox="gallery" data-title="Caption">
          <img src="slides/photo.jpg" alt="Caption" title="Caption" decoding="async">
        </a>
        <figcaption class="bodyCaption centerText">Caption</figcaption>
      </figure>
    </div>`;
}

function featurePair(modifier, left, right) {
    return `    <div class="articleFeaturePair ${modifier}">
${figure(left)}
${figure(right)}
    </div>`;
}

function figure(file) {
    return `      <figure>
        <a class="articleLightboxLink" href="slides/${file}" data-lightbox="gallery" data-title="Caption">
          <img src="thumbs/${file}" alt="Caption" title="Caption" loading="lazy" decoding="async">
        </a>
        <figcaption class="bodyCaption centerText">Caption</figcaption>
      </figure>`;
}

function gallerySection() {
    return `    <section class="articleGallerySection" aria-label="Photo Gallery">
      <h2 class="centerText">Photo Gallery</h2>
      <p class="centerText bodyCaption articleGalleryNote">Click any photo to view full size.</p>
      <div class="thumbs-wrapper">
        <div class="thumbs">
          <div class="thumbnail-wrapper"><a href="slides/photo.jpg" data-lightbox="gallery" data-title="Caption"><img src="thumbs/photo.jpg" class="thumbnail" alt="Caption" title="Caption" loading="lazy" decoding="async"></a></div>
        </div>
      </div>
    </section>`;
}

function renderMediaCard(host, media, path) {
    const filePath = media?.path || path || '';
    const name = fileBaseName(filePath);
    const href = publicHref(filePath);
    const kind = mediaKindLabel(filePath, media);
    const bits = [kind, formatBytes(media?.fileSize)];
    if (Number(media?.width) > 0 && Number(media?.height) > 0) bits.push(`${media.width} × ${media.height}`);
    const link = `<a href="${escapeHtml(href)}" target="_blank">${escapeHtml(name)}</a>`;
    const imageTag = `<img src="${escapeHtml(href)}" alt="${escapeHtml(altText(name))}" loading="lazy">`;
    const body = el('div', { class: 'media-card-body' });
    body.append(el('p', { class: 'media-kicker', text: 'Media / Document' }));
    body.append(el('h2', { text: name || 'File' }));
    body.append(el('p', { class: 'media-path', text: filePath }));
    const meta = el('div', { class: 'media-meta' });
    for (const bit of bits.filter(Boolean)) meta.append(el('span', { class: 'mini', text: bit }));
    body.append(meta);
    body.append(el('p', { class: 'media-kicker', text: 'HTML link' }));
    body.append(el('pre', { class: 'media-snippet', text: link }));
    const actions = el('div', { class: 'media-actions' });
    actions.append(copyButton('Copy HTML Link', link));
    if (isImageMedia(filePath, media)) {
        body.append(el('p', { class: 'media-kicker', text: 'Image tag' }));
        body.append(el('pre', { class: 'media-snippet', text: imageTag }));
        actions.append(copyButton('Copy Image Tag', imageTag));
    }
    body.append(actions);
    host.replaceChildren(body);
}

function copyButton(label, text) {
    const button = el('button', { class: 'btn', type: 'button', text: label, title: label });
    button.addEventListener('click', () => {
        const done = () => {
            const previous = button.textContent;
            button.textContent = 'Copied';
            window.setTimeout(() => { button.textContent = previous; }, 1200);
        };
        const write = navigator.clipboard?.writeText ? navigator.clipboard.writeText(text) : copyWithTextarea(text);
        Promise.resolve(write).then(done).catch(() => copyWithTextarea(text).then(done));
    });
    return button;
}

function copyWithTextarea(text) {
    const area = document.createElement('textarea');
    area.value = text;
    area.setAttribute('aria-hidden', 'true');
    document.body.append(area);
    area.select();
    document.execCommand('copy');
    area.remove();
    return Promise.resolve();
}

function mediaKindLabel(path, media) {
    const ext = fileExtension(path);
    const kind = String(media?.mediaKind || media?.fileType || '').toLowerCase();
    if (kind === 'pdf' || ext === 'pdf') return 'PDF Document';
    if (kind === 'image' || isImageMedia(path, media)) {
        const names = {
            jpg: 'JPEG Image',
            png: 'PNG Image',
            gif: 'GIF Image',
            webp: 'WebP Image',
            svg: 'SVG Image',
            bmp: 'BMP Image',
            tiff: 'TIFF Image',
        };
        return names[ext] || 'Image';
    }
    if (kind === 'video') return 'Video';
    if (kind === 'audio') return 'Audio';
    if (ext === 'doc' || ext === 'docx') return 'Word Document';
    if (kind === 'document') return 'Document';
    return 'File';
}

function isImageMedia(path, media) {
    const kind = String(media?.mediaKind || media?.fileType || '').toLowerCase();
    if (kind === 'image') return true;
    return ['jpg', 'jpeg', 'png', 'gif', 'webp', 'svg', 'bmp', 'tif', 'tiff'].includes(fileExtension(path));
}

function fileExtension(path) {
    const base = fileBaseName(path);
    const dot = base.lastIndexOf('.');
    if (dot <= 0) return '';
    const ext = base.slice(dot + 1).toLowerCase();
    if (ext === 'jpeg') return 'jpg';
    if (ext === 'tif') return 'tiff';
    return ext;
}

function fileBaseName(path) {
    const parts = String(path || '').split(/[\\/]/).filter(Boolean);
    return parts.length ? parts[parts.length - 1] : '';
}

function altText(name) {
    return String(name || 'image').replace(/\.[^.]+$/, '').replace(/\s+/g, ' ').trim() || 'image';
}

function publicHref(path) {
    return String(path || '').split('/').map((part, index) => {
        if (!part) return index === 0 ? '' : part;
        return encodeURIComponent(part);
    }).join('/');
}

function formatBytes(value) {
    const bytes = Number(value) || 0;
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(bytes < 10 * 1024 ? 1 : 0)} KB`;
    return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

function escapeHtml(value) {
    return String(value || '')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}
