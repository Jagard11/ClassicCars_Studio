const SITE_HOSTS = new Set([
    'classiccars.ws',
    'dickshappyclassiccars.com',
    'dickshappyvintageautos.com',
]);
const INTERNAL_QUERY = new Set(['__draft', '__preview_draft', '_t', '_studio']);
const LONG_PRESS_MS = 500;

export function mountPreviewNav(options) {
    const frame = options.frame;
    const backBtn = document.getElementById('btn-preview-back');
    const backMenuBtn = document.getElementById('btn-preview-back-menu');
    const forwardBtn = document.getElementById('btn-preview-forward');
    const forwardMenuBtn = document.getElementById('btn-preview-forward-menu');
    const refreshBtn = document.getElementById('btn-refresh-preview');
    const form = document.getElementById('preview-url-form');
    const address = document.getElementById('preview-url-input');
    const editBtn = document.getElementById('btn-preview-edit-here');
    const previewHistory = { entries: [], index: -1 };
    const menu = document.createElement('div');
    menu.id = 'preview-history-menu';
    menu.className = 'preview-history-menu';
    menu.setAttribute('role', 'menu');
    menu.setAttribute('aria-label', 'Preview history');
    menu.hidden = true;
    document.body.append(menu);

    let pending = null;
    let menuOpen = false;
    let menuKind = '';
    let menuAnchor = null;
    let editing = false;
    let selectOnMouseUp = false;
    let addressHold = '';
    let mouseNavAt = 0;
    let detachDoc = () => {};

    function pageMode() {
        return options.getPageMode?.() || 'code';
    }

    function viewportMode() {
        return options.getViewportMode?.() || 'view';
    }

    function currentEntry() {
        return previewHistory.index >= 0 ? previewHistory.entries[previewHistory.index] : null;
    }

    function currentPath() {
        return currentEntry()?.path || '';
    }

    function canGoBack() {
        return previewHistory.index > 0;
    }

    function canGoForward() {
        return previewHistory.index >= 0 && previewHistory.index < previewHistory.entries.length - 1;
    }

    function sync() {
        const gallery = pageMode() === 'gallery';
        if (backBtn) backBtn.disabled = !canGoBack();
        if (backMenuBtn) backMenuBtn.disabled = !canGoBack();
        if (forwardBtn) forwardBtn.disabled = !canGoForward();
        if (forwardMenuBtn) forwardMenuBtn.disabled = !canGoForward();
        if (refreshBtn) refreshBtn.disabled = gallery || previewHistory.index < 0;
        const path = currentPath();
        if (address && !editing) {
            if (!addressHold || historyKey(addressHold) === historyKey(path)) {
                address.value = path;
                address.title = addressTip(path);
                addressHold = '';
            }
        }
        if (editBtn) {
            const rawFile = editorFile(path);
            const file = rawFile ? (options.normalizePath?.(rawFile) || rawFile) : '';
            const editor = options.normalizePath?.(options.getEditorPath?.() || '') || '';
            editBtn.hidden = !(file && editor && file !== editor);
        }
    }

    function rememberScroll() {
        const entry = currentEntry();
        if (!entry) return;
        const y = readScroll();
        if (y != null) entry.scrollY = y;
    }

    function expectSameDocument() {
        pending = { kind: 'same' };
    }

    function clearPending() {
        pending = null;
    }

    function isSameDocument(url) {
        if (previewHistory.index < 0 || !url) return false;
        const clean = cleanFromInput(url);
        if (!clean) return false;
        return historyKey(currentPath()) === historyKey(clean);
    }

    function goBack() {
        jump(previewHistory.index - 1);
    }

    function goForward() {
        jump(previewHistory.index + 1);
    }

    function jump(to) {
        if (pageMode() === 'gallery') return;
        if (to < 0 || to >= previewHistory.entries.length || to === previewHistory.index) return;
        rememberScroll();
        closeMenu();
        const entry = previewHistory.entries[to];
        const here = currentPath();
        if (here && documentKey(here) === documentKey(entry.path)) {
            previewHistory.index = to;
            writeFrameUrl(entry.path);
            addressHold = '';
            restoreScroll(entry.scrollY || 0);
            sync();
            return;
        }
        pending = { kind: 'jump', index: to };
        loadPreview(entry.path, { replace: false });
    }

    function reloadCurrent() {
        const path = currentPath() || options.getEditorPreviewUrl?.() || '';
        if (!path || pageMode() === 'gallery') return false;
        rememberScroll();
        pending = { kind: 'same' };
        return loadPreview(path, { replace: true });
    }

    function followPath(path) {
        if (!path || pageMode() === 'gallery') return;
        const here = currentPath();
        if (here && historyKey(here) === historyKey(path)) {
            reloadCurrent();
            return;
        }
        if (here && documentKey(here) === documentKey(path)) {
            rememberScroll();
            showHash(path);
            return;
        }
        rememberScroll();
        pending = null;
        loadPreview(path, { replace: false });
    }

    function goAddress(text) {
        const path = parseAddress(text);
        if (!path) {
            editing = false;
            sync();
            return;
        }
        editing = false;
        addressHold = path;
        if (address) {
            address.value = path;
            address.title = addressTip(path);
        }
        followPath(path);
    }

    function showHash(path) {
        let doc = null;
        let win = null;
        try {
            doc = frame.contentDocument;
            win = frame.contentWindow;
        } catch { /* the preview document is not readable */ }
        if (!doc || !win) {
            pending = null;
            loadPreview(path, { replace: false });
            return;
        }
        writeFrameUrl(path);
        const hash = hashOf(path);
        const id = decodeHash(hash);
        if (!id) win.scrollTo({ top: 0, behavior: 'smooth' });
        else {
            const target = findHash(doc, id);
            if (target) target.scrollIntoView({ behavior: 'smooth', block: 'start' });
            else win.scrollTo({ top: 0, behavior: 'smooth' });
        }
        commitPush(path, doc.title || '');
        const entry = currentEntry();
        window.setTimeout(() => {
            if (entry && currentEntry() === entry) {
                const y = readScroll();
                if (y != null) entry.scrollY = y;
            }
        }, 400);
    }

    function commitPush(path, title) {
        const key = historyKey(path);
        const current = currentEntry();
        if (current && historyKey(current.path) === key) {
            current.title = title || current.title;
            current.path = path;
            current.timestamp = Date.now();
            addressHold = '';
            sync();
            return;
        }
        const entry = {
            path,
            title: title || titleFromPath(path),
            scrollY: 0,
            timestamp: Date.now(),
        };
        previewHistory.entries.splice(previewHistory.index + 1);
        previewHistory.entries.push(entry);
        previewHistory.index = previewHistory.entries.length - 1;
        addressHold = '';
        sync();
    }

    function onFrameLoad() {
        attachDocument();
        let href = '';
        let title = '';
        try {
            href = frame.contentDocument?.URL || '';
            title = String(frame.contentDocument?.title || '').trim();
        } catch { /* the preview document is not readable yet */ }
        if (!isPreviewDocument(href)) {
            pending = null;
            sync();
            return;
        }
        const path = cleanFromHref(href);
        if (pending?.kind === 'jump' && previewHistory.entries[pending.index]) {
            const entry = previewHistory.entries[pending.index];
            pending = null;
            entry.title = title || entry.title;
            entry.path = path || entry.path;
            previewHistory.index = previewHistory.entries.indexOf(entry);
            addressHold = '';
            restoreScroll(entry.scrollY || 0);
            sync();
            return;
        }
        const same = pending?.kind === 'same' || (currentEntry() && historyKey(currentPath()) === historyKey(path));
        pending = null;
        if (same && currentEntry()) {
            const entry = currentEntry();
            entry.title = title || entry.title;
            entry.path = path;
            entry.timestamp = Date.now();
            addressHold = '';
            sync();
            return;
        }
        commitPush(path, title);
    }

    function loadPreview(cleanPath, { replace }) {
        const custom = options.previewHtml?.(cleanPath) || '';
        if (custom) {
            frame.dataset.src = cleanPath;
            frame.removeAttribute('src');
            frame.srcdoc = custom;
            commitPush(cleanPath, titleFromPath(cleanPath));
            return true;
        }
        const file = editorFile(cleanPath);
        const prepared = options.prepareUrl?.(cleanPath, file) || '';
        if (!prepared) return false;
        const target = cacheBust(prepared);
        frame.removeAttribute('srcdoc');
        frame.dataset.src = prepared;
        if (replace && replaceLocation(target)) return true;
        frame.src = target;
        return true;
    }

    function showDocument(path) {
        if (!path || pageMode() === 'gallery') return false;
        rememberScroll();
        pending = null;
        return loadPreview(path, { replace: false });
    }

    function replaceLocation(target) {
        try {
            const win = frame.contentWindow;
            const href = frame.contentDocument?.URL || '';
            if (!win || !href || href === 'about:blank' || href.startsWith('about:')) return false;
            win.location.replace(target);
            return true;
        } catch {
            return false;
        }
    }

    function writeFrameUrl(path) {
        try {
            const win = frame.contentWindow;
            if (!win) return;
            const url = new URL(win.location.href);
            const bits = splitClean(path);
            win.history.replaceState(null, '', `${url.pathname}${bits.query ? `?${bits.query}` : ''}${bits.hash}`);
        } catch { /* the preview document rejected the address update */ }
    }

    function restoreScroll(y) {
        const apply = () => {
            try { frame.contentWindow?.scrollTo(0, y || 0); } catch { /* the preview document was replaced */ }
        };
        apply();
        requestAnimationFrame(apply);
        window.setTimeout(apply, 50);
        window.setTimeout(apply, 300);
    }

    function readScroll() {
        try {
            const win = frame.contentWindow;
            if (!win) return null;
            return win.scrollY || win.pageYOffset || 0;
        } catch {
            return null;
        }
    }

    function attachDocument() {
        detachDoc();
        let doc = null;
        let win = null;
        try {
            doc = frame.contentDocument;
            win = frame.contentWindow;
        } catch {
            return;
        }
        if (!doc || !win || !doc.documentElement) return;
        const onClick = (event) => handleLink(event, doc);
        const onAux = (event) => {
            if (event.button === 1) handleLink(event, doc);
            else handleMouseNav(event);
        };
        const onMouseDown = (event) => {
            if (event.button === 3 || event.button === 4) {
                event.preventDefault();
                event.stopPropagation();
            }
        };
        const onKey = (event) => handleKey(event, true);
        doc.addEventListener('click', onClick, true);
        doc.addEventListener('auxclick', onAux, true);
        doc.addEventListener('mouseup', handleMouseNav, true);
        doc.addEventListener('mousedown', onMouseDown, true);
        win.addEventListener('keydown', onKey, true);
        doc.addEventListener('keydown', onKey, true);
        detachDoc = () => {
            try {
                doc.removeEventListener('click', onClick, true);
                doc.removeEventListener('auxclick', onAux, true);
                doc.removeEventListener('mouseup', handleMouseNav, true);
                doc.removeEventListener('mousedown', onMouseDown, true);
                win.removeEventListener('keydown', onKey, true);
                doc.removeEventListener('keydown', onKey, true);
            } catch { /* the previous preview document has already been discarded */ }
        };
    }

    function handleLink(event, doc) {
        if (viewportMode() !== 'view') return;
        if (event.defaultPrevented) return;
        const button = typeof event.button === 'number' ? event.button : 0;
        if (button !== 0 && button !== 1) return;
        const origin = event.target?.nodeType === 1 ? event.target : event.target?.parentElement;
        const anchor = origin?.closest?.('a[href]');
        if (!anchor || anchor.hasAttribute('data-lightbox') || anchor.hasAttribute('download')) return;
        const raw = String(anchor.getAttribute('href') || '').trim();
        if (!raw || /^(javascript|mailto|tel):/i.test(raw)) return;
        const info = classifyHref(raw, doc);
        if (!info || info.ignore || info.external) return;
        event.preventDefault();
        event.stopPropagation();
        followPath(info.path);
    }

    function handleMouseNav(event) {
        if (!event || (event.button !== 3 && event.button !== 4)) return;
        event.preventDefault();
        event.stopPropagation();
        if (event.type === 'mousedown') return;
        const now = Date.now();
        if (now - mouseNavAt < 400) return;
        mouseNavAt = now;
        if (pageMode() === 'gallery' || document.querySelector('dialog[open]')) return;
        closeMenu();
        if (event.button === 3) goBack();
        else goForward();
    }

    function handleKey(event, fromFrame) {
        if (!event || event.isComposing) return;
        if (menuOpen && event.key === 'Escape') {
            event.preventDefault();
            event.stopPropagation();
            const anchor = menuAnchor;
            closeMenu();
            anchor?.focus();
            return;
        }
        if (isAddressChord(event)) {
            if (document.querySelector('dialog[open]')) return;
            event.preventDefault();
            event.stopPropagation();
            focusAddress();
            return;
        }
        if (isHistoryArrow(event)) {
            if (!fromFrame && (inCodeEditor(event.target) || inGalleryReorder(event.target))) return;
            if (pageMode() === 'gallery' || document.querySelector('dialog[open]')) return;
            event.preventDefault();
            event.stopPropagation();
            if (event.repeat) return;
            closeMenu();
            if (event.key === 'ArrowLeft') goBack();
            else goForward();
            return;
        }
        if (event.key !== 'Backspace' || event.altKey || event.ctrlKey || event.metaKey || event.shiftKey) return;
        if (viewportMode() !== 'view' || pageMode() === 'gallery') return;
        if (isTypingTarget(event.target) || document.querySelector('dialog[open]')) return;
        if (!fromFrame && !focusInPreview(event)) return;
        event.preventDefault();
        event.stopPropagation();
        if (event.repeat) return;
        closeMenu();
        goBack();
    }

    function focusInPreview(event) {
        const node = elementOf(event.target);
        if (!node) return false;
        return node === frame || Boolean(node.closest?.('#preview-frame, #preview-stage'));
    }

    function focusAddress() {
        if (!address) return;
        editing = true;
        selectOnMouseUp = true;
        address.focus();
        address.select();
    }

    function openHistory(kind, anchor) {
        const items = historyItems(kind);
        if (!items.length) return;
        if (menuOpen && menuKind === kind) {
            closeMenu();
            return;
        }
        menuKind = kind;
        menuAnchor = anchor;
        menu.replaceChildren();
        for (const item of items) {
            const button = document.createElement('button');
            button.type = 'button';
            button.setAttribute('role', 'menuitem');
            button.textContent = historyLabel(item.entry);
            button.title = `Open ${item.entry?.path || button.textContent} in the preview`;
            button.addEventListener('click', () => jump(item.index));
            menu.append(button);
        }
        menu.hidden = false;
        menuOpen = true;
        backMenuBtn?.setAttribute('aria-expanded', kind === 'back' ? 'true' : 'false');
        forwardMenuBtn?.setAttribute('aria-expanded', kind === 'forward' ? 'true' : 'false');
        placeMenu(anchor);
        menu.querySelector('[role="menuitem"]')?.focus();
    }

    function historyItems(kind) {
        const items = [];
        if (kind === 'back') {
            for (let index = previewHistory.index - 1; index >= 0; index -= 1) {
                items.push({ index, entry: previewHistory.entries[index] });
            }
        } else {
            for (let index = previewHistory.index + 1; index < previewHistory.entries.length; index += 1) {
                items.push({ index, entry: previewHistory.entries[index] });
            }
        }
        return items;
    }

    function closeMenu() {
        menuOpen = false;
        menuKind = '';
        menuAnchor = null;
        menu.hidden = true;
        menu.replaceChildren();
        backMenuBtn?.setAttribute('aria-expanded', 'false');
        forwardMenuBtn?.setAttribute('aria-expanded', 'false');
    }

    function placeMenu(anchor) {
        menu.style.left = '0px';
        menu.style.top = '0px';
        const pad = 8;
        const rect = anchor.getBoundingClientRect();
        const width = menu.offsetWidth;
        const height = menu.offsetHeight;
        let left = rect.left;
        if (left + width > window.innerWidth - pad) left = window.innerWidth - width - pad;
        left = Math.max(pad, left);
        let top = rect.bottom + 4;
        if (top + height > window.innerHeight - pad) top = rect.top - height - 4;
        top = Math.max(pad, top);
        menu.style.left = `${Math.round(left)}px`;
        menu.style.top = `${Math.round(top)}px`;
    }

    function onMenuKey(event) {
        const items = [...menu.querySelectorAll('[role="menuitem"]')];
        const index = items.indexOf(document.activeElement);
        if (event.key === 'ArrowDown' && items.length) {
            event.preventDefault();
            items[(index + 1) % items.length].focus();
        } else if (event.key === 'ArrowUp' && items.length) {
            event.preventDefault();
            items[(index - 1 + items.length) % items.length].focus();
        } else if (event.key === 'Home' && items.length) {
            event.preventDefault();
            items[0].focus();
        } else if (event.key === 'End' && items.length) {
            event.preventDefault();
            items[items.length - 1].focus();
        } else if (event.key === 'Escape') {
            event.preventDefault();
            event.stopPropagation();
            const anchor = menuAnchor;
            closeMenu();
            anchor?.focus();
        }
    }

    function armHistoryButton(button, kind, onActivate) {
        if (!button) return;
        let timer = 0;
        let suppressClick = false;
        const clear = () => {
            if (timer) window.clearTimeout(timer);
            timer = 0;
        };
        button.addEventListener('pointerdown', (event) => {
            if (event.button !== 0 || button.disabled) return;
            clear();
            timer = window.setTimeout(() => {
                timer = 0;
                suppressClick = true;
                openHistory(kind, kind === 'back' ? backMenuBtn : forwardMenuBtn);
            }, LONG_PRESS_MS);
        });
        button.addEventListener('pointerup', clear);
        button.addEventListener('pointercancel', clear);
        button.addEventListener('pointerleave', clear);
        button.addEventListener('click', (event) => {
            if (suppressClick) {
                event.preventDefault();
                event.stopPropagation();
                suppressClick = false;
                return;
            }
            onActivate();
        });
        button.addEventListener('contextmenu', (event) => {
            event.preventDefault();
            event.stopPropagation();
            if (button.disabled) return;
            suppressClick = true;
            openHistory(kind, kind === 'back' ? backMenuBtn : forwardMenuBtn);
        });
    }

    backMenuBtn?.addEventListener('click', () => openHistory('back', backMenuBtn));
    forwardMenuBtn?.addEventListener('click', () => openHistory('forward', forwardMenuBtn));
    armHistoryButton(backBtn, 'back', goBack);
    armHistoryButton(forwardBtn, 'forward', goForward);
    form?.addEventListener('submit', (event) => {
        event.preventDefault();
        goAddress(address?.value || '');
    });
    editBtn?.addEventListener('click', () => {
        const path = editorFile(currentPath());
        if (path) options.onOpenInEditor?.(path);
    });
    address?.addEventListener('focus', () => {
        editing = true;
        selectOnMouseUp = true;
        address.select();
    });
    address?.addEventListener('blur', () => {
        window.setTimeout(() => {
            if (form?.contains(document.activeElement)) return;
            editing = false;
            sync();
        }, 0);
    });
    address?.addEventListener('mouseup', (event) => {
        if (!selectOnMouseUp) return;
        event.preventDefault();
        address.select();
        selectOnMouseUp = false;
    });
    menu.addEventListener('keydown', onMenuKey);
    window.addEventListener('keydown', (event) => handleKey(event, false), true);
    window.addEventListener('mousedown', (event) => {
        if (event.button === 3 || event.button === 4) {
            event.preventDefault();
            event.stopPropagation();
        }
    }, true);
    window.addEventListener('mouseup', handleMouseNav, true);
    window.addEventListener('auxclick', handleMouseNav, true);
    document.addEventListener('pointerdown', (event) => {
        if (!menuOpen || menu.contains(event.target)) return;
        if (event.target === backMenuBtn || event.target === forwardMenuBtn) return;
        if (event.target === backBtn || event.target === forwardBtn) return;
        closeMenu();
    }, true);
    window.addEventListener('resize', closeMenu);
    frame.addEventListener('load', onFrameLoad);
    onFrameLoad();
    sync();

    return {
        canGoBack,
        canGoForward,
        goBack,
        goForward,
        currentPath,
        isSameDocument,
        expectSameDocument,
        clearPending,
        rememberScroll,
        reloadCurrent,
        showDocument,
        destroy() {
            detachDoc();
            closeMenu();
            menu.remove();
        },
    };
}

function classifyHref(raw, doc) {
    let url;
    try { url = new URL(raw, doc.baseURI || doc.URL); } catch { return { ignore: true }; }
    if (url.protocol !== 'http:' && url.protocol !== 'https:') return { ignore: true };
    if (!isSiteHost(url.hostname) && !isLocalHost(url.hostname, doc)) return { external: true };
    return { path: cleanFromUrl(url) };
}

function parseAddress(text) {
    const raw = String(text || '').trim();
    if (!raw || /^(javascript|data|mailto|tel):/i.test(raw)) return '';
    if (/^https?:\/\//i.test(raw)) {
        try {
            const url = new URL(raw);
            if (!isSiteHost(url.hostname) && !isLocalHost(url.hostname, null)) return '';
            return cleanFromUrl(url);
        } catch {
            return '';
        }
    }
    const path = raw.startsWith('/') ? raw : `/${raw}`;
    try { return cleanFromUrl(new URL(path, 'http://preview.local')); } catch { return ''; }
}

function cleanFromInput(value) {
    const text = String(value || '').trim();
    if (!text) return '';
    try {
        const url = /^https?:\/\//i.test(text) ? new URL(text) : new URL(text, location.origin);
        return cleanFromUrl(url);
    } catch {
        return '';
    }
}

function cleanFromHref(href) {
    try { return cleanFromUrl(new URL(href)); } catch { return ''; }
}

function cleanFromUrl(url) {
    const kept = [];
    const params = url.search ? url.search.slice(1).split('&') : [];
    for (const part of params) {
        if (!part) continue;
        let name = part.split('=')[0].replace(/\+/g, ' ');
        try { name = decodeURIComponent(name); } catch { /* keep the raw name */ }
        if (INTERNAL_QUERY.has(name)) continue;
        kept.push(part);
    }
    let path = url.pathname || '/';
    try { path = decodeURIComponent(path); } catch { /* keep the encoded path */ }
    if (!path.startsWith('/')) path = `/${path}`;
    path = path.replace(/\/{2,}/g, '/');
    const query = kept.join('&');
    return `${path}${query ? `?${query}` : ''}${url.hash || ''}`;
}

function splitClean(clean) {
    const hashAt = String(clean || '').indexOf('#');
    const hash = hashAt >= 0 ? clean.slice(hashAt) : '';
    const base = hashAt >= 0 ? clean.slice(0, hashAt) : String(clean || '');
    const queryAt = base.indexOf('?');
    return {
        path: queryAt >= 0 ? base.slice(0, queryAt) : base,
        query: queryAt >= 0 ? base.slice(queryAt + 1) : '',
        hash,
    };
}

function historyKey(clean) {
    const bits = splitClean(clean);
    let path = bits.path || '/';
    if (path.length > 1 && path.endsWith('/')) path = path.slice(0, -1);
    return `${path}\n${bits.query}\n${bits.hash}`;
}

function documentKey(clean) {
    const bits = splitClean(clean);
    let path = bits.path || '/';
    if (path.length > 1 && path.endsWith('/')) path = path.slice(0, -1);
    return `${path}\n${bits.query}`;
}

function hashOf(clean) {
    return splitClean(clean).hash || '';
}

function editorFile(clean) {
    if (!clean) return '';
    const bits = splitClean(clean);
    let path = bits.path || '/';
    try { path = decodeURIComponent(path); } catch { /* keep the path text */ }
    if (!path.startsWith('/')) path = `/${path}`;
    path = path.replace(/\/{2,}/g, '/');
    if (path.length > 1 && path.endsWith('/')) {
        const sales = path === '/sales/' || path.startsWith('/sales/');
        path += sales ? 'index.php' : 'index.html';
    }
    return path.length > 1 && path.endsWith('/') ? path.slice(0, -1) : path;
}

function titleFromPath(path) {
    const bits = splitClean(path);
    const parts = bits.path.split('/').filter(Boolean);
    return parts[parts.length - 1] || path || 'Untitled';
}

function addressTip(path) {
    const text = String(path || '').trim();
    const hint = 'Preview address. Edit the path and press Enter to load it in the preview.';
    return text ? `${text} — ${hint}` : hint;
}

function historyLabel(entry) {
    const title = String(entry?.title || '').trim() || titleFromPath(entry?.path || '');
    return `${title} — ${entry?.path || ''}`;
}

function cacheBust(url) {
    const hashAt = url.indexOf('#');
    const hash = hashAt >= 0 ? url.slice(hashAt) : '';
    const base = hashAt >= 0 ? url.slice(0, hashAt) : url;
    return `${base}${base.includes('?') ? '&' : '?'}_t=${Date.now()}${hash}`;
}

function decodeHash(hash) {
    const raw = String(hash || '').replace(/^#/, '');
    if (!raw) return '';
    try { return decodeURIComponent(raw); } catch { return raw; }
}

function findHash(doc, id) {
    const byId = doc.getElementById(id);
    if (byId) return byId;
    try {
        if (window.CSS?.escape) return doc.querySelector(`[name="${CSS.escape(id)}"]`);
    } catch { /* the selector was not usable */ }
    return null;
}

function isSiteHost(hostname) {
    return SITE_HOSTS.has(String(hostname || '').replace(/^www\./i, '').toLowerCase());
}

function isLocalHost(hostname, doc) {
    const key = String(hostname || '').replace(/^www\./i, '').toLowerCase();
    if (!key) return false;
    if (key === 'localhost' || key === '127.0.0.1' || key === '[::1]' || key === '::1') return true;
    try {
        if (hostKey(location.hostname) === key) return true;
    } catch { /* location is unavailable */ }
    try {
        if (doc && hostKey(new URL(doc.URL).hostname) === key) return true;
    } catch { /* the preview document has no URL yet */ }
    return false;
}

function hostKey(hostname) {
    return String(hostname || '').replace(/^www\./i, '').toLowerCase();
}

function isPreviewDocument(href) {
    if (!/^https?:/i.test(href)) return false;
    try {
        const path = new URL(href).pathname;
        return path !== '/__studio' && !path.startsWith('/__studio/');
    } catch {
        return false;
    }
}

function isAddressChord(event) {
    if (event.repeat || event.shiftKey) return false;
    const key = String(event.key || '').toLowerCase();
    if ((event.ctrlKey || event.metaKey) && !event.altKey && key === 'l') return true;
    return event.altKey && !event.ctrlKey && !event.metaKey && key === 'd';
}

function isHistoryArrow(event) {
    if (!event.altKey || event.ctrlKey || event.metaKey || event.shiftKey) return false;
    return event.key === 'ArrowLeft' || event.key === 'ArrowRight';
}

function inCodeEditor(target) {
    const node = elementOf(target);
    return Boolean(node?.closest?.('#code-mount, .code-shell'));
}

function inGalleryReorder(target) {
    const node = elementOf(target);
    return Boolean(node?.closest?.('#gallery-sheet, #gallery-mount'));
}

function isTypingTarget(target) {
    const node = elementOf(target);
    if (!node) return false;
    const tag = node.tagName;
    if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') return true;
    if (node.isContentEditable) return true;
    return Boolean(node.closest?.('[contenteditable="true"]'));
}

function elementOf(target) {
    if (!target) return null;
    if (target.nodeType === 1) return target;
    return target.parentElement || null;
}
