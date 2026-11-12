const STORAGE_KEY = 'cc-studio-theme';

export const PRESETS = [
    { id: 'gold', name: 'Shappy Gold — Default', detail: 'Admin gold and cyan' },
    { id: 'slate', name: 'Slate Studio', detail: 'Neutral photo review' },
    { id: 'daylight', name: 'Daylight', detail: 'High-legibility light' },
    { id: 'contrast', name: 'High Contrast AAA', detail: 'Black, white, yellow, cyan' },
];

const COLOR_GROUPS = [
    ['Surfaces', [
        ['--bg-app', 'App background'],
        ['--bg-panel', 'Panels'],
        ['--bg-elevated', 'Elevated'],
        ['--bg-input', 'Inputs'],
        ['--bg-hover', 'Hover'],
    ]],
    ['Text and lines', [
        ['--text-primary', 'Primary text'],
        ['--text-secondary', 'Secondary text'],
        ['--text-muted', 'Muted text'],
        ['--brand-heading', 'Headings'],
        ['--border-subtle', 'Subtle border'],
        ['--border-strong', 'Strong border'],
    ]],
    ['Accent and buttons', [
        ['--accent-primary', 'Accent'],
        ['--accent-focus', 'Focus ring'],
        ['--btn-primary-bg', 'Primary button'],
        ['--btn-primary-fg', 'Button text'],
    ]],
    ['Status', [
        ['--status-ok', 'OK'],
        ['--status-warn', 'Warning'],
        ['--status-danger', 'Danger'],
    ]],
    ['Badges', [
        ['--badge-bg', 'Slot badge'],
        ['--badge-fg', 'Slot text'],
        ['--badge-hero-bg', 'Hero badge'],
        ['--badge-hero-fg', 'Hero text'],
        ['--badge-bullet-bg', 'Bullet badge'],
        ['--badge-bullet-fg', 'Bullet text'],
        ['--badge-caption-bg', 'Caption badge'],
        ['--badge-caption-fg', 'Caption text'],
    ]],
];

const SIZE_TOKENS = [
    ['--border-width', 'Border width'],
    ['--focus-ring-width', 'Focus ring'],
];

const PAIRS = [
    ['Primary text on panel', '--text-primary', '--bg-panel'],
    ['Secondary text on panel', '--text-secondary', '--bg-panel'],
    ['Muted text on panel', '--text-muted', '--bg-panel'],
    ['Heading on panel', '--brand-heading', '--bg-panel'],
    ['Primary text on app', '--text-primary', '--bg-app'],
    ['Button label on button', '--btn-primary-fg', '--btn-primary-bg'],
];

const COLOR_TOKENS = COLOR_GROUPS.flatMap((group) => group[1].map((token) => token[0]));
const ALLOWED = new Set([...COLOR_TOKENS, ...SIZE_TOKENS.map((token) => token[0])]);

let preset = 'gold';
let custom = {};
let userSelected = false;
let themeTouched = false;
let layoutTouched = false;
let layoutProvider = () => ({});
let persistTimer = 0;
let statusNode = null;
let mediaBound = false;
const listeners = new Set();

export function isSystemHighContrast() {
    if (typeof window === 'undefined' || typeof window.matchMedia !== 'function') return false;
    try {
        return (
            window.matchMedia('(forced-colors: active)').matches ||
            window.matchMedia('(prefers-contrast: more)').matches
        );
    } catch {
        return false;
    }
}

function bindSystemContrastWatchers() {
    if (mediaBound || typeof window === 'undefined' || typeof window.matchMedia !== 'function') return;
    mediaBound = true;
    const onContrastChange = () => {
        if (!userSelected && !themeTouched && isSystemHighContrast() && preset !== 'contrast') {
            preset = 'contrast';
            applyTheme();
            emit();
        }
    };
    for (const query of ['(forced-colors: active)', '(prefers-contrast: more)']) {
        try {
            const mql = window.matchMedia(query);
            if (typeof mql.addEventListener === 'function') mql.addEventListener('change', onContrastChange);
            else if (typeof mql.addListener === 'function') mql.addListener(onContrastChange);
        } catch { /* ignore unsupported media query */ }
    }
}

export function contrastRatio(foreground, background) {
    const left = relativeLuminance(parseColor(foreground));
    const right = relativeLuminance(parseColor(background));
    if (left == null || right == null) return NaN;
    const lighter = Math.max(left, right);
    const darker = Math.min(left, right);
    return (lighter + 0.05) / (darker + 0.05);
}

export function contrastGrade(ratio) {
    if (!Number.isFinite(ratio)) return 'n/a';
    if (ratio >= 7) return 'AAA';
    if (ratio >= 4.5) return 'AA';
    if (ratio >= 3) return 'AA Large';
    return 'Fail';
}

export function formatContrast(ratio) {
    if (!Number.isFinite(ratio)) return 'n/a';
    return `${ratio.toFixed(1)}:1 ${contrastGrade(ratio)}`;
}

export function bootTheme() {
    bindSystemContrastWatchers();
    const stored = readStored();
    if (!stored) {
        if (isSystemHighContrast()) {
            preset = 'contrast';
            applyTheme();
            return { preset, custom: {} };
        }
        applyTheme();
        return null;
    }
    userSelected = Boolean(stored.userSelected);
    custom = sanitizeCustom(stored.custom);
    if (!userSelected && Object.keys(custom).length === 0 && isSystemHighContrast()) {
        preset = 'contrast';
    } else {
        preset = validPreset(stored.preset);
    }
    applyTheme();
    return stored;
}

export function getTheme() {
    return { preset, custom: { ...custom } };
}

export function wasThemeTouched() { return themeTouched; }
export function wasLayoutTouched() { return layoutTouched; }

export function setLayoutProvider(fn) {
    layoutProvider = typeof fn === 'function' ? fn : () => ({});
}

export function subscribeTheme(fn) {
    listeners.add(fn);
    return () => listeners.delete(fn);
}

export function setPreset(id) {
    const next = validPreset(id);
    if (next === preset && Object.keys(custom).length === 0 && userSelected) return;
    preset = next;
    custom = {};
    userSelected = true;
    themeTouched = true;
    applyTheme();
    emit();
    queuePersist();
}

export function replaceTheme(data, { persistLocal = false } = {}) {
    if (!data || typeof data !== 'object') return;
    const nextCustom = sanitizeCustom(data.custom || {});
    const remoteUserSelected = Boolean(data.userSelected) || (!data.default && Object.keys(nextCustom).length > 0);
    if (remoteUserSelected) userSelected = true;
    if (!userSelected && !remoteUserSelected && isSystemHighContrast()) {
        preset = 'contrast';
        custom = {};
    } else {
        preset = validPreset(data.preset || preset);
        custom = nextCustom;
    }
    applyTheme();
    emit();
    if (persistLocal) writeLocal();
}

export function rememberTheme() {
    writeLocal();
}

export function persistLayout() {
    layoutTouched = true;
    queuePersist();
}

export function mountThemeEditor(button) {
    const dialog = document.createElement('dialog');
    dialog.className = 'theme-dialog';
    dialog.setAttribute('aria-label', 'Customize theme');
    const head = div('dialog-head');
    const title = document.createElement('h2');
    title.textContent = 'Customize theme';
    title.id = 'theme-dialog-title';
    const close = buttonEl('Close');
    close.title = 'Close the theme editor';
    close.addEventListener('click', () => dialog.close());
    head.append(title, close);

    const body = div('dialog-body');
    const presetGrid = div('preset-grid');
    const presetButtons = PRESETS.map((item) => {
        const presetButton = document.createElement('button');
        presetButton.type = 'button';
        presetButton.className = 'preset';
        presetButton.dataset.preset = item.id;
        presetButton.setAttribute('aria-pressed', item.id === preset ? 'true' : 'false');
        presetButton.title = `Switch Studio color theme to ${item.name}`;
        const name = document.createElement('span');
        name.className = 'preset-name';
        name.textContent = item.name;
        const detail = document.createElement('span');
        detail.className = 'preset-desc';
        detail.textContent = item.detail;
        presetButton.append(name, detail);
        presetButton.addEventListener('click', () => setPreset(item.id));
        presetGrid.append(presetButton);
        return presetButton;
    });

    const sample = div('theme-sample');
    const sampleHeading = document.createElement('strong');
    sampleHeading.textContent = 'Heading';
    const sampleText = document.createElement('span');
    sampleText.textContent = 'Body text on the workspace';
    const sampleButton = buttonEl('Primary');
    sampleButton.classList.add('btn-primary');
    sampleButton.title = 'Sample of the primary button color';
    sampleButton.tabIndex = -1;
    sample.append(sampleHeading, sampleText, sampleButton);

    const sizes = div('size-row');
    const sizeInputs = SIZE_TOKENS.map(([token, label]) => {
        const field = document.createElement('label');
        field.textContent = label;
        const input = document.createElement('input');
        input.type = 'number';
        input.min = '1';
        input.max = '8';
        input.step = '1';
        input.dataset.token = token;
        input.setAttribute('aria-label', label);
        input.title = `Set ${label} in pixels`;
        input.addEventListener('input', () => {
            const value = Number(input.value);
            if (!Number.isFinite(value)) return;
            setToken(token, `${Math.min(8, Math.max(1, value))}px`);
        });
        field.append(input);
        sizes.append(field);
        return input;
    });

    const colorInputs = [];
    const groups = document.createDocumentFragment();
    for (const [groupName, tokens] of COLOR_GROUPS) {
        const section = document.createElement('section');
        section.className = 'token-group';
        const heading = document.createElement('h3');
        heading.textContent = groupName;
        const grid = div('token-grid');
        for (const [token, label] of tokens) {
            const field = document.createElement('label');
            field.className = 'token-field';
            const input = document.createElement('input');
            input.type = 'color';
            input.dataset.token = token;
            input.setAttribute('aria-label', label);
            input.title = `Customize ${label} (${token}) and check the WCAG contrast ratios below`;
            input.addEventListener('input', () => setToken(token, input.value));
            const text = document.createElement('span');
            text.textContent = label;
            field.append(input, text);
            grid.append(field);
            colorInputs.push(input);
        }
        section.append(heading, grid);
        groups.append(section);
    }

    const contrastHeading = document.createElement('h3');
    contrastHeading.textContent = 'Contrast';
    const contrastList = document.createElement('ul');
    contrastList.className = 'contrast-list';
    const contrastRows = PAIRS.map(([label]) => {
        const row = document.createElement('li');
        row.className = 'contrast-row';
        const name = document.createElement('span');
        name.textContent = label;
        const value = document.createElement('span');
        row.append(name, value);
        contrastList.append(row);
        return value;
    });

    statusNode = document.createElement('p');
    statusNode.className = 'theme-status';
    statusNode.textContent = 'Changes save immediately.';

    const reset = buttonEl('Reset to preset');
    reset.title = 'Reset theme colors to the selected preset';
    reset.addEventListener('click', clearCustom);

    body.append(presetGrid, sample, sizes, groups, contrastHeading, contrastList, statusNode, reset);
    dialog.append(head, body);
    document.body.append(dialog);

    function syncControls() {
        for (const presetButton of presetButtons) {
            presetButton.setAttribute('aria-pressed', presetButton.dataset.preset === preset ? 'true' : 'false');
        }
        for (const input of colorInputs) {
            const hex = toHex(cssVar(input.dataset.token));
            if (hex && input.value.toLowerCase() !== hex.toLowerCase()) input.value = hex;
        }
        for (const input of sizeInputs) {
            if (document.activeElement === input) continue;
            const parsed = Number.parseFloat(cssVar(input.dataset.token));
            if (Number.isFinite(parsed)) input.value = String(parsed);
        }
        PAIRS.forEach(([label, foreground, background], index) => {
            const ratio = contrastRatio(cssVar(foreground), cssVar(background));
            const grade = contrastGrade(ratio);
            const text = formatContrast(ratio);
            contrastRows[index].textContent = text;
            contrastRows[index].className = `grade-${grade.toLowerCase().replace(/\s+/g, '-')}`;
            const row = contrastRows[index].parentElement;
            if (row) row.title = `${label}: ${text} (${grade}). WCAG contrast for this theme pair.`;
        });
    }

    function open() {
        syncControls();
        if (!dialog.open) dialog.showModal();
        close.focus();
    }

    subscribeTheme(() => syncControls());
    button?.addEventListener('click', open);
    syncControls();
    return { open };
}

function clearCustom() {
    if (Object.keys(custom).length === 0) return;
    custom = {};
    userSelected = true;
    themeTouched = true;
    applyTheme();
    emit();
    queuePersist();
}

function setToken(name, value) {
    if (!allowedValue(name, value)) return;
    custom = { ...custom, [name]: value };
    userSelected = true;
    themeTouched = true;
    applyTheme();
    emit();
    queuePersist();
}

function queuePersist() {
    writeLocal();
    window.clearTimeout(persistTimer);
    persistTimer = window.setTimeout(() => { void postTheme(); }, 200);
}

async function postTheme() {
    const body = payload();
    if (statusNode) statusNode.textContent = 'Saving theme…';
    try {
        const response = await fetch('/__studio/api/theme', {
            method: 'POST',
            headers: { Accept: 'application/json', 'Content-Type': 'application/json' },
            body: JSON.stringify(body),
            cache: 'no-store',
        });
        if (!response.ok) throw new Error('Theme save failed');
        const data = await response.json().catch(() => ({}));
        if (data && data.ok === false) throw new Error(data.error || data.message || 'Theme save failed');
        if (statusNode) statusNode.textContent = 'Saved to studio';
    } catch {
        if (statusNode) statusNode.textContent = 'Saved in this browser';
    }
}

function payload() {
    return {
        preset,
        custom: { ...custom },
        layout: layoutProvider() || {},
        userSelected: userSelected || themeTouched,
    };
}

function writeLocal() {
    try { localStorage.setItem(STORAGE_KEY, JSON.stringify(payload())); } catch { /* private mode */ }
}

function emit() {
    const theme = getTheme();
    for (const fn of listeners) fn(theme);
}

function applyTheme() {
    if (typeof document === 'undefined') return;
    const root = document.documentElement;
    root.dataset.theme = preset;
    for (const token of ALLOWED) root.style.removeProperty(token);
    for (const [token, value] of Object.entries(custom)) {
        if (ALLOWED.has(token)) root.style.setProperty(token, value);
    }
}

function readStored() {
    if (typeof localStorage === 'undefined') return null;
    try {
        const parsed = JSON.parse(localStorage.getItem(STORAGE_KEY) || 'null');
        if (!parsed || typeof parsed !== 'object') return null;
        return parsed;
    } catch {
        return null;
    }
}

function validPreset(id) {
    return PRESETS.some((item) => item.id === id) ? id : 'gold';
}

function sanitizeCustom(value) {
    const next = {};
    if (!value || typeof value !== 'object') return next;
    for (const [token, raw] of Object.entries(value)) {
        if (typeof raw === 'string' && allowedValue(token, raw)) next[token] = raw;
    }
    return next;
}

function allowedValue(token, value) {
    if (!ALLOWED.has(token) || typeof value !== 'string') return false;
    if (token.endsWith('width')) return /^\d+(\.\d+)?px$/.test(value);
    return /^#[0-9a-fA-F]{6}$/.test(value);
}

function cssVar(name) {
    if (typeof document === 'undefined') return '';
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

function parseColor(value) {
    const text = String(value || '').trim();
    const hex = text.match(/^#([0-9a-f]{3}|[0-9a-f]{6})$/i);
    if (hex) {
        const raw = hex[1];
        const full = raw.length === 3 ? raw.split('').map((part) => part + part).join('') : raw;
        return [
            Number.parseInt(full.slice(0, 2), 16),
            Number.parseInt(full.slice(2, 4), 16),
            Number.parseInt(full.slice(4, 6), 16),
        ];
    }
    const rgb = text.match(/^rgba?\(\s*([\d.]+)[,\s]+([\d.]+)[,\s]+([\d.]+)/i);
    if (!rgb) return null;
    return [Number(rgb[1]), Number(rgb[2]), Number(rgb[3])];
}

function relativeLuminance(rgb) {
    if (!rgb) return null;
    const channels = rgb.map((channel) => {
        const value = channel / 255;
        return value <= 0.03928 ? value / 12.92 : ((value + 0.055) / 1.055) ** 2.4;
    });
    return 0.2126 * channels[0] + 0.7152 * channels[1] + 0.0722 * channels[2];
}

function toHex(value) {
    const rgb = parseColor(value);
    if (!rgb) return '';
    return `#${rgb.map((channel) => Math.max(0, Math.min(255, Math.round(channel))).toString(16).padStart(2, '0')).join('')}`;
}

function div(className) {
    const node = document.createElement('div');
    node.className = className;
    return node;
}

function buttonEl(text) {
    const node = document.createElement('button');
    node.type = 'button';
    node.className = 'btn';
    node.textContent = text;
    return node;
}
