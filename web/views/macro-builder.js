import { el } from './dom.js';
import {
    FALLBACK_PRESETS,
    FIELD_TYPES,
    INSERT_TARGETS,
    normalizeMacroDef,
    normalizeMacroField,
    renderMacroTemplate,
    sanitizeMacroSlug,
} from './macro-engine.js';

export function mountMacroBuilder(ctx) {
    const dialog = el('dialog', {
        class: 'macro-builder-dialog',
        'aria-labelledby': 'macro-builder-title',
    });
    let draft = createBlankMacro();
    let isNew = true;
    let statusMsg = '';

    const head = el('div', { class: 'dialog-head macro-builder-head' });
    const titleEl = el('h2', { id: 'macro-builder-title', text: 'Project Macros & Page Tools' });
    const closeBtn = el('button', { class: 'btn', type: 'button', text: 'Close', title: 'Close Project Macros dialog' });
    closeBtn.addEventListener('click', () => dialog.close());
    head.append(titleEl, closeBtn);

    const body = el('div', { class: 'dialog-body macro-builder-layout' });
    const sidebar = el('aside', { class: 'macro-builder-sidebar' });
    const mainPane = el('section', { class: 'macro-builder-main' });
    body.append(sidebar, mainPane);
    dialog.append(head, body);
    document.body.append(dialog);

    function createBlankMacro() {
        return normalizeMacroDef({
            id: `custom-${Math.random().toString(36).slice(2, 6)}`,
            title: 'Custom Page Macro',
            description: 'Custom block with editable fields and formatting check.',
            category: 'Macro',
            enabled: true,
            insertTarget: 'cursor',
            startMarker: '<!-- macro:custom-start -->',
            endMarker: '<!-- macro:custom-end -->',
            appliesTo: { extensions: '.html, .htm, .shtml, .php', sections: '*' },
            template: '<!-- macro:custom-start -->\n<div class="custom-block">\n  <h3>{{heading}}</h3>\n</div>\n<!-- macro:custom-end -->',
            fields: [
                { key: 'heading', label: 'Heading', type: 'text', defaultValue: 'Section Heading', required: true, pattern: '', hint: '', options: [] },
            ],
            validation: { requiredOnPage: false, checkFormatting: true, severity: 'warning' },
        });
    }

    function open({ macroId = '', createNew = false } = {}) {
        statusMsg = '';
        const macros = ctx.getMacros?.() || [];
        if (createNew) {
            draft = createBlankMacro();
            isNew = true;
        } else if (macroId) {
            const found = macros.find((m) => m.id === macroId);
            if (found) {
                draft = normalizeMacroDef(structuredClone ? structuredClone(found) : JSON.parse(JSON.stringify(found)));
                isNew = false;
            } else {
                draft = createBlankMacro();
                isNew = true;
            }
        } else if (macros.length > 0) {
            draft = normalizeMacroDef(JSON.parse(JSON.stringify(macros[0])));
            isNew = false;
        } else {
            draft = createBlankMacro();
            isNew = true;
        }
        renderAll();
        if (!dialog.open) dialog.showModal();
    }

    function renderAll() {
        renderSidebar();
        renderEditor();
    }

    function renderSidebar() {
        sidebar.replaceChildren();
        const macros = ctx.getMacros?.() || [];
        const presets = ctx.getPresets?.()?.length ? ctx.getPresets() : FALLBACK_PRESETS;
        const showBuiltin = ctx.getShowBuiltin?.() !== false;
        const projectRoot = ctx.getProjectRoot?.() || 'Current Project';

        const projBox = el('div', { class: 'macro-side-section' });
        projBox.append(
            el('h3', { text: 'Project Scope' }),
            el('p', { class: 'field-hint macro-root-path', text: projectRoot, title: projectRoot }),
        );
        const builtinWrap = el('label', {
            class: 'field check macro-builtin-toggle',
            title: 'Show or hide ClassicCars-specific built-in widgets (Sales PHP, eXTReMe, etc.) in the right panel for this project',
        });
        const builtinInput = el('input', { type: 'checkbox' });
        builtinInput.checked = showBuiltin;
        builtinInput.addEventListener('change', async () => {
            await ctx.onSaveMacros?.({ showBuiltinWidgets: builtinInput.checked });
            statusMsg = builtinInput.checked
                ? 'Enabled built-in ClassicCars tools for this project.'
                : 'Hidden built-in ClassicCars tools for this project.';
            renderAll();
        });
        builtinWrap.append(builtinInput, document.createTextNode(' Show built-in ClassicCars widgets'));
        projBox.append(builtinWrap);
        sidebar.append(projBox);

        const activeBox = el('div', { class: 'macro-side-section' });
        const activeHead = el('div', { class: 'macro-side-head' });
        activeHead.append(el('h3', { text: `Project Macros (${macros.length})` }));
        const newBtn = el('button', {
            class: 'btn btn-primary',
            type: 'button',
            text: '+ New',
            title: 'Create a new custom macro from scratch',
        });
        newBtn.addEventListener('click', () => {
            draft = createBlankMacro();
            isNew = true;
            statusMsg = '';
            renderAll();
        });
        activeHead.append(newBtn);
        activeBox.append(activeHead);

        const list = el('div', { class: 'macro-side-list' });
        if (!macros.length) {
            list.append(el('p', { class: 'field-hint', text: 'No project macros yet. Create one or add a preset below.' }));
        }
        for (const item of macros) {
            const active = !isNew && item.id === draft.id;
            const row = el('button', {
                class: `macro-side-item${active ? ' is-active' : ''}`,
                type: 'button',
                title: item.description || item.title,
            });
            row.append(
                el('strong', { text: item.title }),
                el('span', { class: 'macro-side-meta', text: `${item.category || 'Macro'} · ${item.fields?.length || 0} fields` }),
            );
            row.addEventListener('click', () => {
                draft = normalizeMacroDef(JSON.parse(JSON.stringify(item)));
                isNew = false;
                statusMsg = '';
                renderAll();
            });
            list.append(row);
        }
        activeBox.append(list);
        sidebar.append(activeBox);

        const presetBox = el('div', { class: 'macro-side-section' });
        presetBox.append(
            el('h3', { text: 'Starter Presets' }),
            el('p', { class: 'field-hint', text: 'Click a preset to add it to this project or customize it.' }),
        );
        const presetList = el('div', { class: 'macro-side-list' });
        for (const preset of presets) {
            const pRow = el('div', { class: 'macro-preset-row' });
            const info = el('div', { class: 'macro-preset-info' });
            info.append(
                el('strong', { text: preset.title }),
                el('span', { class: 'macro-side-meta', text: preset.description }),
            );
            const addPresetBtn = el('button', {
                class: 'btn',
                type: 'button',
                text: '+ Add',
                title: `Add "${preset.title}" to this project`,
            });
            addPresetBtn.addEventListener('click', async () => {
                const clone = normalizeMacroDef(JSON.parse(JSON.stringify(preset)));
                const existingIds = new Set((ctx.getMacros?.() || []).map((m) => m.id));
                if (existingIds.has(clone.id)) {
                    clone.id = sanitizeMacroSlug(`${clone.id}-${existingIds.size + 1}`);
                }
                await ctx.onSaveMacros?.({ action: 'save', macro: clone });
                draft = clone;
                isNew = false;
                statusMsg = `Added preset "${clone.title}" to project.`;
                renderAll();
            });
            pRow.append(info, addPresetBtn);
            presetList.append(pRow);
        }
        presetBox.append(presetList);
        sidebar.append(presetBox);
    }

    function renderEditor() {
        mainPane.replaceChildren();
        const formHead = el('div', { class: 'macro-form-head' });
        formHead.append(
            el('h3', { text: isNew ? 'Create New Project Macro' : `Edit Macro: ${draft.title}` }),
            statusMsg ? el('span', { class: 'pill', 'data-tone': 'ok', text: statusMsg }) : document.createTextNode(''),
        );
        mainPane.append(formHead);

        const gridBasic = el('div', { class: 'macro-form-grid' });
        gridBasic.append(
            labeledInput('Macro Title', draft.title, (v) => {
                draft.title = v;
                if (isNew) {
                    draft.id = sanitizeMacroSlug(v, 'macro');
                    const idInput = mainPane.querySelector('[data-builder-id]');
                    if (idInput) idInput.value = draft.id;
                }
            }),
            labeledInput('Macro ID', draft.id, (v) => {
                draft.id = sanitizeMacroSlug(v, 'macro');
            }, { attr: { 'data-builder-id': 'true' }, hint: 'Letters, numbers, _ or -' }),
            labeledInput('Category / Badge', draft.category, (v) => { draft.category = v; }),
            labeledInput('Description', draft.description, (v) => { draft.description = v; }),
        );
        mainPane.append(gridBasic);

        const gridTarget = el('div', { class: 'macro-form-grid' });
        const targetSelectWrap = el('label', { class: 'field' }, 'Default Insertion Location');
        const targetSelect = el('select');
        for (const t of INSERT_TARGETS) {
            const opt = el('option', { value: t.value, text: `${t.label} — ${t.hint}` });
            if (t.value === draft.insertTarget) opt.selected = true;
            targetSelect.append(opt);
        }
        targetSelect.addEventListener('change', () => { draft.insertTarget = targetSelect.value; });
        targetSelectWrap.append(targetSelect);

        gridTarget.append(
            targetSelectWrap,
            labeledInput('File Extensions', draft.appliesTo.extensions, (v) => { draft.appliesTo.extensions = v; }, {
                hint: 'e.g. .html, .shtml, .php or *',
            }),
            labeledInput('Sections / Folders', draft.appliesTo.sections, (v) => { draft.appliesTo.sections = v; }, {
                hint: '* for all folders, or comma-separated folder names',
            }),
            labeledInput('Start Marker', draft.startMarker, (v) => { draft.startMarker = v; }, {
                hint: 'Opening marker to locate existing block in page',
            }),
            labeledInput('End Marker', draft.endMarker, (v) => { draft.endMarker = v; }, {
                hint: 'Closing marker to locate end of block in page',
            }),
        );
        mainPane.append(gridTarget);

        const fieldsBox = el('div', { class: 'macro-fields-section' });
        const fieldsHead = el('div', { class: 'macro-side-head' });
        fieldsHead.append(
            el('h4', { text: `Customizable Fields (${draft.fields.length})` }),
        );
        const addFieldBtn = el('button', {
            class: 'btn',
            type: 'button',
            text: '+ Add Field',
            title: 'Add a customizable field placeholder to this macro',
        });
        addFieldBtn.addEventListener('click', () => {
            const idx = draft.fields.length + 1;
            draft.fields.push(normalizeMacroField({
                key: `field_${idx}`,
                label: `Field ${idx}`,
                type: 'text',
                defaultValue: '',
                required: false,
            }, idx));
            renderEditor();
        });
        fieldsHead.append(addFieldBtn);
        fieldsBox.append(fieldsHead);

        const fieldsList = el('div', { class: 'macro-fields-list' });
        draft.fields.forEach((field, idx) => {
            fieldsList.append(buildFieldRow(field, idx));
        });
        fieldsBox.append(fieldsList);
        mainPane.append(fieldsBox);

        renderTemplateAndValidation();
    }

    function buildFieldRow(field, idx) {
        const row = el('div', { class: 'macro-field-row' });
        const top = el('div', { class: 'macro-field-row-top' });
        top.append(
            labeledInput('Key ({{key}})', field.key, (v) => {
                field.key = sanitizeMacroSlug(v, 'field').replace(/-/g, '_');
                updatePreview();
            }),
            labeledInput('Label', field.label, (v) => { field.label = v; }),
        );
        const typeWrap = el('label', { class: 'field' }, 'Type');
        const typeSelect = el('select');
        for (const t of FIELD_TYPES) {
            const opt = el('option', { value: t.value, text: t.label });
            if (t.value === field.type) opt.selected = true;
            typeSelect.append(opt);
        }
        typeSelect.addEventListener('change', () => {
            field.type = typeSelect.value;
            renderEditor();
        });
        typeWrap.append(typeSelect);
        top.append(
            typeWrap,
            labeledInput('Default Value', field.defaultValue, (v) => {
                field.defaultValue = v;
                updatePreview();
            }),
        );

        const removeBtn = el('button', {
            class: 'btn macro-field-remove',
            type: 'button',
            text: '×',
            title: `Remove field ${field.key}`,
        });
        removeBtn.addEventListener('click', () => {
            draft.fields.splice(idx, 1);
            renderEditor();
        });
        top.append(removeBtn);
        row.append(top);

        const bottom = el('div', { class: 'macro-field-row-bottom' });
        if (field.type === 'select') {
            bottom.append(
                labeledInput('Select Options (comma-separated)', (field.options || []).join(', '), (v) => {
                    field.options = v.split(',').map((s) => s.trim()).filter(Boolean);
                }),
            );
        }
        bottom.append(
            labeledInput('Validation Regex (optional)', field.pattern, (v) => { field.pattern = v.trim(); }, {
                placeholder: 'e.g. ^\\d{4}-\\d{2}-\\d{2}$',
            }),
            labeledInput('Hint / Tooltip', field.hint, (v) => { field.hint = v; }),
        );
        const reqLabel = el('label', { class: 'field check macro-field-req' });
        const reqCheck = el('input', { type: 'checkbox' });
        reqCheck.checked = Boolean(field.required);
        reqCheck.addEventListener('change', () => { field.required = reqCheck.checked; });
        reqLabel.append(reqCheck, document.createTextNode(' Required'));
        bottom.append(reqLabel);
        row.append(bottom);
        return row;
    }

    function renderTemplateAndValidation() {
        const tplBox = el('div', { class: 'macro-template-section' });
        const tplHead = el('div', { class: 'macro-side-head' });
        tplHead.append(el('h4', { text: 'Code Template & Placeholders' }));
        const chips = el('div', { class: 'macro-token-chips' });
        for (const f of draft.fields) {
            const chip = el('button', {
                class: 'btn macro-token-btn',
                type: 'button',
                text: `+ {{${f.key}}}`,
                title: `Insert {{${f.key}}} into the code template`,
            });
            chip.addEventListener('click', () => {
                const area = mainPane.querySelector('[data-builder-template]');
                if (!area) return;
                const start = area.selectionStart || 0;
                const end = area.selectionEnd || 0;
                const token = `{{${f.key}}}`;
                area.setRangeText(token, start, end, 'end');
                draft.template = area.value;
                area.focus();
                updatePreview();
            });
            chips.append(chip);
        }
        tplHead.append(chips);
        tplBox.append(tplHead);

        const tplGrid = el('div', { class: 'macro-template-grid' });
        const areaWrap = el('label', { class: 'field' }, 'Template Source (use {{fieldKey}} placeholders)');
        const area = el('textarea', {
            'data-builder-template': 'true',
            rows: '6',
            spellcheck: 'false',
        });
        area.value = draft.template;
        area.addEventListener('input', () => {
            draft.template = area.value;
            updatePreview();
        });
        areaWrap.append(area);

        const previewWrap = el('div', { class: 'field' });
        previewWrap.append(
            el('span', { text: 'Live Rendered Preview (with default values)' }),
            el('pre', { class: 'macro-preview-code', 'data-builder-preview': 'true' }),
        );
        tplGrid.append(areaWrap, previewWrap);
        tplBox.append(tplGrid);
        mainPane.append(tplBox);
        updatePreview();

        const valBox = el('div', { class: 'macro-validation-section' });
        valBox.append(el('h4', { text: 'Common Error Check & Formatting Rules' }));
        const valRow = el('div', { class: 'macro-validation-row' });

        const reqPageLabel = el('label', { class: 'field check' });
        const reqPageCheck = el('input', { type: 'checkbox' });
        reqPageCheck.checked = Boolean(draft.validation.requiredOnPage);
        reqPageCheck.addEventListener('change', () => { draft.validation.requiredOnPage = reqPageCheck.checked; });
        reqPageLabel.append(reqPageCheck, document.createTextNode(' Require macro on all matching pages'));

        const fmtLabel = el('label', { class: 'field check' });
        const fmtCheck = el('input', { type: 'checkbox' });
        fmtCheck.checked = draft.validation.checkFormatting !== false;
        fmtCheck.addEventListener('change', () => { draft.validation.checkFormatting = fmtCheck.checked; });
        fmtLabel.append(fmtCheck, document.createTextNode(' Verify block formatting matches template'));

        const sevWrap = el('label', { class: 'field macro-sev-field' }, 'Issue Severity');
        const sevSelect = el('select');
        for (const [val, label] of [['warning', 'Warning'], ['error', 'Error']]) {
            const opt = el('option', { value: val, text: label });
            if (draft.validation.severity === val) opt.selected = true;
            sevSelect.append(opt);
        }
        sevSelect.addEventListener('change', () => { draft.validation.severity = sevSelect.value; });
        sevWrap.append(sevSelect);

        valRow.append(reqPageLabel, fmtLabel, sevWrap);
        valBox.append(valRow);
        mainPane.append(valBox);

        const footer = el('div', { class: 'macro-builder-footer' });
        const saveBtn = el('button', {
            class: 'btn btn-primary',
            type: 'button',
            text: isNew ? 'Save New Macro' : 'Save Changes',
        });
        saveBtn.addEventListener('click', async () => {
            const clean = normalizeMacroDef(draft);
            await ctx.onSaveMacros?.({ action: 'save', macro: clean });
            draft = clean;
            isNew = false;
            statusMsg = `Saved "${clean.title}".`;
            renderAll();
        });
        footer.append(saveBtn);

        const dupBtn = el('button', { class: 'btn', type: 'button', text: 'Duplicate' });
        dupBtn.addEventListener('click', () => {
            const copy = normalizeMacroDef(JSON.parse(JSON.stringify(draft)));
            copy.id = sanitizeMacroSlug(`${copy.id}-copy`);
            copy.title = `${copy.title} (Copy)`;
            draft = copy;
            isNew = true;
            statusMsg = 'Duplicated into a new unsaved macro.';
            renderAll();
        });
        footer.append(dupBtn);

        if (!isNew) {
            const delBtn = el('button', { class: 'btn', type: 'button', text: 'Delete Macro' });
            delBtn.addEventListener('click', async () => {
                await ctx.onSaveMacros?.({ action: 'delete', id: draft.id });
                const remaining = ctx.getMacros?.() || [];
                if (remaining.length) {
                    draft = normalizeMacroDef(JSON.parse(JSON.stringify(remaining[0])));
                    isNew = false;
                } else {
                    draft = createBlankMacro();
                    isNew = true;
                }
                statusMsg = 'Deleted macro.';
                renderAll();
            });
            footer.append(delBtn);
        }
        mainPane.append(footer);
    }

    function updatePreview() {
        const pre = mainPane.querySelector('[data-builder-preview]');
        if (!pre) return;
        const defaults = {};
        for (const f of draft.fields) {
            defaults[f.key] = f.defaultValue || `<${f.key}>`;
        }
        pre.textContent = renderMacroTemplate(draft, defaults);
    }

    function labeledInput(label, value, onInput, { hint = '', placeholder = '', attr = {} } = {}) {
        const wrap = el('label', { class: 'field' });
        wrap.append(document.createTextNode(label));
        const input = el('input', {
            type: 'text',
            value: String(value ?? ''),
            placeholder,
            autocomplete: 'off',
            spellcheck: 'false',
            ...attr,
        });
        input.addEventListener('input', () => onInput(input.value));
        wrap.append(input);
        if (hint) wrap.append(el('p', { class: 'field-hint', text: hint }));
        return wrap;
    }
    return { open, close: () => dialog.close() };
}
