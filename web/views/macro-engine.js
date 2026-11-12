export const INSERT_TARGETS = [
    { value: 'cursor', label: 'At Cursor Position', hint: 'Insert directly at the active cursor in the code editor' },
    { value: 'head-end', label: 'Before </head>', hint: 'Insert at the bottom of the <head> block' },
    { value: 'head-start', label: 'Inside <head> (top)', hint: 'Insert right after the opening <head> tag' },
    { value: 'body-start', label: 'After <body> (top)', hint: 'Insert right after the opening <body> tag' },
    { value: 'body-end', label: 'Before </body> (bottom)', hint: 'Insert right before the closing </body> tag' },
    { value: 'top', label: 'Start of File', hint: 'Insert at the very top of the file' },
    { value: 'bottom', label: 'End of File', hint: 'Insert at the very end of the file' },
];

export const FIELD_TYPES = [
    { value: 'text', label: 'Text Input' },
    { value: 'textarea', label: 'Multiline Text' },
    { value: 'select', label: 'Dropdown Select' },
    { value: 'checkbox', label: 'Checkbox (true / false)' },
    { value: 'date', label: 'Date (YYYY-MM-DD)' },
    { value: 'number', label: 'Number' },
    { value: 'image', label: 'Image / Asset Path' },
];

export const FALLBACK_PRESETS = [
    {
        id: 'page-meta',
        title: 'Page Metadata Comment',
        description: 'Structured HTML comment block for page metadata (author, status, date, notes).',
        category: 'Metadata',
        enabled: true,
        insertTarget: 'head-end',
        startMarker: '<!-- page-meta',
        endMarker: '-->',
        appliesTo: { extensions: '.html, .htm, .shtml, .php', sections: '*' },
        template: '<!-- page-meta\n  author: {{author}}\n  status: {{status}}\n  updated: {{updated}}\n  notes: {{notes}}\n-->',
        fields: [
            { key: 'author', label: 'Author', type: 'text', defaultValue: '', required: true, pattern: '', hint: 'Page author or editor', options: [] },
            { key: 'status', label: 'Status', type: 'select', defaultValue: 'draft', required: true, pattern: '', hint: 'Editorial status', options: ['draft', 'review', 'published', 'archived'] },
            { key: 'updated', label: 'Updated Date', type: 'date', defaultValue: '', required: false, pattern: '^\\d{4}-\\d{2}-\\d{2}$', hint: 'YYYY-MM-DD', options: [] },
            { key: 'notes', label: 'Notes', type: 'textarea', defaultValue: '', required: false, pattern: '', hint: 'Optional notes', options: [] },
        ],
        validation: { requiredOnPage: false, checkFormatting: true, severity: 'warning' },
    },
    {
        id: 'ssi-include',
        title: 'Server-Side Include (SSI)',
        description: 'Virtual include directive for shared site headers, footers, or embeds.',
        category: 'Include',
        enabled: true,
        insertTarget: 'cursor',
        startMarker: '<!--#include virtual="',
        endMarker: '"-->',
        appliesTo: { extensions: '.html, .htm, .shtml, .php', sections: '*' },
        template: '<!--#include virtual="{{includePath}}"-->',
        fields: [
            { key: 'includePath', label: 'Virtual Path', type: 'text', defaultValue: '/includes/header.html', required: true, pattern: '^/[A-Za-z0-9_./?=&-]+$', hint: 'Root-relative path starting with /', options: [] },
        ],
        validation: { requiredOnPage: false, checkFormatting: true, severity: 'error' },
    },
    {
        id: 'callout-section',
        title: 'Callout / Feature Section',
        description: 'Customizable HTML section snippet with CSS class, heading, and body copy.',
        category: 'Snippet',
        enabled: true,
        insertTarget: 'cursor',
        startMarker: '<!-- macro:callout-start -->',
        endMarker: '<!-- macro:callout-end -->',
        appliesTo: { extensions: '.html, .htm, .shtml, .php', sections: '*' },
        template: '<!-- macro:callout-start -->\n<section class="{{className}}">\n  <h2>{{heading}}</h2>\n  <p>{{body}}</p>\n</section>\n<!-- macro:callout-end -->',
        fields: [
            { key: 'className', label: 'CSS Class', type: 'text', defaultValue: 'feature-callout', required: true, pattern: '^[A-Za-z0-9_ -]+$', hint: 'Section class name(s)', options: [] },
            { key: 'heading', label: 'Heading', type: 'text', defaultValue: 'Featured Note', required: true, pattern: '', hint: 'Section heading', options: [] },
            { key: 'body', label: 'Body Text', type: 'textarea', defaultValue: 'Add your callout details here.', required: false, pattern: '', hint: 'Paragraph content', options: [] },
        ],
        validation: { requiredOnPage: false, checkFormatting: true, severity: 'warning' },
    },
    {
        id: 'custom-analytics',
        title: 'Analytics / Tracking Script',
        description: 'Custom analytics or tracking script tag injected before </head>.',
        category: 'Tracking',
        enabled: true,
        insertTarget: 'head-end',
        startMarker: '<!-- macro:analytics-start -->',
        endMarker: '<!-- macro:analytics-end -->',
        appliesTo: { extensions: '.html, .htm, .shtml, .php', sections: '*' },
        template: '<!-- macro:analytics-start -->\n<script defer data-domain="{{domain}}" src="{{scriptUrl}}"></script>\n<!-- macro:analytics-end -->',
        fields: [
            { key: 'domain', label: 'Site Domain', type: 'text', defaultValue: 'example.com', required: true, pattern: '^[A-Za-z0-9.-]+\\.[A-Za-z]{2,}$', hint: 'Tracked domain name', options: [] },
            { key: 'scriptUrl', label: 'Script URL', type: 'text', defaultValue: 'https://plausible.io/js/script.js', required: true, pattern: '^https?://.+', hint: 'Full https:// URL to script', options: [] },
        ],
        validation: { requiredOnPage: false, checkFormatting: true, severity: 'warning' },
    },
    {
        id: 'php-page-vars',
        title: 'PHP Page Variables',
        description: 'Top-of-file PHP configuration block for template variables.',
        category: 'PHP',
        enabled: true,
        insertTarget: 'top',
        startMarker: '/* macro:php-vars-start */',
        endMarker: '/* macro:php-vars-end */',
        appliesTo: { extensions: '.php', sections: '*' },
        template: "<?php\n/* macro:php-vars-start */\n$pageTitle = '{{pageTitle}}';\n$pageLayout = '{{pageLayout}}';\n/* macro:php-vars-end */\n?>",
        fields: [
            { key: 'pageTitle', label: 'Page Title ($pageTitle)', type: 'text', defaultValue: '', required: true, pattern: '', hint: 'PHP $pageTitle value', options: [] },
            { key: 'pageLayout', label: 'Layout ($pageLayout)', type: 'select', defaultValue: 'default', required: true, pattern: '', hint: 'Template layout', options: ['default', 'wide', 'landing', 'article'] },
        ],
        validation: { requiredOnPage: false, checkFormatting: true, severity: 'error' },
    },
];

export function sanitizeMacroSlug(text, fallback = 'macro') {
    const clean = String(text || '')
        .trim()
        .replace(/[^A-Za-z0-9_-]+/g, '-')
        .replace(/^-+|-+$/g, '');
    const prefixed = /^[A-Za-z]/.test(clean) ? clean : `${fallback}-${clean || 'item'}`;
    return prefixed.slice(0, 52);
}

export function macroWidgetId(macroOrId) {
    const rawId = typeof macroOrId === 'string' ? macroOrId : macroOrId?.id;
    return `macro-${sanitizeMacroSlug(rawId, 'custom')}`;
}

export function isMacroWidgetId(widgetId) {
    return typeof widgetId === 'string' && widgetId.startsWith('macro-') && widgetId.length > 6;
}

export function macroIdFromWidget(widgetId) {
    return isMacroWidgetId(widgetId) ? widgetId.slice(6) : '';
}

export function normalizeMacroField(raw = {}, index = 0) {
    const key = sanitizeMacroSlug(raw.key || raw.label || `field_${index + 1}`, 'field').replace(/-/g, '_');
    const rawOpts = raw.options;
    const options = Array.isArray(rawOpts)
        ? rawOpts.map((item) => String(item ?? '').trim()).filter(Boolean)
        : String(rawOpts || '').split(',').map((item) => item.trim()).filter(Boolean);
    return {
        key,
        label: String(raw.label || key).trim() || key,
        type: FIELD_TYPES.some((t) => t.value === raw.type) ? raw.type : 'text',
        defaultValue: String(raw.defaultValue ?? ''),
        required: Boolean(raw.required),
        pattern: String(raw.pattern || '').trim(),
        hint: String(raw.hint || '').trim(),
        options,
    };
}

export function normalizeMacroDef(raw = {}) {
    const title = String(raw.title || 'Custom Macro').trim() || 'Custom Macro';
    const id = sanitizeMacroSlug(raw.id || title, 'macro');
    const fields = Array.isArray(raw.fields)
        ? raw.fields.map((f, idx) => normalizeMacroField(f, idx))
        : [];
    const insertTarget = INSERT_TARGETS.some((t) => t.value === raw.insertTarget) ? raw.insertTarget : 'cursor';
    const applies = raw.appliesTo && typeof raw.appliesTo === 'object' ? raw.appliesTo : {};
    const val = raw.validation && typeof raw.validation === 'object' ? raw.validation : {};
    return {
        id,
        title,
        description: String(raw.description || '').trim(),
        category: String(raw.category || 'Macro').trim() || 'Macro',
        enabled: raw.enabled !== false,
        insertTarget,
        startMarker: String(raw.startMarker || ''),
        endMarker: String(raw.endMarker || ''),
        appliesTo: {
            extensions: String(applies.extensions || '.html, .htm, .shtml, .php').trim() || '*',
            sections: String(applies.sections || '*').trim() || '*',
        },
        template: String(raw.template || `<!-- ${id} -->`),
        fields,
        validation: {
            requiredOnPage: Boolean(val.requiredOnPage),
            checkFormatting: val.checkFormatting !== false,
            severity: val.severity === 'error' ? 'error' : 'warning',
        },
    };
}

export function macroAppliesToDoc(macro, doc) {
    if (!macro || macro.enabled === false || !doc || !doc.path || doc.isMedia) return false;
    const path = String(doc.path || '').toLowerCase();
    const extSpec = String(macro.appliesTo?.extensions || '*').trim().toLowerCase();
    if (extSpec && extSpec !== '*') {
        const exts = extSpec.split(/[\s,]+/).filter(Boolean).map((e) => (e.startsWith('.') ? e : `.${e}`));
        if (exts.length && !exts.some((ext) => path.endsWith(ext))) return false;
    }
    const secSpec = String(macro.appliesTo?.sections || '*').trim().toLowerCase();
    if (secSpec && secSpec !== '*') {
        const secs = secSpec.split(/[\s,]+/).filter(Boolean).map((s) => s.replace(/^\/+|\/+$/g, ''));
        const docSec = String(doc.section || '').toLowerCase().replace(/^\/+|\/+$/g, '');
        const cleanPath = path.replace(/^\/+/, '');
        const matchSec = secs.some((s) => docSec === s || cleanPath.startsWith(`${s}/`) || cleanPath === s);
        if (secs.length && !matchSec) return false;
    }
    return true;
}

function escapeRegex(str) {
    return String(str || '').replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function buildTemplateRegex(template, { anchored = false } = {}) {
    const keys = [];
    const tokenRe = /\{\{\s*([A-Za-z][A-Za-z0-9_-]*)\s*\}\}/g;
    let last = 0;
    let pattern = '';
    let match = tokenRe.exec(template);
    while (match) {
        const literal = template.slice(last, match.index);
        pattern += escapeRegex(literal).replace(/\r?\n/g, '\\r?\\n');
        keys.push(match[1]);
        pattern += '([\\s\\S]*?)';
        last = match.index + match[0].length;
        match = tokenRe.exec(template);
    }
    pattern += escapeRegex(template.slice(last)).replace(/\r?\n/g, '\\r?\\n');
    try {
        return { regex: new RegExp(anchored ? `^${pattern}$` : pattern), keys };
    } catch {
        return null;
    }
}

export function locateMacroBlock(macro, source) {
    const text = String(source || '');
    if (!text || !macro) return null;
    const startMarker = String(macro.startMarker || '').trim();
    const endMarker = String(macro.endMarker || '').trim();
    if (startMarker) {
        const startIdx = text.indexOf(startMarker);
        if (startIdx >= 0) {
            if (endMarker) {
                const searchFrom = startIdx + startMarker.length;
                const endIdx = text.indexOf(endMarker, searchFrom);
                if (endIdx >= 0) {
                    const endOffset = endIdx + endMarker.length;
                    return {
                        startOffset: startIdx,
                        endOffset,
                        raw: text.slice(startIdx, endOffset),
                    };
                }
            } else {
                const lineEnd = text.indexOf('\n', startIdx);
                const endOffset = lineEnd >= 0 ? lineEnd : text.length;
                return {
                    startOffset: startIdx,
                    endOffset,
                    raw: text.slice(startIdx, endOffset),
                };
            }
        }
    }
    const compiled = buildTemplateRegex(String(macro.template || '').trim(), { anchored: false });
    if (compiled && compiled.regex) {
        const m = compiled.regex.exec(text);
        if (m && m[0]) {
            return {
                startOffset: m.index,
                endOffset: m.index + m[0].length,
                raw: m[0],
            };
        }
    }
    return null;
}

export function parseMacroFields(macro, rawBlock) {
    const values = {};
    const fields = Array.isArray(macro?.fields) ? macro.fields : [];
    for (const f of fields) {
        values[f.key] = f.defaultValue ?? '';
    }
    if (!rawBlock) return values;
    const trimmedRaw = String(rawBlock).replace(/\r\n/g, '\n').trim();
    const trimmedTpl = String(macro?.template || '').replace(/\r\n/g, '\n').trim();
    const compiled = buildTemplateRegex(trimmedTpl, { anchored: true });
    if (compiled && compiled.regex) {
        const m = compiled.regex.exec(trimmedRaw);
        if (m) {
            compiled.keys.forEach((key, idx) => {
                if (values[key] === undefined || m[idx + 1] !== undefined) {
                    values[key] = m[idx + 1] ?? '';
                }
            });
            return values;
        }
    }
    for (const f of fields) {
        const keyEsc = escapeRegex(f.key);
        const tokenRe = new RegExp(`([\\s\\S]{0,24})\\{\\{\\s*${keyEsc}\\s*\\}\\}([\\s\\S]{0,24})`);
        const ctxMatch = tokenRe.exec(trimmedTpl);
        let extracted = null;
        if (ctxMatch) {
            const before = ctxMatch[1].split('\n').pop() || '';
            const after = (ctxMatch[2].split('\n')[0] || '');
            if (before || after) {
                try {
                    const pairRe = new RegExp(`${escapeRegex(before)}([^\\r\\n]*?)${after ? escapeRegex(after) : '$'}`);
                    const pm = pairRe.exec(trimmedRaw);
                    if (pm) extracted = pm[1];
                } catch { /* fallback below */ }
            }
        }
        if (extracted === null) {
            const kvRe = new RegExp(`(?:^|\\n|\\s|\\$)${keyEsc}\\s*[:=]\\s*(?:'([^']*)'|"([^"]*)"|([^;\\r\\n]+))`, 'i');
            const km = kvRe.exec(trimmedRaw);
            if (km) extracted = (km[1] ?? km[2] ?? km[3] ?? '').trim();
        }
        if (extracted !== null) {
            values[f.key] = extracted;
        }
    }
    return values;
}

export function renderMacroTemplate(macro, values = {}) {
    const tpl = String(macro?.template || '');
    const fieldMap = new Map((macro?.fields || []).map((f) => [f.key, f]));
    return tpl.replace(/\{\{\s*([A-Za-z][A-Za-z0-9_-]*)\s*\}\}/g, (_, key) => {
        const field = fieldMap.get(key);
        const rawVal = values[key];
        if (field?.type === 'checkbox') {
            if (typeof rawVal === 'boolean') return rawVal ? 'true' : 'false';
            if (typeof rawVal === 'string' && rawVal !== '') return rawVal;
            return field.defaultValue || 'false';
        }
        if (rawVal !== undefined && rawVal !== null) return String(rawVal);
        return field?.defaultValue ?? '';
    });
}

export function evaluateMacro(macro, doc, sourceOverride = null) {
    const norm = normalizeMacroDef(macro);
    const applies = macroAppliesToDoc(norm, doc);
    const source = typeof sourceOverride === 'string' ? sourceOverride : String(doc?.content || '');
    const block = locateMacroBlock(norm, source);
    const present = Boolean(block);
    const values = present ? parseMacroFields(norm, block.raw) : parseMacroFields(norm, '');
    const rendered = renderMacroTemplate(norm, values);
    const normRaw = block ? block.raw.replace(/\r\n/g, '\n').trim() : '';
    const normRendered = rendered.replace(/\r\n/g, '\n').trim();
    const formattingOutOfSync = Boolean(
        present
        && norm.validation.checkFormatting !== false
        && normRaw !== normRendered,
    );
    const issues = [];
    const severity = norm.validation.severity === 'error' ? 'error' : 'warning';

    if (applies) {
        if (!present) {
            if (norm.validation.requiredOnPage) {
                issues.push({
                    id: `macro-missing:${norm.id}`,
                    macroId: norm.id,
                    severity,
                    title: `${norm.title} is missing`,
                    message: `Required project macro "${norm.title}" is not present on this page.`,
                    fixable: true,
                });
            }
        } else {
            for (const field of norm.fields) {
                const val = String(values[field.key] ?? '').trim();
                if (field.required && !val) {
                    issues.push({
                        id: `macro-field-required:${norm.id}:${field.key}`,
                        macroId: norm.id,
                        severity,
                        title: `${norm.title}: ${field.label} is empty`,
                        message: `Required field "${field.label}" (${field.key}) in ${norm.title} is missing a value.`,
                        fixable: true,
                    });
                } else if (val && field.pattern) {
                    let regexOk = true;
                    try {
                        regexOk = new RegExp(field.pattern).test(val);
                    } catch {
                        regexOk = true;
                    }
                    if (!regexOk) {
                        issues.push({
                            id: `macro-field-pattern:${norm.id}:${field.key}`,
                            macroId: norm.id,
                            severity,
                            title: `${norm.title}: ${field.label} format invalid`,
                            message: `Value "${val}" does not match pattern ${field.pattern}${field.hint ? ` (${field.hint})` : ''}.`,
                            fixable: true,
                        });
                    }
                }
            }
            if (formattingOutOfSync) {
                issues.push({
                    id: `macro-format:${norm.id}`,
                    macroId: norm.id,
                    severity,
                    title: `${norm.title}: Macro formatting out of sync`,
                    message: `The ${norm.title} block in this page does not match the configured template formatting.`,
                    fixable: true,
                });
            }
        }
    }

    let statusLabel = 'Not in page';
    let statusTone = '';
    if (present) {
        if (issues.some((i) => i.id.startsWith('macro-field-'))) {
            statusLabel = 'Field Issue';
            statusTone = severity === 'error' ? 'danger' : 'warn';
        } else if (formattingOutOfSync) {
            statusLabel = 'Formatting Issue';
            statusTone = 'warn';
        } else {
            statusLabel = 'Active';
            statusTone = 'ok';
        }
    } else if (applies && norm.validation.requiredOnPage) {
        statusLabel = 'Missing';
        statusTone = severity === 'error' ? 'danger' : 'warn';
    }

    return {
        macro: norm,
        applies,
        present,
        range: block ? { startOffset: block.startOffset, endOffset: block.endOffset } : null,
        values,
        rendered,
        formattingOutOfSync,
        statusLabel,
        statusTone,
        issues,
    };
}

export function evaluateAllMacros(macros, doc, sourceOverride = null) {
    const evaluations = [];
    const byId = new Map();
    const issues = [];
    if (!doc || !Array.isArray(macros)) return { evaluations, byId, issues };
    for (const raw of macros) {
        const res = evaluateMacro(raw, doc, sourceOverride);
        if (!res.applies && !res.present) continue;
        evaluations.push(res);
        byId.set(res.macro.id, res);
        for (const issue of res.issues) issues.push(issue);
    }
    return { evaluations, byId, issues };
}

function insertAtTarget(source, snippet, target, cursorOffset = -1) {
    const text = String(source || '');
    const clean = snippet.replace(/\s+$/, '');
    const clampCursor = Number.isFinite(cursorOffset) && cursorOffset >= 0
        ? Math.min(text.length, Math.max(0, Math.floor(cursorOffset)))
        : -1;

    if (target === 'cursor' && clampCursor >= 0) {
        const before = text.slice(0, clampCursor);
        const after = text.slice(clampCursor);
        const prefix = before.length && !before.endsWith('\n') ? '\n' : '';
        const suffix = after.length && !after.startsWith('\n') ? '\n' : '';
        const next = `${before}${prefix}${clean}${suffix}${after}`;
        const start = before.length + prefix.length;
        return { content: next, range: { startOffset: start, endOffset: start + clean.length } };
    }
    if (target === 'head-end') {
        const m = /<\/head\s*>/i.exec(text);
        if (m) {
            const idx = m.index;
            const before = text.slice(0, idx);
            const prefix = before.length && !before.endsWith('\n') ? '\n' : '';
            const next = `${before}${prefix}${clean}\n${text.slice(idx)}`;
            const start = before.length + prefix.length;
            return { content: next, range: { startOffset: start, endOffset: start + clean.length } };
        }
    }
    if (target === 'head-start') {
        const m = /<head(?:\s[^>]*)?>/i.exec(text);
        if (m) {
            const idx = m.index + m[0].length;
            const next = `${text.slice(0, idx)}\n${clean}${text.slice(idx)}`;
            const start = idx + 1;
            return { content: next, range: { startOffset: start, endOffset: start + clean.length } };
        }
    }
    if (target === 'body-start') {
        const m = /<body(?:\s[^>]*)?>/i.exec(text);
        if (m) {
            const idx = m.index + m[0].length;
            const next = `${text.slice(0, idx)}\n${clean}${text.slice(idx)}`;
            const start = idx + 1;
            return { content: next, range: { startOffset: start, endOffset: start + clean.length } };
        }
    }
    if (target === 'body-end') {
        const m = /<\/body\s*>/i.exec(text);
        if (m) {
            const idx = m.index;
            const before = text.slice(0, idx);
            const prefix = before.length && !before.endsWith('\n') ? '\n' : '';
            const next = `${before}${prefix}${clean}\n${text.slice(idx)}`;
            const start = before.length + prefix.length;
            return { content: next, range: { startOffset: start, endOffset: start + clean.length } };
        }
    }
    if (target === 'bottom') {
        const prefix = text.length && !text.endsWith('\n') ? '\n' : '';
        const next = `${text}${prefix}${clean}\n`;
        const start = text.length + prefix.length;
        return { content: next, range: { startOffset: start, endOffset: start + clean.length } };
    }
    const next = text.length ? `${clean}\n${text}` : `${clean}\n`;
    return { content: next, range: { startOffset: 0, endOffset: clean.length } };
}

export function applyMacroToSource(macro, source, inputValues = {}, options = {}) {
    const norm = normalizeMacroDef(macro);
    const text = String(source || '');
    const mode = options.mode || 'apply';
    const existing = locateMacroBlock(norm, text);

    if (mode === 'remove') {
        if (!existing) return { content: text, range: null, label: `Removed ${norm.title}` };
        let start = existing.startOffset;
        let end = existing.endOffset;
        if (text[end] === '\r' && text[end + 1] === '\n') end += 2;
        else if (text[end] === '\n') end += 1;
        else if (start > 0 && text[start - 1] === '\n') start -= 1;
        return {
            content: `${text.slice(0, start)}${text.slice(end)}`,
            range: null,
            label: `Removed ${norm.title}`,
        };
    }

    const parsed = existing ? parseMacroFields(norm, existing.raw) : parseMacroFields(norm, '');
    const mergedValues = { ...parsed, ...(inputValues || {}) };

    if (mode === 'repair') {
        for (const field of norm.fields) {
            const cur = String(mergedValues[field.key] ?? '').trim();
            if (field.required && !cur) {
                mergedValues[field.key] = field.defaultValue || (field.options?.[0] ?? 'value');
            } else if (cur && field.pattern) {
                try {
                    if (!new RegExp(field.pattern).test(cur) && field.defaultValue) {
                        mergedValues[field.key] = field.defaultValue;
                    }
                } catch { /* ignore invalid regex */ }
            }
        }
    }

    const rendered = renderMacroTemplate(norm, mergedValues).replace(/\s+$/, '');

    if (mode === 'insert-cursor') {
        const res = insertAtTarget(text, rendered, 'cursor', options.cursorOffset ?? 0);
        return {
            content: res.content,
            range: res.range,
            label: `Inserted ${norm.title} at cursor`,
        };
    }

    if (existing) {
        const next = `${text.slice(0, existing.startOffset)}${rendered}${text.slice(existing.endOffset)}`;
        return {
            content: next,
            range: {
                startOffset: existing.startOffset,
                endOffset: existing.startOffset + rendered.length,
            },
            label: `Updated ${norm.title}`,
        };
    }

    const res = insertAtTarget(text, rendered, norm.insertTarget, options.cursorOffset ?? -1);
    return {
        content: res.content,
        range: res.range,
        label: `Inserted ${norm.title}`,
    };
}

