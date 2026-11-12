import { el } from './dom.js';

const SECTIONS = [
    ['articles', 'Articles'],
    ['awards', 'Awards'],
    ['projects', 'Projects'],
    ['sales', 'Sales'],
    ['video', 'Video'],
    ['whatsnew', "What's New"],
    ['parts', 'Parts'],
    ['contact', 'Contact'],
    ['newsletter', 'Newsletter'],
];

const GALLERY_SECTIONS = new Set(['articles', 'awards', 'projects', 'sales']);

const STARTERS = [
    {
        id: 'blank',
        title: 'None (Blank Page)',
        hint: 'A blank HTML or PHP page',
        section: '',
        kind: 'blank',
        filenameFor(slug, section) {
            if (section === 'sales') return 'index.php';
            if (section === 'articles' && slug) return `${slug}.html`;
            return 'index.html';
        },
    },
    {
        id: 'article-2026',
        title: '2026 Lightbox Article / Event Coverage',
        hint: 'articles/<slug>/<slug>.html',
        section: 'articles',
        kind: 'article',
        filenameFor: (slug) => `${slug || 'article'}.html`,
    },
    {
        id: 'award-2026',
        title: 'Award Showcase with Lightbox Gallery',
        hint: 'awards/<slug>/index.html',
        section: 'awards',
        kind: 'page',
        filenameFor: () => 'index.html',
    },
    {
        id: 'project-2026',
        title: 'Restoration Project Gallery',
        hint: 'projects/<slug>/index.html',
        section: 'projects',
        kind: 'page',
        filenameFor: () => 'index.html',
    },
    {
        id: 'sales-vehicle',
        title: 'Inventory Vehicle Listing',
        hint: 'sales/<slug>/index.php',
        section: 'sales',
        kind: 'sales',
        filenameFor: () => 'index.php',
    },
];

export function mountNewPage({ button, apiSend, announce, exists, getSections, onCreated }) {
    const dialog = el('dialog', { class: 'new-page-dialog', 'aria-labelledby': 'new-page-title' });
    const head = el('div', { class: 'dialog-head' });
    head.append(el('h2', { id: 'new-page-title', text: 'New Page' }), closeButton());
    const note = el('p', { class: 'field-hint', hidden: true });
    const form = el('form', { class: 'new-step' });
    const templateInput = select('Template', 'new-template', STARTERS.map((item) => [item.id, item.title]));
    const hint = el('p', { class: 'field-hint span-2', text: '' });
    const sectionInput = select('Section', 'new-section', SECTIONS);
    const slugInput = input('Folder slug', 'new-slug', 'newportconcours2026');
    const fileInput = input('Filename', 'new-filename', 'index.html');
    const titleInput = input('Title / Vehicle Name', 'new-title', 'Newport Concours 2026');
    const subtitleInput = input('Subtitle / Location & Date', 'new-subtitle', 'Newport, Rhode Island · June 2026');
    const authorInput = input('Byline / Author', 'new-author', 'Nicole LeBoeuf');
    const descriptionInput = area('Description', 'new-description');
    const vehicleTypeInput = select('Suggestion vehicle type', 'new-vehicle-type', [
        ['', 'Any'],
        ['car', 'car'],
        ['motorcycle', 'motorcycle'],
    ]);
    const makeInput = input('Suggestion make', 'new-make', 'Duesenberg');
    const suggestNameInput = input('Suggestion vehicle name', 'new-suggest-name', '');
    const overrideInput = input('Override IDs', 'new-override', '8096, 3030');
    const inventoryInput = input('Inventory number', 'new-inventory', '8096');
    const priceInput = input('Price', 'new-price', 'Price on Request');
    const salesTypeInput = select('Listing vehicle type', 'new-sales-type', [
        ['car', 'car'],
        ['motorcycle', 'motorcycle'],
    ]);
    const salesMakeInput = input('Listing make', 'new-sales-make', 'Duesenberg');
    const salesFields = el('div', { class: 'span-2 sales-fields', hidden: true });
    const salesGrid = el('div', { class: 'new-grid' });
    salesGrid.append(inventoryInput.wrap, priceInput.wrap, salesTypeInput.wrap, salesMakeInput.wrap);
    salesFields.append(el('h3', { text: 'Inventory' }), salesGrid);
    const pathPreview = el('p', { class: 'path-preview span-2', text: '' });
    const formError = el('p', { class: 'field-error span-2', hidden: true, role: 'alert' });
    const actions = el('div', { class: 'confirm-actions span-2' });
    const create = el('button', { class: 'btn btn-primary', type: 'submit', text: 'Create Page', title: 'Create the page in the project and open it' });
    templateInput.wrap.classList.add('span-2');
    actions.append(create);
    const grid = el('div', { class: 'new-grid' });
    grid.append(
        templateInput.wrap,
        hint,
        sectionInput.wrap,
        slugInput.wrap,
        fileInput.wrap,
        titleInput.wrap,
        subtitleInput.wrap,
        authorInput.wrap,
        descriptionInput.wrap,
        vehicleTypeInput.wrap,
        makeInput.wrap,
        suggestNameInput.wrap,
        overrideInput.wrap,
        salesFields,
        pathPreview,
        formError,
        actions,
    );
    form.append(grid);
    const body = el('div', { class: 'dialog-body' });
    body.append(note, form);
    dialog.append(head, body);
    document.body.append(dialog);

    let templates = STARTERS.slice();
    let selected = STARTERS[0];
    let slugManual = false;
    let filenameManual = false;
    let creating = false;

    button.addEventListener('click', () => { void open(); });
    templateInput.input.addEventListener('change', () => chooseId(templateInput.input.value));
    sectionInput.input.addEventListener('change', () => refreshFile());
    titleInput.input.addEventListener('input', () => {
        if (!slugManual) slugInput.input.value = slugify(titleInput.input.value);
        refreshFile();
    });
    slugInput.input.addEventListener('input', () => {
        slugManual = true;
        refreshFile();
    });
    fileInput.input.addEventListener('input', () => {
        filenameManual = true;
        refreshPath();
    });
    form.addEventListener('submit', (event) => {
        event.preventDefault();
        void createPage();
    });
    for (const node of [vehicleTypeInput.input, makeInput.input, suggestNameInput.input, salesTypeInput.input, salesMakeInput.input]) {
        node.addEventListener('input', refreshPath);
        node.addEventListener('change', refreshPath);
    }

    async function open() {
        if (dialog.open) return;
        slugManual = false;
        filenameManual = false;
        creating = false;
        templates = STARTERS.slice();
        fillSectionSelect();
        resetFields();
        formError.hidden = true;
        note.hidden = true;
        note.textContent = '';
        fillTemplateSelect();
        dialog.showModal();
        templateInput.input.focus();
        try {
            const data = await apiSend('GET', '/__studio/api/templates');
            const remote = extractTemplates(data);
            if (remote.length) {
                templates = mergeTemplates(remote);
                fillTemplateSelect();
            } else {
                note.hidden = false;
                note.textContent = 'Showing the built-in starters.';
            }
        } catch {
            note.hidden = false;
            note.textContent = 'Showing the built-in starters. The template list will load when the studio API is ready.';
        }
    }

    function fillSectionSelect() {
        const dynamic = typeof getSections === 'function' ? (getSections() || []) : [];
        const options = [['/', 'Root (/)']];
        const seen = new Set(['/']);
        for (const sec of dynamic) {
            const id = String(sec?.id || '').trim();
            if (!id || seen.has(id)) continue;
            seen.add(id);
            options.push([id, sec.label || id]);
        }
        if (options.length === 1) {
            for (const [id, label] of SECTIONS) {
                if (seen.has(id)) continue;
                seen.add(id);
                options.push([id, label]);
            }
        }
        sectionInput.input.replaceChildren();
        for (const [value, text] of options) {
            sectionInput.input.append(el('option', { value, text }));
        }
    }

    function fillTemplateSelect() {
        const current = templateInput.input.value || selected?.id || 'blank';
        templateInput.input.replaceChildren();
        for (const template of templates) {
            templateInput.input.append(el('option', { value: template.id, text: template.title }));
        }
        chooseId(templates.some((item) => item.id === current) ? current : 'blank');
    }

    function chooseId(id) {
        selected = templates.find((item) => item.id === id) || templates[0];
        if (templateInput.input.value !== selected.id) templateInput.input.value = selected.id;
        applyKind();
    }

    function applyKind() {
        const template = selected || STARTERS[0];
        const blank = template.kind === 'blank';
        const sales = template.kind === 'sales';
        if (!blank && template.section) sectionInput.input.value = template.section;
        sectionInput.input.disabled = !blank;
        sectionInput.input.title = blank
            ? 'Site section the new page is created in'
            : 'This template chooses the section.';
        titleInput.wrap.hidden = blank;
        subtitleInput.wrap.hidden = blank || sales;
        authorInput.wrap.hidden = blank || sales;
        descriptionInput.wrap.hidden = blank;
        for (const field of [vehicleTypeInput, makeInput, suggestNameInput, overrideInput]) {
            field.wrap.hidden = blank || sales;
        }
        salesFields.hidden = !sales;
        refreshFile();
    }

    function refreshFile() {
        if (!selected || filenameManual) {
            refreshPath();
            return;
        }
        fileInput.input.value = selected.filenameFor(slugInput.input.value.trim(), currentSection());
        refreshPath();
    }

    function refreshPath() {
        const section = currentSection();
        const slug = slugInput.input.value.trim();
        pathPreview.textContent = destination() || 'Choose a section, slug, and filename.';
        hint.textContent = describe(selected, section);
        const taken = Boolean(section && slug && exists?.(section, slug));
        if (taken) {
            formError.hidden = false;
            formError.textContent = 'That folder already exists. Open it from the file tree.';
        } else if (formError.textContent.startsWith('That folder already')) formError.hidden = true;
    }

    function currentSection() {
        if (selected && selected.kind !== 'blank' && selected.section) return selected.section;
        return sectionInput.input.value;
    }

    function destination() {
        const section = currentSection();
        const slug = slugInput.input.value.trim();
        const filename = fileInput.input.value.trim();
        if (!section || !filename) return '';
        if (section === '/') {
            return slug ? `/${slug}/${filename}` : `/${filename}`;
        }
        return slug ? `/${section}/${slug}/${filename}` : `/${section}/${filename}`;
    }

    async function createPage() {
        if (creating || !selected) return;
        const blank = selected.kind === 'blank';
        const sales = selected.kind === 'sales';
        const section = currentSection();
        const slug = slugInput.input.value.trim();
        const filename = fileInput.input.value.trim();
        const title = blank ? '' : titleInput.input.value.trim();
        const problem = validate(slug, filename, title, blank);
        if (problem) {
            formError.hidden = false;
            formError.textContent = problem;
            return;
        }
        if (exists?.(section, slug)) {
            formError.hidden = false;
            formError.textContent = 'That folder already exists. Open it from the file tree.';
            return;
        }
        const payload = {
            templateId: blank ? 'blank' : selected.id,
            section,
            slug,
            filename,
            title,
            subtitle: blank || sales ? '' : subtitleInput.input.value.trim(),
            author: blank || sales ? '' : authorInput.input.value.trim(),
            description: blank ? '' : descriptionInput.input.value.trim(),
            vehicleType: blank ? '' : (sales ? salesTypeInput.input.value : vehicleTypeInput.input.value),
            makeType: blank ? '' : (sales ? salesMakeInput.input.value.trim() : makeInput.input.value.trim()),
            vehicleName: blank ? '' : (suggestNameInput.input.value.trim() || (sales ? title : '')),
            overrideIds: blank || sales ? '' : overrideInput.input.value.trim(),
            inventoryNumber: sales ? inventoryInput.input.value.trim() : '',
            price: sales ? priceInput.input.value.trim() : '',
        };
        creating = true;
        create.disabled = true;
        create.textContent = 'Creating…';
        formError.hidden = true;
        try {
            const result = await apiSend('POST', '/__studio/api/page/create', payload);
            const path = result.path || destination();
            dialog.close();
            await onCreated({ ...result, path, section: result.section || section, slug: result.slug || slug });
            announce(`Created ${path}`);
        } catch (error) {
            formError.hidden = false;
            formError.textContent = error.message || 'Could not create the page.';
        } finally {
            creating = false;
            create.disabled = false;
            create.textContent = 'Create Page';
        }
    }

    function validate(slug, filename, title, blank) {
        if ((!blank || slug) && !/^[a-z0-9][a-z0-9_-]*$/i.test(slug)) {
            return 'Use a folder slug made of letters, numbers, hyphens, and underscores.';
        }
        if (!blank && !title) return 'Enter a title or vehicle name.';
        if (!filename || filename.includes('..') || /[\\/]/.test(filename) || !/\.[A-Za-z0-9]+$/.test(filename)) {
            return 'Enter a filename such as index.html or newportconcours2026.html.';
        }
        return '';
    }

    function resetFields() {
        sectionInput.input.disabled = false;
        const hasArticles = [...sectionInput.input.options].some((opt) => opt.value === 'articles');
        const firstNonRoot = [...sectionInput.input.options].find((opt) => opt.value !== '/');
        sectionInput.input.value = hasArticles ? 'articles' : (firstNonRoot?.value || '/');
        slugInput.input.value = '';
        fileInput.input.value = '';
        titleInput.input.value = '';
        subtitleInput.input.value = '';
        authorInput.input.value = 'Nicole LeBoeuf';
        descriptionInput.input.value = '';
        vehicleTypeInput.input.value = '';
        makeInput.input.value = '';
        suggestNameInput.input.value = '';
        overrideInput.input.value = '';
        inventoryInput.input.value = '';
        priceInput.input.value = '';
        salesTypeInput.input.value = 'car';
        salesMakeInput.input.value = '';
        pathPreview.textContent = '';
        salesFields.hidden = true;
    }

    function closeButton() {
        const buttonNode = el('button', { class: 'btn', type: 'button', text: 'Close', title: 'Close the new page dialog' });
        buttonNode.addEventListener('click', () => dialog.close());
        return buttonNode;
    }

    return { open };
}

function describe(template, section) {
    if (!template || template.kind === 'blank') {
        return GALLERY_SECTIONS.has(section)
            ? 'Blank page. Also creates empty slides/ and thumbs/ folders.'
            : 'Blank page.';
    }
    return template.hint || '';
}

function extractTemplates(data) {
    if (Array.isArray(data)) return data;
    if (Array.isArray(data?.templates)) return data.templates;
    if (Array.isArray(data?.items)) return data.items;
    return [];
}

function mergeTemplates(remote) {
    const byId = new Map();
    for (const item of remote) {
        const normalized = normalizeTemplate(item);
        if (normalized && !byId.has(normalized.id)) byId.set(normalized.id, normalized);
    }
    const ordered = STARTERS.map((starter) => byId.get(starter.id) || starter);
    for (const [id, item] of byId) {
        if (!STARTERS.some((starter) => starter.id === id)) ordered.push(item);
    }
    return ordered;
}

function normalizeTemplate(item) {
    const id = String(item.id || item.templateId || item.slug || item.name || '').trim();
    if (!id || id === 'blank' || id === 'none') return null;
    const base = STARTERS.find((starter) => starter.id === id);
    const section = item.section || base?.section || 'articles';
    const pattern = String(item.defaultFilename || item.filename || 'index.html');
    return {
        id,
        title: base?.title || item.title || item.label || item.name || id,
        hint: base?.hint || item.hint || item.description || pattern,
        section,
        kind: base?.kind || (section === 'sales' ? 'sales' : 'page'),
        filenameFor: base?.filenameFor || ((slug) => (
            pattern.includes('<slug>') ? pattern.replaceAll('<slug>', slug || 'page') : pattern
        )),
    };
}

function slugify(value) {
    return String(value || '')
        .normalize('NFKD')
        .replace(/[\u0300-\u036f]/g, '')
        .toLowerCase()
        .replace(/[^a-z0-9]+/g, '')
        .slice(0, 80);
}

const FIELD_TIPS = {
    'new-template': 'Choose a blank page or a section template',
    'new-section': 'Site section the new page is created in',
    'new-slug': 'Folder slug used in the page path',
    'new-filename': 'Filename created inside the folder',
    'new-title': 'Page title or vehicle name',
    'new-subtitle': 'Subtitle, location, or date shown under the title',
    'new-author': 'Byline stored with the page',
    'new-description': 'Description used for SEO and the share card',
    'new-vehicle-type': 'Suggestion strip vehicleType filter',
    'new-make': 'Suggestion strip makeType filter',
    'new-suggest-name': 'Suggestion strip vehicleName filter',
    'new-override': 'Override IDs — comma-separated inventory numbers',
    'new-inventory': 'Sales listing $inventoryNumber',
    'new-price': 'Sales listing $price. Use a number, Price on Request, or SOLD.',
    'new-sales-type': 'Sales listing $vehicleType',
    'new-sales-make': 'Sales listing $makeType',
};

function input(label, id, placeholder) {
    const wrap = el('label', { class: 'field', for: id });
    wrap.append(document.createTextNode(label));
    const field = el('input', { id, type: 'text', autocomplete: 'off', spellcheck: 'false', placeholder, title: FIELD_TIPS[id] || label });
    if (FIELD_TIPS[id]) wrap.title = FIELD_TIPS[id];
    wrap.append(field);
    return { wrap, input: field };
}

function area(label, id) {
    const wrap = el('label', { class: 'field span-2', for: id });
    wrap.append(document.createTextNode(label));
    const field = el('textarea', { id, rows: '4', title: FIELD_TIPS[id] || label });
    if (FIELD_TIPS[id]) wrap.title = FIELD_TIPS[id];
    wrap.append(field);
    return { wrap, input: field };
}

function select(label, id, options) {
    const wrap = el('label', { class: 'field', for: id });
    wrap.append(document.createTextNode(label));
    const field = el('select', { id, title: FIELD_TIPS[id] || label });
    if (FIELD_TIPS[id]) wrap.title = FIELD_TIPS[id];
    for (const [value, text] of options) {
        field.append(el('option', { value, text }));
    }
    wrap.append(field);
    return { wrap, input: field };
}
