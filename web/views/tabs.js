import { el } from './dom.js';

export function mountDocTabs(container, handlers) {
    const menu = el('div', { class: 'doc-tab-menu', role: 'menu', hidden: true });
    let menuPath = '';
    document.body.append(menu);

    container.addEventListener('click', (event) => {
        const tab = event.target.closest('[data-tab-path]');
        if (!tab || !container.contains(tab)) return;
        if (event.target.closest('.doc-tab-close')) {
            event.preventDefault();
            handlers.onClose(tab.dataset.tabPath);
            return;
        }
        handlers.onActivate(tab.dataset.tabPath);
    });
    container.addEventListener('dblclick', (event) => {
        const tab = event.target.closest('[data-tab-path]');
        if (!tab || !container.contains(tab) || event.target.closest('.doc-tab-close')) return;
        event.preventDefault();
        handlers.onPin(tab.dataset.tabPath);
    });
    container.addEventListener('auxclick', (event) => {
        if (event.button !== 1) return;
        const tab = event.target.closest('[data-tab-path]');
        if (!tab || !container.contains(tab)) return;
        event.preventDefault();
        handlers.onClose(tab.dataset.tabPath);
    });
    container.addEventListener('mousedown', (event) => {
        if (event.button === 1) event.preventDefault();
    });
    container.addEventListener('contextmenu', (event) => {
        const tab = event.target.closest('[data-tab-path]');
        if (!tab || !container.contains(tab)) return;
        event.preventDefault();
        openMenu(tab.dataset.tabPath, event.clientX, event.clientY);
    });
    container.addEventListener('keydown', (event) => {
        const current = event.target.closest?.('[data-tab-path]');
        if (!current || !container.contains(current)) return;
        const tabs = [...container.querySelectorAll('[data-tab-path]')];
        const index = tabs.indexOf(current);
        if (index < 0) return;
        if (event.key === 'ArrowRight' || event.key === 'ArrowLeft' || event.key === 'Home' || event.key === 'End') {
            event.preventDefault();
            let next = index;
            if (event.key === 'ArrowRight') next = (index + 1) % tabs.length;
            if (event.key === 'ArrowLeft') next = (index + tabs.length - 1) % tabs.length;
            if (event.key === 'Home') next = 0;
            if (event.key === 'End') next = tabs.length - 1;
            tabs[next].focus();
            handlers.onActivate(tabs[next].dataset.tabPath);
            return;
        }
        if (event.key === 'Delete') {
            event.preventDefault();
            handlers.onClose(current.dataset.tabPath);
            return;
        }
        if (event.key === 'Enter' && current.dataset.preview === 'true') {
            event.preventDefault();
            handlers.onPin(current.dataset.tabPath);
        }
    });
    document.addEventListener('pointerdown', (event) => {
        if (menu.hidden || menu.contains(event.target)) return;
        closeMenu();
    }, true);
    document.addEventListener('keydown', (event) => {
        if (event.key !== 'Escape' || menu.hidden) return;
        event.preventDefault();
        event.stopPropagation();
        closeMenu();
    }, true);

    let shown = '';

    function render(tabs) {
        const active = tabs.find((tab) => tab.active)?.path || '';
        const focusPath = container.contains(document.activeElement) ? document.activeElement.dataset?.tabPath : '';
        const scrollLeft = container.scrollLeft;
        const scrollTop = container.scrollTop;
        const frag = document.createDocumentFragment();
        for (const tab of tabs) frag.append(renderTab(tab));
        container.replaceChildren(frag);
        container.scrollLeft = scrollLeft;
        container.scrollTop = scrollTop;
        if (focusPath) findTab(container, focusPath)?.focus({ preventScroll: true });
        if (active !== shown) {
            shown = active;
            container.querySelector('[aria-selected="true"]')?.scrollIntoView({ block: 'nearest', inline: 'nearest' });
        }
    }

    function renderTab(tab) {
        const button = el('div', {
            class: 'doc-tab',
            role: 'tab',
            'data-tab-path': tab.path,
            'data-preview': tab.isPreview ? 'true' : 'false',
            title: `${tab.path || tab.title || ''} — Single-click to switch · Double-click to pin preview tab · Middle-click to close`,
            tabindex: tab.active ? '0' : '-1',
        });
        button.setAttribute('aria-selected', tab.active ? 'true' : 'false');
        const bits = [tab.label || tab.path];
        if (tab.isPreview) bits.push('preview');
        if (tab.unsaved) bits.push('unsaved');
        button.setAttribute('aria-label', bits.join(', '));
        if (tab.ext) {
            const kind = String(tab.ext).toUpperCase();
            const kindTip = kind === 'PHP' ? 'PHP page' : kind === 'HTML' ? 'HTML page' : kind === 'SHTML' ? 'SHTML page' : `${kind} file`;
            button.append(el('span', { class: 'doc-tab-badge', 'data-kind': tab.ext, text: tab.ext, title: kindTip }));
        }
        button.classList.add('file-tab');
        if (tab.unsaved) button.classList.add('is-unsaved');
        button.append(el('span', { class: 'doc-tab-label', text: tab.label || tab.path }));
        button.append(el('button', {
            class: 'doc-tab-close file-tab-close',
            type: 'button',
            title: 'Close tab (Ctrl+W)',
            'aria-label': 'Close tab',
        }, [
            el('span', {
                class: 'file-tab-dot',
                'aria-hidden': 'true',
                text: '●',
                title: tab.unsaved ? 'Has unsaved or uncommitted changes' : '',
            }),
            el('span', { class: 'file-tab-x', 'aria-hidden': 'true', text: '×' }),
        ]));
        return button;
    }

    function openMenu(path, x, y) {
        const tab = findTab(container, path);
        const preview = tab?.dataset.preview === 'true';
        const count = container.querySelectorAll('[data-tab-path]').length;
        menuPath = path;
        menu.replaceChildren(
            menuItem('Keep Open', 'keep', !preview),
            menuItem('Close', 'close', false),
            menuItem('Close Others', 'others', count < 2),
            menuItem('Close Saved', 'saved', false),
            menuItem('Close All', 'all', false),
        );
        menu.hidden = false;
        const width = menu.offsetWidth || 180;
        const height = menu.offsetHeight || 160;
        const left = Math.max(8, Math.min(x, window.innerWidth - width - 8));
        const top = Math.max(8, Math.min(y, window.innerHeight - height - 8));
        menu.style.left = `${left}px`;
        menu.style.top = `${top}px`;
    }

    function menuItem(label, action, disabled) {
        const tips = {
            keep: 'Pin this preview tab so the next file opens beside it',
            close: 'Close this editor tab (Ctrl+W)',
            others: 'Close every editor tab except this one',
            saved: 'Close editor tabs that have no unsaved changes',
            all: 'Close all open editor tabs',
        };
        const button = el('button', {
            class: 'doc-tab-menu-item',
            type: 'button',
            role: 'menuitem',
            text: label,
            title: tips[action] || label,
            disabled: disabled || undefined,
        });
        button.addEventListener('click', () => {
            const path = menuPath;
            closeMenu();
            if (action === 'keep') handlers.onPin(path);
            else if (action === 'close') handlers.onClose(path);
            else if (action === 'others') handlers.onCloseOthers(path);
            else if (action === 'saved') handlers.onCloseSaved();
            else if (action === 'all') handlers.onCloseAll();
        });
        return button;
    }

    function closeMenu() {
        menu.hidden = true;
        menuPath = '';
    }

    return { render, closeMenu };
}

function findTab(container, path) {
    return [...container.querySelectorAll('[data-tab-path]')].find((node) => node.dataset.tabPath === path) || null;
}
