import { el } from './dom.js';
import { INSERT_TARGETS, macroWidgetId } from './macro-engine.js';

export function buildMacroToolbar({ macroCount = 0, showBuiltin = true, onNewMacro, onManageMacros } = {}) {
    const bar = el('div', { class: 'macro-panel-toolbar', role: 'toolbar', 'aria-label': 'Project Macros and Page Tools' });
    const copy = el('div', { class: 'macro-toolbar-meta' });
    copy.append(
        el('span', { class: 'macro-toolbar-title', text: 'Page Tools & Macros' }),
        el('span', {
            class: 'macro-toolbar-sub',
            text: `${macroCount} project macro${macroCount === 1 ? '' : 's'}${showBuiltin ? ' · Site tools on' : ' · Site tools hidden'}`,
        }),
    );
    const actions = el('div', { class: 'macro-toolbar-actions' });
    const addBtn = el('button', {
        class: 'btn btn-primary macro-toolbar-btn',
        type: 'button',
        text: '+ New Macro',
        title: 'Create a customizable project macro for this project',
    });
    addBtn.addEventListener('click', () => onNewMacro?.());
    const manageBtn = el('button', {
        class: 'btn macro-toolbar-btn',
        type: 'button',
        text: 'Manage',
        title: 'Manage project macros, presets, and built-in site widgets',
    });
    manageBtn.addEventListener('click', () => onManageMacros?.());
    actions.append(addBtn, manageBtn);
    bar.append(copy, actions);
    return bar;
}

export function readMacroCardValues(root, macro) {
    const widgetId = macroWidgetId(macro);
    const card = root?.querySelector(`[data-widget="${widgetId}"]`);
    const values = {};
    for (const field of macro?.fields || []) {
        const input = card?.querySelector(`[data-macro-field="${field.key}"]`);
        if (!input) {
            values[field.key] = field.defaultValue ?? '';
            continue;
        }
        if (field.type === 'checkbox') {
            values[field.key] = input.checked ? 'true' : 'false';
        } else {
            values[field.key] = input.value;
        }
    }
    return values;
}

export function buildMacroCard(evaluation, helpers) {
    const { macro, present, range, values, issues, statusLabel, statusTone } = evaluation;
    const widgetId = macroWidgetId(macro);
    const targetMeta = INSERT_TARGETS.find((t) => t.value === macro.insertTarget) || INSERT_TARGETS[0];
    const body = el('div', { class: 'widget-body macro-card-body' });

    const metaLine = [
        macro.category ? `[${macro.category}]` : '',
        macro.description || '',
        `Target: ${targetMeta.label}`,
    ].filter(Boolean).join(' · ');
    body.append(el('p', { class: 'field-hint macro-card-hint', text: metaLine }));

    if (issues.length > 0) {
        const alertBox = el('div', { class: 'macro-issue-banner', 'data-severity': macro.validation?.severity || 'warning' });
        for (const issue of issues) {
            alertBox.append(el('p', { class: 'macro-issue-line', text: `• ${issue.title}: ${issue.message}` }));
        }
        body.append(alertBox);
    }

    for (const field of macro.fields) {
        body.append(renderMacroFieldInput(field, values[field.key], helpers.doc));
    }

    const actions = el('div', { class: 'widget-actions macro-card-actions' });
    const applyBtn = el('button', {
        class: 'btn btn-primary',
        type: 'button',
        text: present ? 'Apply / Sync' : `Insert (${targetMeta.label})`,
        title: present
            ? `Update the ${macro.title} block in-place in this page`
            : `Insert ${macro.title} at ${targetMeta.label}`,
        'data-action': widgetId,
    });
    applyBtn.addEventListener('click', () => {
        helpers.onApplyMacro(macro, 'apply');
    });
    actions.append(applyBtn);

    const cursorBtn = el('button', {
        class: 'btn',
        type: 'button',
        text: 'Insert at Cursor',
        title: `Insert ${macro.title} directly at the active cursor position in the code editor`,
        'data-action': widgetId,
    });
    cursorBtn.addEventListener('click', () => {
        helpers.onApplyMacro(macro, 'insert-cursor');
    });
    actions.append(cursorBtn);

    if (present && range) {
        const jumpBtn = el('button', {
            class: 'btn',
            type: 'button',
            text: 'Jump to Code',
            title: `Highlight ${macro.title} in the code editor`,
        });
        jumpBtn.addEventListener('click', () => {
            helpers.onJumpRange(range, macro.title);
        });
        actions.append(jumpBtn);
    }

    const editBtn = el('button', {
        class: 'btn',
        type: 'button',
        text: '⚙ Edit Macro',
        title: `Customize template, fields, and validation rules for ${macro.title}`,
    });
    editBtn.addEventListener('click', () => {
        helpers.onEditMacro(macro);
    });
    actions.append(editBtn);

    if (present) {
        const removeBtn = el('button', {
            class: 'btn',
            type: 'button',
            text: 'Remove',
            title: `Remove ${macro.title} from this page`,
            'data-action': widgetId,
        });
        removeBtn.addEventListener('click', () => {
            helpers.onApplyMacro(macro, 'remove');
        });
        actions.append(removeBtn);
    }

    body.append(actions);

    return helpers.shell(widgetId, macro.title, {
        idle: !present,
        badge: statusLabel,
        tone: statusTone,
        body,
        extraClass: 'widget-macro',
    });
}

function renderMacroFieldInput(field, rawValue, doc) {
    const val = rawValue !== undefined && rawValue !== null ? String(rawValue) : String(field.defaultValue ?? '');
    const labelText = field.required ? `${field.label} *` : field.label;

    if (field.type === 'checkbox') {
        const wrap = el('label', { class: 'field check', title: field.hint || field.label });
        const input = el('input', {
            type: 'checkbox',
            'data-macro-field': field.key,
        });
        input.checked = val === 'true' || val === '1';
        wrap.append(input, document.createTextNode(` ${labelText}`));
        return wrap;
    }

    const wrap = el('label', { class: 'field', title: field.hint || field.label });
    wrap.append(document.createTextNode(labelText));

    if (field.type === 'textarea') {
        const area = el('textarea', {
            'data-macro-field': field.key,
            rows: '3',
            placeholder: field.defaultValue || '',
        });
        area.value = val;
        wrap.append(area);
    } else if (field.type === 'select') {
        const select = el('select', { 'data-macro-field': field.key });
        const opts = Array.isArray(field.options) ? field.options.slice() : [];
        if (val && !opts.includes(val)) opts.unshift(val);
        if (!field.required && !opts.includes('')) opts.unshift('');
        for (const opt of opts) {
            const option = el('option', { value: opt, text: opt || '(blank)' });
            if (opt === val) option.selected = true;
            select.append(option);
        }
        wrap.append(select);
    } else {
        const inputType = field.type === 'date' && (/^\d{4}-\d{2}-\d{2}$/.test(val) || !val)
            ? 'date'
            : field.type === 'number'
                ? 'number'
                : 'text';
        const input = el('input', {
            type: inputType,
            'data-macro-field': field.key,
            value: val,
            placeholder: field.defaultValue || '',
            autocomplete: 'off',
            spellcheck: 'false',
        });
        if (field.type === 'image' && doc) {
            const listId = `macro-img-${field.key}`;
            input.setAttribute('list', listId);
            const dl = el('datalist', { id: listId });
            for (const img of ['shareimage.jpg', 'folderthumb.jpg', ...(doc.images || []).map((i) => `slides/${i.name}`)]) {
                dl.append(el('option', { value: img }));
            }
            wrap.append(dl);
        }
        wrap.append(input);
    }

    if (field.hint || field.pattern) {
        const hintParts = [];
        if (field.hint) hintParts.push(field.hint);
        if (field.pattern) hintParts.push(`Format: ${field.pattern}`);
        wrap.append(el('p', { class: 'field-hint', text: hintParts.join(' · ') }));
    }
    return wrap;
}
