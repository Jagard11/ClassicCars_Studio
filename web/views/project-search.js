import { el } from './dom.js';

const MODE_INFO = {
    literal: {
        label: 'Literal',
        badge: 'ab',
        hint: 'Exact text match. Replacement text is inserted literally.',
        queryPlaceholder: 'Find text across project…',
        replacePlaceholder: 'Replace with…',
    },
    regex: {
        label: 'Regex',
        badge: '.*',
        hint: 'Regular expression (. * + ? ^ $ \\b \\d \\w [a-z] (groups)). Use $1–$9 or $& in Replace.',
        queryPlaceholder: 'Regex pattern (e.g. \\$price\\s*=\\s*"([^"]+)")',
        replacePlaceholder: 'Replace ($1, $2, $& for captures)…',
    },
    wildcard: {
        label: 'Wildcard',
        badge: '* ?',
        hint: 'Wildcard pattern: * matches any text and ? matches 1 character. Each * or ? is captured as $1, $2… for Replace!',
        queryPlaceholder: 'Wildcard pattern (e.g. <h1>*</h1> or href="*.html")',
        replacePlaceholder: 'Replace ($1, $2 for each * or ?)…',
    },
};

const SCOPES = [
    { id: 'all', label: 'All Text' },
    { id: 'html-attr', label: 'HTML Tag Attributes & Links' },
    { id: 'comments', label: 'HTML Comments & Page Values' },
    { id: 'php', label: 'PHP Code & Variables' },
];

export function mountProjectSearch(parent, {
    apiSend,
    confirmDialog,
    announce,
    onOpenMatch,
    onFilesModified,
    onMatchCountChange,
} = {}) {
    if (!parent) return { focus() {}, refresh() {}, setQuery() {} };

    let mode = 'literal';
    let caseSensitive = false;
    let wholeWord = false;
    let scope = 'all';
    let collapsedFiles = new Set();
    let lastResult = null;
    let searchTimer = 0;
    let searchAbort = null;
    let busy = false;

    const root = el('div', { class: 'ps-panel' });

    /* Search & Replace controls box */
    const formBox = el('div', { class: 'ps-controls' });

    /* Mode selector row */
    const modeRow = el('div', { class: 'ps-mode-row', role: 'group', 'aria-label': 'Search pattern mode' });
    const modeBtns = {};
    for (const [id, meta] of Object.entries(MODE_INFO)) {
        const btn = el('button', {
            type: 'button',
            class: `ps-mode-btn${id === mode ? ' is-active' : ''}`,
            'data-search-mode': id,
            'aria-pressed': id === mode ? 'true' : 'false',
            title: `${meta.label} mode (${meta.badge}): ${meta.hint}`,
        });
        btn.append(
            el('code', { class: 'ps-mode-code', text: meta.badge }),
            el('span', { text: meta.label }),
        );
        btn.addEventListener('click', () => {
            setMode(id);
            scheduleSearch(80);
        });
        modeBtns[id] = btn;
        modeRow.append(btn);
    }

    /* Find input row with Case & Whole Word toggles */
    const queryWrap = el('div', { class: 'ps-input-wrap' });
    const queryInput = el('input', {
        type: 'search',
        id: 'project-search-query',
        class: 'ps-input',
        placeholder: MODE_INFO.literal.queryPlaceholder,
        autocomplete: 'off',
        spellcheck: 'false',
        'aria-label': 'Find in project',
        title: 'Search text or pattern across all editable files in the project (Enter to search)',
    });
    const flagGroup = el('div', { class: 'ps-flags' });
    const caseBtn = el('button', {
        type: 'button',
        class: 'ps-flag-btn',
        'aria-pressed': 'false',
        text: 'Aa',
        title: 'Match Case (toggle case-sensitive matching)',
    });
    const wordBtn = el('button', {
        type: 'button',
        class: 'ps-flag-btn',
        'aria-pressed': 'false',
        text: 'ab|',
        title: 'Match Whole Word (require word boundaries around match)',
    });
    flagGroup.append(caseBtn, wordBtn);
    queryWrap.append(queryInput, flagGroup);

    /* Replace input row + Replace All button */
    const replaceWrap = el('div', { class: 'ps-replace-row' });
    const replaceInput = el('input', {
        type: 'text',
        id: 'project-search-replace',
        class: 'ps-input',
        placeholder: MODE_INFO.literal.replacePlaceholder,
        autocomplete: 'off',
        spellcheck: 'false',
        'aria-label': 'Replace with',
        title: 'Replacement text. In Regex or Wildcard mode, use $1..$9 for capture groups and $& for full match.',
    });
    const replaceAllBtn = el('button', {
        type: 'button',
        class: 'btn ps-replace-all-btn',
        text: 'Replace All',
        disabled: true,
        title: 'Replace all matching occurrences across the project',
    });
    replaceWrap.append(replaceInput, replaceAllBtn);

    /* Mode helper hint */
    const modeHint = el('p', { class: 'ps-mode-hint', text: MODE_INFO.literal.hint });

    /* Scope + Include/Exclude filters */
    const filtersDetails = el('details', { class: 'ps-filters', open: true });
    const filtersSummary = el('summary', {
        class: 'ps-filters-summary',
        text: 'Scope & File Filters',
        title: 'Filter search by syntax scope (HTML attributes, comments, PHP) or include/exclude file globs',
    });
    const filtersBody = el('div', { class: 'ps-filters-body' });

    const scopeLabel = el('label', { class: 'ps-filter-field' });
    scopeLabel.append(el('span', { class: 'ps-filter-kicker', text: 'Scope' }));
    const scopeSelect = el('select', {
        id: 'project-search-scope',
        class: 'ps-select',
        'aria-label': 'Search scope',
        title: 'Limit matches to a specific part of the code (All Text, HTML Attributes, Comments/Page-Values, or PHP)',
    });
    for (const item of SCOPES) {
        scopeSelect.append(el('option', { value: item.id, text: item.label }));
    }
    scopeLabel.append(scopeSelect);

    const includeLabel = el('label', { class: 'ps-filter-field' });
    includeLabel.append(el('span', { class: 'ps-filter-kicker', text: 'Files to include' }));
    const includeInput = el('input', {
        type: 'text',
        id: 'project-search-include',
        class: 'ps-input ps-input-sm',
        placeholder: 'e.g. sales/**, articles/**, *.php',
        autocomplete: 'off',
        spellcheck: 'false',
        title: 'Comma-separated file or folder globs to include (leave blank for all project files)',
    });
    includeLabel.append(includeInput);

    const excludeLabel = el('label', { class: 'ps-filter-field' });
    excludeLabel.append(el('span', { class: 'ps-filter-kicker', text: 'Files to exclude' }));
    const excludeInput = el('input', {
        type: 'text',
        id: 'project-search-exclude',
        class: 'ps-input ps-input-sm',
        placeholder: 'e.g. newsletter/**, *.json',
        autocomplete: 'off',
        spellcheck: 'false',
        title: 'Comma-separated file or folder globs to exclude from search',
    });
    excludeLabel.append(excludeInput);

    filtersBody.append(scopeLabel, includeLabel, excludeLabel);
    filtersDetails.append(filtersSummary, filtersBody);

    formBox.append(modeRow, queryWrap, replaceWrap, modeHint, filtersDetails);

    /* Summary bar */
    const summaryBar = el('div', { class: 'ps-summary-bar' });
    const summaryText = el('span', { class: 'ps-summary-text', text: 'Type a query to search across the project.' });
    const summaryActions = el('div', { class: 'ps-summary-actions' });
    const toggleAllBtn = el('button', {
        type: 'button',
        class: 'ps-mini-btn',
        text: 'Collapse All',
        hidden: true,
        title: 'Collapse or expand all file result groups',
    });
    const refreshBtn = el('button', {
        type: 'button',
        class: 'ps-mini-btn',
        text: '↻',
        title: 'Re-run project search',
        'aria-label': 'Refresh search results',
    });
    summaryActions.append(toggleAllBtn, refreshBtn);
    summaryBar.append(summaryText, summaryActions);

    /* Results container */
    const resultsList = el('div', { class: 'ps-results', role: 'region', 'aria-label': 'Search results' });

    root.append(formBox, summaryBar, resultsList);
    parent.append(root);

    function setMode(next) {
        if (!MODE_INFO[next]) return;
        mode = next;
        const info = MODE_INFO[mode];
        for (const [id, btn] of Object.entries(modeBtns)) {
            const on = id === mode;
            btn.classList.toggle('is-active', on);
            btn.setAttribute('aria-pressed', on ? 'true' : 'false');
        }
        queryInput.placeholder = info.queryPlaceholder;
        replaceInput.placeholder = info.replacePlaceholder;
        modeHint.textContent = info.hint;
    }

    caseBtn.addEventListener('click', () => {
        caseSensitive = !caseSensitive;
        caseBtn.setAttribute('aria-pressed', caseSensitive ? 'true' : 'false');
        caseBtn.classList.toggle('is-active', caseSensitive);
        scheduleSearch(80);
    });

    wordBtn.addEventListener('click', () => {
        wholeWord = !wholeWord;
        wordBtn.setAttribute('aria-pressed', wholeWord ? 'true' : 'false');
        wordBtn.classList.toggle('is-active', wholeWord);
        scheduleSearch(80);
    });

    scopeSelect.addEventListener('change', () => {
        scope = scopeSelect.value || 'all';
        scheduleSearch(80);
    });

    queryInput.addEventListener('input', () => scheduleSearch(200));
    queryInput.addEventListener('keydown', (event) => {
        if (event.key === 'Enter') {
            event.preventDefault();
            void runSearch();
        }
    });

    replaceInput.addEventListener('input', () => {
        if (queryInput.value) scheduleSearch(200);
    });
    replaceInput.addEventListener('keydown', (event) => {
        if (event.key === 'Enter' && (event.ctrlKey || event.metaKey)) {
            event.preventDefault();
            void confirmAndReplaceAll();
        }
    });

    includeInput.addEventListener('input', () => scheduleSearch(260));
    excludeInput.addEventListener('input', () => scheduleSearch(260));
    refreshBtn.addEventListener('click', () => { void runSearch(); });
    replaceAllBtn.addEventListener('click', () => { void confirmAndReplaceAll(); });

    toggleAllBtn.addEventListener('click', () => {
        const files = lastResult?.files || [];
        if (!files.length) return;
        const allCollapsed = files.every((f) => collapsedFiles.has(f.path));
        if (allCollapsed) collapsedFiles.clear();
        else for (const f of files) collapsedFiles.add(f.path);
        renderResults();
    });

    function currentParams() {
        return {
            query: queryInput.value,
            replacement: replaceInput.value,
            mode,
            caseSensitive,
            wholeWord,
            scope,
            include: includeInput.value.trim(),
            exclude: excludeInput.value.trim(),
        };
    }

    function scheduleSearch(delay = 200) {
        window.clearTimeout(searchTimer);
        if (!queryInput.value) {
            clearResults();
            return;
        }
        searchTimer = window.setTimeout(() => { void runSearch(); }, delay);
    }

    function clearResults() {
        if (searchAbort) {
            searchAbort.abort();
            searchAbort = null;
        }
        lastResult = null;
        replaceAllBtn.disabled = true;
        toggleAllBtn.hidden = true;
        summaryText.textContent = 'Type a query to search across the project.';
        resultsList.replaceChildren();
        onMatchCountChange?.(0);
    }

    async function runSearch() {
        window.clearTimeout(searchTimer);
        const params = currentParams();
        if (!params.query) {
            clearResults();
            return;
        }
        if (searchAbort) searchAbort.abort();
        const controller = new AbortController();
        searchAbort = controller;
        summaryText.textContent = 'Searching project…';
        try {
            const data = await apiSend('POST', '/__studio/api/search', params, controller.signal);
            if (searchAbort !== controller) return;
            lastResult = data;
            onMatchCountChange?.(Number(data?.totalMatches) || 0);
            renderResults();
        } catch (error) {
            if (error.name === 'AbortError' || searchAbort !== controller) return;
            lastResult = null;
            replaceAllBtn.disabled = true;
            toggleAllBtn.hidden = true;
            summaryText.textContent = error.message || 'Search failed.';
            resultsList.replaceChildren(
                el('p', { class: 'ps-empty ps-error', text: error.message || 'Search failed.' }),
            );
            onMatchCountChange?.(0);
        }
    }

    function renderResults() {
        resultsList.replaceChildren();
        if (!lastResult) return;
        const files = Array.isArray(lastResult.files) ? lastResult.files : [];
        const totalMatches = Number(lastResult.totalMatches) || 0;
        const totalFiles = Number(lastResult.totalFiles) || files.length;
        const hasReplaceText = replaceInput.value.length > 0;

        replaceAllBtn.disabled = busy || totalMatches === 0;
        toggleAllBtn.hidden = files.length === 0;

        if (totalMatches === 0) {
            summaryText.textContent = 'No matches found.';
            resultsList.append(el('p', { class: 'ps-empty', text: 'No matching text found in the selected scope and file filters.' }));
            return;
        }

        const truncNote = lastResult.truncated ? ' (first 2,000 shown)' : '';
        summaryText.textContent = `${totalMatches.toLocaleString()} ${totalMatches === 1 ? 'match' : 'matches'} in ${totalFiles.toLocaleString()} ${totalFiles === 1 ? 'file' : 'files'}${truncNote}`;
        const allCollapsed = files.every((f) => collapsedFiles.has(f.path));
        toggleAllBtn.textContent = allCollapsed ? 'Expand All' : 'Collapse All';

        for (const file of files) {
            const collapsed = collapsedFiles.has(file.path);
            const group = el('section', {
                class: `ps-file-group${collapsed ? ' is-collapsed' : ''}`,
                'data-file-path': file.path,
            });

            const head = el('div', { class: 'ps-file-head' });
            const toggleBtn = el('button', {
                type: 'button',
                class: 'ps-file-toggle',
                'aria-expanded': collapsed ? 'false' : 'true',
                title: `${file.path} — ${file.matchCount} ${file.matchCount === 1 ? 'match' : 'matches'}`,
            });
            const dirLabel = [file.section, file.slug].filter(Boolean).join('/');
            toggleBtn.append(
                el('span', { class: 'ps-twist', 'aria-hidden': 'true', text: collapsed ? '▸' : '▾' }),
                el('span', { class: 'ps-file-name', text: file.name || file.path }),
                dirLabel ? el('span', { class: 'ps-file-dir', text: dirLabel }) : null,
            );
            toggleBtn.addEventListener('click', () => {
                if (collapsedFiles.has(file.path)) collapsedFiles.delete(file.path);
                else collapsedFiles.add(file.path);
                renderResults();
            });

            const fileBadge = el('span', {
                class: 'ps-file-count',
                text: String(file.matchCount || 0),
                title: `${file.matchCount} ${file.matchCount === 1 ? 'match' : 'matches'} in ${file.path}`,
            });

            const replaceFileBtn = el('button', {
                type: 'button',
                class: 'ps-action-btn',
                text: '⇄',
                disabled: busy,
                title: `Replace all ${file.matchCount} ${file.matchCount === 1 ? 'match' : 'matches'} in ${file.path}`,
                'aria-label': `Replace all in ${file.name || file.path}`,
            });
            replaceFileBtn.addEventListener('click', (event) => {
                event.stopPropagation();
                void executeReplace({ path: file.path });
            });

            head.append(toggleBtn, fileBadge, replaceFileBtn);
            group.append(head);

            if (!collapsed) {
                const matchList = el('div', { class: 'ps-match-list' });
                for (const m of file.matches || []) {
                    const row = el('div', { class: 'ps-match-row' });
                    const openBtn = el('button', {
                        type: 'button',
                        class: 'ps-match-main',
                        title: `${file.path}:${m.line}:${m.column} — Click to open in editor and jump to match`,
                    });
                    const lineNo = el('span', { class: 'ps-line-num', text: String(m.line) });
                    const snippet = buildSnippetNode(m, hasReplaceText);
                    openBtn.append(lineNo, snippet);
                    openBtn.addEventListener('click', () => {
                        onOpenMatch?.(file.path, m);
                    });

                    const replaceOneBtn = el('button', {
                        type: 'button',
                        class: 'ps-action-btn ps-match-replace-btn',
                        text: '↵',
                        disabled: busy,
                        title: `Replace this match on line ${m.line}`,
                        'aria-label': `Replace match on line ${m.line}`,
                    });
                    replaceOneBtn.addEventListener('click', (event) => {
                        event.stopPropagation();
                        void executeReplace({
                            path: file.path,
                            startOffset: m.startOffset,
                            matchIndex: m.indexInFile,
                        });
                    });

                    row.append(openBtn, replaceOneBtn);
                    matchList.append(row);
                }
                group.append(matchList);
            }

            resultsList.append(group);
        }
    }

    function buildSnippetNode(match, showPreview) {
        const lineText = String(match.lineText || '');
        const s = Math.max(0, Math.min(lineText.length, Number(match.lineStartCol) || 0));
        const e = Math.max(s, Math.min(lineText.length, Number(match.lineEndCol) || s));
        const before = lineText.slice(0, s).replace(/^\s+/, '');
        const hit = lineText.slice(s, e) || String(match.matchText || '');
        const after = lineText.slice(e);
        const wrap = el('span', { class: 'ps-snippet' });
        if (before) wrap.append(document.createTextNode(before));
        if (showPreview && typeof match.replacePreview === 'string' && match.replacePreview !== hit) {
            wrap.append(
                el('del', { class: 'ps-hit-del', text: hit }),
                el('ins', { class: 'ps-hit-ins', text: match.replacePreview || '∅' }),
            );
        } else {
            wrap.append(el('mark', { class: 'ps-hit', text: hit }));
        }
        if (after) wrap.append(document.createTextNode(after));
        return wrap;
    }

    async function confirmAndReplaceAll() {
        if (!lastResult || !lastResult.totalMatches || busy) return;
        const count = Number(lastResult.totalMatches) || 0;
        const filesCount = Number(lastResult.totalFiles) || 0;
        const repl = replaceInput.value;
        const ok = await confirmDialog(
            'Replace across project?',
            `Replace ${count.toLocaleString()} ${count === 1 ? 'occurrence' : 'occurrences'} across ${filesCount.toLocaleString()} ${filesCount === 1 ? 'file' : 'files'} with ${repl ? `"${repl}"` : 'empty text'}?`,
            `Replace ${count.toLocaleString()}`,
        );
        if (!ok) return;
        await executeReplace({});
    }

    async function executeReplace(extra = {}) {
        if (busy) return;
        busy = true;
        replaceAllBtn.disabled = true;
        try {
            const payload = {
                ...currentParams(),
                ...extra,
            };
            const res = await apiSend('POST', '/__studio/api/search/replace', payload);
            const replacedMatches = Number(res?.replacedMatches) || 0;
            const replacedFiles = Number(res?.replacedFiles) || 0;
            const modifiedPaths = Array.isArray(res?.modifiedPaths) ? res.modifiedPaths : [];
            announce?.(
                `Replaced ${replacedMatches} ${replacedMatches === 1 ? 'match' : 'matches'} in ${replacedFiles} ${replacedFiles === 1 ? 'file' : 'files'}.`,
            );
            if (modifiedPaths.length && typeof onFilesModified === 'function') {
                await onFilesModified(modifiedPaths);
            }
            await runSearch();
        } catch (error) {
            announce?.(error.message || 'Replace failed.');
        } finally {
            busy = false;
            replaceAllBtn.disabled = !lastResult || !lastResult.totalMatches;
        }
    }

    return {
        focus(selectText = true) {
            queryInput.focus();
            if (selectText) queryInput.select();
        },
        refresh() {
            if (queryInput.value) void runSearch();
        },
        setQuery(text) {
            if (typeof text === 'string' && text) {
                queryInput.value = text;
                void runSearch();
            }
        },
    };
}
