import { el } from './dom.js';

const HISTORY_KEY = 'studio.terminalHistory';
const CWD_KEY = 'studio.terminalCwd';
const MAX_HISTORY = 100;
const MAX_ENTRIES = 120;
const POLL_INTERVAL_MS = 100;
const COMMAND_TIMEOUT_MS = 300000;

const COMMON_COMMANDS = [
    'git status',
    'git diff',
    'git log --oneline -n 10',
    'git branch',
    'git push',
    'ls -la',
    'pwd',
    'clear',
    'help',
];

export function mountTerminal(panel, {
    apiSend,
    getRepoRoot,
    getKnownPaths,
    onOpenFile,
    onClose,
    onCommandComplete,
    announce,
} = {}) {
    if (!panel) {
        return {
            focus() {},
            clear() {},
            resetCwd() {},
            syncRepoRoot() {},
            refreshInfo: async () => {},
            runCommand: async () => {},
            getCwd: () => '',
            containsFocus: () => false,
            hasSelection: () => false,
            copySelection: () => false,
            selectAll: () => {},
        };
    }

    let cwd = '';
    let repoRoot = '';
    let shellName = 'sh';
    let userHost = '';
    let busy = false;
    let activeJobId = 0;
    let history = loadHistory();
    let historyIndex = history.length;
    let draftInput = '';
    let infoLoaded = false;
    let cachedSelection = '';
    let copyFeedbackTimer = 0;

    try {
        cwd = localStorage.getItem(CWD_KEY) || '';
    } catch {
        cwd = '';
    }

    const shell = el('div', { class: 'term-shell' });

    /* Header Bar */
    const header = el('div', { class: 'term-header' });
    const headLeft = el('div', { class: 'term-head-left' });
    const titleGroup = el('div', { class: 'term-title-group' });
    const icon = el('span', { class: 'term-icon', 'aria-hidden': 'true', text: '>_' });
    const title = el('span', { class: 'term-title', text: 'Terminal' });
    titleGroup.append(icon, title);

    const shellPill = el('span', {
        class: 'term-shell-pill',
        text: 'bash',
        title: 'Active command shell',
    });

    const cwdBadge = el('span', {
        class: 'term-cwd-badge',
        id: 'terminal-cwd-badge',
        text: '.',
        title: 'Current working directory',
    });

    headLeft.append(titleGroup, shellPill, cwdBadge);

    const headActions = el('div', { class: 'term-head-actions' });
    const copyBtn = el('button', {
        type: 'button',
        class: 'term-action-btn',
        id: 'terminal-copy-btn',
        text: 'Copy',
        title: 'Copy selected terminal text or full output log (Ctrl+C / Ctrl+Shift+C)',
    });
    const resetCwdBtn = el('button', {
        type: 'button',
        class: 'term-action-btn',
        id: 'terminal-reset-cwd',
        text: '⌂ Root',
        title: 'Reset working directory to Project Root',
    });
    const clearBtn = el('button', {
        type: 'button',
        class: 'term-action-btn',
        id: 'terminal-clear-btn',
        text: 'Clear',
        title: 'Clear terminal output (Ctrl+L)',
    });
    const closeBtn = el('button', {
        type: 'button',
        class: 'term-action-btn term-close-btn',
        id: 'terminal-close-btn',
        text: '×',
        'aria-label': 'Close terminal',
        title: 'Hide Terminal panel (Ctrl+`)',
    });
    headActions.append(copyBtn, resetCwdBtn, clearBtn, closeBtn);
    header.append(headLeft, headActions);

    /* Scrollable Output Log (tabindex="-1" so selecting text focuses outputLog rather than the empty input) */
    const outputLog = el('div', {
        class: 'term-output',
        id: 'terminal-output',
        role: 'log',
        tabindex: '-1',
        'aria-live': 'polite',
        'aria-label': 'Terminal output',
    });

    /* Prompt Input Bar */
    const inputForm = el('form', {
        class: 'term-input-form',
        id: 'terminal-form',
        autocomplete: 'off',
    });
    const promptPrefix = el('label', {
        class: 'term-input-prefix',
        for: 'terminal-input',
        title: 'Current working directory prompt',
    });
    const promptPrefixCwd = el('span', { class: 'term-input-prefix-cwd', text: '.' });
    const promptPrefixSym = el('span', { class: 'term-prompt-dollar', text: '$' });
    promptPrefix.append(promptPrefixCwd, promptPrefixSym);

    const input = el('input', {
        id: 'terminal-input',
        class: 'term-input',
        type: 'text',
        spellcheck: 'false',
        autocomplete: 'off',
        autocapitalize: 'off',
        placeholder: 'Type a command and press Enter (Ctrl+C copy selection, Ctrl+L clear, ↑/↓ history)…',
        'aria-label': 'Terminal command input',
    });

    const runBtn = el('button', {
        type: 'submit',
        class: 'term-action-btn',
        id: 'terminal-run-btn',
        text: 'Run ↵',
        title: 'Execute command (Enter)',
    });

    inputForm.append(promptPrefix, input, runBtn);
    shell.append(header, outputLog, inputForm);
    panel.replaceChildren(shell);

    /* Right-click Context Menu for Terminal Output */
    const ctxMenu = el('div', {
        class: 'term-context-menu',
        role: 'menu',
        hidden: true,
        'aria-label': 'Terminal context menu',
    });
    document.body.append(ctxMenu);

    renderWelcome();
    updateCwdUi();

    /* Track selection inside terminal so clicking Copy or Menu -> Copy never loses it */
    document.addEventListener('selectionchange', () => {
        const live = getLivePanelSelection();
        if (live) {
            cachedSelection = live;
        }
    });

    /* Prevent mousedown on Copy button from collapsing the active DOM selection */
    copyBtn.addEventListener('mousedown', (event) => {
        event.preventDefault();
    });

    copyBtn.addEventListener('click', () => {
        copySelectionOrAll();
    });

    clearBtn.addEventListener('click', () => {
        clear();
        focus();
    });

    resetCwdBtn.addEventListener('click', () => {
        resetCwd();
        focus();
    });

    closeBtn.addEventListener('click', () => {
        onClose?.();
    });

    /* When user begins selecting inside outputLog, blur the prompt input so Ctrl+C targets the output selection */
    outputLog.addEventListener('mousedown', (event) => {
        if (event.button === 2) return;
        const interactive = event.target?.closest?.('.term-file-link, .term-entry-copy, button');
        if (interactive) return;
        cachedSelection = '';
        if (document.activeElement === input) {
            input.blur();
        }
    });

    outputLog.addEventListener('click', (event) => {
        const copyEntryBtn = event.target?.closest?.('.term-entry-copy');
        if (copyEntryBtn) {
            event.preventDefault();
            const entry = copyEntryBtn.closest('.term-entry');
            const textToCopy = extractEntryText(entry);
            if (textToCopy) {
                writeTextToClipboard(textToCopy);
                flashButton(copyEntryBtn, 'Copied ✓', 'Copy');
                announce?.('Copied command output to clipboard.');
            }
            return;
        }
        const link = event.target?.closest?.('.term-file-link');
        if (link && link.dataset.path) {
            event.preventDefault();
            onOpenFile?.(link.dataset.path);
        }
        /* Intentionally do NOT call input.focus() here so single, double, triple, and drag selections stay intact */
    });

    outputLog.addEventListener('contextmenu', (event) => {
        event.preventDefault();
        event.stopPropagation();
        openContextMenu(event.clientX, event.clientY);
    });

    document.addEventListener('pointerdown', (event) => {
        if (ctxMenu.hidden || ctxMenu.contains(event.target)) return;
        closeContextMenu();
    }, true);

    inputForm.addEventListener('click', (event) => {
        if (event.target === inputForm || promptPrefix.contains(event.target)) {
            focus();
        }
    });

    inputForm.addEventListener('submit', (event) => {
        event.preventDefault();
        if (busy && activeJobId > 0) {
            void stopActiveCommand();
            return;
        }
        const raw = input.value;
        if (!raw.trim() || busy) return;
        input.value = '';
        draftInput = '';
        void runCommand(raw);
    });

    input.addEventListener('keydown', (event) => {
        const mod = event.ctrlKey || event.metaKey;
        const key = event.key.toLowerCase();
        if (mod && !event.altKey && !event.shiftKey && key === 'l') {
            event.preventDefault();
            event.stopPropagation();
            clear();
            return;
        }
        if (mod && !event.altKey && key === 'c') {
            const inputHasSelection = input.selectionStart !== input.selectionEnd;
            const panelSel = getLivePanelSelection();
            if (!inputHasSelection && panelSel) {
                event.preventDefault();
                event.stopPropagation();
                writeTextToClipboard(panelSel);
                flashButton(copyBtn, 'Copied ✓', 'Copy');
                announce?.('Copied terminal selection to clipboard.');
                return;
            }
            if (event.shiftKey) {
                event.preventDefault();
                event.stopPropagation();
                if (inputHasSelection) {
                    const sub = input.value.slice(input.selectionStart, input.selectionEnd);
                    writeTextToClipboard(sub);
                    flashButton(copyBtn, 'Copied ✓', 'Copy');
                    announce?.('Copied selection to clipboard.');
                } else {
                    copySelectionOrAll();
                }
                return;
            }
            if (!inputHasSelection && busy && activeJobId > 0) {
                event.preventDefault();
                void stopActiveCommand();
                return;
            }
            if (!inputHasSelection && input.value) {
                event.preventDefault();
                appendSystemLine(`${formatShortCwd(cwd)} $ ${input.value}^C`);
                input.value = '';
                draftInput = '';
                historyIndex = history.length;
                return;
            }
        }
        if (event.key === 'ArrowUp' && !mod && !event.altKey) {
            event.preventDefault();
            if (!history.length) return;
            if (historyIndex === history.length) {
                draftInput = input.value;
            }
            if (historyIndex > 0) {
                historyIndex -= 1;
                input.value = history[historyIndex];
                moveCaretToEnd();
            }
            return;
        }
        if (event.key === 'ArrowDown' && !mod && !event.altKey) {
            event.preventDefault();
            if (historyIndex < history.length) {
                historyIndex += 1;
                input.value = historyIndex === history.length ? draftInput : history[historyIndex];
                moveCaretToEnd();
            }
            return;
        }
        if (event.key === 'Tab' && !mod && !event.altKey && !event.shiftKey) {
            event.preventDefault();
            applyTabCompletion();
        }
    });

    panel.addEventListener('keydown', (event) => {
        if (!ctxMenu.hidden && event.key === 'Escape') {
            event.preventDefault();
            event.stopPropagation();
            closeContextMenu();
            return;
        }
        const mod = event.ctrlKey || event.metaKey;
        const key = event.key.toLowerCase();
        if (mod && !event.altKey && !event.shiftKey && key === 'l') {
            event.preventDefault();
            event.stopPropagation();
            clear();
            focus();
            return;
        }
        if (mod && !event.altKey && key === 'c') {
            const inputFocusedWithSel = document.activeElement === input && input.selectionStart !== input.selectionEnd;
            if (!inputFocusedWithSel) {
                const selText = getLivePanelSelection() || (event.shiftKey ? cachedSelection : '');
                if (selText) {
                    event.preventDefault();
                    event.stopPropagation();
                    writeTextToClipboard(selText);
                    flashButton(copyBtn, 'Copied ✓', 'Copy');
                    announce?.('Copied terminal selection to clipboard.');
                    return;
                }
                if (event.shiftKey) {
                    event.preventDefault();
                    event.stopPropagation();
                    copySelectionOrAll();
                    return;
                }
                if (busy && activeJobId > 0) {
                    event.preventDefault();
                    event.stopPropagation();
                    void stopActiveCommand();
                    return;
                }
            }
        }
        if (mod && !event.altKey && !event.shiftKey && key === 'a' && document.activeElement !== input) {
            event.preventDefault();
            event.stopPropagation();
            selectAllOutput();
            return;
        }
        /* If outputLog has focus and the user starts typing a command, redirect focus to the prompt input */
        if (document.activeElement !== input && !mod && !event.altKey) {
            if (event.key.length === 1 && !getLivePanelSelection()) {
                input.focus({ preventScroll: true });
            } else if (event.key === 'Backspace' || event.key === 'Enter') {
                input.focus({ preventScroll: true });
            }
        }
    });

    /* Also support Ctrl+C / Ctrl+Shift+C when terminal output is selected even if focus is on body/panel */
    document.addEventListener('keydown', (event) => {
        if (panel.hidden) return;
        const mod = event.ctrlKey || event.metaKey;
        if (!mod || event.altKey || event.key.toLowerCase() !== 'c') return;
        if (panel.contains(event.target)) return; /* handled by panel/input listeners above */
        const selText = getLivePanelSelection();
        if (!selText) return;
        event.preventDefault();
        event.stopPropagation();
        writeTextToClipboard(selText);
        flashButton(copyBtn, 'Copied ✓', 'Copy');
        announce?.('Copied terminal selection to clipboard.');
    }, true);

    function openContextMenu(x, y) {
        const selText = getLivePanelSelection() || cachedSelection;
        const hasOut = Boolean(extractAllOutputText());
        ctxMenu.replaceChildren();

        const addItem = (label, shortcut, disabled, onClick) => {
            const item = el('button', {
                type: 'button',
                class: 'term-context-item',
                role: 'menuitem',
                disabled: Boolean(disabled),
            });
            item.append(el('span', { text: label }));
            if (shortcut) item.append(el('kbd', { text: shortcut }));
            item.addEventListener('mousedown', (ev) => ev.preventDefault());
            item.addEventListener('click', (ev) => {
                ev.preventDefault();
                ev.stopPropagation();
                closeContextMenu();
                onClick();
            });
            ctxMenu.append(item);
        };

        addItem('Copy Selection', 'Ctrl+C', !selText, () => {
            if (selText) {
                writeTextToClipboard(selText);
                flashButton(copyBtn, 'Copied ✓', 'Copy');
                announce?.('Copied terminal selection to clipboard.');
            }
        });
        addItem('Copy All Output', 'Ctrl+Shift+C', !hasOut, () => {
            const all = extractAllOutputText();
            if (all) {
                writeTextToClipboard(all);
                flashButton(copyBtn, 'Copied ✓', 'Copy');
                announce?.('Copied all terminal output to clipboard.');
            }
        });
        ctxMenu.append(el('div', { class: 'term-context-sep', role: 'separator' }));
        addItem('Select All Output', 'Ctrl+A', !hasOut, () => {
            selectAllOutput();
        });
        addItem('Paste into Prompt', 'Ctrl+V', false, async () => {
            try {
                const clip = await navigator.clipboard.readText();
                if (clip) {
                    const clean = clip.replace(/[\r\n]+/g, ' ');
                    const start = input.selectionStart ?? input.value.length;
                    const end = input.selectionEnd ?? start;
                    input.setRangeText(clean, start, end, 'end');
                }
            } catch { /* ignore if blocked */ }
            focus();
        });
        ctxMenu.append(el('div', { class: 'term-context-sep', role: 'separator' }));
        addItem('Clear Terminal', 'Ctrl+L', false, () => {
            clear();
            focus();
        });

        ctxMenu.hidden = false;
        const width = ctxMenu.offsetWidth || 200;
        const height = ctxMenu.offsetHeight || 160;
        const left = Math.max(8, Math.min(x, window.innerWidth - width - 8));
        const top = Math.max(8, Math.min(y, window.innerHeight - height - 8));
        ctxMenu.style.left = `${Math.round(left)}px`;
        ctxMenu.style.top = `${Math.round(top)}px`;
    }

    function closeContextMenu() {
        ctxMenu.hidden = true;
    }

    function getLivePanelSelection() {
        const sel = window.getSelection?.();
        if (!sel || sel.isCollapsed || sel.rangeCount === 0) return '';
        const anchorIn = sel.anchorNode && panel.contains(sel.anchorNode);
        const focusIn = sel.focusNode && panel.contains(sel.focusNode);
        if (!anchorIn && !focusIn) return '';
        return String(sel.toString() || '');
    }

    function selectAllOutput() {
        const sel = window.getSelection?.();
        if (!sel) return;
        outputLog.focus({ preventScroll: true });
        const range = document.createRange();
        range.selectNodeContents(outputLog);
        sel.removeAllRanges();
        sel.addRange(range);
        cachedSelection = String(sel.toString() || '');
        announce?.('Selected all terminal output.');
    }

    function extractEntryText(entry) {
        if (!entry) return '';
        const pres = [...entry.querySelectorAll('.term-pre')];
        if (pres.length) {
            return pres.map((p) => p.textContent || '').join('\n').trim();
        }
        const cmd = entry.querySelector('.term-cmd-text')?.textContent || '';
        return cmd.trim();
    }

    function extractAllOutputText() {
        const entries = [...outputLog.querySelectorAll('.term-entry')];
        if (!entries.length) {
            return (outputLog.textContent || '').trim();
        }
        const blocks = [];
        for (const entry of entries) {
            const cwdText = entry.querySelector('.term-prompt-cwd')?.textContent || '';
            const cmdText = entry.querySelector('.term-cmd-text')?.textContent || '';
            const pres = [...entry.querySelectorAll('.term-pre')].map((p) => p.textContent || '');
            const lines = [];
            if (cmdText) lines.push(`${cwdText ? `${cwdText} ` : ''}$ ${cmdText}`);
            for (const p of pres) {
                if (p) lines.push(p);
            }
            if (lines.length) blocks.push(lines.join('\n'));
        }
        return blocks.join('\n\n').trim();
    }

    function copySelectionOrAll() {
        const selText = getLivePanelSelection() || cachedSelection;
        if (selText) {
            writeTextToClipboard(selText);
            flashButton(copyBtn, 'Copied ✓', 'Copy');
            announce?.('Copied terminal selection to clipboard.');
            return true;
        }
        const allText = extractAllOutputText();
        if (allText) {
            writeTextToClipboard(allText);
            flashButton(copyBtn, 'Copied ✓', 'Copy');
            announce?.('Copied terminal output to clipboard.');
            return true;
        }
        announce?.('Nothing in terminal to copy yet.');
        return false;
    }

    function flashButton(btn, tempText, normalText) {
        if (!btn) return;
        btn.textContent = tempText;
        btn.classList.add('is-copied');
        if (btn === copyBtn) {
            window.clearTimeout(copyFeedbackTimer);
            copyFeedbackTimer = window.setTimeout(() => {
                btn.textContent = normalText;
                btn.classList.remove('is-copied');
            }, 1400);
        } else {
            window.setTimeout(() => {
                btn.textContent = normalText;
                btn.classList.remove('is-copied');
            }, 1400);
        }
    }

    function moveCaretToEnd() {
        const len = input.value.length;
        input.setSelectionRange(len, len);
    }

    function applyTabCompletion() {
        const val = input.value;
        const trimmed = val.trimStart();
        if (!trimmed.includes(' ')) {
            const match = COMMON_COMMANDS.find((cmd) => cmd.startsWith(trimmed) && cmd !== trimmed);
            if (match) {
                input.value = match;
                moveCaretToEnd();
                return;
            }
        }
        const parts = val.split(/\s+/);
        const lastToken = parts[parts.length - 1] || '';
        if (!lastToken) return;
        const known = typeof getKnownPaths === 'function' ? (getKnownPaths() || []) : [];
        const needle = lastToken.replace(/^\/+/, '').toLowerCase();
        const hit = known.find((p) => {
            const clean = String(p || '').replace(/^\/+/, '');
            return clean.toLowerCase().startsWith(needle) && clean.toLowerCase() !== needle;
        });
        if (hit) {
            const clean = String(hit).replace(/^\/+/, '');
            parts[parts.length - 1] = lastToken.startsWith('/') ? `/${clean}` : clean;
            input.value = parts.join(' ');
            moveCaretToEnd();
        }
    }

    function formatShortCwd(targetCwd) {
        const root = repoRoot || (typeof getRepoRoot === 'function' ? getRepoRoot() : '') || '';
        const cur = targetCwd || root || '';
        if (!cur) return '.';
        const normRoot = root.replace(/\/+$/, '');
        const normCur = cur.replace(/\/+$/, '');
        if (normRoot && normCur === normRoot) return '.';
        if (normRoot && normCur.startsWith(`${normRoot}/`)) {
            return `./${normCur.slice(normRoot.length + 1)}`;
        }
        return normCur;
    }

    function setCwd(nextCwd) {
        if (!nextCwd || typeof nextCwd !== 'string') return;
        cwd = nextCwd;
        try {
            localStorage.setItem(CWD_KEY, cwd);
        } catch { /* ignore */ }
        updateCwdUi();
    }

    function updateCwdUi() {
        const root = repoRoot || (typeof getRepoRoot === 'function' ? getRepoRoot() : '') || '';
        const effective = cwd || root || '';
        const shortCwd = formatShortCwd(effective);
        cwdBadge.textContent = effective ? `${shortCwd === '.' ? effective : `${shortCwd} (${effective})`}` : '.';
        cwdBadge.title = effective ? `Working directory: ${effective}` : 'Working directory';
        promptPrefixCwd.textContent = shortCwd;
        promptPrefix.title = effective ? `Working directory: ${effective}` : 'Prompt';
        shellPill.textContent = shellName || 'sh';
        if (userHost) shellPill.title = `${userHost} (${shellName || 'sh'})`;
        const atRoot = !effective || !root || effective.replace(/\/+$/, '') === root.replace(/\/+$/, '');
        resetCwdBtn.disabled = busy || atRoot;
    }

    function renderWelcome() {
        const welcome = el('div', { class: 'term-welcome' });
        welcome.append(
            el('strong', { text: 'ClassicCars Studio Terminal' }),
            ' — Run shell commands, Git workflows, or PHP checks. Select text and press ',
            el('kbd', { text: 'Ctrl+C' }),
            ' (or right-click) to copy, ',
            el('kbd', { text: 'Ctrl+L' }),
            ' to clear, or ',
            el('kbd', { text: 'Ctrl+`' }),
            ' to toggle.',
        );
        outputLog.append(welcome);
    }

    function appendSystemLine(text) {
        const row = el('div', { class: 'term-entry' });
        row.append(el('pre', { class: 'term-pre', text }));
        outputLog.append(row);
        scrollToBottom();
    }

    function isNearBottom() {
        return outputLog.scrollHeight - outputLog.scrollTop - outputLog.clientHeight <= 48;
    }

    function scrollToBottom() {
        outputLog.scrollTop = outputLog.scrollHeight;
    }

    function clear() {
        cachedSelection = '';
        outputLog.replaceChildren();
        renderWelcome();
        announce?.('Terminal output cleared.');
    }

    function resetCwd() {
        const root = repoRoot || (typeof getRepoRoot === 'function' ? getRepoRoot() : '') || '';
        if (root) {
            setCwd(root);
            appendSystemLine(`Working directory reset to project root: ${root}`);
            announce?.(`Terminal working directory reset to ${root}`);
        }
    }

    function syncRepoRoot(newRoot) {
        if (!newRoot) return;
        const prevRoot = repoRoot;
        repoRoot = newRoot;
        if (!cwd || (prevRoot && cwd.startsWith(prevRoot) && !cwd.startsWith(newRoot))) {
            setCwd(newRoot);
        } else {
            updateCwdUi();
        }
    }

    async function refreshInfo() {
        if (typeof apiSend !== 'function') return;
        try {
            const query = cwd ? `?cwd=${encodeURIComponent(cwd)}` : '';
            const data = await apiSend('GET', `/__studio/api/terminal/info${query}`);
            infoLoaded = true;
            if (data?.repoRoot) repoRoot = data.repoRoot;
            if (data?.cwd) setCwd(data.cwd);
            if (data?.shell) shellName = data.shell;
            if (data?.user && data?.host) userHost = `${data.user}@${data.host}`;
            updateCwdUi();
        } catch {
            /* ignore offline during startup */
        }
    }

    async function stopActiveCommand() {
        if (!busy || activeJobId <= 0 || typeof apiSend !== 'function') return;
        const id = activeJobId;
        try {
            await apiSend('POST', '/__studio/api/terminal/stop', { id });
            announce?.('Sent interrupt signal to running command.');
        } catch {
            /* ignore if already finished */
        }
    }

    async function runCommand(rawCommand) {
        const command = String(rawCommand || '').trim();
        if (!command || busy) return;

        pushHistory(command);

        if (command === 'clear' || command === 'cls') {
            clear();
            return;
        }

        if (command === 'help') {
            appendHelpEntry(command);
            return;
        }

        if (!infoLoaded) {
            void refreshInfo();
        }

        const entryCwd = cwd || repoRoot || (typeof getRepoRoot === 'function' ? getRepoRoot() : '') || '';
        const entry = el('div', { class: 'term-entry' });
        const head = el('div', { class: 'term-entry-head' });
        const cmdLine = el('div', { class: 'term-cmd-line' });
        cmdLine.append(
            el('span', { class: 'term-prompt-cwd', text: formatShortCwd(entryCwd) }),
            el('span', { class: 'term-prompt-dollar', text: '$' }),
            el('span', { class: 'term-cmd-text', text: command }),
        );
        const meta = el('div', { class: 'term-entry-meta', text: 'Running…' });
        head.append(cmdLine, meta);
        entry.append(head);
        outputLog.append(entry);
        trimEntries();
        scrollToBottom();

        const emulator = createTerminalEmulator();
        const livePre = el('pre', { class: 'term-pre' });
        let preAttached = false;

        const syncPreFromEmulator = () => {
            const stickBottom = isNearBottom();
            const segments = emulator.getSegments();
            if (!segments.length && !preAttached) return;
            if (!preAttached) {
                entry.append(livePre);
                preAttached = true;
            }
            renderSegmentsIntoPre(livePre, segments);
            if (stickBottom && !getLivePanelSelection()) {
                scrollToBottom();
            }
        };

        setBusy(true);
        try {
            const startData = await apiSend('POST', '/__studio/api/terminal/start', {
                command,
                cwd: entryCwd,
                timeoutMs: COMMAND_TIMEOUT_MS,
            });
            if (startData?.repoRoot) repoRoot = startData.repoRoot;
            const jobId = Number(startData?.id ?? 0);
            activeJobId = jobId;

            let offset = 0;
            let finalData = startData;

            while (jobId > 0) {
                const pollData = await apiSend('GET', `/__studio/api/terminal/poll?id=${jobId}&offset=${offset}`);
                finalData = pollData;

                if (typeof pollData?.chunk === 'string' && pollData.chunk.length > 0) {
                    emulator.feed(pollData.chunk);
                    syncPreFromEmulator();
                }
                if (typeof pollData?.offset === 'number' && pollData.offset >= offset) {
                    offset = pollData.offset;
                }

                const elapsedMs = Number(pollData?.durationMs ?? 0);
                if (pollData?.running) {
                    if (elapsedMs >= 1000) {
                        meta.textContent = `Running… ${(elapsedMs / 1000).toFixed(1)}s`;
                    }
                    await delayMs(POLL_INTERVAL_MS);
                    continue;
                }
                break;
            }

            if (offset === 0 && typeof finalData?.output === 'string' && finalData.output.length > 0) {
                emulator.feed(finalData.output);
                syncPreFromEmulator();
            }

            if (finalData?.repoRoot) repoRoot = finalData.repoRoot;
            if (finalData?.cwd) setCwd(finalData.cwd);

            const exitCode = Number(finalData?.exitCode ?? 0);
            const durationMs = Number(finalData?.durationMs ?? 0);
            meta.replaceChildren();
            meta.append(el('span', {
                class: 'term-duration',
                text: durationMs >= 1000 ? `${(durationMs / 1000).toFixed(2)}s` : `${durationMs}ms`,
            }));
            if (exitCode !== 0 || finalData?.timedOut) {
                meta.append(el('span', {
                    class: 'term-exit-badge is-err',
                    text: finalData?.timedOut ? 'Timed out (124)' : `exit ${exitCode}`,
                }));
            } else {
                meta.append(el('span', {
                    class: 'term-exit-badge is-ok',
                    text: '✓',
                    title: 'Exit code 0',
                }));
            }
            const entryCopyBtn = el('button', {
                type: 'button',
                class: 'term-entry-copy',
                text: 'Copy',
                title: 'Copy this command output',
            });
            entryCopyBtn.addEventListener('mousedown', (ev) => ev.preventDefault());
            meta.append(entryCopyBtn);

            if (finalData?.truncated) {
                entry.append(el('pre', {
                    class: 'term-pre is-stderr',
                    text: '[Output truncated at 512 KB]',
                }));
            }
            onCommandComplete?.(finalData);
        } catch (error) {
            meta.replaceChildren(el('span', {
                class: 'term-exit-badge is-err',
                text: 'error',
            }));
            entry.append(el('pre', {
                class: 'term-pre is-stderr',
                text: error?.message || 'Terminal execution failed.',
            }));
        } finally {
            activeJobId = 0;
            setBusy(false);
            scrollToBottom();
            if (!getLivePanelSelection()) {
                focus();
            }
        }
    }

    function appendHelpEntry(command) {
        const entry = el('div', { class: 'term-entry' });
        const head = el('div', { class: 'term-entry-head' });
        const cmdLine = el('div', { class: 'term-cmd-line' });
        cmdLine.append(
            el('span', { class: 'term-prompt-cwd', text: formatShortCwd(cwd) }),
            el('span', { class: 'term-prompt-dollar', text: '$' }),
            el('span', { class: 'term-cmd-text', text: command }),
        );
        head.append(cmdLine);
        const helpText = [
            'Built-in shortcuts & commands:',
            '  cd <dir>       Change the terminal working directory (persisted across commands)',
            '  clear / cls    Clear terminal output log (or press Ctrl+L)',
            '  Ctrl+C         Copy selected terminal text (or interrupt a running command)',
            '  Ctrl+Shift+C   Copy selected terminal text or full output log',
            '  Up / Down      Cycle through previous commands',
            '  Tab            Auto-complete common commands or site file paths',
            '  Ctrl+`         Show or hide the bottom terminal panel',
        ].join('\n');
        entry.append(head, el('pre', { class: 'term-pre', text: helpText }));
        outputLog.append(entry);
        scrollToBottom();
    }

    function renderSegmentsIntoPre(pre, segments) {
        pre.replaceChildren();
        for (const seg of segments) {
            const node = seg.classes.length
                ? el('span', { class: seg.classes.join(' ') })
                : document.createDocumentFragment();
            appendWithFileLinks(node, seg.text);
            pre.append(node);
        }
    }

    function appendWithFileLinks(target, text) {
        const pathRegex = /(?:^|(?<=[\s"'(:]))(\/?(?:sales|articles|awards|projects|parts|video|whatsnew|contact|newsletter)\/[A-Za-z0-9_./-]+\.(?:php|html|shtml|htm|json|txt|css|js))\b/g;
        let lastIndex = 0;
        let match;
        while ((match = pathRegex.exec(text)) !== null) {
            const matchedPath = match[1];
            const start = match.index;
            if (start > lastIndex) {
                target.append(document.createTextNode(text.slice(lastIndex, start)));
            }
            const normalized = matchedPath.startsWith('/') ? matchedPath : `/${matchedPath}`;
            const btn = el('button', {
                type: 'button',
                class: 'term-file-link',
                'data-path': normalized,
                text: matchedPath,
                title: `Open ${normalized} in Studio editor`,
            });
            target.append(btn);
            lastIndex = start + matchedPath.length;
        }
        if (lastIndex < text.length) {
            target.append(document.createTextNode(text.slice(lastIndex)));
        }
    }

    function setBusy(nextBusy) {
        busy = Boolean(nextBusy);
        runBtn.disabled = false;
        runBtn.textContent = busy ? 'Stop ■' : 'Run ↵';
        runBtn.title = busy ? 'Interrupt running command (Ctrl+C)' : 'Execute command (Enter)';
        updateCwdUi();
    }

    function trimEntries() {
        const entries = outputLog.querySelectorAll('.term-entry');
        if (entries.length <= MAX_ENTRIES) return;
        const removeCount = entries.length - MAX_ENTRIES;
        for (let i = 0; i < removeCount; i += 1) {
            entries[i].remove();
        }
    }

    function pushHistory(cmd) {
        if (!cmd) return;
        if (history[history.length - 1] !== cmd) {
            history.push(cmd);
            if (history.length > MAX_HISTORY) {
                history = history.slice(history.length - MAX_HISTORY);
            }
            try {
                localStorage.setItem(HISTORY_KEY, JSON.stringify(history));
            } catch { /* ignore */ }
        }
        historyIndex = history.length;
    }

    function focus() {
        if (!infoLoaded) void refreshInfo();
        requestAnimationFrame(() => {
            if (!getLivePanelSelection()) {
                input.focus({ preventScroll: true });
            }
        });
    }

    return {
        focus,
        clear,
        resetCwd,
        syncRepoRoot,
        refreshInfo,
        runCommand,
        getCwd: () => cwd,
        containsFocus: () => panel.contains(document.activeElement),
        hasSelection: () => Boolean(getLivePanelSelection() || cachedSelection),
        copySelection: () => copySelectionOrAll(),
        selectAll: () => selectAllOutput(),
    };
}

function delayMs(ms) {
    return new Promise((resolve) => window.setTimeout(resolve, ms));
}

function writeTextToClipboard(text) {
    if (!text) return false;
    const sel = window.getSelection?.();
    const savedRanges = [];
    if (sel && sel.rangeCount > 0) {
        for (let i = 0; i < sel.rangeCount; i += 1) {
            savedRanges.push(sel.getRangeAt(i).cloneRange());
        }
    }
    const prevActive = document.activeElement;
    let copied = false;
    try {
        const ta = document.createElement('textarea');
        ta.value = text;
        ta.setAttribute('readonly', '');
        ta.style.position = 'fixed';
        ta.style.top = '-9999px';
        ta.style.left = '-9999px';
        ta.style.opacity = '0';
        document.body.appendChild(ta);
        ta.focus({ preventScroll: true });
        ta.select();
        ta.setSelectionRange(0, text.length);
        copied = document.execCommand('copy');
        document.body.removeChild(ta);
    } catch {
        copied = false;
    }
    if (sel && savedRanges.length > 0) {
        try {
            sel.removeAllRanges();
            for (const r of savedRanges) sel.addRange(r);
        } catch { /* ignore */ }
    }
    if (prevActive && typeof prevActive.focus === 'function' && prevActive !== document.body) {
        try {
            prevActive.focus({ preventScroll: true });
        } catch { /* ignore */ }
    }
    try {
        navigator.clipboard?.writeText?.(text)?.catch?.(() => {});
    } catch { /* ignore */ }
    return copied;
}

function loadHistory() {
    try {
        const parsed = JSON.parse(localStorage.getItem(HISTORY_KEY) || '[]');
        if (Array.isArray(parsed)) {
            return parsed.filter((item) => typeof item === 'string' && item.trim().length > 0).slice(-MAX_HISTORY);
        }
    } catch { /* ignore */ }
    return [];
}

/**
 * Stateful terminal line & cursor emulator.
 * Handles \r (in-place carriage return overwrite), \n, \b, \t,
 * CSI erase sequences (\x1b[K, \x1b[0K, \x1b[1K, \x1b[2K, \x1b[J),
 * CSI cursor motion (\x1b[G, \x1b[A, \x1b[B, \x1b[C, \x1b[D, \x1b[E, \x1b[F),
 * and SGR color/style attributes (\x1b[...m) across streamed chunks.
 */
export function createTerminalEmulator() {
    const lines = [[]];
    let row = 0;
    let col = 0;
    let pendingCrOverwrite = false;
    let activeClasses = new Set();
    let activeClassStr = '';
    let pendingEsc = '';

    function ensureRow(r) {
        while (lines.length <= r) {
            lines.push([]);
        }
    }

    function applySgr(paramStr) {
        const codes = (paramStr || '0').split(';').map((c) => Number(c || 0));
        for (const code of codes) {
            if (code === 0) {
                activeClasses = new Set();
            } else if (code === 1) {
                activeClasses.add('term-ansi-bold');
            } else if (code === 2) {
                activeClasses.add('term-ansi-dim');
            } else if (code === 4) {
                activeClasses.add('term-ansi-underline');
            } else if (code === 22) {
                activeClasses.delete('term-ansi-bold');
                activeClasses.delete('term-ansi-dim');
            } else if (code === 24) {
                activeClasses.delete('term-ansi-underline');
            } else if (code === 31 || code === 91) {
                clearFg(activeClasses);
                activeClasses.add('term-ansi-fg-red');
            } else if (code === 32 || code === 92) {
                clearFg(activeClasses);
                activeClasses.add('term-ansi-fg-green');
            } else if (code === 33 || code === 93) {
                clearFg(activeClasses);
                activeClasses.add('term-ansi-fg-yellow');
            } else if (code === 34 || code === 94) {
                clearFg(activeClasses);
                activeClasses.add('term-ansi-fg-blue');
            } else if (code === 35 || code === 95) {
                clearFg(activeClasses);
                activeClasses.add('term-ansi-fg-magenta');
            } else if (code === 36 || code === 96) {
                clearFg(activeClasses);
                activeClasses.add('term-ansi-fg-cyan');
            } else if (code === 90 || code === 37) {
                clearFg(activeClasses);
                activeClasses.add('term-ansi-fg-gray');
            } else if (code === 39) {
                clearFg(activeClasses);
            }
        }
        activeClassStr = [...activeClasses].join(' ');
    }

    function writeChar(sym) {
        ensureRow(row);
        const line = lines[row];
        if (pendingCrOverwrite && col === 0) {
            line.length = 0;
            pendingCrOverwrite = false;
        }
        while (line.length < col) {
            line.push({ ch: ' ', cls: '' });
        }
        line[col] = { ch: sym, cls: activeClassStr };
        col += 1;
    }

    function feed(rawChunk) {
        const input = pendingEsc + String(rawChunk || '');
        pendingEsc = '';
        let i = 0;
        const len = input.length;

        while (i < len) {
            const code = input.charCodeAt(i);

            if (code === 0x1b) { /* ESC */
                if (i + 1 >= len) {
                    pendingEsc = input.slice(i);
                    break;
                }
                const next = input[i + 1];
                if (next === '[') { /* CSI sequence: \x1b[... */
                    let j = i + 2;
                    while (j < len) {
                        const c = input.charCodeAt(j);
                        if (c >= 0x20 && c <= 0x3f) {
                            j += 1;
                        } else {
                            break;
                        }
                    }
                    if (j >= len) {
                        if (len - i < 32) {
                            pendingEsc = input.slice(i);
                            break;
                        }
                        i += 2;
                        continue;
                    }
                    const params = input.slice(i + 2, j);
                    const finalChar = input[j];
                    i = j + 1;

                    if (finalChar === 'm') {
                        applySgr(params);
                    } else if (finalChar === 'K') {
                        const mode = parseInt(params || '0', 10) || 0;
                        ensureRow(row);
                        pendingCrOverwrite = false;
                        if (mode === 0) {
                            lines[row].splice(col);
                        } else if (mode === 1) {
                            const stop = Math.min(col + 1, lines[row].length);
                            for (let k = 0; k < stop; k += 1) {
                                lines[row][k] = { ch: ' ', cls: '' };
                            }
                        } else if (mode === 2) {
                            lines[row].length = 0;
                        }
                    } else if (finalChar === 'G' || finalChar === '`') {
                        const n = parseInt(params || '1', 10);
                        col = Math.max(0, (Number.isFinite(n) && n > 0 ? n : 1) - 1);
                        pendingCrOverwrite = col === 0;
                    } else if (finalChar === 'A') {
                        const n = parseInt(params || '1', 10);
                        row = Math.max(0, row - (Number.isFinite(n) && n > 0 ? n : 1));
                        pendingCrOverwrite = false;
                    } else if (finalChar === 'B') {
                        const n = parseInt(params || '1', 10);
                        row += (Number.isFinite(n) && n > 0 ? n : 1);
                        pendingCrOverwrite = false;
                        ensureRow(row);
                    } else if (finalChar === 'C') {
                        const n = parseInt(params || '1', 10);
                        col += (Number.isFinite(n) && n > 0 ? n : 1);
                        pendingCrOverwrite = false;
                    } else if (finalChar === 'D') {
                        const n = parseInt(params || '1', 10);
                        col = Math.max(0, col - (Number.isFinite(n) && n > 0 ? n : 1));
                        pendingCrOverwrite = false;
                    } else if (finalChar === 'E') {
                        const n = parseInt(params || '1', 10);
                        row += (Number.isFinite(n) && n > 0 ? n : 1);
                        col = 0;
                        pendingCrOverwrite = false;
                        ensureRow(row);
                    } else if (finalChar === 'F') {
                        const n = parseInt(params || '1', 10);
                        row = Math.max(0, row - (Number.isFinite(n) && n > 0 ? n : 1));
                        col = 0;
                        pendingCrOverwrite = true;
                    } else if (finalChar === 'H' || finalChar === 'f') {
                        const parts = params.split(';');
                        const r = parseInt(parts[0] || '1', 10);
                        const c = parseInt(parts[1] || '1', 10);
                        row = Math.max(0, (Number.isFinite(r) && r > 0 ? r : 1) - 1);
                        col = Math.max(0, (Number.isFinite(c) && c > 0 ? c : 1) - 1);
                        pendingCrOverwrite = false;
                        ensureRow(row);
                    } else if (finalChar === 'J') {
                        const mode = parseInt(params || '0', 10) || 0;
                        ensureRow(row);
                        pendingCrOverwrite = false;
                        if (mode === 0) {
                            lines[row].splice(col);
                            lines.splice(row + 1);
                        } else if (mode === 2 || mode === 3) {
                            lines.length = 0;
                            lines.push([]);
                            row = 0;
                            col = 0;
                        }
                    }
                    continue;
                }
                if (next === ']') { /* OSC sequence: \x1b]...\x07 or \x1b]...(\x1b\\) */
                    let j = i + 2;
                    let foundEnd = false;
                    while (j < len) {
                        if (input.charCodeAt(j) === 0x07) {
                            j += 1;
                            foundEnd = true;
                            break;
                        }
                        if (input.charCodeAt(j) === 0x1b && j + 1 < len && input[j + 1] === '\\') {
                            j += 2;
                            foundEnd = true;
                            break;
                        }
                        j += 1;
                    }
                    if (!foundEnd && len - i < 256) {
                        pendingEsc = input.slice(i);
                        break;
                    }
                    i = j;
                    continue;
                }
                if (next === 'M') {
                    row = Math.max(0, row - 1);
                    pendingCrOverwrite = false;
                }
                i += 2;
                continue;
            }

            if (code === 0x0d) { /* \r: carriage return to column 0 of current line */
                col = 0;
                pendingCrOverwrite = true;
                i += 1;
                continue;
            }

            if (code === 0x0a) { /* \n: newline */
                col = 0;
                pendingCrOverwrite = false;
                row += 1;
                ensureRow(row);
                i += 1;
                continue;
            }

            if (code === 0x08) { /* \b: backspace */
                pendingCrOverwrite = false;
                if (col > 0) col -= 1;
                i += 1;
                continue;
            }

            if (code === 0x09) { /* \t: horizontal tab */
                const spaces = 8 - (col % 8);
                for (let s = 0; s < spaces; s += 1) {
                    writeChar(' ');
                }
                i += 1;
                continue;
            }

            if (code < 0x20 || code === 0x7f) {
                i += 1;
                continue;
            }

            const cp = input.codePointAt(i);
            const sym = String.fromCodePoint(cp);
            writeChar(sym);
            i += sym.length;
        }
    }

    function getSegments() {
        let lineCount = lines.length;
        if (lineCount > 1 && lines[lineCount - 1].length === 0) {
            lineCount -= 1;
        }
        const segments = [];
        let curText = '';
        let curCls = null;

        const pushCell = (ch, cls) => {
            if (curCls === null) {
                curCls = cls;
                curText = ch;
            } else if (curCls === cls) {
                curText += ch;
            } else {
                segments.push({
                    text: curText,
                    classes: curCls ? curCls.split(' ') : [],
                });
                curCls = cls;
                curText = ch;
            }
        };

        for (let r = 0; r < lineCount; r += 1) {
            const line = lines[r];
            let end = line.length;
            while (end > 0 && line[end - 1].ch === ' ' && !line[end - 1].cls) {
                end -= 1;
            }
            for (let c = 0; c < end; c += 1) {
                pushCell(line[c].ch, line[c].cls);
            }
            if (r + 1 < lineCount) {
                pushCell('\n', '');
            }
        }

        if (curCls !== null && curText.length > 0) {
            segments.push({
                text: curText,
                classes: curCls ? curCls.split(' ') : [],
            });
        }
        return segments;
    }

    function getText() {
        return getSegments().map((s) => s.text).join('');
    }

    return {
        feed,
        getSegments,
        getText,
    };
}

function clearFg(set) {
    for (const cls of [...set]) {
        if (cls.startsWith('term-ansi-fg-')) set.delete(cls);
    }
}
