import { el } from './dom.js';

const WIDGET_ID = /^[A-Za-z][A-Za-z0-9_-]{0,63}$/;
const DRAG_SLOP = 4;
const SCROLL_EDGE = 36;
const SCROLL_STEP = 14;
const HANDLE_TITLE = 'Drag to reorder widget (or Alt+↑ / Alt+↓)';

export function sanitizeWidgetOrder(list) {
    const out = [];
    const seen = new Set();
    for (const item of Array.isArray(list) ? list : []) {
        if (typeof item !== 'string' || !WIDGET_ID.test(item) || seen.has(item)) continue;
        seen.add(item);
        out.push(item);
        if (out.length >= 64) break;
    }
    return out;
}

export function createWidgetOrder(root, { initialOrder, onChange } = {}) {
    let stored = sanitizeWidgetOrder(initialOrder);
    let drag = null;
    let native = null;
    bindNativeDrop();

    function ids() {
        return stored.slice();
    }

    function setOrder(list) {
        stored = sanitizeWidgetOrder(list);
    }

    function cards() {
        return [...root.querySelectorAll(':scope > .widget-card')];
    }

    function visibleIds() {
        return cards().map((card) => card.dataset.widget).filter((id) => WIDGET_ID.test(id));
    }

    function publish() {
        const visible = visibleIds();
        const next = stored.length ? weaveOrder(stored, visible) : visible.slice();
        if (sameOrder(next, stored)) return;
        stored = next;
        onChange?.();
    }

    function arrange() {
        if (!stored.length) return;
        const list = cards();
        const byId = new Map();
        for (const card of list) {
            const id = card.dataset.widget;
            if (id && !byId.has(id)) byId.set(id, card);
        }
        const sorted = [];
        const seen = new Set();
        for (const id of stored) {
            const card = byId.get(id);
            if (!card || seen.has(id)) continue;
            sorted.push(card);
            seen.add(id);
        }
        const added = [];
        for (const card of list) {
            const id = card.dataset.widget;
            if (!id || seen.has(id)) continue;
            sorted.push(card);
            seen.add(id);
            added.push(id);
        }
        for (const card of sorted) root.append(card);
        if (!added.length) return;
        stored = stored.concat(added);
        onChange?.();
    }

    function handle(title) {
        const button = el('button', {
            class: 'widget-drag-handle',
            type: 'button',
            draggable: 'false',
            title: HANDLE_TITLE,
            'aria-label': `Drag to reorder ${title}`,
            'aria-keyshortcuts': 'Alt+ArrowUp Alt+ArrowDown',
        });
        button.append(el('span', { 'aria-hidden': 'true', text: '⋮⋮' }));
        button.addEventListener('pointerdown', (event) => beginDrag(button, event));
        button.addEventListener('dragstart', (event) => beginNativeDrag(button, event));
        button.addEventListener('dragend', () => finishNative(true));
        button.addEventListener('keydown', onKey);
        return button;
    }

    function bindNativeDrop() {
        root.addEventListener('dragover', onNativeOver);
        root.addEventListener('drop', onNativeDrop);
        const port = scroller();
        if (port && port !== root) {
            port.addEventListener('dragover', onNativeOver);
            port.addEventListener('drop', onNativeDrop);
        }
    }

    function beginNativeDrag(button, event) {
        if (drag?.active) {
            event.preventDefault();
            return;
        }
        const card = button.closest('.widget-card');
        if (!card || !event.dataTransfer) return;
        native = {
            card,
            handle: button,
            before: undefined,
            speed: 0,
            frame: 0,
            scroller: scroller(),
        };
        event.dataTransfer.effectAllowed = 'move';
        event.dataTransfer.setData('text/plain', card.dataset.widget || 'widget');
        card.classList.add('is-dragging');
        button.classList.add('is-grabbing');
        document.documentElement.classList.add('is-widget-dragging');
    }

    function onNativeOver(event) {
        if (!native) return;
        event.preventDefault();
        if (event.dataTransfer) event.dataTransfer.dropEffect = 'move';
        native.before = dropBefore(native.card, event.clientY);
        markDrop(native.card, native.before);
        scrollFor(native, event.clientY, () => {
            if (!native) return;
            native.before = dropBefore(native.card, native.lastY);
            markDrop(native.card, native.before);
        });
        native.lastY = event.clientY;
    }

    function onNativeDrop(event) {
        if (!native) return;
        event.preventDefault();
        native.before = dropBefore(native.card, event.clientY);
        finishNative(true);
    }

    function onKey(event) {
        if (event.key !== 'ArrowUp' && event.key !== 'ArrowDown') return;
        if (!event.altKey && document.activeElement !== event.currentTarget) return;
        event.preventDefault();
        event.stopPropagation();
        const card = event.currentTarget.closest('.widget-card');
        if (!card) return;
        moveBy(card, event.key === 'ArrowUp' ? -1 : 1);
    }

    function moveBy(card, delta) {
        const list = cards();
        const index = list.indexOf(card);
        const next = index + delta;
        if (index < 0 || next < 0 || next >= list.length) return;
        const sibling = list[next];
        if (delta < 0) root.insertBefore(card, sibling);
        else root.insertBefore(card, sibling.nextSibling);
        publish();
        const grip = card.querySelector('.widget-drag-handle');
        grip?.focus({ preventScroll: true });
        card.scrollIntoView({ block: 'nearest' });
    }

    function scroller() {
        return root.closest('.widget-scroll') || root.parentElement;
    }

    function beginDrag(button, event) {
        if (event.button !== 0 || drag) return;
        const card = button.closest('.widget-card');
        if (!card) return;
        event.preventDefault();
        event.stopPropagation();
        drag = {
            handle: button,
            card,
            pointerId: event.pointerId,
            startX: event.clientX,
            startY: event.clientY,
            lastY: event.clientY,
            active: false,
            before: undefined,
            speed: 0,
            frame: 0,
            scroller: scroller(),
        };
        try { button.setPointerCapture(event.pointerId); } catch { /* pointer already gone */ }
        button.focus({ preventScroll: true });
        window.addEventListener('pointermove', onDragMove);
        window.addEventListener('pointerup', onDragEnd);
        window.addEventListener('pointercancel', onDragEnd);
    }

    function onDragMove(event) {
        if (!drag || event.pointerId !== drag.pointerId) return;
        event.preventDefault();
        drag.lastY = event.clientY;
        if (!drag.active) {
            if (Math.hypot(event.clientX - drag.startX, event.clientY - drag.startY) < DRAG_SLOP) return;
            drag.active = true;
            drag.card.classList.add('is-dragging');
            drag.handle.classList.add('is-grabbing');
            document.documentElement.classList.add('is-widget-dragging');
        }
        drag.before = dropBefore(drag.card, event.clientY);
        markDrop(drag.card, drag.before);
        scrollFor(drag, event.clientY, () => {
            if (!drag) return;
            drag.before = dropBefore(drag.card, drag.lastY);
            markDrop(drag.card, drag.before);
        });
    }

    function onDragEnd(event) {
        if (!drag || event.pointerId !== drag.pointerId) return;
        finishDrag(true);
    }

    function dropBefore(card, clientY) {
        const list = cards().filter((item) => item !== card);
        for (const item of list) {
            const rect = item.getBoundingClientRect();
            if (clientY < rect.top + rect.height / 2) return item;
        }
        return list.length ? null : undefined;
    }

    function markDrop(card, before) {
        clearMarks();
        if (before) before.classList.add('widget-drop-before');
        else if (before === null) {
            const list = cards().filter((item) => item !== card);
            list[list.length - 1]?.classList.add('widget-drop-after');
        }
    }

    function scrollFor(state, clientY, onTick) {
        const box = state.scroller;
        state.lastY = clientY;
        if (!box) {
            state.speed = 0;
            return;
        }
        const rect = box.getBoundingClientRect();
        let speed = 0;
        if (clientY < rect.top + SCROLL_EDGE) {
            const span = Math.max(1, rect.top + SCROLL_EDGE - clientY);
            speed = -Math.max(1, Math.ceil((SCROLL_STEP * span) / SCROLL_EDGE));
        } else if (clientY > rect.bottom - SCROLL_EDGE) {
            const span = Math.max(1, clientY - (rect.bottom - SCROLL_EDGE));
            speed = Math.max(1, Math.ceil((SCROLL_STEP * span) / SCROLL_EDGE));
        }
        state.speed = speed;
        if (!speed || state.frame) return;
        const tick = () => {
            if (!state.speed || (state !== drag && state !== native)) {
                state.frame = 0;
                return;
            }
            state.scroller.scrollTop += state.speed;
            onTick();
            state.frame = requestAnimationFrame(tick);
        };
        state.frame = requestAnimationFrame(tick);
    }

    function clearMarks() {
        for (const node of root.querySelectorAll('.widget-drop-before, .widget-drop-after')) {
            node.classList.remove('widget-drop-before', 'widget-drop-after');
        }
    }

    function finishDrag(commit) {
        if (!drag) return;
        const state = drag;
        drag = null;
        if (state.frame) cancelAnimationFrame(state.frame);
        window.removeEventListener('pointermove', onDragMove);
        window.removeEventListener('pointerup', onDragEnd);
        window.removeEventListener('pointercancel', onDragEnd);
        clearMarks();
        state.card.classList.remove('is-dragging');
        state.handle.classList.remove('is-grabbing');
        document.documentElement.classList.remove('is-widget-dragging');
        try { state.handle.releasePointerCapture(state.pointerId); } catch { /* not captured */ }
        if (!commit || !state.active) return;
        moveTo(state.card, state.before);
        if (state.handle.isConnected) state.handle.focus({ preventScroll: true });
    }

    function finishNative(commit) {
        if (!native) return;
        const state = native;
        native = null;
        if (state.frame) cancelAnimationFrame(state.frame);
        clearMarks();
        state.card.classList.remove('is-dragging');
        state.handle.classList.remove('is-grabbing');
        if (!drag) document.documentElement.classList.remove('is-widget-dragging');
        if (!commit) return;
        moveTo(state.card, state.before);
        if (state.handle.isConnected) state.handle.focus({ preventScroll: true });
    }

    function moveTo(card, before) {
        if (before) root.insertBefore(card, before);
        else if (before === null) root.append(card);
        else return;
        publish();
    }

    return {
        ids,
        setOrder,
        arrange,
        handle,
        cancelDrag() {
            finishDrag(false);
            finishNative(false);
        },
    };
}

function weaveOrder(full, visible) {
    const slot = new Set(visible);
    const queue = visible.slice();
    const next = [];
    const seen = new Set();
    for (const id of full) {
        if (!WIDGET_ID.test(id) || seen.has(id)) continue;
        if (slot.has(id)) {
            let pick = '';
            while (queue.length) {
                pick = queue.shift();
                if (pick && !seen.has(pick)) break;
                pick = '';
            }
            if (!pick) continue;
            next.push(pick);
            seen.add(pick);
        } else {
            next.push(id);
            seen.add(id);
        }
    }
    for (const id of queue) {
        if (!seen.has(id) && WIDGET_ID.test(id)) {
            next.push(id);
            seen.add(id);
        }
    }
    return next;
}

function sameOrder(left, right) {
    if (left.length !== right.length) return false;
    for (let i = 0; i < left.length; i += 1) if (left[i] !== right[i]) return false;
    return true;
}
