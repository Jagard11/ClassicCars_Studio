const VALID_TABS = new Set(['files', 'search', 'git']);

export function mountLeftShelf({
    panel,
    tabBar,
    panes,
    initialTab = 'files',
    onChange = null,
} = {}) {
    let activeTab = VALID_TABS.has(initialTab) ? initialTab : 'files';
    const tabs = tabBar ? [...tabBar.querySelectorAll('[data-shelf-tab]')] : [];
    const gitBadge = tabBar?.querySelector('#shelf-git-badge') || null;
    const searchBadge = tabBar?.querySelector('#shelf-search-badge') || null;

    function applyTab(next, { focusTab = false, silent = false } = {}) {
        const target = VALID_TABS.has(next) ? next : 'files';
        const changed = target !== activeTab;
        activeTab = target;
        if (panel) panel.dataset.shelfView = activeTab;
        for (const btn of tabs) {
            const on = btn.dataset.shelfTab === activeTab;
            btn.setAttribute('aria-selected', on ? 'true' : 'false');
            btn.tabIndex = on ? 0 : -1;
            btn.classList.toggle('is-active', on);
            if (on && focusTab) btn.focus();
        }
        if (panes) {
            for (const [id, node] of Object.entries(panes)) {
                if (!node) continue;
                const on = id === activeTab;
                node.hidden = !on;
                node.toggleAttribute('inert', !on);
            }
        }
        if (!silent && changed && typeof onChange === 'function') {
            onChange(activeTab);
        }
        return activeTab;
    }

    for (const btn of tabs) {
        btn.addEventListener('click', () => {
            applyTab(btn.dataset.shelfTab);
        });
        btn.addEventListener('keydown', (event) => {
            if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) return;
            event.preventDefault();
            const idx = tabs.indexOf(btn);
            if (idx < 0) return;
            let nextIdx = idx;
            if (event.key === 'ArrowRight') nextIdx = (idx + 1) % tabs.length;
            else if (event.key === 'ArrowLeft') nextIdx = (idx + tabs.length - 1) % tabs.length;
            else if (event.key === 'Home') nextIdx = 0;
            else if (event.key === 'End') nextIdx = tabs.length - 1;
            const target = tabs[nextIdx];
            if (target) applyTab(target.dataset.shelfTab, { focusTab: true });
        });
    }

    function setGitBadge(count) {
        if (!gitBadge) return;
        const n = Number(count) || 0;
        gitBadge.hidden = n <= 0;
        gitBadge.textContent = n > 99 ? '99+' : String(n);
        gitBadge.title = n === 1 ? '1 file with uncommitted Git changes' : `${n} files with uncommitted Git changes`;
    }

    function setSearchBadge(count) {
        if (!searchBadge) return;
        const n = Number(count) || 0;
        searchBadge.hidden = n <= 0;
        searchBadge.textContent = n > 999 ? '999+' : String(n);
        searchBadge.title = n === 1 ? '1 search match across project' : `${n} search matches across project`;
    }

    applyTab(activeTab, { silent: true });

    return {
        getTab: () => activeTab,
        setTab: (id, options) => applyTab(id, options),
        setGitBadge,
        setSearchBadge,
    };
}
