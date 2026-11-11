import { locateElementInSource } from './preview-context.js';

const STYLE_ID = 'studio-inline-style';
const HTML_SELECTOR = 'h1, h2, h3, h4, h5, h6, p, figcaption, li, a, td, th, span, b, i, em, strong, .comment, .bodyCaption';
const HTML_TAGS = new Set(['h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'p', 'figcaption', 'li', 'a', 'td', 'th', 'span', 'b', 'i', 'em', 'strong']);
const BLOCKS = new Set(['ul', 'ol', 'div', 'p', 'blockquote', 'table', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'section', 'figure']);
const INLINE = new Set(['a', 'span', 'b', 'i', 'em', 'strong', 'small', 'br', 'img', 'sup', 'sub']);
const ENTITIES = {
    amp: '&', lt: '<', gt: '>', quot: '"', apos: "'", nbsp: '\u00A0', shy: '\u00AD',
};
const EDIT_STYLE = `
[data-studio-editable="1"] { cursor: text !important; }
[data-studio-hover="1"] {
  outline: 1px dashed #f59e0b !important;
  outline-offset: 2px !important;
  cursor: text !important;
}
[data-studio-editing="1"] {
  outline: 2px solid #f59e0b !important;
  outline-offset: 2px !important;
  cursor: text !important;
}
.studio-caption-editor {
  display: block;
  position: relative;
  z-index: 5;
  box-sizing: border-box;
  margin: 6px 0 0;
  padding: 4px 8px;
  min-height: 1.4em;
  border-radius: 4px;
  background: #fff7ed !important;
  color: #111111 !important;
  text-decoration: none !important;
  font: 14px/1.4 Georgia, "Times New Roman", serif;
  text-align: left;
  cursor: text !important;
  outline: 2px solid #f59e0b !important;
}
`;

export function mountPreviewInlineEdit(options) {
    const frame = options.frame;
    const viewButton = options.viewButton;
    const editButton = options.editButton;
    const root = options.root;
    let mode = 'view';
    let session = null;
    let hoverEl = null;
    let suspend = false;
    let detach = () => {};
    let detachCommands = () => {};

    function paint() {
        const editing = mode === 'edit';
        if (root) {
            root.dataset.viewportMode = mode;
            root.setAttribute('aria-label', editing ? 'Mode: Edit' : 'Mode: View');
        }
        viewButton?.setAttribute('aria-pressed', editing ? 'false' : 'true');
        editButton?.setAttribute('aria-pressed', editing ? 'true' : 'false');
        if (options.stage) options.stage.dataset.viewportMode = mode;
    }

    function setMode(next) {
        const wanted = next === 'edit' ? 'edit' : 'view';
        if (wanted === 'edit' && editButton?.disabled) return;
        if (wanted === mode) {
            paint();
            return;
        }
        mode = wanted;
        paint();
        if (mode === 'view') disarm();
        else arm();
        options.onMode?.(mode);
    }

    function setEnabled(on) {
        const enabled = Boolean(on);
        const viewTip = 'View Mode (Cyan): Links and Lightbox work normally as on the live website (Ctrl+E to toggle)';
        const editTip = 'Inline Edit Mode (Amber): Click any heading, paragraph, caption, or vehicle field in the preview to edit text directly in place (Ctrl+E to toggle)';
        if (viewButton) viewButton.title = viewTip;
        if (editButton) {
            editButton.disabled = !enabled;
            editButton.title = enabled
                ? editTip
                : 'Inline Edit Mode (Amber) needs a page with source loaded (Ctrl+E to toggle)';
        }
        if (!enabled && mode === 'edit') setMode('view');
    }

    function currentDoc() {
        try { return frame?.contentDocument || null; } catch { return null; }
    }

    function usable(doc) {
        const href = String(doc?.URL || '');
        return Boolean(doc?.body && href && href !== 'about:blank');
    }

    function arm() {
        detach();
        const doc = currentDoc();
        if (!usable(doc)) return;
        scrub(doc);
        injectStyle(doc);
        doc.documentElement.dataset.studioViewport = 'edit';
        if (isSalesVehicle(options.getPath?.())) armSales(doc);
        else markHtml(doc.body);
        listen(doc);
    }

    function disarm() {
        if (session) syncSession(false);
        suspend = true;
        session = null;
        hoverEl = null;
        detach();
        const doc = currentDoc();
        if (doc) scrub(doc);
        suspend = false;
    }

    function onFrameLoad() {
        session = null;
        hoverEl = null;
        detach();
        bindCommands();
        if (mode === 'edit') arm();
    }

    function bindCommands() {
        detachCommands();
        const doc = currentDoc();
        if (!usable(doc)) return;
        const onKey = (event) => {
            const key = String(event.key || '').toLowerCase();
            const mod = event.ctrlKey || event.metaKey;
            const refresh = (key === 'f5' && !event.altKey) || (mod && key === 'r' && !event.altKey);
            if (refresh) {
                event.preventDefault();
                event.stopPropagation();
                if (!event.repeat) options.onPreviewReload?.();
                return;
            }
            if (!mod || event.altKey) return;
            if (!options.onCommand?.(key, event)) return;
            event.preventDefault();
            event.stopPropagation();
        };
        const onFocus = (event) => {
            if (event.target?.isContentEditable) options.onEditTarget?.(event.target);
        };
        doc.addEventListener('keydown', onKey, true);
        doc.addEventListener('focusin', onFocus, true);
        detachCommands = () => {
            try {
                doc.removeEventListener('keydown', onKey, true);
                doc.removeEventListener('focusin', onFocus, true);
            } catch { /* the preview document was replaced */ }
        };
    }

    viewButton?.addEventListener('click', () => setMode('view'));
    editButton?.addEventListener('click', () => setMode('edit'));
    root?.addEventListener('keydown', (event) => {
        if (event.key !== 'ArrowLeft' && event.key !== 'ArrowRight') return;
        event.preventDefault();
        const next = mode === 'view' ? 'edit' : 'view';
        setMode(next);
        (next === 'edit' ? editButton : viewButton)?.focus();
    });
    frame?.addEventListener('load', onFrameLoad);
    bindCommands();
    paint();

    function listen(doc) {
        detach();
        const onMouseDown = (event) => beginFromEvent(event);
        const onClick = (event) => blockActivation(event, false);
        const onAux = (event) => blockActivation(event, true);
        const onMove = (event) => moveHover(event);
        const onLeave = () => setHover(null);
        const onInput = (event) => {
            if (!session || suspend) return;
            if (event.target !== session.element && !session.element.contains(event.target)) return;
            syncSession(false);
        };
        const onFocusOut = (event) => {
            if (suspend || !session) return;
            if (event.target !== session.element && !session.element.contains(event.target)) return;
            const next = event.relatedTarget;
            if (next && (next === session.element || session.element.contains(next))) return;
            syncSession(true);
            deactivate(session.element);
        };
        const onKeyDown = (event) => onFrameKey(event);
        doc.addEventListener('mousedown', onMouseDown, true);
        doc.addEventListener('click', onClick, true);
        doc.addEventListener('auxclick', onAux, true);
        doc.addEventListener('mousemove', onMove, true);
        doc.addEventListener('input', onInput, true);
        doc.addEventListener('focusout', onFocusOut, true);
        const onFocusIn = (event) => options.onEditTarget?.(event.target);
        doc.addEventListener('keydown', onKeyDown, true);
        doc.addEventListener('focusin', onFocusIn, true);
        doc.documentElement.addEventListener('mouseleave', onLeave);
        detach = () => {
            try {
                doc.removeEventListener('mousedown', onMouseDown, true);
                doc.removeEventListener('click', onClick, true);
                doc.removeEventListener('auxclick', onAux, true);
                doc.removeEventListener('mousemove', onMove, true);
                doc.removeEventListener('input', onInput, true);
                doc.removeEventListener('focusout', onFocusOut, true);
                doc.removeEventListener('keydown', onKeyDown, true);
                doc.removeEventListener('focusin', onFocusIn, true);
                doc.documentElement.removeEventListener('mouseleave', onLeave);
            } catch { /* the preview document was replaced */ }
        };
    }

    function beginFromEvent(event) {
        if (mode !== 'edit' || event.button !== 0 || suspend) return;
        const found = resolve(event.target);
        if (!found) return;
        if (found.type === 'photo-image' || found.type === 'photo-editor') {
            event.preventDefault();
            event.stopPropagation();
        }
        const same = session && matchesSession(found);
        if (session && !same) {
            syncSession(true);
            deactivate(session.element);
            session = null;
        }
        if (same && session) {
            activate(session.element);
            if (session.kind === 'photo') session.element.focus();
            return;
        }
        const next = createSession(found);
        if (!next) {
            if (found.element?.hasAttribute?.('data-studio-editable')) {
                options.onAnnounce?.('That text is not in this file’s source.');
            }
            return;
        }
        session = next;
        activate(session.element);
        setHover(null);
        options.onEditTarget?.(session.element);
        if (session.kind === 'photo') session.element.focus();
        if (session.highlightEnd > session.highlightStart) options.onHighlight?.(sessionRange(), { scroll: true });
    }

    function matchesSession(found) {
        if (!session) return false;
        if (session.element === found.element || session.anchor === found.element) return true;
        if (found.type === 'photo-image' && session.anchor === found.element) return true;
        if (found.type === 'photo-editor' && session.element === found.element) return true;
        return false;
    }

    function blockActivation(event, aux) {
        if (mode !== 'edit') return;
        const origin = event.target?.nodeType === 3 ? event.target.parentElement : event.target;
        const anchor = origin?.closest?.('a');
        if (anchor) {
            event.preventDefault();
            event.stopPropagation();
        }
        if (aux || !session) return;
        if (session.kind === 'photo' && session.anchor && (origin === session.anchor || session.anchor.contains?.(origin))) {
            session.element.focus();
            return;
        }
        if (origin === session.element || session.element.contains(origin)) placeCaret(session.element, event);
    }

    function onFrameKey(event) {
        if (mode !== 'edit' || !session) return;
        const inField = event.target === session.element || session.element.contains(event.target);
        if (!inField) return;
        if (event.key === 'Escape') {
            event.preventDefault();
            session.element.blur();
            return;
        }
        if (event.key === 'Enter' && session.singleLine) {
            event.preventDefault();
            session.element.blur();
            return;
        }
        if (event.key === 'Enter') {
            event.preventDefault();
            insertNewline(session.element);
            syncSession(false);
        }
    }

    function moveHover(event) {
        if (mode !== 'edit') return;
        const found = resolve(event.target);
        const el = found?.element || null;
        if (!el || el === session?.element || el === session?.anchor || el.classList?.contains('studio-caption-editor')) {
            setHover(null);
            return;
        }
        setHover(el);
    }

    function setHover(el) {
        if (hoverEl === el) return;
        if (hoverEl?.isConnected) hoverEl.removeAttribute('data-studio-hover');
        hoverEl = el;
        if (hoverEl) hoverEl.setAttribute('data-studio-hover', '1');
    }

    function createSession(found) {
        const source = String(options.getSource?.() || '');
        if (found.type === 'photo-image' || found.type === 'photo-bullet' || found.type === 'photo-editor') {
            return bindPhoto(found, source);
        }
        if (!source.trim()) return null;
        if (found.type === 'vehicleName' || found.type === 'price' || found.type === 'inventory') {
            return bindAssignment(found, source);
        }
        if (found.type === 'description') return bindDescription(source, found.element);
        return bindHtmlElement(source, found.element, options.getFileType?.() || 'html');
    }

    function bindAssignment(found, source) {
        const name = found.type === 'inventory' ? 'inventoryNumber' : found.type;
        const spec = assignmentValueRange(source, name);
        if (!spec) return null;
        const slice = source.slice(spec.start, spec.end);
        return {
            kind: 'php',
            name,
            element: found.element,
            anchor: found.element,
            spec: { ...spec },
            sourceStart: spec.start,
            sourceEnd: spec.end,
            slice,
            live: decodePhpValue(slice, spec),
            highlightStart: spec.statementStart,
            highlightEnd: spec.statementEnd,
            label: `$${name}`,
            singleLine: true,
            extract: found.type === 'price' ? priceSourceValue : found.type === 'inventory' ? inventorySourceValue : cleanLine,
        };
    }

    function bindDescription(source, element) {
        const live = readLiveText(element);
        const direct = descriptionTextRange(source, live);
        if (direct) {
            return htmlSession(element, source.slice(direct.start, direct.end), direct.start, direct.end, direct.start, direct.end, '$description', live);
        }
        return bindHtmlElement(source, element, 'php', '$description');
    }

    function bindHtmlElement(source, element, fileType, label) {
        const found = rangeForElement(source, element, fileType);
        if (found) {
            return htmlSession(element, found.slice, found.start, found.end, found.highlightStart, found.highlightEnd, label || found.label, readLiveText(element));
        }
        const live = readLiveText(element);
        const unique = uniqueTextRange(source, live);
        if (!unique) return null;
        return htmlSession(element, source.slice(unique.start, unique.end), unique.start, unique.end, unique.start, unique.end, label || labelFor(element), live);
    }

    function htmlSession(element, slice, start, end, highlightStart, highlightEnd, label, live) {
        return {
            kind: 'html',
            element,
            anchor: element,
            sourceStart: start,
            sourceEnd: end,
            slice,
            live,
            highlightStart,
            highlightEnd,
            label: label || labelFor(element),
            singleLine: false,
        };
    }

    function bindPhoto(found, source) {
        let img = null;
        let editor = found.element;
        if (found.type === 'photo-image') {
            img = found.element;
            editor = ensureCaptionEditor(img);
        } else if (found.type === 'photo-editor') {
            editor = found.element;
            img = found.element._studioImg || null;
        }
        const names = photoNames(found, img);
        const highlight = imageDataHighlight(source);
        if (!names.length) options.onAnnounce?.('That caption is not tied to a gallery photo.');
        return {
            kind: 'photo',
            element: editor,
            anchor: img || editor,
            names,
            live: readLiveText(editor),
            singleLine: true,
            highlightStart: highlight?.startOffset || 0,
            highlightEnd: highlight?.endOffset || 0,
            label: highlight?.label || 'photo caption',
        };
    }

    function photoNames(found, img) {
        const images = options.listPhotos?.() || [];
        if (found.type === 'photo-bullet') {
            const caption = cleanLine(readLiveText(found.element));
            return images
                .filter((image) => cleanLine(image?.caption || '') === caption && image?.name)
                .map((image) => image.name);
        }
        const file = baseName(img?.getAttribute?.('src') || '');
        const hit = images.find((image) => [image?.name, image?.slideUrl, image?.thumbUrl]
            .some((value) => baseName(value) === file));
        if (hit?.name) return [hit.name];
        return file ? [file] : [];
    }

    function syncSession(immediate) {
        if (suspend || !session) return false;
        const wrote = session.kind === 'photo'
            ? syncPhoto(immediate)
            : session.kind === 'php'
                ? syncPhp(immediate)
                : syncHtml(immediate);
        if (immediate && !wrote) options.onFlush?.();
        return wrote;
    }

    function syncHtml(immediate) {
        const nextLive = readLiveText(session.element);
        if (nextLive === session.live) return false;
        const source = String(options.getSource?.() || '');
        if (!relocateHtml(session, source)) {
            warnOnce('Could not match that text in the source.');
            return false;
        }
        const nextSlice = spliceDomEdit(session.slice, session.live, nextLive);
        if (nextSlice == null) {
            warnOnce('Could not match that text in the source.');
            return false;
        }
        if (nextSlice === session.slice) {
            session.live = nextLive;
            return false;
        }
        return commitSlice(source, nextSlice, nextLive, immediate);
    }

    function syncPhp(immediate) {
        const value = session.extract(readLiveText(session.element));
        if (value === session.live) return false;
        const source = String(options.getSource?.() || '');
        if (!relocatePhp(session, source)) {
            warnOnce('Could not match that field in the source.');
            return false;
        }
        const encoded = encodePhpValue(value, session.spec);
        const oldStart = session.sourceStart;
        const oldEnd = session.sourceEnd;
        const delta = encoded.length - (oldEnd - oldStart);
        const updated = source.slice(0, oldStart) + encoded + source.slice(oldEnd);
        let nextSlice = encoded;
        let nextStart = oldStart;
        let nextEnd = oldStart + encoded.length;
        if (!session.spec.quote && encoded.length >= 2 && encoded.startsWith("'") && encoded.endsWith("'")) {
            nextSlice = encoded.slice(1, -1);
            nextStart = oldStart + 1;
            nextEnd = oldStart + encoded.length - 1;
            session.spec = { ...session.spec, quote: "'", numeric: false };
        }
        session.highlightEnd += delta;
        session.sourceStart = nextStart;
        session.sourceEnd = nextEnd;
        session.slice = nextSlice;
        session.live = value;
        session.spec.start = nextStart;
        session.spec.end = nextEnd;
        options.onSource?.(updated, { immediate, scroll: false, range: sessionRange() });
        return true;
    }

    function syncPhoto(immediate) {
        let next = readLiveText(session.element);
        if (session.singleLine) next = next.replace(/[\r\n]+/g, ' ');
        if (immediate) next = cleanLine(next);
        if (next === session.live) return false;
        const previous = session.live;
        session.live = next;
        if (immediate && session.element.textContent !== next) session.element.textContent = next;
        paintPhoto(previous, next);
        options.onPhotoCaption?.({
            names: session.names,
            caption: next,
            immediate,
            scroll: false,
            range: session.highlightEnd > session.highlightStart ? sessionRange() : null,
        });
        return true;
    }

    function paintPhoto(previous, next) {
        const doc = session.element.ownerDocument;
        if (!doc) return;
        const files = new Set((session.names || []).map((name) => baseName(name)));
        doc.querySelectorAll('img.hero, img.thumbnail').forEach((img) => {
            if (!files.has(baseName(img.getAttribute('src') || ''))) return;
            img.setAttribute('alt', next);
            img.setAttribute('title', next);
            const link = img.closest('a');
            if (link?.hasAttribute('data-title')) link.setAttribute('data-title', next);
        });
        doc.querySelectorAll('.photoCaptions li').forEach((li) => {
            if (li === session.element) return;
            if (cleanLine(li.textContent) === cleanLine(previous)) li.textContent = next;
        });
    }

    function commitSlice(source, nextSlice, nextLive, immediate) {
        const oldStart = session.sourceStart;
        const oldEnd = session.sourceEnd;
        const delta = nextSlice.length - (oldEnd - oldStart);
        const updated = source.slice(0, oldStart) + nextSlice + source.slice(oldEnd);
        if (session.highlightStart >= oldEnd) session.highlightStart += delta;
        else if (session.highlightStart > oldStart) session.highlightStart += delta;
        if (session.highlightEnd >= oldEnd) session.highlightEnd += delta;
        else if (session.highlightEnd > oldStart) session.highlightEnd += delta;
        session.sourceEnd = oldStart + nextSlice.length;
        session.slice = nextSlice;
        session.live = nextLive;
        options.onSource?.(updated, { immediate, scroll: false, range: sessionRange() });
        return true;
    }

    function sessionRange() {
        return {
            startOffset: session.highlightStart,
            endOffset: session.highlightEnd,
            label: session.label,
        };
    }

    function warnOnce(message) {
        if (!session || session.warned) return;
        session.warned = true;
        options.onAnnounce?.(message);
    }

    function resolve(target) {
        return isSalesVehicle(options.getPath?.()) ? findSales(target) : findHtml(target);
    }

    return {
        setMode,
        getMode: () => mode,
        setEnabled,
        isSessionActive: () => Boolean(session),
        refresh() {
            if (mode === 'edit') arm();
        },
        destroy() {
            frame?.removeEventListener('load', onFrameLoad);
            if (mode !== 'view') setMode('view');
            else disarm();
        },
    };
}

function armSales(doc) {
    for (const selector of ['.idTitle h1', '.price h2', '.inventory h2', '.photoCaptions li', 'img.hero', 'img.thumbnail']) {
        doc.querySelectorAll(selector).forEach((el) => {
            if (!isSkipped(el)) el.setAttribute('data-studio-editable', '1');
        });
    }
    doc.querySelectorAll('.description').forEach((box) => {
        if (box.classList.contains('expandable-lists-container') || isSkipped(box)) return;
        wrapLooseText(box);
        markHtml(box);
    });
}

function markHtml(root) {
    if (!root?.querySelectorAll) return;
    root.querySelectorAll(HTML_SELECTOR).forEach((el) => {
        if (isSkipped(el) || !isHtmlCandidate(el)) return;
        el.setAttribute('data-studio-editable', '1');
    });
}

function wrapLooseText(root) {
    for (const group of splitByBreaks(root)) {
        if (group.length === 1 && group[0].nodeType === 1 && BLOCKS.has(group[0].tagName.toLowerCase())) {
            markHtml(group[0]);
            continue;
        }
        const inline = group.every((node) => node.nodeType === 3 || INLINE.has(String(node.tagName || '').toLowerCase()));
        if (!inline) {
            group.forEach((node) => { if (node.nodeType === 1) markHtml(node); });
            continue;
        }
        if (!group.some((node) => String(node.textContent || '').trim())) continue;
        const span = root.ownerDocument.createElement('span');
        span.setAttribute('data-studio-editable', '1');
        span.setAttribute('data-studio-wrapped', '1');
        group[0].parentNode.insertBefore(span, group[0]);
        for (const node of group) span.appendChild(node);
    }
}

function splitByBreaks(root) {
    const groups = [];
    let current = [];
    let breaks = 0;
    const flush = () => {
        if (current.length) groups.push(current);
        current = [];
    };
    for (const kid of [...root.childNodes]) {
        if (kid.nodeType === 1 && kid.tagName === 'BR') {
            breaks += 1;
            if (breaks >= 2) flush();
            continue;
        }
        if (kid.nodeType === 3 && !String(kid.textContent || '').trim()) {
            if (breaks < 2) current.push(kid);
            continue;
        }
        breaks = 0;
        current.push(kid);
    }
    flush();
    return groups.filter((group) => group.some((node) => String(node.textContent || '').trim()));
}

function findHtml(target) {
    let el = elementFrom(target);
    while (el && el.tagName !== 'BODY' && el.tagName !== 'HTML') {
        if (isSkipped(el)) return null;
        if (isHtmlCandidate(el)) return { type: 'html', element: el };
        el = el.parentElement;
    }
    return null;
}

function findSales(target) {
    const el = elementFrom(target);
    if (!el || isSkipped(el)) return null;
    if (el.classList?.contains('studio-caption-editor')) return { type: 'photo-editor', element: el };
    const bullet = el.closest('.photoCaptions li');
    if (bullet) return { type: 'photo-bullet', element: bullet };
    const img = el.closest('img.hero, img.thumbnail');
    if (img) return { type: 'photo-image', element: img };
    const title = el.closest('.idTitle');
    if (title) {
        const heading = el.closest('h1') || title.querySelector('h1');
        if (heading) return { type: 'vehicleName', element: heading };
    }
    const price = el.closest('.price');
    if (price) {
        const heading = el.closest('h2') || price.querySelector('h2');
        if (heading) return { type: 'price', element: heading };
    }
    const inventory = el.closest('.inventory');
    if (inventory) {
        const heading = el.closest('h2') || inventory.querySelector('h2');
        if (heading) return { type: 'inventory', element: heading };
    }
    const description = el.closest('.description');
    if (description && !el.closest('.expandable-lists-container')) {
        const wrapped = el.closest('[data-studio-wrapped]');
        if (wrapped && description.contains(wrapped)) return { type: 'description', element: wrapped };
        const leaf = innermost(el, description);
        if (leaf) return { type: 'description', element: leaf };
    }
    return null;
}

function innermost(el, stop) {
    let current = el;
    while (current && current !== stop) {
        if (isHtmlCandidate(current)) return current;
        current = current.parentElement;
    }
    return null;
}

function isHtmlCandidate(el) {
    const tag = String(el.tagName || '').toLowerCase();
    const cls = classText(el);
    const classHit = /(^|\s)(comment|bodyCaption)(\s|$)/.test(cls);
    if (!HTML_TAGS.has(tag) && !classHit) return false;
    return textLength(el) > 0;
}

function isSkipped(el) {
    if (!el || el.nodeType !== 1) return true;
    const tag = el.tagName;
    if (tag === 'SCRIPT' || tag === 'STYLE' || tag === 'BUTTON' || tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT' || tag === 'NOSCRIPT') return true;
    if (el.id === 'studio-preview-pick' || el.id === STYLE_ID) return true;
    try {
        return Boolean(el.closest('#banner, .banner, #searchContainer, .soldTopBanner, .bannerFooter'));
    } catch {
        return true;
    }
}

function activate(el) {
    if (!el) return;
    el.setAttribute('contenteditable', 'true');
    el.setAttribute('spellcheck', 'false');
    el.setAttribute('data-studio-editing', '1');
}

function deactivate(el) {
    if (!el?.removeAttribute) return;
    el.removeAttribute('contenteditable');
    el.removeAttribute('spellcheck');
    el.removeAttribute('data-studio-editing');
}

function scrub(doc) {
    doc.querySelectorAll('[data-studio-wrapped]').forEach((el) => unwrap(el));
    doc.querySelectorAll('.studio-caption-editor').forEach((el) => {
        if (el._studioImg) el._studioImg._studioCaptionEditor = null;
        el.remove();
    });
    doc.querySelectorAll('[data-studio-editable], [data-studio-hover], [data-studio-editing]').forEach((el) => {
        el.removeAttribute('data-studio-editable');
        el.removeAttribute('data-studio-hover');
        el.removeAttribute('data-studio-editing');
        el.removeAttribute('contenteditable');
        el.removeAttribute('spellcheck');
    });
    doc.getElementById(STYLE_ID)?.remove();
    doc.documentElement.removeAttribute('data-studio-viewport');
}

function unwrap(node) {
    const parent = node.parentNode;
    if (!parent) {
        node.remove();
        return;
    }
    while (node.firstChild) parent.insertBefore(node.firstChild, node);
    node.remove();
}

function injectStyle(doc) {
    if (doc.getElementById(STYLE_ID)) return;
    const style = doc.createElement('style');
    style.id = STYLE_ID;
    style.textContent = EDIT_STYLE;
    (doc.head || doc.documentElement).append(style);
}

function ensureCaptionEditor(img) {
    if (img._studioCaptionEditor?.isConnected) return img._studioCaptionEditor;
    const doc = img.ownerDocument;
    const editor = doc.createElement('span');
    editor.className = 'studio-caption-editor';
    editor.setAttribute('role', 'textbox');
    editor.setAttribute('aria-label', 'Photo caption');
    editor.textContent = img.getAttribute('alt') || img.getAttribute('title') || '';
    editor._studioImg = img;
    img._studioCaptionEditor = editor;
    img.insertAdjacentElement('afterend', editor);
    return editor;
}

function placeCaret(element, event) {
    const doc = element.ownerDocument;
    const sel = doc.getSelection?.();
    if (sel?.anchorNode && element.contains(sel.anchorNode)) return;
    let range = null;
    try {
        if (typeof doc.caretRangeFromPoint === 'function') range = doc.caretRangeFromPoint(event.clientX, event.clientY);
        if (!range && typeof doc.caretPositionFromPoint === 'function') {
            const pos = doc.caretPositionFromPoint(event.clientX, event.clientY);
            if (pos?.offsetNode) {
                range = doc.createRange();
                range.setStart(pos.offsetNode, pos.offset);
                range.collapse(true);
            }
        }
    } catch { /* the preview rejected the caret query */ }
    element.focus();
    if (!range || !sel || !element.contains(range.startContainer)) return;
    sel.removeAllRanges();
    sel.addRange(range);
}

function insertNewline(element) {
    const doc = element.ownerDocument;
    const sel = doc.getSelection?.();
    if (!sel?.rangeCount) return;
    const range = sel.getRangeAt(0);
    if (!element.contains(range.startContainer)) return;
    range.deleteContents();
    const node = doc.createTextNode('\n');
    range.insertNode(node);
    range.setStartAfter(node);
    range.collapse(true);
    sel.removeAllRanges();
    sel.addRange(range);
}

function relocateHtml(session, source) {
    if (source.slice(session.sourceStart, session.sourceEnd) === session.slice) return true;
    const at = source.indexOf(session.slice);
    if (at < 0 || at !== source.lastIndexOf(session.slice)) return false;
    const shift = at - session.sourceStart;
    session.sourceStart += shift;
    session.sourceEnd += shift;
    session.highlightStart += shift;
    session.highlightEnd += shift;
    return true;
}

function relocatePhp(session, source) {
    if (source.slice(session.sourceStart, session.sourceEnd) === session.slice) return true;
    const spec = assignmentValueRange(source, session.name);
    if (!spec) return false;
    const slice = source.slice(spec.start, spec.end);
    if (slice !== session.slice && decodePhpValue(slice, spec) !== session.live) return false;
    const shift = spec.start - session.sourceStart;
    session.sourceStart = spec.start;
    session.sourceEnd = spec.end;
    session.slice = slice;
    session.highlightStart += shift;
    session.highlightEnd += shift;
    session.spec = { ...spec };
    return true;
}

function rangeForElement(source, element, fileType) {
    const live = readLiveText(element);
    let located = null;
    try {
        located = locateElementInSource(source, element, fileType, { text: live });
    } catch {
        located = null;
    }
    const jumps = [];
    for (const jump of located?.jumps || []) jumps.push(jump);
    if (located?.inner) jumps.push(located.inner);
    if (located && located.endOffset > located.startOffset) jumps.push(located);
    let best = null;
    for (const jump of jumps) {
        if (!jump || !(jump.endOffset > jump.startOffset)) continue;
        const inner = elementInnerBounds(source, jump.startOffset, jump.endOffset);
        const start = inner ? inner.start : jump.startOffset;
        const end = inner ? inner.end : jump.endOffset;
        if (end < start) continue;
        const slice = source.slice(start, end);
        const decoded = decodeHtmlText(slice).map((item) => item.ch).join('');
        const exact = decoded === live;
        const loose = normalizeSpace(decoded) === normalizeSpace(live);
        const same = jump.element === element;
        if (!same && !exact && !loose) continue;
        const score = (exact ? 100 : 0) + (loose ? 40 : 0) + (same ? 20 : 0) - ((end - start) / 100000);
        if (!best || score > best.score) {
            best = {
                score,
                start,
                end,
                slice,
                highlightStart: jump.startOffset,
                highlightEnd: jump.endOffset,
                label: jump.label || labelFor(element),
            };
        }
    }
    return best;
}

function elementFrom(target) {
    if (!target) return null;
    if (target.nodeType === 3) return target.parentElement || null;
    if (target.nodeType === 1) return target;
    return null;
}

function readLiveText(element) {
    return String(element?.textContent || '').replace(/\r\n/g, '\n').replace(/\r/g, '\n');
}

function textLength(el) {
    return String(el?.textContent || '').replace(/\s+/g, '').length;
}

function classText(el) {
    const value = el?.getAttribute?.('class');
    if (typeof value === 'string') return value;
    return typeof el?.className === 'string' ? el.className : '';
}

function labelFor(el) {
    const tag = String(el?.tagName || '').toLowerCase();
    const cls = classText(el);
    if (tag === 'figcaption' || /(^|\s)(comment|bodyCaption)(\s|$)/.test(cls)) return 'caption';
    if (/^h[1-6]$/.test(tag)) return tag;
    if (tag === 'p') return 'paragraph';
    if (tag === 'li') return 'list item';
    if (tag === 'a') return 'link';
    return tag || 'text';
}

function isSalesVehicle(path) {
    return /\/sales\/[^/]+\/index\.php$/i.test(String(path || '').replace(/\\/g, '/'));
}

function baseName(value) {
    let text = String(value || '').trim();
    if (!text) return '';
    text = text.split('#')[0].split('?')[0];
    try { text = decodeURIComponent(text.replace(/\+/g, ' ')); } catch { /* keep the original name */ }
    const parts = text.split(/[/\\]/);
    return (parts[parts.length - 1] || '').toLowerCase();
}

function imageDataHighlight(source) {
    const match = /\$image_data\s*=\s*read_image_data\s*\([^)]*\)\s*;/.exec(String(source || ''));
    if (!match) return null;
    return {
        startOffset: match.index,
        endOffset: match.index + match[0].length,
        label: '$image_data',
    };
}

export function cleanLine(text) {
    return String(text || '').replace(/\u00A0/g, ' ').replace(/[\r\n]+/g, ' ').replace(/\s+/g, ' ').trim();
}

export function priceSourceValue(displayed) {
    const text = cleanLine(displayed);
    if (/^sold$/i.test(text)) return 'SOLD';
    const money = text.replace(/^\$/, '').replace(/,/g, '').trim();
    if (/^\d+(?:\.\d+)?$/.test(money)) return money;
    return text;
}

export function inventorySourceValue(displayed) {
    const text = cleanLine(displayed);
    const match = /inventory\s*number\s*:\s*([\s\S]*)$/i.exec(text);
    return (match ? match[1] : text).trim();
}

export function assignmentValueRange(source, name) {
    const match = new RegExp(`\\$${name}\\b\\s*=\\s*`).exec(String(source || ''));
    if (!match) return null;
    const statementStart = match.index;
    let index = match.index + match[0].length;
    let statementEnd = source.indexOf('\n', statementStart);
    if (statementEnd < 0) statementEnd = source.length;
    const semi = source.indexOf(';', index);
    if (semi >= 0 && semi < statementEnd) statementEnd = semi + 1;
    else if (semi >= 0 && statementEnd === source.length) statementEnd = semi + 1;
    if (source[index] === "'" || source[index] === '"') {
        const quote = source[index];
        const start = index + 1;
        let cursor = start;
        while (cursor < source.length) {
            if (source[cursor] === '\\') {
                cursor += 2;
                continue;
            }
            if (source[cursor] === quote) break;
            cursor += 1;
        }
        return {
            name,
            quote,
            numeric: false,
            start,
            end: cursor,
            statementStart,
            statementEnd,
        };
    }
    let end = index;
    while (end < source.length && source[end] !== ';' && source[end] !== '\n' && source[end] !== '\r') end += 1;
    const raw = source.slice(index, end).trim();
    const lead = source.slice(index, end).length - source.slice(index, end).trimStart().length;
    return {
        name,
        quote: '',
        numeric: /^-?\d+(?:\.\d+)?$/.test(raw),
        start: index + lead,
        end: index + lead + raw.length,
        statementStart,
        statementEnd,
    };
}

export function decodePhpValue(raw, spec) {
    const text = String(raw ?? '');
    if (!spec?.quote) return text.trim();
    return text.replace(/\\([\\'"])/g, '$1');
}

export function encodePhpValue(value, spec) {
    const text = String(value ?? '');
    if (!spec?.quote) {
        if (/^-?\d+(?:\.\d+)?$/.test(text)) return text;
        return `'${escapePhp(text, "'")}'`;
    }
    return escapePhp(text, spec.quote);
}

export function rewriteAssignment(source, name, nextValue) {
    const spec = assignmentValueRange(source, name);
    if (!spec) return null;
    const encoded = encodePhpValue(nextValue, spec);
    return source.slice(0, spec.start) + encoded + source.slice(spec.end);
}

function escapePhp(text, quote) {
    return String(text).replace(/\\/g, '\\\\').replace(new RegExp(quote, 'g'), `\\${quote}`);
}

export function descriptionRegion(source) {
    const open = /\$description\s*=\s*<<<(['"]?)DESC\1[^\n]*\r?\n/.exec(String(source || ''));
    if (!open) return null;
    const start = open.index + open[0].length;
    const close = /\r?\n[ \t]*DESC\b/.exec(source.slice(start));
    if (!close) return null;
    return { start, end: start + close.index, html: source.slice(start, start + close.index) };
}

export function descriptionTextRange(source, text) {
    const region = descriptionRegion(source);
    if (!region) return null;
    const exact = String(text ?? '').replace(/\r\n/g, '\n').replace(/\r/g, '\n');
    const trimmed = exact.trim();
    if (!trimmed) return null;
    let at = exact ? region.html.indexOf(exact) : -1;
    if (at >= 0 && region.html.indexOf(exact, at + exact.length) < 0) {
        return { start: region.start + at, end: region.start + at + exact.length, label: '$description' };
    }
    at = region.html.indexOf(trimmed);
    if (at >= 0 && region.html.indexOf(trimmed, at + trimmed.length) < 0) {
        return { start: region.start + at, end: region.start + at + trimmed.length, label: '$description' };
    }
    return null;
}

export function elementInnerBounds(source, startOffset, endOffset) {
    if (!(endOffset > startOffset)) return null;
    const slice = String(source || '').slice(startOffset, endOffset);
    if (!slice.startsWith('<')) return null;
    const openEnd = findTagEnd(slice, 0);
    if (openEnd < 0) return null;
    const open = slice.slice(0, openEnd + 1);
    if (/^<\s*\//.test(open) || /\/\s*>$/.test(open)) return null;
    const name = /^<\s*([a-zA-Z][\w:-]*)/.exec(open);
    if (!name) return null;
    const close = new RegExp(`<\\/\\s*${name[1]}\\s*>\\s*$`, 'i').exec(slice);
    if (!close) return null;
    const start = startOffset + openEnd + 1;
    const end = startOffset + close.index;
    if (end < start) return null;
    return { start, end, tag: name[1].toLowerCase() };
}

function uniqueTextRange(source, text) {
    const needle = String(text || '');
    if (needle.trim().length < 8) return null;
    const at = source.indexOf(needle);
    if (at < 0 || source.indexOf(needle, at + needle.length) >= 0) return null;
    const before = source.slice(0, at);
    if (before.lastIndexOf('<') > before.lastIndexOf('>')) return null;
    return { start: at, end: at + needle.length };
}

export function spliceDomEdit(slice, previousDom, nextDom) {
    const html = String(slice ?? '');
    const previous = String(previousDom ?? '');
    const next = String(nextDom ?? '');
    if (previous === next) return html;
    const decoded = decodeHtmlText(html);
    const sourceText = decoded.map((item) => item.ch).join('');
    if (normalizeSpace(sourceText) === normalizeSpace(next) && normalizeSpace(previous) === normalizeSpace(next)) return html;
    const fromText = sourceText === previous ? previous : sourceText;
    if (sourceText !== previous && normalizeSpace(sourceText) !== normalizeSpace(previous)) return null;
    const diff = sourceText === previous ? diffEdges(previous, next) : diffEdges(previous, next);
    if (!diff) return html;
    const startChar = sourceText === previous ? diff.prefix : alignIndex(sourceText, previous, diff.prefix);
    const endChar = sourceText === previous
        ? previous.length - diff.suffix
        : alignIndex(sourceText, previous, previous.length - diff.suffix);
    if (startChar < 0 || endChar < startChar) return null;
    if (fromText && startChar === endChar && !diff.insert && sourceText === previous) return html;
    return replaceDecodedSpan(html, decoded, startChar, endChar, diff.insert);
}

function replaceDecodedSpan(slice, decoded, startChar, endChar, insert) {
    const start = startChar >= decoded.length
        ? (decoded.length ? decoded[decoded.length - 1].srcEnd : 0)
        : decoded[startChar].srcStart;
    const end = endChar >= decoded.length
        ? (decoded.length ? decoded[decoded.length - 1].srcEnd : 0)
        : decoded[endChar].srcStart;
    if (end < start) return null;
    return slice.slice(0, start) + escapeHtml(insert) + slice.slice(end);
}

function diffEdges(previousText, nextText) {
    if (previousText === nextText) return null;
    let prefix = 0;
    const max = Math.min(previousText.length, nextText.length);
    while (prefix < max && previousText[prefix] === nextText[prefix]) prefix += 1;
    let suffix = 0;
    while (
        suffix < previousText.length - prefix
        && suffix < nextText.length - prefix
        && previousText[previousText.length - 1 - suffix] === nextText[nextText.length - 1 - suffix]
    ) suffix += 1;
    return {
        prefix,
        suffix,
        insert: nextText.slice(prefix, nextText.length - suffix),
    };
}

export function alignIndex(sourceText, domText, domIndex) {
    const target = Math.max(0, Math.min(Number(domIndex) || 0, domText.length));
    let sourceAt = 0;
    let domAt = 0;
    while (domAt < target) {
        if (sourceAt >= sourceText.length || domAt >= domText.length) return -1;
        const sourceChar = sourceText[sourceAt];
        const domChar = domText[domAt];
        const sourceSpace = isSpace(sourceChar);
        const domSpace = isSpace(domChar);
        if (sourceSpace && domSpace) {
            sourceAt += 1;
            domAt += 1;
            continue;
        }
        if (sourceSpace) {
            sourceAt += 1;
            continue;
        }
        if (domSpace) {
            domAt += 1;
            continue;
        }
        if (sourceChar !== domChar) return -1;
        sourceAt += 1;
        domAt += 1;
    }
    if (domAt !== target) return -1;
    const upcoming = domText[domAt];
    if (upcoming == null || !isSpace(upcoming)) {
        while (sourceAt < sourceText.length && isSpace(sourceText[sourceAt]) && (domAt >= domText.length || !isSpace(domText[domAt]))) {
            sourceAt += 1;
        }
    }
    return sourceAt;
}

function decodeHtmlText(html) {
    const chars = [];
    const source = String(html ?? '');
    let index = 0;
    while (index < source.length) {
        if (source[index] === '<' && (source[index + 1] === '/' || source[index + 1] === '!' || /[A-Za-z]/.test(source[index + 1] || ''))) {
            const end = findTagEnd(source, index);
            if (end >= 0) {
                index = end + 1;
                continue;
            }
        }
        if (source[index] === '&') {
            const match = /^&(#x[0-9a-fA-F]+|#\d+|[a-zA-Z][a-zA-Z0-9]+);/.exec(source.slice(index));
            if (match) {
                const decoded = decodeEntity(match[0]);
                if ([...decoded].length === 1) {
                    chars.push({ ch: decoded, srcStart: index, srcEnd: index + match[0].length });
                    index += match[0].length;
                    continue;
                }
            }
        }
        chars.push({ ch: source[index], srcStart: index, srcEnd: index + 1 });
        index += 1;
    }
    return chars;
}

function findTagEnd(source, start) {
    let quote = '';
    for (let index = start; index < source.length; index += 1) {
        const ch = source[index];
        if (quote) {
            if (ch === quote) quote = '';
            continue;
        }
        if (ch === '"' || ch === "'") {
            quote = ch;
            continue;
        }
        if (ch === '>') return index;
    }
    return -1;
}

function decodeEntity(token) {
    const body = token.slice(1, -1);
    if (body[0] === '#') {
        const hex = body[1] === 'x' || body[1] === 'X';
        const code = Number.parseInt(body.slice(hex ? 2 : 1), hex ? 16 : 10);
        if (!Number.isFinite(code) || code < 0 || code > 0x10FFFF) return token;
        try { return String.fromCodePoint(code); } catch { return token; }
    }
    return Object.prototype.hasOwnProperty.call(ENTITIES, body.toLowerCase()) ? ENTITIES[body.toLowerCase()] : token;
}

function escapeHtml(text) {
    return String(text).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function normalizeSpace(text) {
    return String(text || '').replace(/\u00A0/g, ' ').replace(/\s+/g, ' ').trim();
}

function isSpace(ch) {
    return ch === ' ' || ch === '\n' || ch === '\t' || ch === '\r' || ch === '\u00A0' || ch === '\f';
}
