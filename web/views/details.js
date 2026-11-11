import { getStudioConfig } from './config.js';

const brandName = () => getStudioConfig().brandName || 'our showroom';
export const KNOWN_MAKES = [
    'American LaFrance', 'Ariel', 'Auburn', 'Cadillac', 'Cameron', 'Chrysler', 'Cord', 'Crocker',
    'Curtiss', 'Duesenberg', 'Flying Merkel', 'Ford', 'Harley-Davidson', 'Henderson', 'Indian',
    'Jaguar', 'Lamborghini', 'Lincoln', 'Locomobile', 'Mercer', 'Packard', 'Pierce-Arrow', 'Pope',
    'Reading Standard', 'ReVere', 'Rolls-Royce', 'Stanley', 'Stutz', 'Yale',
];

export function formatPriceLabel(price, soldFlag = false) {
    const raw = String(price ?? '').trim();
    if (soldFlag || raw.toUpperCase() === 'SOLD') {
        return { key: 'sold', label: 'SOLD', tone: 'danger', short: 'SOLD' };
    }
    const digits = raw.replace(/[$,\s]/g, '');
    if (/^\d+(?:\.\d+)?$/.test(digits) && Number(digits) > 0) {
        const amount = Number(digits);
        const formatted = new Intl.NumberFormat('en-US', {
            style: 'currency',
            currency: 'USD',
            maximumFractionDigits: Number.isInteger(amount) ? 0 : 2,
        }).format(amount);
        return { key: 'sale', label: `For Sale · ${formatted}`, tone: 'ok', short: formatted };
    }
    return { key: 'por', label: 'Price on Request', tone: 'warn', short: 'On request' };
}

export function normalizePrice(price) {
    const raw = String(price ?? '').trim();
    if (/^sold$/i.test(raw)) return 'SOLD';
    if (/^price on request$/i.test(raw)) return 'Price on Request';
    const digits = raw.replace(/[$,\s]/g, '');
    if (/^\d+(?:\.\d+)?$/.test(digits) && Number(digits) > 0) {
        return Number.isInteger(Number(digits)) ? String(Number(digits)) : digits;
    }
    return raw;
}

export function buildMetaPreview({ vehicleName = '', makeType = '', description = '', price = '' } = {}) {
    const name = cleanText(vehicleName);
    const make = cleanText(String(makeType || '').replace(/_/g, ' '));
    const sold = String(price ?? '').trim().toUpperCase() === 'SOLD';
    const formatted = formatPriceLabel(price).key === 'sale' ? formatPriceLabel(price).short : '';
    const blocks = String(description ?? '')
        .replace(/<ul\b[^>]*>[\s\S]*?<\/ul>/gi, '\n\n')
        .replace(/<br\s*\/?>/gi, '\n')
        .replace(/<\/p>/gi, '\n\n')
        .split(/\n\s*\n/)
        .map((block) => cleanText(block))
        .filter(Boolean);
    const para = blocks.find((block) => block.length >= 45) || blocks.join(' ');
    const yearMatch = name.match(/\b(18\d\d|19\d\d|20\d\d)\b/);
    const year = yearMatch ? yearMatch[1] : '';
    let model = name;
    if (year && model.startsWith(year)) model = model.slice(year.length).trim();
    if (make && model.toLowerCase().startsWith(make.toLowerCase())) model = model.slice(make.length).trim();
    let full = para;
    if (!sold && year && make) {
        const parts = [year, make, model].filter(Boolean);
        const lead = `For sale: ${parts.join(', ')}${formatted ? ` (${formatted})` : ''}`;
        const body = para || `Offered by ${brandName()} in Providence, Rhode Island.`;
        full = `${lead} - ${body}`;
    } else if (!para) {
        full = sold
            ? `${name} from the ${brandName()} archive in Providence, Rhode Island.`
            : `${name} for sale${formatted ? ` (${formatted})` : ''} at ${brandName()} in Providence, Rhode Island.`;
    } else if (!sold && !/\bfor\s+sale\b/i.test(para) && (!formatted || !para.includes(formatted))) {
        full = `${formatted ? `For sale (${formatted}). ` : 'For sale. '}${para}`;
    }
    const text = truncateMeta(full);
    return {
        text,
        length: text.length,
        fullLength: full.trim().length,
        truncated: text !== full.trim(),
    };
}

export function mountDetails(parent, ctx) {
    const root = el('form', {
        class: 'view view-details',
        id: 'view-details',
        role: 'tabpanel',
        'aria-labelledby': 'mode-details',
        hidden: true,
        autocomplete: 'off',
    });
    root.addEventListener('submit', (event) => event.preventDefault());
    root.addEventListener('input', () => {
        updatePreview();
        ctx.onDetailsEdit();
    });
    root.addEventListener('change', () => {
        updatePreview();
        ctx.onDetailsEdit();
    });
    parent.append(root);

    let doc = null;

    function setDoc(next) {
        doc = next;
        root.dataset.slug = next?.slug || '';
        render();
    }

    function matchesSlug(slug) {
        return root.dataset.slug === slug;
    }

    function setActive(active) {
        root.hidden = !active;
    }

    function render() {
        root.replaceChildren();
        if (!doc) {
            root.append(el('p', { class: 'empty', text: 'Open a folder to edit its details.' }));
            return;
        }
        const section = doc.section || ctx.section();
        const heading = el('h2', { text: section === 'sales' ? 'Vehicle details' : 'Page details' });
        const path = el('p', { class: 'hero-file', text: `${section} / ${doc.slug || ''}` });
        if (section === 'sales') renderSales(heading, path);
        else renderPage(heading, path);
    }

    function renderSales(heading, path) {
        const details = doc.details || {};
        const grid = el('div', { class: 'form-grid' });
        grid.append(el('div', { class: 'span-2' }, [heading, path]));
        const name = field('Vehicle Name', 'text', details.vehicleName || '', 'vehicleName');
        const inventory = field('Inventory Number', 'text', details.inventoryNumber || '', 'inventoryNumber');
        const price = field('Price', 'text', details.price || '', 'price');
        const priceRow = el('div', { class: 'quick-row' });
        for (const label of ['Price on Request', 'SOLD']) {
            const button = el('button', {
                class: 'btn',
                type: 'button',
                text: label,
                title: label === 'SOLD' ? 'Set $price to SOLD' : 'Set $price to Price on Request',
            });
            button.addEventListener('click', () => {
                price.input.value = label;
                price.input.dispatchEvent(new Event('input', { bubbles: true }));
            });
            priceRow.append(button);
        }
        price.wrap.append(el('p', { class: 'field-hint', text: 'Numbers are stored without commas or a dollar sign.' }), priceRow);
        const type = selectField('Vehicle Type', details.vehicleType || 'car', 'vehicleType', vehicleTypeOptions(details.vehicleType));
        const make = field('Make', 'text', details.makeType || '', 'makeType');
        make.input.setAttribute('list', 'studio-makes');
        make.wrap.append(makeList(details.makeType, ctx.makes?.() || []));
        const hiddenWrap = el('label', { class: 'field check span-2' });
        const hidden = el('input', { type: 'checkbox', 'data-key': 'doNotRender' });
        hidden.checked = Boolean(details.doNotRender);
        hiddenWrap.title = FIELD_TIPS.doNotRender;
        hidden.title = FIELD_TIPS.doNotRender;
        hiddenWrap.append(hidden, document.createTextNode(' Hidden ($doNotRender)'));
        const description = areaField('Description (HTML)', details.description || '', 'description');
        const previewLabel = el('div', { class: 'field span-2' });
        const previewTitle = el('span', { text: 'SEO meta description' });
        const preview = el('p', { class: 'meta-preview', id: 'meta-preview' });
        const count = el('p', { class: 'field-hint', id: 'meta-count' });
        previewLabel.append(previewTitle, preview, count);
        grid.append(name.wrap, inventory.wrap, price.wrap, type.wrap, make.wrap, hiddenWrap, description.wrap, previewLabel);
        root.append(grid);
        updatePreview();
    }

    function renderPage(heading, path) {
        const page = { title: '', description: '', image: '', type: '', date: '', ...(doc.pageValues || {}) };
        const related = { vehicleType: '', makeType: '', vehicleName: '', overrideIds: '', ...(doc.relatedSales || {}) };
        const grid = el('div', { class: 'form-grid' });
        grid.append(el('div', { class: 'span-2' }, [
            heading,
            path,
            el('p', { class: 'field-hint', text: 'Articles, awards, and projects store these fields in the page-values and related-sales comments.' }),
        ]));
        grid.append(el('h3', { class: 'span-2', text: 'Page values' }));
        grid.append(
            field('Title', 'text', page.title, 'title').wrap,
            areaField('Description', page.description, 'description').wrap,
            field('Image', 'text', page.image, 'image').wrap,
            field('Type', 'text', page.type, 'type').wrap,
            field('Date', 'text', page.date, 'date').wrap,
        );
        grid.append(el('h3', { class: 'span-2', text: 'Related sales' }));
        grid.append(
            field('Vehicle Type', 'text', related.vehicleType, 'relatedVehicleType').wrap,
            field('Make', 'text', related.makeType, 'relatedMakeType').wrap,
            field('Vehicle Name', 'text', related.vehicleName, 'relatedVehicleName').wrap,
            field('Override IDs', 'text', related.overrideIds, 'overrideIds').wrap,
        );
        const previewLabel = el('div', { class: 'field span-2' });
        previewLabel.append(
            el('span', { text: 'SEO meta description' }),
            el('p', { class: 'meta-preview', id: 'meta-preview' }),
            el('p', { class: 'field-hint', id: 'meta-count' }),
        );
        grid.append(previewLabel);
        root.append(grid);
        updatePreview();
    }

    function updatePreview() {
        const preview = root.querySelector('#meta-preview');
        const count = root.querySelector('#meta-count');
        if (!preview || !count || !doc) return;
        const section = doc.section || ctx.section();
        if (section !== 'sales') {
            const text = valueOf('description');
            preview.textContent = text || 'No description is set.';
            count.textContent = `${text.length} characters`;
            return;
        }
        const meta = buildMetaPreview({
            vehicleName: valueOf('vehicleName'),
            makeType: valueOf('makeType'),
            description: valueOf('description'),
            price: valueOf('price'),
        });
        preview.textContent = meta.text;
        count.textContent = meta.truncated
            ? `${meta.length} characters shown · truncated from ${meta.fullLength}`
            : `${meta.length} characters`;
    }

    function draftDetails() {
        if (!doc || root.dataset.slug !== ctx.slug() || (doc.section || ctx.section()) !== 'sales' || !root.querySelector('[data-key="vehicleName"]')) {
            return { ...(doc?.details || {}) };
        }
        return {
            ...(doc.details || {}),
            vehicleName: valueOf('vehicleName'),
            inventoryNumber: valueOf('inventoryNumber'),
            price: normalizePrice(valueOf('price')),
            vehicleType: valueOf('vehicleType') || 'car',
            makeType: valueOf('makeType'),
            doNotRender: Boolean(root.querySelector('[data-key="doNotRender"]')?.checked),
            description: valueOf('description'),
        };
    }

    function payload() {
        const section = doc?.section || ctx.section();
        const slug = doc?.slug || ctx.slug();
        if (section === 'sales') {
            return {
                section,
                slug,
                details: draftDetails(),
                pageValues: { ...(doc?.pageValues || {}) },
                relatedSales: { ...(doc?.relatedSales || {}) },
            };
        }
        return {
            section,
            slug,
            details: { ...(doc?.details || {}) },
            pageValues: {
                title: valueOf('title'),
                description: valueOf('description'),
                image: valueOf('image'),
                type: valueOf('type'),
                date: valueOf('date'),
            },
            relatedSales: {
                vehicleType: valueOf('relatedVehicleType'),
                makeType: valueOf('relatedMakeType'),
                vehicleName: valueOf('relatedVehicleName'),
                overrideIds: valueOf('overrideIds'),
            },
        };
    }

    function valueOf(key) {
        const node = root.querySelector(`[data-key="${key}"]`);
        return node ? node.value : '';
    }

    return { setDoc, setActive, draftDetails, payload, matchesSlug };
}

function vehicleTypeOptions(current) {
    const options = ['car', 'motorcycle'];
    if (current && !options.includes(current)) options.push(current);
    return options;
}

function makeList(current, extra) {
    const list = el('datalist', { id: 'studio-makes' });
    const names = new Set(KNOWN_MAKES);
    if (current) names.add(current);
    for (const name of extra || []) if (name) names.add(name);
    for (const name of [...names].sort((a, b) => a.localeCompare(b))) {
        list.append(el('option', { value: name }));
    }
    return list;
}

const FIELD_TIPS = {
    vehicleName: 'Sales listing $vehicleName shown as the vehicle title.',
    inventoryNumber: 'Sales listing $inventoryNumber.',
    price: 'Sales listing $price. Use a number, Price on Request, or SOLD.',
    vehicleType: 'Sales listing $vehicleType.',
    makeType: 'Sales listing $makeType. Spaces are stored as underscores.',
    doNotRender: 'Sales listing $doNotRender. When set, the public site hides this vehicle.',
    description: 'Description used for the page and the share card.',
    title: 'Page title used for SEO and the share card.',
    image: 'Share and SEO image. Fallback order: shareimage.jpg, the hero slide, then folderthumb.jpg.',
    type: 'Page type stored with the page values.',
    date: 'Publication date (YYYY-MM-DD).',
    relatedVehicleType: 'Suggestion strip vehicleType filter.',
    relatedMakeType: 'Suggestion strip makeType filter.',
    relatedVehicleName: 'Suggestion strip vehicleName filter.',
    overrideIds: 'Override IDs — comma-separated inventory numbers.',
};

function field(label, type, value, key) {
    const wrap = el('label', { class: 'field' });
    wrap.append(document.createTextNode(label));
    const input = el('input', { type, 'data-key': key, value, title: FIELD_TIPS[key] || '' });
    if (!FIELD_TIPS[key]) input.removeAttribute('title');
    if (FIELD_TIPS[key]) wrap.title = FIELD_TIPS[key];
    wrap.append(input);
    return { wrap, input };
}

function areaField(label, value, key) {
    const wrap = el('label', { class: 'field span-2' });
    wrap.append(document.createTextNode(label));
    const input = el('textarea', { 'data-key': key, rows: '8', title: FIELD_TIPS[key] || '' });
    if (!FIELD_TIPS[key]) input.removeAttribute('title');
    if (FIELD_TIPS[key]) wrap.title = FIELD_TIPS[key];
    input.value = value;
    wrap.append(input);
    return { wrap, input };
}

function selectField(label, value, key, options) {
    const wrap = el('label', { class: 'field' });
    wrap.append(document.createTextNode(label));
    const input = el('select', { 'data-key': key, title: FIELD_TIPS[key] || '' });
    if (!FIELD_TIPS[key]) input.removeAttribute('title');
    if (FIELD_TIPS[key]) wrap.title = FIELD_TIPS[key];
    for (const optionValue of options) {
        const option = el('option', { value: optionValue, text: optionValue });
        if (optionValue === value) option.selected = true;
        input.append(option);
    }
    wrap.append(input);
    return { wrap, input };
}

function cleanText(value) {
    return String(value ?? '')
        .replace(/\u00ad|&shy;|&#0*173;|&#x0*ad;/gi, '')
        .replace(/<[^>]+>/g, ' ')
        .replace(/&nbsp;/gi, ' ')
        .replace(/&amp;/gi, '&')
        .replace(/&quot;/gi, '"')
        .replace(/&#0*39;|&apos;/gi, "'")
        .replace(/&lt;/gi, '<')
        .replace(/&gt;/gi, '>')
        .replace(/\s+/g, ' ')
        .trim();
}

function truncateMeta(value) {
    const plain = String(value ?? '').trim();
    if (plain.length <= 155) return plain;
    let cut = plain.slice(0, 155);
    const lastSpace = cut.lastIndexOf(' ');
    if (lastSpace > 80) cut = cut.slice(0, lastSpace).replace(/[\s.,;:\-]+$/g, '');
    return `${cut}...`;
}

function el(tag, props = {}, children = []) {
    const node = document.createElement(tag);
    if (tag === 'button' && !props.type) node.type = 'button';
    for (const [key, value] of Object.entries(props)) {
        if (value == null || value === false) continue;
        if (key === 'class') node.className = value;
        else if (key === 'text') node.textContent = String(value);
        else if (value === true) node.setAttribute(key, '');
        else node.setAttribute(key, String(value));
    }
    for (const child of [].concat(children)) {
        if (child == null || child === false) continue;
        node.append(child instanceof Node ? child : document.createTextNode(String(child)));
    }
    return node;
}
