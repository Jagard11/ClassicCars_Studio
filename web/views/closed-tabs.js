const closedTabs = [];
let restoringPath = '';

export function rememberClosedTab(entry) {
    const path = String(entry?.path || '');
    if (!path || path === restoringPath) return;
    const index = entry?.index;
    closedTabs.push({
        path,
        index: Number.isInteger(index) && index >= 0 ? index : 0,
        isPreview: Boolean(entry?.isPreview),
        mode: entry?.mode === 'gallery' ? 'gallery' : 'code',
    });
}

export function takeClosedTab() {
    return closedTabs.pop() || null;
}

export function hasClosedTabs() {
    return closedTabs.length > 0;
}

export function beginClosedTabRestore(path) {
    restoringPath = String(path || '');
}

export function endClosedTabRestore() {
    restoringPath = '';
}
