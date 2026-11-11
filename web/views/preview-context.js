import { el } from './dom.js';

const VOID_TAGS = new Set(['area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input', 'link', 'meta', 'param', 'source', 'track', 'wbr']);
const RAW_TAGS = new Set(['script', 'style']);
const LEAF_TAGS = new Set(['img', 'a', 'figcaption', 'span', 'em', 'strong', 'i', 'b', 'small', 'button', 'caption', 'label']);
const SEMANTIC_TAGS = new Set(['figure', 'p', 'li', 'tr', 'ul', 'ol', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'blockquote']);
const FEATURE_CLASSES = ['articleFeatureSingle', 'articleFeaturePair', 'thumbnail-wrapper', 'articleGallerySection'];
const KEPT_ATTRS = new Set(['id', 'class', 'href', 'src', 'data-lightbox', 'data-title']);
const SKIP_CLASSES = new Set(['centerText', 'bodyCaption', 'textJustify', 'thumbnail', 'articleLightboxLink', 'redFont', 'active']);
const ENTITIES = {
    amp: '&',
    lt: '<',
    gt: '>',
    quot: '"',
    apos: "'",
    nbsp: ' ',
    shy: '\u00AD',
    mdash: '\u2014',
    ndash: '\u2013',
    hellip: '\u2026',
    bull: '\u2022',
    rsquo: '\u2019',
    lsquo: '\u2018',
    rdquo: '\u201D',
    ldquo: '\u201C',
    sbquo: '\u201A',
    bdquo: '\u201E',
    times: '\u00D7',
    copy: '\u00A9',
    reg: '\u00AE',
    trade: '\u2122',
};
const PICK_STYLE_ID = 'studio-preview-pick-style';
const PICK_BOX_ID = 'studio-preview-pick';

export function mountPreviewContext(options) {
    const frame = options.frame;
    const menu = el('div', {
        class: 'preview-menu',
        role: 'menu',
        hidden: true,
        'aria-label': 'Preview element',
    });
    document.body.append(menu);

    let menuOpen = false;
    let altHeld = false;
    let lastTarget = null;
    let unlisten = () => {};
    let detachWindow = () => {};
    let detachStage = () => {};
    let submenuControls = null;
    let submenuTimer = 0;

    function closeMenu() {
        window.clearTimeout(submenuTimer);
        submenuControls = null;
        menuOpen = false;
        menu.hidden = true;
        menu.replaceChildren();
        if (!altHeld) hideBox(currentDoc());
    }

    function swallowReloadChord(event) {
        if (!event || event.altKey) return false;
        const reload = event.key === 'F5'
            || ((event.ctrlKey || event.metaKey) && (event.key === 'r' || event.key === 'R'));
        if (!reload) return false;
        event.preventDefault();
        event.stopPropagation();
        closeMenu();
        if (!event.repeat) options.onReloadPreview?.();
        return true;
    }

    function onWindowKey(event) {
        if (event.type === 'keydown' && swallowReloadChord(event)) return;
        if (event.key === 'Alt') {
            altHeld = event.type === 'keydown';
            if (event.type === 'keyup' && !menuOpen) hideBox(currentDoc());
            if (event.type === 'keydown' && altHeld && !menuOpen && lastTarget) showBox(currentDoc(), lastTarget);
        } else if (event.key === 'Escape' && menuOpen) {
            event.preventDefault();
            event.stopPropagation();
            closeMenu();
        }
    }

    function onWindowPointer(event) {
        if (!menuOpen || menu.contains(event.target)) return;
        closeMenu();
    }

    window.addEventListener('keydown', onWindowKey, true);
    window.addEventListener('keyup', onWindowKey, true);
    window.addEventListener('blur', () => {
        altHeld = false;
        if (!menuOpen) hideBox(currentDoc());
    });
    document.addEventListener('pointerdown', onWindowPointer, true);
    detachWindow = () => {
        window.removeEventListener('keydown', onWindowKey, true);
        window.removeEventListener('keyup', onWindowKey, true);
        document.removeEventListener('pointerdown', onWindowPointer, true);
    };

    menu.addEventListener('keydown', (event) => {
        const jump = document.activeElement?.classList?.contains('is-jump') ? document.activeElement : null;
        const inSub = Boolean(document.activeElement?.closest?.('.preview-submenu'));
        if (jump && (event.key === 'ArrowRight' || event.key === 'Enter' || event.key === ' ')) {
            event.preventDefault();
            submenuControls?.open();
            submenuControls?.focusFirst();
            return;
        }
        if (inSub && event.key === 'ArrowLeft') {
            event.preventDefault();
            submenuControls?.close();
            submenuControls?.focusHost();
            return;
        }
        const items = [...menu.querySelectorAll('[role="menuitem"]:not(:disabled)')].filter((item) => !item.closest('[hidden]'));
        const index = items.indexOf(document.activeElement);
        if (event.key === 'ArrowDown' && jump && submenuControls) {
            event.preventDefault();
            submenuControls.open();
            submenuControls.focusFirst();
            return;
        }
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
            if (inSub) {
                submenuControls?.close();
                submenuControls?.focusHost();
                return;
            }
            closeMenu();
        }
    });

    function currentDoc() {
        try { return frame.contentDocument; } catch { return null; }
    }

    function attach() {
        unlisten();
        closeMenu();
        lastTarget = null;
        let doc;
        let win;
        try {
            doc = frame.contentDocument;
            win = frame.contentWindow;
        } catch {
            return;
        }
        if (!doc || !win || !doc.documentElement) return;
        const href = String(doc.URL || '');
        if (!href || href === 'about:blank') return;

        const onMenu = (event) => openMenu(doc, event);
        const onMove = (event) => {
            if (event.target && event.target.id !== PICK_BOX_ID) lastTarget = event.target;
            if (menuOpen) return;
            if (altHeld || event.altKey) showBox(doc, event.target);
            else hideBox(doc);
        };
        const onLeave = () => {
            if (!menuOpen && !altHeld) hideBox(doc);
        };
        const onScroll = () => {
            if (menuOpen) closeMenu();
            else if (altHeld && lastTarget) showBox(doc, lastTarget);
        };
        const onPointer = (event) => {
            if (event.button !== 0 || !menuOpen) return;
            closeMenu();
        };
        const onKey = (event) => {
            if (event.type === 'keydown' && swallowReloadChord(event)) return;
            if (event.key === 'Alt') altHeld = event.type === 'keydown';
            if (event.key === 'Escape' && menuOpen) {
                event.preventDefault();
                closeMenu();
            }
        };

        doc.addEventListener('contextmenu', onMenu, true);
        doc.addEventListener('mousemove', onMove, true);
        doc.addEventListener('mouseleave', onLeave, true);
        doc.addEventListener('pointerdown', onPointer, true);
        doc.addEventListener('scroll', onScroll, true);
        win.addEventListener('scroll', onScroll, true);
        win.addEventListener('keydown', onKey, true);
        win.addEventListener('keyup', onKey, true);
        unlisten = () => {
            try {
                doc.removeEventListener('contextmenu', onMenu, true);
                doc.removeEventListener('mousemove', onMove, true);
                doc.removeEventListener('mouseleave', onLeave, true);
                doc.removeEventListener('pointerdown', onPointer, true);
                doc.removeEventListener('scroll', onScroll, true);
                win.removeEventListener('scroll', onScroll, true);
                win.removeEventListener('keydown', onKey, true);
                win.removeEventListener('keyup', onKey, true);
                removePick(doc);
            } catch { /* the previous preview document has already been discarded */ }
        };
    }

    function reloadPreviewResult() {
        if (typeof options.onReloadPreview === 'function') {
            options.onReloadPreview();
            return;
        }
        try {
            const win = frame.contentWindow;
            const doc = frame.contentDocument;
            if (win && doc && doc.URL && doc.URL !== 'about:blank') {
                win.location.reload();
                return;
            }
        } catch { /* the preview document is not reachable from the shell */ }
        const src = String(frame.getAttribute('src') || '');
        if (!src || src === 'about:blank') return;
        const bare = src.replace(/([?&])_studio=\d+/, '').replace(/[?&]$/, '');
        frame.removeAttribute('srcdoc');
        frame.src = `${bare}${bare.includes('?') ? '&' : '?'}_studio=${Date.now()}`;
    }

    function buildReloadItem() {
        const button = el('button', {
            type: 'button',
            role: 'menuitem',
            class: 'is-reload',
            title: 'Reload the PHP + SSI preview while preserving scroll position (F5)',
            'aria-keyshortcuts': 'F5 Control+R',
        });
        button.append(
            el('span', { text: 'Reload' }),
            el('span', { class: 'preview-menu-accel', text: 'F5 / Ctrl+R' }),
        );
        button.addEventListener('click', () => {
            closeMenu();
            reloadPreviewResult();
        });
        return button;
    }

    function buildNavItem(label, accel, shortcut, enabled, onClick) {
        const props = {
            type: 'button',
            role: 'menuitem',
            class: 'is-nav',
            title: label === 'Back'
                ? `Go back to the previous preview page (${accel})`
                : `Go forward to the next preview page (${accel})`,
            'aria-keyshortcuts': shortcut,
        };
        if (!enabled) props.disabled = true;
        const button = el('button', props);
        button.append(
            el('span', { text: label }),
            el('span', { class: 'preview-menu-accel', text: accel }),
        );
        button.addEventListener('click', () => {
            if (!enabled) return;
            closeMenu();
            onClick();
        });
        return button;
    }

    function openMenu(doc, event) {
        event.preventDefault();
        event.stopPropagation();
        let target = event.target;
        if (target?.nodeType === 3) target = target.parentElement;
        if (!elementTarget(doc, target)) {
            hideBox(doc);
            renderMenu(doc, null, emptyLocated(), parentPoint(frame, event));
            return;
        }
        showBox(doc, target);
        const source = String(options.getSource?.() || '');
        const fileType = options.getFileType?.() || '';
        const hint = hintFromEvent(doc, event);
        const located = locateElementInSource(source, target, fileType, hint);
        renderMenu(doc, target, located, parentPoint(frame, event));
    }

    function openFromParent(event) {
        event.preventDefault();
        event.stopPropagation();
        const doc = currentDoc();
        const point = { x: event.clientX, y: event.clientY };
        const target = elementFromParentEvent(frame, doc, event);
        if (!doc || !elementTarget(doc, target)) {
            if (doc) hideBox(doc);
            renderMenu(doc, null, emptyLocated(), point);
            return;
        }
        showBox(doc, target);
        const local = frameClientPoint(frame, event);
        const source = String(options.getSource?.() || '');
        const fileType = options.getFileType?.() || '';
        const hint = hintFromEvent(doc, { clientX: local.x, clientY: local.y });
        const located = locateElementInSource(source, target, fileType, hint);
        renderMenu(doc, target, located, point);
    }

    function renderMenu(doc, target, located, point) {
        const chain = (located.jumps || []).filter((item) => item.endOffset > item.startOffset);
        const allowPhoto = Boolean(target) && located.photo && options.getHasGallery?.() !== false;
        window.clearTimeout(submenuTimer);
        submenuControls = null;
        menu.replaceChildren();
        menu.append(el('p', {
            class: 'preview-menu-kicker',
            text: target ? `Right-clicked ${friendlyName(target)}` : 'Rendered preview',
        }));
        menu.append(buildNavItem('Back', 'Alt+←', 'Alt+ArrowLeft', Boolean(options.getCanGoBack?.()), () => options.onBack?.()));
        menu.append(buildNavItem('Forward', 'Alt+→', 'Alt+ArrowRight', Boolean(options.getCanGoForward?.()), () => options.onForward?.()));
        menu.append(buildReloadItem());
        if (target && !chain.length && !allowPhoto) {
            menu.append(el('div', { class: 'preview-menu-rule' }));
            menu.append(el('button', {
                type: 'button',
                role: 'menuitem',
                disabled: true,
                text: sourceMissing(options) ? 'Page source is not loaded yet' : 'No source block for this element',
                title: sourceMissing(options) ? 'Open a page with source before jumping to a code block' : 'This element has no matching source block yet',
            }));
        }
        if (chain.length) {
            menu.append(el('div', { class: 'preview-menu-rule' }));
            menu.append(buildJumpMenu(doc, target, chain));
        }
        if (allowPhoto) {
            menu.append(el('div', { class: 'preview-menu-rule' }));
            const photo = el('button', {
                type: 'button',
                role: 'menuitem',
                class: 'is-photo',
                text: 'Select photo in gallery',
                title: `Select ${located.photo} in Gallery Order & Captions`,
            });
            photo.addEventListener('click', () => {
                const name = located.photo;
                closeMenu();
                options.onSelectPhoto?.(name);
            });
            menu.append(photo);
        }
        menu.hidden = false;
        menuOpen = true;
        placeMenu(menu, point.x, point.y);
        menu.querySelector('[role="menuitem"]:not(:disabled)')?.focus({ preventScroll: true });
    }

    function buildJumpMenu(doc, target, chain) {
        const host = el('div', { class: 'preview-submenu-host' });
        const jump = el('button', {
            type: 'button',
            role: 'menuitem',
            class: 'is-jump',
            'aria-haspopup': 'menu',
            'aria-expanded': 'false',
        });
        jump.title = 'Jump to the matching source block in the code editor';
        jump.append(el('span', { text: 'Jump to code block' }), el('span', { class: 'preview-menu-caret', text: '\u25B8', 'aria-hidden': 'true' }));
        const submenu = el('div', { class: 'preview-submenu', role: 'menu', hidden: true, 'aria-label': 'Jump to code block' });
        chain.forEach((item, index) => {
            const button = el('button', { type: 'button', role: 'menuitem', class: 'preview-chain-item' });
            button.style.setProperty('--depth', String(index));
            const lines = lineSpan(item.startLine, item.endLine);
            button.append(el('span', { class: 'preview-chain-name', text: item.label || 'element' }));
            if (lines) button.append(el('span', { class: 'preview-chain-lines', text: lines }));
            button.title = `Jump to ${item.label || 'this element'} in the code editor · ${linePhrase(item.startLine, item.endLine)}`;
            const outline = () => {
                if (item.element) showBox(doc, item.element);
            };
            button.addEventListener('mouseenter', outline);
            button.addEventListener('focus', outline);
            button.addEventListener('click', () => {
                closeMenu();
                options.onJump?.(item);
            });
            submenu.append(button);
        });
        const open = () => {
            window.clearTimeout(submenuTimer);
            submenu.hidden = false;
            jump.setAttribute('aria-expanded', 'true');
            placeSubmenu(jump, submenu);
        };
        const close = () => {
            submenu.hidden = true;
            jump.setAttribute('aria-expanded', 'false');
            showBox(doc, target);
        };
        const scheduleClose = () => {
            window.clearTimeout(submenuTimer);
            submenuTimer = window.setTimeout(close, 160);
        };
        jump.addEventListener('mouseenter', open);
        jump.addEventListener('focus', open);
        jump.addEventListener('click', (event) => {
            event.preventDefault();
            open();
        });
        jump.addEventListener('mouseleave', scheduleClose);
        submenu.addEventListener('mouseenter', open);
        submenu.addEventListener('mouseleave', scheduleClose);
        host.append(jump, submenu);
        submenuControls = {
            open,
            close,
            focusHost: () => jump.focus(),
            focusFirst: () => submenu.querySelector('[role="menuitem"]')?.focus(),
        };
        return host;
    }

    const stageNodes = previewStageNodes(frame, options.stage);
    const onStageMenu = (event) => openFromParent(event);
    stageNodes.forEach((node) => node.addEventListener('contextmenu', onStageMenu, true));
    detachStage = () => {
        stageNodes.forEach((node) => node.removeEventListener('contextmenu', onStageMenu, true));
    };

    frame.addEventListener('load', attach);
    attach();

    return {
        refresh: attach,
        close: closeMenu,
        destroy() {
            unlisten();
            detachStage();
            detachWindow();
            closeMenu();
            menu.remove();
        },
    };
}

function emptyLocated() {
    return { jumps: [], photo: '' };
}

function elementTarget(doc, target) {
    if (!doc || !target || target.nodeType !== 1) return false;
    if (target.id === PICK_BOX_ID) return false;
    if (target === doc.body || target === doc.documentElement) return false;
    return true;
}

function elementFromParentEvent(frame, doc, event) {
    if (!doc || !frame || event.target !== frame) return null;
    const point = frameClientPoint(frame, event);
    let node = null;
    try { node = doc.elementFromPoint(point.x, point.y); } catch { return null; }
    if (node?.nodeType === 3) node = node.parentElement;
    return elementTarget(doc, node) ? node : null;
}

function frameClientPoint(frame, event) {
    if (!frame) return { x: event.clientX, y: event.clientY };
    const rect = frame.getBoundingClientRect();
    const width = frame.clientWidth || rect.width || 1;
    const height = frame.clientHeight || rect.height || 1;
    return {
        x: ((event.clientX - rect.left) * width) / (rect.width || 1),
        y: ((event.clientY - rect.top) * height) / (rect.height || 1),
    };
}

function previewStageNodes(frame, stage) {
    const nodes = [];
    const add = (node) => {
        if (!node || node.nodeType !== 1 || nodes.includes(node)) return;
        nodes.push(node);
    };
    const doc = frame?.ownerDocument || stage?.ownerDocument || null;
    add(stage);
    add(frame?.parentElement);
    add(frame?.closest?.('.preview-stage'));
    add(frame?.closest?.('.preview-viewport-wrap'));
    add(doc?.getElementById?.('preview-stage'));
    add(doc?.getElementById?.('preview-viewport-wrap'));
    add(doc?.querySelector?.('.preview-stage'));
    add(doc?.querySelector?.('.preview-viewport-wrap'));
    return nodes;
}

function sourceMissing(options) {
    const source = String(options.getSource?.() || '');
    return !source.trim();
}

function parentPoint(frame, event) {
    const rect = frame.getBoundingClientRect();
    const width = frame.clientWidth || rect.width || 1;
    const height = frame.clientHeight || rect.height || 1;
    return {
        x: rect.left + (event.clientX * rect.width) / width,
        y: rect.top + (event.clientY * rect.height) / height,
    };
}

function placeMenu(menu, x, y) {
    menu.style.left = '0px';
    menu.style.top = '0px';
    const pad = 8;
    const width = menu.offsetWidth;
    const height = menu.offsetHeight;
    const left = clamp(x, pad, Math.max(pad, window.innerWidth - width - pad));
    const top = clamp(y, pad, Math.max(pad, window.innerHeight - height - pad));
    menu.style.left = `${Math.round(left)}px`;
    menu.style.top = `${Math.round(top)}px`;
}

function placeSubmenu(host, submenu) {
    submenu.hidden = false;
    submenu.style.visibility = 'hidden';
    submenu.style.left = '0px';
    submenu.style.top = '0px';
    const pad = 8;
    const anchor = host.getBoundingClientRect();
    const width = submenu.offsetWidth;
    const height = submenu.offsetHeight;
    let left = anchor.right - 4;
    if (left + width > window.innerWidth - pad) left = anchor.left - width + 4;
    left = clamp(left, pad, Math.max(pad, window.innerWidth - width - pad));
    let top = anchor.top;
    if (top + height > window.innerHeight - pad) top = window.innerHeight - height - pad;
    top = clamp(top, pad, Math.max(pad, window.innerHeight - height - pad));
    submenu.style.left = `${Math.round(left)}px`;
    submenu.style.top = `${Math.round(top)}px`;
    submenu.style.visibility = '';
}

function lineSpan(start, end) {
    if (!start) return '';
    if (!end || start === end) return String(start);
    return `${start}\u2013${end}`;
}

function hintFromEvent(doc, event) {
    const pos = caretAt(doc, event.clientX, event.clientY);
    if (!pos?.node) return {};
    return { text: segmentText(pos.node) };
}

function caretAt(doc, x, y) {
    try {
        if (typeof doc.caretPositionFromPoint === 'function') {
            const pos = doc.caretPositionFromPoint(x, y);
            if (pos?.offsetNode) return { node: pos.offsetNode, offset: pos.offset };
        }
        if (typeof doc.caretRangeFromPoint === 'function') {
            const range = doc.caretRangeFromPoint(x, y);
            if (range?.startContainer) return { node: range.startContainer, offset: range.startOffset };
        }
    } catch { /* the preview document rejected the caret query */ }
    return null;
}

function segmentText(node) {
    const origin = node?.nodeType === 3 ? node : node;
    const parent = origin?.parentNode;
    if (!parent?.childNodes) return normalizeText(origin?.textContent || '');
    const groups = [];
    let current = [];
    let breaks = 0;
    const flush = () => {
        if (current.length) groups.push(current);
        current = [];
    };
    for (const kid of parent.childNodes) {
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
    const group = groups.find((list) => list.some((item) => item === origin || item.contains?.(origin)));
    return normalizeText((group || [origin]).map((item) => item.textContent || '').join(' '));
}

function showBox(doc, target) {
    if (!doc || !target || target.nodeType !== 1) return;
    if (target.id === PICK_BOX_ID || target === doc.body || target === doc.documentElement) {
        hideBox(doc);
        return;
    }
    const box = ensureBox(doc);
    if (!box) return;
    const rect = target.getBoundingClientRect();
    if (rect.width < 1 && rect.height < 1) {
        box.style.display = 'none';
        return;
    }
    box.style.display = 'block';
    box.style.left = `${rect.left}px`;
    box.style.top = `${rect.top}px`;
    box.style.width = `${Math.max(rect.width, 2)}px`;
    box.style.height = `${Math.max(rect.height, 2)}px`;
}

function hideBox(doc) {
    const box = doc?.getElementById?.(PICK_BOX_ID);
    if (box) box.style.display = 'none';
}

function ensureBox(doc) {
    if (!doc.body) return null;
    if (!doc.getElementById(PICK_STYLE_ID)) {
        const style = doc.createElement('style');
        style.id = PICK_STYLE_ID;
        style.textContent = `
            #${PICK_BOX_ID} {
                position: fixed;
                z-index: 2147483646;
                pointer-events: none;
                box-sizing: border-box;
                border: 2px solid #ffcc00;
                box-shadow: 0 0 0 1px #000, inset 0 0 0 1px rgba(0, 0, 0, 0.45);
                background: rgba(255, 204, 0, 0.14);
                border-radius: 2px;
                display: none;
            }
        `;
        (doc.head || doc.documentElement).append(style);
    }
    let box = doc.getElementById(PICK_BOX_ID);
    if (!box) {
        box = doc.createElement('div');
        box.id = PICK_BOX_ID;
        doc.body.append(box);
    }
    return box;
}

function removePick(doc) {
    doc?.getElementById?.(PICK_STYLE_ID)?.remove();
    doc?.getElementById?.(PICK_BOX_ID)?.remove();
}

/**
 * Map a preview DOM element to the source block that rendered it.
 * Returns the enclosing block as startOffset/endOffset/startLine/endLine/label,
 * plus jumps[] when the inner element and the container are different.
 */
export function locateElementInSource(sourceText, clickedEl, fileType, hint) {
    const photo = photoOf(clickedEl);
    const blank = {
        startOffset: 0,
        endOffset: 0,
        startLine: 0,
        endLine: 0,
        label: '',
        photo,
        jumps: [],
        inner: null,
    };
    const source = String(sourceText ?? '');
    if (!clickedEl || !source.trim()) return blank;
    try {
        return packChain(locateChain(source, clickedEl, fileType, hint), photo);
    } catch {
        return blank;
    }
}

const CHAIN_SKIP = new Set(['html', 'head', 'body', 'script', 'style', 'noscript', 'link', 'meta', 'br', 'wbr']);
const START_CLASSES = ['hero-wrapper', 'idTitle', 'inventory', 'price'];

function locateChain(source, el, fileType, hint) {
    if (closest(el, '#banner, .banner, #searchContainer, .bannerFooter')) {
        const header = findInclude(source, '/includes/header.html');
        const anchor = closest(el, '#banner, .banner, #searchContainer, .bannerFooter') || el;
        return header ? [chainItem(anchor, header, 'header include')] : [];
    }
    if (closest(el, '.soldTopBanner')) return soldChain(source, el);
    if (closest(el, '.index-sales-line')) return salesChain(source, el);
    if (isPhpSource(fileType) && closest(el, '.description')) return descriptionChain(source, el, hint);
    if (isPhpSource(fileType) && (closest(el, '.idTitle') || (tagName(el) === 'h1' && closest(el, '.infoContainer')))) {
        return sharedFieldChain(source, el, assignmentRange(source, 'vehicleName'));
    }
    if (isPhpSource(fileType) && closest(el, '.inventory')) return sharedFieldChain(source, el, assignmentRange(source, 'inventoryNumber'));
    if (isPhpSource(fileType) && closest(el, '.price')) return sharedFieldChain(source, el, assignmentRange(source, 'price'));
    if (isPhpSource(fileType) && closest(el, '.hero-wrapper, .thumbnail-wrapper')) {
        return sharedFieldChain(source, el, imageDataRange(source));
    }
    if (isPhpSource(fileType)) return [];
    return htmlChain(source, previewChain(el));
}

function previewChain(el) {
    const all = [];
    let current = el;
    while (current && current.nodeType === 1) {
        if (current.id !== PICK_BOX_ID && !CHAIN_SKIP.has(tagName(current))) all.push(current);
        current = current.parentElement;
    }
    all.reverse();
    if (!all.length) return [];
    return all.slice(chainStartIndex(all));
}

function chainStartIndex(chain) {
    let nearestFeature = -1;
    chain.forEach((node, index) => {
        if (isDomFeature(node)) nearestFeature = index;
    });
    if (nearestFeature >= 0) return nearestFeature;
    const outermostSemantic = chain.findIndex((node) => isDomSemantic(node));
    if (outermostSemantic >= 0) return outermostSemantic;
    let wrapper = -1;
    for (let index = chain.length - 1; index >= 0 && chain.length - 1 - index < 4; index -= 1) {
        const tag = tagName(chain[index]);
        if (tag === 'div' || tag === 'section' || tag === 'article') wrapper = index;
    }
    return wrapper >= 0 ? wrapper : chain.length - 1;
}

function isDomFeature(element) {
    const names = tokens(classNameOf(element));
    if (names.includes('D_35p_1f') && names.includes('list')) return true;
    return FEATURE_CLASSES.some((name) => names.includes(name)) || START_CLASSES.some((name) => names.includes(name));
}

function isDomSemantic(element) {
    return ['figure', 'figcaption', 'p', 'li', 'ul', 'ol', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'blockquote', 'table', 'tr', 'td', 'th'].includes(tagName(element));
}

function htmlChain(source, domNodes) {
    if (!domNodes.length) return [];
    const root = scanHtml(source, 0);
    numberTree(root, source);
    const nodes = flatten(root);
    assignOccurrence(nodes);
    return alignChain(domNodes, nodes);
}

function alignChain(domNodes, astNodes) {
    if (!domNodes.length) return [];
    let anchorIndex = -1;
    let anchor = null;
    for (let index = domNodes.length - 1; index >= 0; index -= 1) {
        const tag = tagName(domNodes[index]);
        const found = matchElement(astNodes.filter((node) => node.kind === 'element' && node.tag === tag), domNodes[index]);
        if (!found) continue;
        anchorIndex = index;
        anchor = found;
        break;
    }
    if (!anchor) return [];
    const pairs = new Map();
    pairs.set(domNodes[anchorIndex], anchor);
    let cursor = anchor;
    for (let index = anchorIndex - 1; index >= 0; index -= 1) {
        const wanted = tagName(domNodes[index]);
        let parent = cursor.parent;
        let found = null;
        while (parent) {
            if (parent.kind === 'element' && parent.tag === wanted) {
                found = parent;
                break;
            }
            parent = parent.parent;
        }
        if (!found) continue;
        pairs.set(domNodes[index], found);
        cursor = found;
    }
    cursor = anchor;
    for (let index = anchorIndex + 1; index < domNodes.length; index += 1) {
        const pool = flatten(cursor).filter((node) => node.kind === 'element' && node.tag === tagName(domNodes[index]));
        const found = matchElement(pool, domNodes[index]);
        if (!found) continue;
        pairs.set(domNodes[index], found);
        cursor = found;
    }
    return domNodes.map((element) => (pairs.has(element) ? chainItem(element, pairs.get(element)) : null)).filter(Boolean);
}

function sharedFieldChain(source, el, node) {
    if (!node) return [];
    const range = rangeOf(node);
    if (!range) return [];
    const dom = previewChain(el);
    const nodes = dom.length ? dom : [el];
    return nodes.map((element) => ({
        ...range,
        label: friendlyName(element),
        element,
    }));
}

function descriptionChain(source, el, hint) {
    const region = descriptionRegion(source);
    if (!region) return [];
    const dom = previewChain(el).filter((node) => !tokens(classNameOf(node)).includes('description'));
    const root = scanHtml(region.html, region.start);
    numberTree(root, source);
    const nodes = flatten(root);
    assignOccurrence(nodes);
    const matched = alignChain(dom, nodes);
    if (matched.length) return matched;
    const needle = normalizeText(hint?.text || '');
    const hit = needle ? bestParagraph(brParagraphs(region, source), needle) : null;
    if (!hit) return [];
    const range = rangeOf(hit);
    return range ? [{ ...range, label: 'paragraph', element: el }] : [];
}

function soldChain(source, el) {
    const include = findInclude(source, 'similar_vehicles.php');
    const widget = findWidget(source, 'related-sales');
    const banner = closest(el, '.soldTopBanner') || el;
    const node = include || widget || assignmentRange(source, 'vehicleType');
    const item = node ? chainItem(banner, node, 'suggestion strip') : null;
    return item ? [item] : [];
}

function salesChain(source, el) {
    const found = locateSalesLine(source, el);
    const items = [];
    const card = closest(el, '.D_35p_1f') || el;
    if (found?.primary?.kind === 'element') {
        const item = chainItem(card, found.primary);
        if (item) items.push(item);
    }
    const include = found?.inner?.kind === 'comment' ? found.inner : (found?.primary?.kind === 'comment' ? found.primary : null);
    if (include) {
        const item = chainItem(el, include, 'sales link include');
        if (item) items.push(item);
    }
    return items;
}

function chainItem(element, node, name) {
    const range = rangeOf(node);
    if (!range) return null;
    const label = name || (node.kind === 'element' ? friendlyName(element) : sourceFriendlyName(node));
    return { ...range, label, element: element || null };
}

function friendlyName(el) {
    const tag = tagName(el);
    if (tag === 'a') return 'hyperlink';
    if (tag === 'img') return 'img';
    if (tag === 'div') return 'div';
    if (tag === 'figure') return 'figure';
    if (tag === 'figcaption') return 'caption';
    if (tag === 'section') return 'section';
    if (tag === 'p') return 'paragraph';
    if (/^h[1-6]$/.test(tag)) return `heading (${tag})`;
    if (tag === 'ul' || tag === 'ol') return 'list';
    if (tag === 'li') return 'list item';
    if (tag === 'table') return 'table';
    if (tag === 'tr') return 'row';
    if (tag === 'td' || tag === 'th') return 'cell';
    if (tag === 'span') return 'span';
    if (tag === 'button') return 'button';
    if (tag === 'hr') return 'divider';
    return tag || 'element';
}

function sourceFriendlyName(node) {
    if (!node) return 'element';
    if (node.kind === 'paragraph') return 'paragraph';
    if (node.kind === 'php') return node.label || 'field';
    if (node.kind === 'comment') {
        const raw = node.raw || '';
        if (/header\.html/i.test(raw)) return 'header include';
        if (/similar_vehicles\.php/i.test(raw)) return 'suggestion strip';
        if (/index_sales_links\.php/i.test(raw)) return 'sales link include';
        if (/related-sales/i.test(raw)) return 'suggestion strip';
        return 'include';
    }
    return friendlyName({ tagName: node.tag });
}

function packChain(chain, photo) {
    const jumps = [];
    for (const item of chain || []) {
        if (!item || item.endOffset <= item.startOffset) continue;
        jumps.push(item);
    }
    const primary = jumps[0] || {
        startOffset: 0,
        endOffset: 0,
        startLine: 0,
        endLine: 0,
        label: '',
    };
    return {
        startOffset: primary.startOffset,
        endOffset: primary.endOffset,
        startLine: primary.startLine,
        endLine: primary.endLine,
        label: primary.label,
        photo: photo || '',
        jumps,
        inner: jumps.length > 1 ? jumps[jumps.length - 1] : null,
    };
}

function locateDocument(source, el) {
    const special = locateSpecial(source, el);
    if (special) return special;
    const root = scanHtml(source, 0);
    numberTree(root, source);
    const nodes = flatten(root);
    assignOccurrence(nodes);
    const exact = matchElement(nodes, el);
    if (!exact) return null;
    return chooseTargets(exact);
}

function locatePhp(source, el, hint) {
    if (closest(el, '#banner, .banner, #searchContainer, .bannerFooter')) {
        const header = findInclude(source, '/includes/header.html');
        return header ? { primary: header, inner: null } : null;
    }
    if (closest(el, '.soldTopBanner')) return locateSoldBanner(source);
    if (closest(el, '.index-sales-line')) return locateSalesLine(source, el);
    if (closest(el, '.description')) return locateDescription(source, el, hint);
    if (closest(el, '.idTitle') || (tagName(el) === 'h1' && closest(el, '.infoContainer'))) {
        const node = assignmentRange(source, 'vehicleName');
        return node ? { primary: node, inner: null } : null;
    }
    if (closest(el, '.inventory')) {
        const node = assignmentRange(source, 'inventoryNumber');
        return node ? { primary: node, inner: null } : null;
    }
    if (closest(el, '.price')) {
        const node = assignmentRange(source, 'price');
        return node ? { primary: node, inner: null } : null;
    }
    if (closest(el, '.hero-wrapper, .thumbnail-wrapper')) {
        const node = imageDataRange(source);
        return node ? { primary: node, inner: null } : null;
    }
    return null;
}

function locateSpecial(source, el) {
    if (closest(el, '#banner, .banner, #searchContainer, .bannerFooter')) {
        const header = findInclude(source, '/includes/header.html');
        return header ? { primary: header, inner: null } : null;
    }
    if (closest(el, '.soldTopBanner')) return locateSoldBanner(source);
    if (closest(el, '.index-sales-line')) return locateSalesLine(source, el);
    return null;
}

function locateSoldBanner(source) {
    const include = findInclude(source, 'similar_vehicles.php');
    const widget = findWidget(source, 'related-sales');
    if (include && widget) return { primary: include, inner: widget };
    if (include || widget) return { primary: include || widget, inner: null };
    const typeLine = assignmentRange(source, 'vehicleType');
    const makeLine = assignmentRange(source, 'makeType');
    if (typeLine && makeLine) return { primary: typeLine, inner: makeLine };
    return typeLine ? { primary: typeLine, inner: null } : null;
}

function locateSalesLine(source, el) {
    const card = closest(el, '.D_35p_1f');
    const root = scanHtml(source, 0);
    numberTree(root, source);
    const cards = flatten(root).filter((node) => node.kind === 'element' && node.tag === 'div' && hasClass(node, 'D_35p_1f') && hasClass(node, 'list'));
    const href = card?.querySelector?.('h3 a, h2 a, a')?.getAttribute?.('href') || '';
    const src = card?.querySelector?.('img')?.getAttribute?.('src') || '';
    let block = null;
    if (card) {
        block = cards.find((node) => {
            const html = source.slice(node.startOffset, node.endOffset);
            return htmlContainsUrl(html, href) || htmlContainsUrl(html, src);
        }) || matchElement(cards, card);
    }
    const includes = findAllIncludes(source, 'index_sales_links.php');
    let include = null;
    if (block) {
        include = includes.find((node) => node.startOffset >= block.startOffset && node.endOffset <= block.endOffset) || null;
    }
    if (!include && href) {
        include = includes.find((node) => htmlContainsUrl(node.raw, href)) || null;
    }
    if (!include && includes.length === 1) include = includes[0];
    if (block && include) return { primary: block, inner: include };
    if (block) return { primary: block, inner: null };
    if (include) return { primary: include, inner: null };
    return null;
}

function locateDescription(source, el, hint) {
    const region = descriptionRegion(source);
    if (!region) return null;
    const specific = descriptionTarget(el);
    const paragraphs = brParagraphs(region, source);
    if (specific) {
        const root = scanHtml(region.html, region.start);
        numberTree(root, source);
        const nodes = flatten(root);
        assignOccurrence(nodes);
        const exact = matchElement(nodes, specific);
        if (exact) return withParagraph(chooseTargets(exact), paragraphs);
    }
    const needle = normalizeText(hint?.text || '');
    if (!needle) return null;
    const hit = bestParagraph(paragraphs, needle);
    return hit ? { primary: hit, inner: null } : null;
}

function descriptionTarget(el) {
    const box = closest(el, '.description');
    if (!box || el === box) return null;
    const meaningful = new Set(['li', 'a', 'ul', 'ol', 'p', 'img', 'figcaption', 'figure', 'tr', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6']);
    let current = el;
    while (current && current !== box) {
        if (meaningful.has(tagName(current))) return current;
        current = current.parentElement;
    }
    return null;
}

function withParagraph(targets, paragraphs) {
    if (!targets?.primary) return null;
    if (targets.inner) return targets;
    const exact = targets.primary;
    const para = paragraphs.find((item) => item.startOffset <= exact.startOffset
        && item.endOffset >= exact.endOffset
        && item.endOffset - item.startOffset > exact.endOffset - exact.startOffset + 8);
    if (para && LEAF_TAGS.has(exact.tag)) return { primary: para, inner: exact };
    return targets;
}

function chooseTargets(exact) {
    if (!exact) return null;
    if (LEAF_TAGS.has(exact.tag)) {
        const feature = nearest(exact.parent, isFeature);
        const semantic = nearest(exact.parent, isSemantic);
        const primary = feature || semantic;
        if (primary && primary !== exact) return { primary, inner: exact };
    }
    return { primary: exact, inner: null };
}

function nearest(start, test) {
    let current = start;
    while (current) {
        if (test(current)) return current;
        current = current.parent;
    }
    return null;
}

function isFeature(node) {
    if (!node || node.kind !== 'element') return false;
    if (hasClass(node, 'D_35p_1f') && hasClass(node, 'list')) return true;
    return FEATURE_CLASSES.some((name) => hasClass(node, name));
}

function isSemantic(node) {
    return Boolean(node && node.kind === 'element' && SEMANTIC_TAGS.has(node.tag));
}

function matchElement(nodes, el) {
    const probe = probeFrom(el);
    if (!probe.tag) return null;
    const pool = nodes.filter((node) => node.kind === 'element' && node.tag === probe.tag);
    const unique = uniqueAttribute(pool, probe);
    if (unique) return unique;
    let best = null;
    let bestScore = 0;
    for (const node of pool) {
        const score = scoreNode(node, probe);
        if (score > bestScore) {
            bestScore = score;
            best = node;
        }
    }
    return bestScore >= 40 ? best : null;
}

function uniqueAttribute(nodes, probe) {
    if (probe.id) {
        const byId = nodes.filter((node) => node.attrs.id && node.attrs.id === probe.id);
        if (byId.length === 1) return byId[0];
    }
    const wanted = [];
    if (probe.tag === 'img' && probe.src) wanted.push(probe.src);
    else if (probe.tag === 'a' && probe.href) wanted.push(probe.href);
    else {
        if (probe.src) wanted.push(probe.src);
        if (probe.href) wanted.push(probe.href);
    }
    if (!wanted.length) return null;
    const hits = nodes.filter((node) => {
        const urls = nodeUrls(node);
        return wanted.every((url) => urls.some((candidate) => urlsMatch(candidate, url)));
    });
    return hits.length === 1 ? hits[0] : null;
}

function scoreNode(node, probe) {
    let score = 1;
    if (probe.id && node.attrs.id) {
        if (node.attrs.id === probe.id) score += 100;
        else return -1;
    }
    if (probe.src && node.attrs.src && node.tag === 'img' && !urlsMatch(node.attrs.src, probe.src)) return -1;
    if (probe.href && node.attrs.href && node.tag === 'a' && !urlsMatch(node.attrs.href, probe.href)) return -1;
    score += classPoints(node.attrs.class, probe.className);
    if (sharedUrl(node, probe)) score += 60;
    score += textPoints(node.text, probe.text);
    if (probe.index >= 0 && node.occ === probe.index && (probe.className || probe.index > 0 || probe.text)) score += 20;
    return score;
}

function classPoints(nodeClass, probeClass) {
    const left = tokens(nodeClass);
    const right = tokens(probeClass);
    if (!left.length || !right.length) return 0;
    const have = new Set(left);
    let shared = 0;
    for (const token of right) if (have.has(token)) shared += 1;
    if (!shared) return -10;
    if (shared === left.length && shared === right.length) return 30;
    return Math.min(30, shared * 10);
}

function textPoints(nodeText, probeText) {
    const left = normalizeText(nodeText);
    const right = normalizeText(probeText);
    if (!left || !right) return 0;
    if (left === right) return 50;
    const short = left.length < right.length ? left : right;
    const long = left.length < right.length ? right : left;
    if (long.startsWith(short) && short.length >= 20) return 50;
    return 0;
}

function sharedUrl(node, probe) {
    const urls = nodeUrls(node);
    return [probe.src, probe.href].some((url) => url && urls.some((candidate) => urlsMatch(candidate, url)));
}

function nodeUrls(node) {
    const urls = [node.attrs?.src, node.attrs?.href];
    const image = firstDescendant(node, 'img');
    const link = firstDescendant(node, 'a');
    if (image?.attrs?.src) urls.push(image.attrs.src);
    if (link?.attrs?.href) urls.push(link.attrs.href);
    return urls.filter(Boolean);
}

function firstDescendant(node, tag) {
    for (const child of node.children || []) {
        if (child.kind !== 'element') continue;
        if (child.tag === tag) return child;
        const nested = firstDescendant(child, tag);
        if (nested) return nested;
    }
    return null;
}

function probeFrom(el) {
    const tag = tagName(el);
    const src = ownOrChildUrl(el, 'src', 'img');
    const href = ownOrChildUrl(el, 'href', 'a');
    const text = normalizeText(el.textContent || '') || normalizeText(el.getAttribute?.('alt') || el.getAttribute?.('title') || '');
    return {
        tag,
        id: el.getAttribute?.('id') || '',
        className: classNameOf(el),
        src,
        href,
        text,
        index: occurrenceIndex(el),
    };
}

function ownOrChildUrl(el, attr, childTag) {
    const own = el.getAttribute?.(attr);
    if (own && (tagName(el) === childTag || (attr === 'src' && tagName(el) === 'img') || (attr === 'href' && tagName(el) === 'a'))) {
        return own;
    }
    if (own && tagName(el) !== 'img' && tagName(el) !== 'a') {
        /* a container's own attribute is not a child url */
    }
    const child = tagName(el) === childTag ? el : el.querySelector?.(childTag);
    return child?.getAttribute?.(attr) || (tagName(el) === childTag ? own || '' : '');
}

function occurrenceIndex(el) {
    const doc = el.ownerDocument;
    if (!doc || typeof doc.getElementsByTagName !== 'function') return 0;
    const wanted = tokenKey(classNameOf(el));
    let index = 0;
    const list = doc.getElementsByTagName(tagName(el));
    for (const node of list) {
        if (node === el) return index;
        if (tokenKey(classNameOf(node)) === wanted) index += 1;
    }
    return 0;
}

function assignOccurrence(nodes) {
    const groups = new Map();
    for (const node of nodes) {
        if (node.kind !== 'element') continue;
        const key = `${node.tag}|${tokenKey(node.attrs.class)}`;
        const seen = groups.get(key) || 0;
        node.occ = seen;
        groups.set(key, seen + 1);
    }
}

function scanHtml(source, baseOffset) {
    const root = { kind: 'root', tag: '#root', children: [], parent: null, attrs: {} };
    const stack = [root];
    const length = source.length;
    let index = 0;
    while (index < length) {
        const lt = source.indexOf('<', index);
        if (lt < 0) {
            appendText(stack[stack.length - 1], source.slice(index));
            break;
        }
        if (lt > index) appendText(stack[stack.length - 1], source.slice(index, lt));
        if (source.startsWith('<!--', lt)) {
            const end = source.indexOf('-->', lt + 4);
            const close = end < 0 ? length : end + 3;
            const raw = source.slice(lt, close);
            if (isTrackedComment(raw)) addChild(stack[stack.length - 1], commentNode(raw, baseOffset + lt));
            index = close;
            continue;
        }
        if (source.startsWith('<!', lt) || source.startsWith('<?', lt)) {
            const end = source.indexOf('>', lt + 2);
            index = end < 0 ? length : end + 1;
            continue;
        }
        const tagEnd = findTagEnd(source, lt);
        if (tagEnd < 0) {
            appendText(stack[stack.length - 1], source.slice(lt));
            break;
        }
        const rawOpen = source.slice(lt, tagEnd + 1);
        if (rawOpen[1] === '/') {
            const name = /^<\s*\/\s*([a-zA-Z][\w:-]*)/.exec(rawOpen);
            if (name) closeTag(stack, name[1].toLowerCase(), baseOffset + lt, baseOffset + tagEnd + 1);
            index = tagEnd + 1;
            continue;
        }
        const name = /^<\s*([a-zA-Z][\w:-]*)/.exec(rawOpen);
        if (!name) {
            appendText(stack[stack.length - 1], '<');
            index = lt + 1;
            continue;
        }
        const tag = name[1].toLowerCase();
        const node = {
            kind: 'element',
            tag,
            attrs: parseAttrs(rawOpen),
            startOffset: baseOffset + lt,
            endOffset: baseOffset + tagEnd + 1,
            openEnd: baseOffset + tagEnd + 1,
            parent: null,
            children: [],
            text: '',
        };
        addChild(stack[stack.length - 1], node);
        const selfClose = /\/\s*>$/.test(rawOpen);
        if (selfClose || VOID_TAGS.has(tag)) {
            index = tagEnd + 1;
            continue;
        }
        stack.push(node);
        if (RAW_TAGS.has(tag)) {
            const rest = source.slice(tagEnd + 1);
            const closeMatch = new RegExp(`<\\/\\s*${tag}\\s*>`, 'i').exec(rest);
            if (!closeMatch) {
                appendText(node, rest);
                node.endOffset = baseOffset + length;
                stack.pop();
                index = length;
                continue;
            }
            appendText(node, rest.slice(0, closeMatch.index));
            const closeStart = tagEnd + 1 + closeMatch.index;
            const closeEnd = closeStart + closeMatch[0].length;
            node.endOffset = baseOffset + closeEnd;
            stack.pop();
            index = closeEnd;
            continue;
        }
        index = tagEnd + 1;
    }
    const end = baseOffset + length;
    while (stack.length > 1) {
        const node = stack.pop();
        if (node.endOffset <= node.openEnd) node.endOffset = end;
    }
    fillText(root);
    return root;
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

function parseAttrs(raw) {
    const attrs = {};
    const body = raw.replace(/^<\s*\/?\s*[^\s/>]+/, '').replace(/\/?\s*>$/, '');
    const re = /([^\s=\/]+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'=<>`]+)))?/g;
    let match = re.exec(body);
    while (match) {
        const key = match[1].toLowerCase();
        if (KEPT_ATTRS.has(key)) {
            const value = match[2] ?? match[3] ?? match[4] ?? '';
            attrs[key] = decodeEntities(value);
        }
        match = re.exec(body);
    }
    return attrs;
}

function closeTag(stack, name, closeStart, closeEnd) {
    let found = -1;
    for (let index = stack.length - 1; index > 0; index -= 1) {
        if (stack[index].tag === name) {
            found = index;
            break;
        }
    }
    if (found < 0) return;
    for (let index = stack.length - 1; index > found; index -= 1) {
        if (stack[index].endOffset <= stack[index].openEnd) stack[index].endOffset = closeStart;
    }
    stack[found].endOffset = closeEnd;
    stack.length = found;
}

function addChild(parent, node) {
    node.parent = parent;
    parent.children.push(node);
}

function appendText(parent, text) {
    if (!text || !parent) return;
    parent.children.push({ kind: 'text', text, parent });
}

function fillText(node) {
    if (!node || node.kind === 'text') return node?.text || '';
    if (node.kind === 'comment' || node.kind === 'php' || node.kind === 'paragraph') return node.text || '';
    let text = '';
    for (const child of node.children || []) {
        if (child.kind === 'text') text += child.text;
        else if (child.kind === 'element') text += fillText(child);
    }
    node.text = text;
    return text;
}

function flatten(node, out = []) {
    for (const child of node.children || []) {
        if (child.kind === 'element' || child.kind === 'comment') out.push(child);
        if (child.kind === 'element') flatten(child, out);
    }
    return out;
}

function numberTree(node, source) {
    const visit = (current) => {
        if (current.kind === 'element' || current.kind === 'comment') {
            current.startLine = lineNumber(source, current.startOffset);
            current.endLine = endLineNumber(source, current.endOffset);
        }
        for (const child of current.children || []) {
            if (child.kind === 'element' || child.kind === 'comment') visit(child);
        }
    };
    visit(node);
}

function isTrackedComment(raw) {
    return /<!--\s*#include\b/i.test(raw)
        || /<!--\s*related-sales\b/i.test(raw)
        || /<!--\s*page-values\b/i.test(raw);
}

function commentNode(raw, startOffset) {
    return {
        kind: 'comment',
        tag: '#comment',
        raw,
        attrs: {},
        text: '',
        startOffset,
        endOffset: startOffset + raw.length,
        parent: null,
        children: [],
    };
}

function findInclude(source, needle) {
    return findAllIncludes(source, needle)[0] || null;
}

function findAllIncludes(source, needle) {
    const found = [];
    const re = /<!--\s*#include\b[\s\S]*?-->/gi;
    let match = re.exec(source);
    while (match) {
        if (match[0].toLowerCase().includes(String(needle).toLowerCase())) {
            const node = commentNode(match[0], match.index);
            node.startLine = lineNumber(source, node.startOffset);
            node.endLine = endLineNumber(source, node.endOffset);
            found.push(node);
        }
        match = re.exec(source);
    }
    return found;
}

function findWidget(source, name) {
    const re = new RegExp(`<!--\\s*${name}\\b[\\s\\S]*?-->`, 'i');
    const match = re.exec(source);
    if (!match) return null;
    const node = commentNode(match[0], match.index);
    node.startLine = lineNumber(source, node.startOffset);
    node.endLine = endLineNumber(source, node.endOffset);
    return node;
}

function descriptionRegion(source) {
    const open = /\$description\s*=\s*<<<(['"]?)DESC\1[^\n]*\n/.exec(source);
    if (!open) return null;
    const start = open.index + open[0].length;
    const close = /\n[ \t]*DESC\b/.exec(source.slice(start));
    if (!close) return null;
    const end = start + close.index;
    return { start, end, html: source.slice(start, end) };
}

function brParagraphs(region, source) {
    const html = region.html;
    const re = /<br\s*\/?\s*>(?:\s*<br\s*\/?\s*>)+/gi;
    const cuts = [];
    let last = 0;
    let match = re.exec(html);
    while (match) {
        cuts.push([last, match.index]);
        last = match.index + match[0].length;
        match = re.exec(html);
    }
    cuts.push([last, html.length]);
    const nodes = [];
    for (const [from, to] of cuts) {
        let start = from;
        let end = to;
        while (start < end && /\s/.test(html[start])) start += 1;
        while (end > start && /\s/.test(html[end - 1])) end -= 1;
        if (start >= end) continue;
        const slice = html.slice(start, end);
        if (/^<ul[\s>]/i.test(slice) && /<\/ul>\s*$/i.test(slice)) continue;
        if (/^<ol[\s>]/i.test(slice) && /<\/ol>\s*$/i.test(slice)) continue;
        const text = normalizeText(slice.replace(/<[^>]+>/g, ' '));
        if (!text) continue;
        const startOffset = region.start + start;
        const endOffset = region.start + end;
        nodes.push({
            kind: 'paragraph',
            tag: 'p',
            attrs: {},
            text,
            startOffset,
            endOffset,
            startLine: lineNumber(source, startOffset),
            endLine: endLineNumber(source, endOffset),
            parent: null,
            children: [],
        });
    }
    return nodes;
}

function bestParagraph(paragraphs, needle) {
    const wanted = normalizeText(needle);
    if (!wanted) return null;
    let best = null;
    let bestScore = 0;
    for (const paragraph of paragraphs) {
        const text = paragraph.text;
        if (!text) continue;
        if (text === wanted) return paragraph;
        if (wanted.length >= 8 && text.includes(wanted)) {
            const score = wanted.length / text.length;
            if (score > bestScore) {
                bestScore = score;
                best = paragraph;
            }
        } else if (text.length >= 8 && wanted.includes(text)) {
            const score = (text.length / wanted.length) * 0.9;
            if (score > bestScore) {
                bestScore = score;
                best = paragraph;
            }
        }
    }
    return bestScore >= 0.2 ? best : null;
}

function assignmentRange(source, name) {
    const re = new RegExp(`\\$${name}\\b\\s*=`);
    const match = re.exec(source);
    if (!match) return null;
    const startOffset = match.index;
    let endOffset = source.indexOf('\n', startOffset);
    if (endOffset < 0) endOffset = source.length;
    if (!source.slice(startOffset, endOffset).includes(';')) {
        const semi = source.indexOf(';', endOffset);
        if (semi >= 0) endOffset = semi + 1;
    }
    return phpNode(source, startOffset, endOffset, `$${name}`);
}

function imageDataRange(source) {
    const match = /\$image_data\s*=\s*read_image_data\s*\([^)]*\)\s*;/.exec(source);
    if (!match) return null;
    const node = phpNode(source, match.index, match.index + match[0].length, '$image_data');
    node.menuText = 'Jump to $image_data in index.php';
    return node;
}

function phpNode(source, startOffset, endOffset, label) {
    return {
        kind: 'php',
        tag: 'php',
        label,
        attrs: {},
        text: '',
        menuText: '',
        startOffset,
        endOffset,
        startLine: lineNumber(source, startOffset),
        endLine: endLineNumber(source, endOffset),
        parent: null,
        children: [],
    };
}

function pack(found, photo) {
    const jumps = [];
    const push = (node) => {
        const range = rangeOf(node);
        if (!range) return;
        if (jumps.some((item) => item.startOffset === range.startOffset && item.endOffset === range.endOffset)) return;
        jumps.push(range);
    };
    if (found) {
        push(found.primary);
        push(found.inner);
    }
    const primary = jumps[0] || {
        startOffset: 0,
        endOffset: 0,
        startLine: 0,
        endLine: 0,
        label: '',
    };
    return {
        startOffset: primary.startOffset,
        endOffset: primary.endOffset,
        startLine: primary.startLine,
        endLine: primary.endLine,
        label: primary.label,
        photo: photo || '',
        jumps,
        inner: jumps[1] || null,
    };
}

function rangeOf(node) {
    if (!node || node.endOffset == null || node.endOffset <= node.startOffset) return null;
    return {
        startOffset: node.startOffset,
        endOffset: node.endOffset,
        startLine: node.startLine,
        endLine: node.endLine,
        label: describeNode(node),
        menuText: node.menuText || '',
    };
}

function describeNode(node) {
    if (!node) return 'source';
    if (node.kind === 'php') return node.label || '$variable';
    if (node.kind === 'paragraph') {
        const preview = node.text.length > 48 ? `${node.text.slice(0, 48).trim()}\u2026` : node.text;
        return preview ? `paragraph \u201C${preview}\u201D` : 'paragraph';
    }
    if (node.kind === 'comment') {
        if (/related-sales/i.test(node.raw || '')) return '<!-- related-sales -->';
        if (/page-values/i.test(node.raw || '')) return '<!-- page-values -->';
        const virt = (node.raw || '').match(/virtual\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+))/i);
        const path = (virt?.[1] || virt?.[2] || virt?.[3] || '').split('?')[0];
        return path ? `<!--#include virtual="${path}" -->` : '<!--#include -->';
    }
    const tag = node.tag || 'element';
    if ((tag === 'img' || tag === 'a') && (node.attrs.src || node.attrs.href)) {
        const attr = node.attrs.src ? 'src' : 'href';
        return `<${tag} ${attr}="${shortUrl(node.attrs.src || node.attrs.href)}">`;
    }
    const cls = identityClass(node.attrs.class);
    if (cls) return `<${tag} class="${cls}">`;
    if (node.attrs.id) return `<${tag} id="${node.attrs.id}">`;
    return `<${tag}>`;
}

function kickerFor(target) {
    const tag = tagName(target) || 'element';
    const id = target.getAttribute?.('id');
    if (id) return `Right-clicked <${tag} id="${id}">`;
    const cls = identityClass(classNameOf(target));
    if (cls) return `Right-clicked <${tag} class="${cls}">`;
    return `Right-clicked <${tag}>`;
}

function identityClass(className) {
    return tokens(className).filter((token) => !SKIP_CLASSES.has(token)).slice(0, 3).join(' ');
}

function hasClass(node, name) {
    return tokens(node?.attrs?.class).includes(name);
}

function htmlContainsUrl(html, url) {
    if (!html || !url) return false;
    const decoded = safeDecode(String(url));
    const bare = decoded.split('#')[0].split('?')[0];
    return html.includes(url) || html.includes(decoded) || (bare && html.includes(bare));
}

function photoOf(el) {
    if (!el) return '';
    const tag = tagName(el);
    const className = classNameOf(el);
    const box = tag === 'img' || tag === 'a' || tag === 'figure'
        || /\bthumbnail-wrapper\b/.test(className)
        || /\bhero-wrapper\b/.test(className);
    if (!box) return '';
    const values = [];
    if (tag === 'img') values.push(el.getAttribute?.('src'), el.src);
    if (tag === 'a') values.push(el.getAttribute?.('href'), el.href);
    const image = tag === 'img' ? el : el.querySelector?.('img');
    const link = tag === 'a' ? el : (el.querySelector?.('a') || (tag === 'img' ? el.closest?.('a') : null));
    if (image) values.push(image.getAttribute?.('src'), image.src);
    if (link) values.push(link.getAttribute?.('href'), link.href);
    for (const value of values) {
        if (isGalleryPhoto(value)) return fileBase(value);
    }
    return '';
}

function isGalleryPhoto(value) {
    const path = normUrl(value);
    return /(?:^|\/)(?:slides|thumbs)\/[^/]+\.(?:jpe?g|png|gif|webp)$/i.test(path);
}

function fileBase(value) {
    const path = safeDecode(String(value || '').split('#')[0].split('?')[0]);
    const parts = path.split(/[/\\]/);
    return parts[parts.length - 1] || '';
}

function shortUrl(value) {
    let text = safeDecode(String(value || '').trim());
    text = text.split('#')[0].split('?')[0];
    text = text.replace(/^[a-z][a-z0-9+.-]*:\/\/[^/]+/i, '');
    text = text.replace(/^\.\//, '');
    const slides = text.match(/(?:^|\/)((?:slides|thumbs)\/[^/]+)$/i);
    if (slides) return slides[1];
    return text || value;
}

function urlsMatch(left, right) {
    const a = normUrl(left);
    const b = normUrl(right);
    if (!a || !b) return false;
    if (a === b) return true;
    if (a.endsWith(`/${b}`) || b.endsWith(`/${a}`)) return true;
    if ((b.startsWith('slides/') || b.startsWith('thumbs/')) && a.endsWith(b)) return true;
    if ((a.startsWith('slides/') || a.startsWith('thumbs/')) && b.endsWith(a)) return true;
    return false;
}

function normUrl(value) {
    let text = String(value || '').trim();
    if (!text) return '';
    text = text.replace(/&amp;/gi, '&');
    text = safeDecode(text);
    text = text.split('#')[0].split('?')[0];
    text = text.replace(/^[a-z][a-z0-9+.-]*:\/\/[^/]+/i, '');
    text = text.replace(/^\.\//, '');
    return text.replace(/\/+/g, '/').toLowerCase();
}

function safeDecode(value) {
    try { return decodeURIComponent(String(value || '').replace(/\+/g, ' ')); } catch { return String(value || ''); }
}

function linePhrase(start, end) {
    if (!start) return 'the source';
    if (!end || start === end) return `Line ${start}`;
    return `Lines ${start}\u2013${end}`;
}

function lineNumber(source, offset) {
    let line = 1;
    const end = Math.max(0, Math.min(offset, source.length));
    for (let index = 0; index < end; index += 1) {
        if (source.charCodeAt(index) === 10) line += 1;
    }
    return line;
}

function endLineNumber(source, endOffset) {
    if (endOffset <= 0) return 1;
    return lineNumber(source, endOffset - 1);
}

function isPhpSource(fileType) {
    const text = String(fileType || '').toLowerCase();
    return text === 'php' || text.endsWith('.php');
}

function closest(el, selector) {
    if (!el || typeof el.closest !== 'function') return null;
    try { return el.closest(selector); } catch { return null; }
}

function tagName(el) {
    return String(el?.tagName || '').toLowerCase();
}

function classNameOf(el) {
    const value = el?.getAttribute?.('class');
    if (typeof value === 'string') return value;
    return typeof el?.className === 'string' ? el.className : '';
}

function tokens(value) {
    return String(value || '').split(/\s+/).filter(Boolean);
}

function tokenKey(value) {
    return tokens(value).sort().join(' ');
}

function normalizeText(text) {
    return decodeEntities(text)
        .replace(/\u00AD/g, '')
        .replace(/&shy;/gi, '')
        .replace(/\u00A0/g, ' ')
        .replace(/\s+/g, ' ')
        .trim();
}

function decodeEntities(text) {
    return String(text || '').replace(/&(#x[0-9a-f]+|#\d+|[a-z][a-z0-9]+);/gi, (all, body) => {
        if (body[0] === '#') {
            const hex = body[1] === 'x' || body[1] === 'X';
            const code = Number.parseInt(body.slice(hex ? 2 : 1), hex ? 16 : 10);
            if (!Number.isFinite(code) || code < 0 || code > 0x10FFFF) return all;
            try { return String.fromCodePoint(code); } catch { return all; }
        }
        const named = ENTITIES[body.toLowerCase()];
        return named == null ? all : named;
    });
}

function clamp(value, min, max) {
    return Math.max(min, Math.min(max, value));
}
