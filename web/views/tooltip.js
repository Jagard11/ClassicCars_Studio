const DELAY = 280;
const MARGIN = 8;
const GAP = 10;
const CURSOR_X = 14;
const CURSOR_Y = 16;
const SCROLL_SUPPRESS_MS = 150;
const SHORTCUT = /\s*(\((?:[^()]*(?:Ctrl|Alt|Shift|Cmd|Meta|Esc|Enter|Space|Tab|F\d+|Arrow)[^()]*)\)|\[(?:[^[\]]*(?:Ctrl|Alt|Shift|Cmd|Meta|Esc|Enter|Space|Tab|F\d+|Arrow)[^[\]]*)\])\s*$/i;

let tip = null;
let timer = 0;
let anchor = null;
let mode = '';
let suppress = null;
let point = null;
let usePopover = false;
let removal = null;
let attrs = null;
let scrollUntil = 0;

export function mountTooltips() {
    if (tip || typeof document === 'undefined') return;
    tip = document.createElement('div');
    tip.id = 'studio-tooltip';
    tip.setAttribute('role', 'tooltip');
    tip.hidden = true;
    usePopover = typeof tip.showPopover === 'function';
    if (usePopover) tip.popover = 'manual';
    document.body.append(tip);

    document.addEventListener('pointerover', onPointerOver, true);
    document.addEventListener('pointerout', onPointerOut, true);
    document.addEventListener('pointermove', onPointerMove, true);
    document.addEventListener('focusin', onFocusIn, true);
    document.addEventListener('focusout', onFocusOut, true);
    document.addEventListener('pointerdown', () => dismiss(), true);
    document.addEventListener('wheel', noteScroll, { capture: true, passive: true });
    document.addEventListener('scroll', noteScroll, { capture: true, passive: true });
    document.addEventListener('keydown', () => dismiss(), true);
    window.addEventListener('blur', () => dismiss());
}

function scrolling() {
    return Date.now() < scrollUntil;
}

function noteScroll() {
    scrollUntil = Date.now() + SCROLL_SUPPRESS_MS;
    if (timer || anchor) dismiss();
}

function onPointerOver(event) {
    if (scrolling()) return;
    const next = hostFrom(event.target);
    if (!next || next === suppress) return;
    suppress = null;
    point = { x: event.clientX, y: event.clientY };
    schedule(next, 'pointer');
}

function onPointerOut(event) {
    if (scrolling()) return;
    if (mode === 'focus' && !timer && anchor) return;
    const next = hostFrom(event.relatedTarget);
    if (next && (next === anchor || next === suppress)) return;
    if (suppress && !containsNode(suppress, event.relatedTarget)) suppress = null;
    if (next) schedule(next, 'pointer');
    else if (mode === 'pointer' || timer) cancel();
}

function onPointerMove(event) {
    if (scrolling()) return;
    if (!anchor || mode !== 'pointer') return;
    point = { x: event.clientX, y: event.clientY };
    if (tip && !tip.hidden) place(anchor);
}

function onFocusIn(event) {
    const next = hostFrom(event.target);
    if (!next) return;
    window.requestAnimationFrame(() => {
        if (!next.isConnected || next === suppress) return;
        const focused = document.activeElement;
        if (focused !== next && !next.contains(focused)) return;
        const visible = next.matches(':focus-visible')
            || (focused instanceof Element && next.contains(focused) && focused.matches(':focus-visible'));
        if (!visible) return;
        suppress = null;
        point = null;
        schedule(next, 'focus');
    });
}

function onFocusOut(event) {
    if (mode !== 'focus' && !timer) return;
    const next = hostFrom(event.relatedTarget);
    if (next === anchor) return;
    if (suppress && !containsNode(suppress, event.relatedTarget)) suppress = null;
    if (next && focusVisible(next)) schedule(next, 'focus');
    else if (mode === 'focus') cancel();
}

function focusVisible(node) {
    return node.matches(':focus-visible')
        || (document.activeElement instanceof Element
            && node.contains(document.activeElement)
            && document.activeElement.matches(':focus-visible'));
}

function schedule(next, nextMode) {
    if (scrolling()) return;
    const text = claim(next);
    if (!text || !next.isConnected) {
        cancel();
        return;
    }
    if (next === anchor && mode === nextMode) {
        if (tip && !tip.hidden) place(next);
        return;
    }
    window.clearTimeout(timer);
    anchor = next;
    mode = nextMode;
    watch(next);
    timer = window.setTimeout(() => {
        timer = 0;
        if (anchor !== next || !next.isConnected) {
            hide();
            return;
        }
        const fresh = claim(next);
        if (!fresh) {
            hide();
            return;
        }
        render(fresh);
        reveal();
        place(next);
    }, DELAY);
}

function dismiss() {
    window.clearTimeout(timer);
    timer = 0;
    if (anchor) suppress = anchor;
    hide();
}

function cancel() {
    window.clearTimeout(timer);
    timer = 0;
    hide();
}

function hide() {
    anchor = null;
    mode = '';
    point = null;
    unwatch();
    conceal();
}

function hostFrom(node) {
    let el = node instanceof Element ? node : node?.parentElement || null;
    while (el && el !== document.documentElement) {
        if (el === tip) return null;
        const data = el.getAttribute('data-tooltip');
        const title = el.getAttribute('title');
        if ((data && data.trim()) || (title && title.trim())) return el;
        el = el.parentElement;
    }
    return null;
}

function readTip(el) {
    if (!el) return '';
    if (el.hasAttribute('title')) return el.getAttribute('title').trim();
    return (el.getAttribute('data-tooltip') || '').trim();
}

function claim(el) {
    if (!el || !el.hasAttribute('title')) return (el?.getAttribute('data-tooltip') || '').trim();
    if (scrolling()) return el.getAttribute('title').trim();
    const text = el.getAttribute('title').trim();
    el.removeAttribute('title');
    if (text) el.setAttribute('data-tooltip', text);
    else el.removeAttribute('data-tooltip');
    if (text && !el.hasAttribute('aria-label') && !el.hasAttribute('aria-labelledby')) {
        const visible = (el.textContent || '').replace(/\s+/g, ' ').trim();
        if (visible.length <= 1) el.setAttribute('aria-label', text);
    }
    return text;
}

function render(text) {
    tip.replaceChildren();
    const match = text.match(SHORTCUT);
    if (!match || match.index == null) {
        tip.textContent = text;
        return;
    }
    const body = text.slice(0, match.index).trimEnd();
    if (body) tip.append(document.createTextNode(body));
    const key = document.createElement('kbd');
    key.className = 'studio-tooltip-key';
    key.textContent = match[1];
    tip.append(key);
}

function reveal() {
    tip.hidden = false;
    if (!usePopover) return;
    try {
        if (!tip.matches(':popover-open')) tip.showPopover();
    } catch {
        usePopover = false;
    }
}

function conceal() {
    if (!tip) return;
    if (usePopover && tip.matches(':popover-open')) {
        try { tip.hidePopover(); } catch { usePopover = false; }
    }
    tip.hidden = true;
}

function place(el) {
    if (scrolling()) return;
    const box = tip.getBoundingClientRect();
    const rect = el.getBoundingClientRect();
    const vw = document.documentElement.clientWidth;
    const vh = document.documentElement.clientHeight;
    const atPointer = mode === 'pointer' && point;
    let left = atPointer ? point.x + CURSOR_X : rect.left;
    let top = atPointer ? point.y + CURSOR_Y : rect.bottom + GAP;
    if (left + box.width > vw - MARGIN) {
        left = atPointer ? point.x - box.width - CURSOR_X : vw - MARGIN - box.width;
    }
    if (left < MARGIN) left = MARGIN;
    if (top + box.height > vh - MARGIN) {
        const flipped = (atPointer ? point.y : rect.top) - GAP - box.height;
        top = flipped >= MARGIN ? flipped : Math.max(MARGIN, vh - MARGIN - box.height);
    }
    if (top < MARGIN) top = MARGIN;
    tip.style.left = `${Math.round(left)}px`;
    tip.style.top = `${Math.round(top)}px`;
}

function watch(node) {
    unwatch();
    attrs = new MutationObserver(() => {
        if (!node.isConnected) {
            hide();
            return;
        }
        if (!anchor || tip.hidden) return;
        const text = readTip(node);
        if (!text) hide();
        else render(text);
    });
    attrs.observe(node, { attributes: true, attributeFilter: ['title', 'data-tooltip'] });
    removal = new MutationObserver(() => {
        if (anchor && !anchor.isConnected) hide();
    });
    removal.observe(document.body, { childList: true, subtree: true });
}

function unwatch() {
    attrs?.disconnect();
    removal?.disconnect();
    attrs = null;
    removal = null;
}

function containsNode(el, node) {
    return node instanceof Node && el.contains(node);
}
