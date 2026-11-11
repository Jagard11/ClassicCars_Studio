import { getStudioConfig } from './config.js';
import { formatPriceLabel, normalizePrice, KNOWN_MAKES } from './details.js';
import { el } from './dom.js';
import { mountOverrideIds } from './override-ids.js';
import { createWidgetOrder } from './widget-order.js';

const gaId = () => getStudioConfig().googleMeasurementId || '';
const extremeLogin = () => getStudioConfig().extremeLogin || '';
const siteHost = () => getStudioConfig().siteHost || '';
const PAGE_TYPES = ['Article', 'NewsArticle', 'CollectionPage'];
const VEHICLE_TYPES = ['car', 'motorcycle', 'truck', 'sidecar'];
const PIDS = ['articles', 'awards', 'award', 'projects', 'newsletr', 'salesinv', 'parts', 'contacts', 'video', 'whtsnew'];
const PID_FOR_SECTION = {
    articles: 'articles',
    awards: 'awards',
    projects: 'projects',
    newsletter: 'newsletr',
    parts: 'parts',
    contact: 'contacts',
    video: 'video',
    whatsnew: 'whtsnew',
    sales: 'salesinv',
};
const INHERITED = new Set(['relatedSales', 'adminTracking', 'googleTracking', 'extremeTracking', 'newsletterSubscribe']);
const FIELD_TIPS = {
    title: 'Page title. Written into the browser tab, the share card, and the generated SEO title.',
    description: 'Page description used for SEO and the share card.',
    image: 'Share and SEO image. Fallback order: shareimage.jpg, the hero slide, then folderthumb.jpg.',
    type: 'Page type stored in the page-values comment.',
    date: 'Publication date (YYYY-MM-DD) stored in the page-values comment.',
    vehicleName: 'Sales listing $vehicleName shown as the vehicle title.',
    inventoryNumber: 'Sales listing $inventoryNumber. Used by search, jump, and the suggestion strip.',
    price: 'Sales listing $price. Use a number, Price on Request, or SOLD.',
    vehicleType: 'Sales listing $vehicleType.',
    makeType: 'Sales listing $makeType. Spaces are stored as underscores.',
    doNotRender: 'Sales listing $doNotRender. When set, the public site hides this vehicle.',
    relatedVehicleType: 'Suggestion strip vehicleType filter.',
    relatedMakeType: 'Suggestion strip makeType filter.',
    relatedVehicleName: 'Suggestion strip vehicleName filter.',
    overrideIds: 'Override IDs — inventory numbers pinned in this order. Hover a chip for the vehicle name, photo, and ID.',
    hasInclude: 'Include similar_vehicles.php?embed=1 on this page.',
    measurementId: 'Google Tracking (gtag.js) measurement ID.',
    extremeLogin: 'eXTReMe Pro Tracking account login.',
    extremePid: 'eXTReMe Pro Tracking page id (pid).',
    hasNoscript: 'Include the eXTReMe <noscript> tracking fallback.',
    searchImage: 'Search image override path.',
    nnId: 'New project inventory NN marker id.',
};
const HEAD_TIPS = {
    'error-check': 'Common Error Check — the same warnings and errors as the admin dashboard',
    pageValues: 'Page Values & Share Card — title, description, image, type, and date used for SEO',
    salesListing: 'Vehicle listing PHP variables ($vehicleName, $inventoryNumber, $price, $vehicleType, $makeType, $doNotRender)',
    relatedSales: 'Suggestion Widget Settings for similar_vehicles.php',
    googleTracking: 'Google Tracking (gtag.js)',
    extremeTracking: 'eXTReMe Pro Tracking',
    adminTracking: 'Admin Dashboard Tracking loaded by the site header',
    indexSalesLinks: 'Sales links included from this section index',
    projectInventory: 'Project inventory NN markers',
    searchImage: 'Search image override',
    newsletterArchive: 'Newsletter archive marker',
    newsletterSubscribe: 'Newsletter subscribe box',
};
const ACTION_TIPS = {
    'Apply Page Values': 'Apply & Sync SEO — write these page values into the page and the generated SEO block',
    'Apply Vehicle Listing': 'Write $vehicleName, $inventoryNumber, $price, $vehicleType, $makeType, and $doNotRender into the sales page',
    'Apply Related Sales': 'Apply Suggestion Settings for the similar-vehicles strip',
    'Apply Google Tag': 'Insert or repair the Google Tracking (gtag.js) tag',
    'Apply eXTReMe Tracker': 'Insert or repair the eXTReMe Pro Tracking tag',
    'Apply Search Image': 'Apply the search image override',
    'Upgrade to header.html': 'Insert or repair Admin Dashboard Tracking by switching this page to the site header include',
    Remove: 'Remove this block from the page source',
    'Run Newsletter Cleaner': 'Run the newsletter archive cleaner',
    '+ Add Widget': 'Insert this support block into the page',
    '+ Add Another Sales Link Include': 'Add another sales link include to this index page',
    '+ Add NN Marker': 'Add a project inventory NN marker',
};

export function mountWidgets(parent, ctx) {
    const root = el('div', { id: 'right-widgets', class: 'widget-list' });
    parent.append(root);
    const shelfOrder = createWidgetOrder(root, {
        initialOrder: ctx.initialOrder,
        onChange() { ctx.onWidgetState?.(collapsedMap()); },
    });
    let doc = null;
    let busy = false;
    let lastSnap = '';
    let relatedBufferTimer = 0;
    let relatedBufferSent = null;
    const widgetCollapsed = {};

    adoptCollapsed(ctx.initialCollapsed);

    function adoptCollapsed(map) {
        for (const key of Object.keys(widgetCollapsed)) delete widgetCollapsed[key];
        if (!map || typeof map !== 'object') return;
        for (const [key, value] of Object.entries(map)) {
            if (typeof value === 'boolean' && /^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(key)) widgetCollapsed[key] = value;
        }
    }

    function collapsedMap() {
        const out = {};
        for (const [key, value] of Object.entries(widgetCollapsed)) {
            if (value === true || value === false) out[key] = value;
        }
        return out;
    }

    function isCollapsed(name) {
        return widgetCollapsed[name] === true;
    }

    function setWidgetCollapsed(name, collapsed, { persist = true } = {}) {
        if (!name) return;
        widgetCollapsed[name] = Boolean(collapsed);
        if (persist) ctx.onWidgetState?.(collapsedMap());
    }

    function applyCollapse(section) {
        if (!section) return;
        const name = section.dataset.widget || '';
        const collapsed = isCollapsed(name);
        section.classList.toggle('is-collapsed', collapsed);
        const body = [...section.children].find((node) => node.classList?.contains('widget-body'));
        if (body) body.hidden = collapsed;
        const toggle = section.querySelector('.widget-toggle');
        if (!toggle) return;
        const title = section.querySelector('h2')?.textContent || 'widget';
        toggle.textContent = collapsed ? '▸' : '▾';
        toggle.setAttribute('aria-expanded', collapsed ? 'false' : 'true');
        toggle.setAttribute('aria-label', `${collapsed ? 'Expand' : 'Collapse'} ${title}`);
        toggle.title = `${collapsed ? 'Expand' : 'Collapse'} ${title}`;
    }

    function setCollapsed(map) {
        adoptCollapsed(map);
        for (const section of root.querySelectorAll('[data-widget]')) applyCollapse(section);
    }

    function setDoc(next, { force = false } = {}) {
        const previous = doc?.path || '';
        doc = next;
        const path = next?.path || '';
        if (path !== previous) relatedBufferSent = null;
        const typing = !force
            && path
            && path === previous
            && root.contains(document.activeElement)
            && document.activeElement.matches('input, textarea, select');
        if (typing && root.childElementCount) renderStatus();
        else render();
    }

    function noteSource(content) {
        if (!doc || typeof content !== 'string') return;
        if ((doc.content || '') === content) return;
        doc.content = content;
        doc.sourceLoaded = true;
        const typing = root.contains(document.activeElement)
            && document.activeElement.matches('input, textarea, select');
        if (typing) return;
        const snap = shelfSnapshot(doc);
        if (snap === lastSnap && root.childElementCount) return;
        render();
    }

    function render() {
        shelfOrder.cancelDrag();
        const scroller = root.parentElement;
        const scrollTop = scroller ? scroller.scrollTop : 0;
        const parsed = supportOf(doc);
        root.replaceChildren();
        if (!doc) {
            root.append(el('p', { class: 'empty', text: 'Open a page to add support scripts.' }));
            lastSnap = '';
            return;
        }
        if (doc.isMedia || !isMarkupPage(doc)) {
            root.append(errorWidget());
            if (doc.isMedia) {
                root.append(el('p', {
                    class: 'field-hint',
                    text: 'This file is a document or media asset. Page-value and tracking checks apply to HTML and PHP pages.',
                }));
            }
            shelfOrder.arrange();
            lastSnap = shelfSnapshot(doc);
            updateActions();
            if (scroller) scroller.scrollTop = scrollTop;
            return;
        }
        root.append(errorWidget());
        if (isSalesVehicle(doc)) root.append(salesWidget(parsed));
        if (!isSalesVehicle(doc) || parsed.pageValues.present) root.append(pageWidget(parsed));
        root.append(relatedWidget(parsed));
        root.append(headerWidget(parsed));
        root.append(googleWidget(parsed));
        root.append(extremeWidget(parsed));
        root.append(indexWidget(parsed));
        root.append(projectWidget(parsed));
        root.append(searchWidget(parsed));
        root.append(archiveWidget(parsed));
        root.append(subscribeWidget(parsed));
        shelfOrder.arrange();
        paintShare();
        for (const input of root.querySelectorAll('[data-share-source]')) {
            input.addEventListener('input', paintShare);
        }
        const price = root.querySelector('[data-field="price"]');
        price?.addEventListener('input', () => {
            const hint = root.querySelector('[data-price-hint]');
            if (hint) hint.textContent = formatPriceLabel(price.value).label;
        });
        lastSnap = shelfSnapshot(doc);
        updateActions();
        if (scroller) scroller.scrollTop = scrollTop;
    }

    function renderStatus() {
        const summary = summarize(doc);
        setPill('errors', summary.label, summary.tone);
        const list = root.querySelector('[data-error-list]');
        if (list) list.replaceWith(buildErrorList());
        updateActions();
        paintShare();
    }

    function setPill(name, text, tone) {
        const node = root.querySelector(`[data-status="${name}"]`);
        if (!node) return;
        node.textContent = text;
        const widget = node.closest('[data-widget]')?.dataset.widget || '';
        const head = HEAD_TIPS[widget] || node.closest('.widget')?.querySelector('h2')?.textContent || '';
        node.title = head ? `${head}: ${text}` : text;
        if (tone) node.dataset.tone = tone;
        else delete node.dataset.tone;
    }

    function updateActions() {
        const ready = Boolean(doc && (doc.sourceLoaded || doc.content || doc.slug));
        for (const button of root.querySelectorAll('[data-action]')) {
            button.disabled = busy || !ready;
        }
    }

    async function submit(widget, extra = {}) {
        if (busy || !doc) return;
        const repair = extra.repair === true;
        const data = { ...readData(widget, { repair }), ...extra };
        delete data.repair;
        if (widget === 'relatedSales') {
            window.clearTimeout(relatedBufferTimer);
            if (typeof data.overrideIds === 'string') relatedBufferSent = data.overrideIds;
        }
        if ((widget === 'indexSalesLinks' || widget === 'projectInventory') && data.action === 'add' && !(Number(data.cursor) > 0)) {
            const cursor = Number(ctx.getCursor?.()?.selectionStart);
            if (cursor > 0) data.cursor = cursor;
        }
        openWidget(widget);
        busy = true;
        updateActions();
        try {
            await ctx.onApply(widget, data);
        } finally {
            busy = false;
            updateActions();
        }
    }

    function readData(widget, { repair = false } = {}) {
        if (widget === 'salesListing') {
            return {
                vehicleName: valueOf('vehicleName'),
                inventoryNumber: valueOf('inventoryNumber'),
                price: normalizePrice(valueOf('price')),
                vehicleType: valueOf('vehicleType') || 'car',
                makeType: valueOf('makeType'),
                doNotRender: checked('doNotRender'),
            };
        }
        if (widget === 'pageValues') {
            return {
                title: valueOf('title'),
                description: valueOf('description'),
                image: valueOf('image'),
                type: valueOf('type') || 'Article',
                date: valueOf('date'),
            };
        }
        if (widget === 'relatedSales') {
            return {
                vehicleType: valueOf('relatedVehicleType'),
                makeType: valueOf('relatedMakeType'),
                vehicleName: valueOf('relatedVehicleName'),
                overrideIds: valueOf('overrideIds'),
                hasInclude: repair ? true : checked('hasInclude'),
            };
        }
        if (widget === 'googleTracking') {
            return { measurementId: valueOf('measurementId') || gaId() };
        }
        if (widget === 'extremeTracking') {
            return {
                login: valueOf('extremeLogin') || extremeLogin(),
                pid: valueOf('extremePid') || defaultPid(doc, supportOf(doc)),
                hasNoscript: repair ? true : checked('hasNoscript'),
            };
        }
        if (widget === 'searchImage') return { path: valueOf('searchImage') };
        if (widget === 'indexSalesLinks') return { page: valueOf('indexPage') };
        if (widget === 'projectInventory') return { id: valueOf('nnId') };
        return {};
    }

    function valueOf(key) {
        const node = root.querySelector(`[data-field="${key}"]`);
        return node ? node.value : '';
    }

    function checked(key) {
        return Boolean(root.querySelector(`[data-field="${key}"]`)?.checked);
    }

    function openWidget(name) {
        const widget = root.querySelector(`[data-widget="${name}"]`);
        setWidgetCollapsed(name, false);
        if (!widget) return;
        applyCollapse(widget);
        widget.scrollIntoView({ block: 'nearest' });
    }

    function showWidgets(names, { focus = true } = {}) {
        const list = Array.isArray(names) ? names : [names];
        let first = null;
        let changed = false;
        for (const name of list) {
            const widget = root.querySelector(`[data-widget="${name}"]`);
            if (!widget) continue;
            if (isCollapsed(name)) {
                widgetCollapsed[name] = false;
                applyCollapse(widget);
                changed = true;
            }
            if (!first) first = widget;
        }
        if (changed) ctx.onWidgetState?.(collapsedMap());
        if (!first) return false;
        first.scrollIntoView({ block: 'start' });
        if (focus) {
            const field = first.querySelector('input:not([readonly]), textarea, select, button:not(.widget-toggle):not(.widget-drag-handle)');
            (field || first.querySelector('.widget-toggle'))?.focus();
        }
        return true;
    }

    function errorWidget() {
        const summary = summarize(doc);
        const body = el('div', { class: 'widget-body' });
        body.append(el('p', {
            class: 'field-hint',
            text: 'Same checks as the admin dashboard. Tools → Run Common Error Check refreshes this list.',
        }));
        body.append(buildErrorList());
        return shell('error-check', 'Common Error Check', {
            body,
            badge: summary.label,
            tone: summary.tone,
            status: 'errors',
            extraClass: 'widget-check',
        });
    }

    function buildErrorList() {
        const list = el('div', { class: 'error-list', 'data-error-list': 'true' });
        const items = Array.isArray(doc?.errors) ? doc.errors : [];
        if (!doc) {
            list.append(el('p', { class: 'empty', text: 'Open a page to run the admin dashboard checks.' }));
            return list;
        }
        if (!items.length) {
            const text = doc.explicitErrors
                ? 'All admin dashboard checks passed.'
                : 'Gallery warnings show here. The full dashboard check list appears when the page API returns it.';
            list.append(el('p', { class: 'empty', text }));
            return list;
        }
        for (const item of items) list.append(errorItem(item));
        return list;
    }

    function errorItem(item) {
        const row = el('article', { class: 'error-item', 'data-severity': item.severity === 'warning' ? 'warning' : 'error' });
        const copy = el('div');
        copy.append(el('h3', { text: item.title || 'Check' }));
        if (item.message) copy.append(el('p', { text: item.message }));
        row.append(copy);
        row.title = [item.title, item.message].filter(Boolean).join(' — ') || 'Common Error Check item';
        if (item.fixable) {
            const button = el('button', { class: 'btn', type: 'button', text: 'Fix', title: `Fix: ${item.title || 'this Common Error Check item'}` });
            button.addEventListener('click', () => {
                const kind = widgetForError(item);
                if (kind === 'gallery') {
                    ctx.onOpenGallery();
                    return;
                }
                openWidget(kind);
                void submit(kind, { repair: true, enabled: true });
            });
            row.append(button);
        }
        return row;
    }

    function salesWidget(parsed) {
        const sales = parsed.salesListing;
        const body = el('div', { class: 'widget-body' });
        body.append(el('p', { class: 'field-hint', text: 'Sales pages store these fields in the PHP header.' }));
        body.append(textField('Vehicle Name', 'vehicleName', sales.vehicleName || '', { share: true }));
        body.append(textField('Inventory Number', 'inventoryNumber', sales.inventoryNumber || ''));
        const price = textField('Price', 'price', sales.price || '', {
            hint: formatPriceLabel(sales.price, sales.sold).label,
            hintAttr: 'data-price-hint',
        });
        const pills = el('div', { class: 'quick-row' });
        for (const label of ['Price on Request', 'SOLD']) {
            const button = el('button', {
                class: 'btn',
                type: 'button',
                text: label,
                title: label === 'SOLD' ? 'Set $price to SOLD' : 'Set $price to Price on Request',
            });
            button.addEventListener('click', () => {
                const input = price.querySelector('input');
                input.value = label;
                input.dispatchEvent(new Event('input', { bubbles: true }));
            });
            pills.append(button);
        }
        price.append(pills);
        body.append(price);
        body.append(selectField('Vehicle Type', 'vehicleType', sales.vehicleType || 'car', withCurrent(VEHICLE_TYPES, sales.vehicleType)));
        body.append(makeField('Make', 'makeType', sales.makeType || ''));
        body.append(checkField('Hidden ($doNotRender)', 'doNotRender', Boolean(sales.doNotRender)));
        body.append(actionButton('Apply Vehicle Listing', 'salesListing'));
        body.append(el('div', { 'data-share-mount': 'true' }));
        return shell('salesListing', 'Vehicle Listing PHP Variables', { body });
    }

    function pageWidget(parsed) {
        const page = parsed.pageValues;
        if (!page.present) {
            return shell('pageValues', 'Page Values & Generated SEO', {
                idle: true,
                add: addButton('pageValues', {
                    title: '',
                    description: '',
                    image: '',
                    type: 'Article',
                    date: '',
                }),
            });
        }
        const body = el('div', { class: 'widget-body' });
        body.append(el('p', { class: 'field-hint', text: page.seo }));
        body.append(textField('Title', 'title', page.title || '', { share: true }));
        body.append(areaField('Description', 'description', page.description || '', { share: true }));
        body.append(imageField(page.image || ''));
        body.append(selectField('Type', 'type', page.type || 'Article', typeOptions(page.type)));
        body.append(textField('Date', 'date', page.date || '', {
            type: /^\d{4}-\d{2}-\d{2}$/.test(page.date || '') || !page.date ? 'date' : 'text',
            hint: 'YYYY-MM-DD',
        }));
        const actions = el('div', { class: 'widget-actions' });
        actions.append(actionButton('Apply Page Values', 'pageValues'));
        actions.append(jumpButton('Jump to Code', '<!-- page-values'));
        actions.append(actionButton('Remove', 'pageValues', { enabled: false }, false));
        body.append(actions);
        body.append(el('div', { 'data-share-mount': 'true' }));
        return shell('pageValues', 'Page Values & Generated SEO', { body, badge: page.seoToneLabel, tone: page.seoTone });
    }

    function stageRelatedSales() {
        window.clearTimeout(relatedBufferTimer);
        relatedBufferTimer = window.setTimeout(() => {
            if (!doc) return;
            const next = valueOf('overrideIds');
            if (next === relatedBufferSent) return;
            if (busy) {
                stageRelatedSales();
                return;
            }
            void submit('relatedSales');
        }, 120);
    }

    function relatedWidget(parsed) {
        const related = parsed.relatedSales;
        if (inheritedOnly(doc, 'relatedSales', related.present)) return inheritedShell('relatedSales', 'Related Sales Suggestion Strip');
        if (!related.present) {
            return shell('relatedSales', 'Related Sales Suggestion Strip', {
                idle: true,
                add: addButton('relatedSales', {
                    vehicleType: 'car',
                    makeType: '',
                    vehicleName: '',
                    overrideIds: '',
                    hasInclude: true,
                }),
            });
        }
        const body = el('div', { class: 'widget-body' });
        body.append(selectField(
            'Vehicle Type',
            'relatedVehicleType',
            related.vehicleType || '',
            withCurrent(['', ...VEHICLE_TYPES], related.vehicleType),
            { labels: { '': '(any / blank)' } },
        ));
        body.append(makeField('Make', 'relatedMakeType', related.makeType || ''));
        body.append(textField('Vehicle Name', 'relatedVehicleName', related.vehicleName || ''));
        body.append(mountOverrideIds({
            value: related.overrideIds || '',
            path: doc?.path || '',
            tip: FIELD_TIPS.overrideIds,
            onChange: stageRelatedSales,
        }));
        body.append(checkField('Include similar_vehicles.php?embed=1', 'hasInclude', Boolean(related.hasInclude)));
        const actions = el('div', { class: 'widget-actions' });
        actions.append(actionButton('Apply Related Sales', 'relatedSales'));
        actions.append(jumpButton('Jump to Code', '<!-- related-sales'));
        actions.append(actionButton('Remove', 'relatedSales', { enabled: false }, false));
        body.append(actions);
        return shell('relatedSales', 'Related Sales Suggestion Strip', { body });
    }

    function headerWidget(parsed) {
        const admin = parsed.adminTracking;
        if (inheritedOnly(doc, 'adminTracking', admin.present || admin.legacy)) {
            return inheritedShell('adminTracking', 'Site Header & Admin Traffic Logger');
        }
        if (!admin.present && !admin.legacy) {
            return shell('adminTracking', 'Site Header & Admin Traffic Logger', {
                idle: true,
                add: addButton('adminTracking', { enabled: true }),
            });
        }
        const body = el('div', { class: 'widget-body' });
        const checks = el('ul', { class: 'check-list' });
        checks.append(checkItem(admin.present, true, 'Site Header SSI', '<!--#include virtual="/includes/header.html"-->'));
        checks.append(checkItem(admin.present || admin.recordsViaLogview, true, 'Server-Side View Logger', '/php/logview.php'));
        checks.append(checkItem(admin.present || admin.hasTrackJs, true, 'First-Party Beacon', '/includes/track.js + /php/hit.php'));
        body.append(checks);
        body.append(el('p', { class: 'field-hint', text: 'header.html loads logview.php and track.js. Studio preview keeps traffic exclusion on.' }));
        const actions = el('div', { class: 'widget-actions' });
        if (admin.legacy) actions.append(actionButton('Upgrade to header.html', 'adminTracking', { upgrade: true, enabled: true }));
        actions.append(jumpButton('Jump to Header Include', admin.present ? '/includes/header.html' : 'trackerforfeatures.html'));
        actions.append(actionButton('Remove', 'adminTracking', { enabled: false }, false));
        body.append(actions);
        return shell('adminTracking', 'Site Header & Admin Traffic Logger', { body });
    }

    function googleWidget(parsed) {
        const google = parsed.googleTracking;
        if (inheritedOnly(doc, 'googleTracking', google.present)) return inheritedShell('googleTracking', 'Google Analytics');
        if (!google.present) {
            return shell('googleTracking', 'Google Analytics', {
                idle: true,
                add: addButton('googleTracking', { measurementId: gaId(), enabled: true }),
            });
        }
        const body = el('div', { class: 'widget-body' });
        body.append(textField('Measurement ID', 'measurementId', google.measurementId || gaId()));
        const actions = el('div', { class: 'widget-actions' });
        actions.append(actionButton('Apply Google Tag', 'googleTracking'));
        actions.append(jumpButton('Jump to Code', '<!-- Google tag (gtag.js) -->'));
        actions.append(actionButton('Remove', 'googleTracking', { enabled: false }, false));
        body.append(actions);
        return shell('googleTracking', 'Google Analytics', { body });
    }

    function extremeWidget(parsed) {
        const extreme = parsed.extremeTracking;
        if (inheritedOnly(doc, 'extremeTracking', extreme.present)) return inheritedShell('extremeTracking', 'eXTReMe Pro Tracker');
        if (!extreme.present) {
            return shell('extremeTracking', 'eXTReMe Pro Tracker', {
                idle: true,
                add: addButton('extremeTracking', {
                    login: extremeLogin(),
                    pid: defaultPid(doc, parsed),
                    hasNoscript: true,
                    enabled: true,
                }),
            });
        }
        const body = el('div', { class: 'widget-body' });
        body.append(textField('Login', 'extremeLogin', extreme.login || extremeLogin()));
        body.append(selectField('Page ID (pid)', 'extremePid', extreme.pid || defaultPid(doc, parsed), pidOptions(extreme.pid || defaultPid(doc, parsed))));
        body.append(checkField('Include <noscript> fallback', 'hasNoscript', Boolean(extreme.hasNoscript)));
        const actions = el('div', { class: 'widget-actions' });
        actions.append(actionButton('Apply eXTReMe Tracker', 'extremeTracking'));
        actions.append(jumpButton('Jump to Code', '<!-- Start eXTReMe Pro Tracker Code -->'));
        actions.append(actionButton('Remove', 'extremeTracking', { enabled: false }, false));
        body.append(actions);
        return shell('extremeTracking', 'eXTReMe Pro Tracker', { body });
    }

    function indexWidget(parsed) {
        const index = parsed.indexSales;
        if (!index.present) {
            return shell('indexSalesLinks', 'Section Index Sales Links', {
                idle: true,
                add: addButton('indexSalesLinks', { action: 'add', page: '' }),
            });
        }
        const body = el('div', { class: 'widget-body' });
        const list = el('div', { class: 'marker-list' });
        index.items.forEach((item, number) => {
            const row = el('div', { class: 'marker-row', 'data-index-row': String(number) });
            const input = el('input', {
                type: 'text',
                value: item.page,
                spellcheck: 'false',
                autocomplete: 'off',
                'aria-label': `Sales link ${number + 1}`,
                title: `Sales link ${number + 1} — page included from this index`,
            });
            const jump = el('button', { class: 'btn', type: 'button', text: 'Jump', title: 'Jump to this sales link in the code editor' });
            jump.addEventListener('click', () => jumpTo(item.token));
            const update = el('button', { class: 'btn btn-primary', type: 'button', text: 'Apply', title: 'Apply this sales link', 'data-action': 'indexSalesLinks' });
            update.addEventListener('click', () => {
                void submit('indexSalesLinks', { action: 'update', index: number, page: input.value });
            });
            const remove = el('button', { class: 'btn', type: 'button', text: 'Remove', title: 'Remove this sales link', 'data-action': 'indexSalesLinks' });
            remove.addEventListener('click', () => {
                void submit('indexSalesLinks', { action: 'remove', index: number });
            });
            row.append(input, jump, update, remove);
            list.append(row);
        });
        body.append(list);
        body.append(actionButton('+ Add Another Sales Link Include', 'indexSalesLinks', {
            action: 'add',
            page: '',
        }));
        return shell('indexSalesLinks', 'Section Index Sales Links', { body });
    }

    function projectWidget(parsed) {
        const project = parsed.projectInventory;
        if (!project.present) {
            return shell('projectInventory', 'Project Inventory Validator', {
                idle: true,
                add: addButton('projectInventory', { action: 'add', id: '1000' }),
            });
        }
        const body = el('div', { class: 'widget-body' });
        const list = el('div', { class: 'marker-list' });
        project.items.forEach((item, number) => {
            const row = el('div', { class: 'marker-row' });
            const input = el('input', {
                type: 'text',
                value: item.id,
                spellcheck: 'false',
                autocomplete: 'off',
                'aria-label': `NN ${item.id}`,
                title: `Project inventory marker NN ${item.id}`,
            });
            const jump = el('button', { class: 'btn', type: 'button', text: 'Jump', title: 'Jump to this NN marker in the code editor' });
            jump.addEventListener('click', () => jumpTo(`<!-- NN ${item.id}`));
            const update = el('button', { class: 'btn btn-primary', type: 'button', text: 'Apply', title: 'Apply this NN marker', 'data-action': 'projectInventory' });
            update.addEventListener('click', () => {
                void submit('projectInventory', { action: 'update', index: number, id: input.value.trim() });
            });
            const remove = el('button', { class: 'btn', type: 'button', text: 'Remove', title: 'Remove this NN marker', 'data-action': 'projectInventory' });
            remove.addEventListener('click', () => {
                void submit('projectInventory', { action: 'remove', index: number });
            });
            row.append(input, jump, update, remove);
            list.append(row);
        });
        body.append(list);
        body.append(textField('New NN id', 'nnId', ''));
        body.append(actionButton('+ Add NN Marker', 'projectInventory', { action: 'add' }));
        return shell('projectInventory', 'Project Inventory Validator', { body });
    }

    function searchWidget(parsed) {
        const search = parsed.searchImage;
        if (!search.present) {
            return shell('searchImage', 'Search Image Override', {
                idle: true,
                add: addButton('searchImage', { path: '', enabled: true }),
            });
        }
        const body = el('div', { class: 'widget-body' });
        body.append(imageField(search.path || '', { key: 'searchImage', label: 'Search Image Path' }));
        const actions = el('div', { class: 'widget-actions' });
        actions.append(actionButton('Apply Search Image', 'searchImage'));
        actions.append(jumpButton('Jump to Code', 'SEARCH_IMAGE:'));
        actions.append(actionButton('Remove', 'searchImage', { enabled: false }, false));
        body.append(actions);
        return shell('searchImage', 'Search Image Override', { body });
    }

    function archiveWidget(parsed) {
        const archive = parsed.newsletterArchive;
        if (!archive.present) {
            return shell('newsletterArchive', 'Newsletter Archive Marker', {
                idle: true,
                add: addButton('newsletterArchive', { enabled: true }),
            });
        }
        const body = el('div', { class: 'widget-body' });
        body.append(el('p', { class: 'field-hint', text: 'Managed by clean_newsletter_archive.py. Cleaning rewrites the newsletter archive and does not stage git.' }));
        const actions = el('div', { class: 'widget-actions' });
        actions.append(actionButton('Run Newsletter Cleaner', 'newsletterArchive', { clean: true, enabled: true }));
        actions.append(jumpButton('Jump to Code', '<!-- newsletter-archive -->'));
        actions.append(actionButton('Remove', 'newsletterArchive', { enabled: false }, false));
        body.append(actions);
        return shell('newsletterArchive', 'Newsletter Archive Marker', { body, badge: 'Active', tone: 'ok' });
    }

    function subscribeWidget(parsed) {
        const subscribe = parsed.newsletterSubscribe;
        if (inheritedOnly(doc, 'newsletterSubscribe', subscribe.present)) {
            return inheritedShell('newsletterSubscribe', 'Newsletter Subscribe Box');
        }
        if (!subscribe.present) {
            return shell('newsletterSubscribe', 'Newsletter Subscribe Box', {
                idle: true,
                add: addButton('newsletterSubscribe', { enabled: true }),
            });
        }
        const body = el('div', { class: 'widget-body' });
        body.append(el('p', { class: 'field-hint', text: 'Includes /includes/subscribe.html' }));
        const actions = el('div', { class: 'widget-actions' });
        actions.append(jumpButton('Jump to Code', '/includes/subscribe.html'));
        actions.append(actionButton('Remove', 'newsletterSubscribe', { enabled: false }, false));
        body.append(actions);
        return shell('newsletterSubscribe', 'Newsletter Subscribe Box', { body, badge: 'Active', tone: 'ok' });
    }

    function inheritedShell(name, title) {
        return shell(name, title, { idle: true, badge: 'Inherited via php/inventory.php', tone: 'ok' });
    }

    function paintShare() {
        const mount = root.querySelector('[data-share-mount]');
        if (!mount) return;
        mount.replaceChildren(buildShareCard());
    }

    function buildShareCard() {
        const share = doc?.widgets?.share || {};
        const hero = Array.isArray(doc?.images) ? doc.images[0] : null;
        const title = valueOf('title') || valueOf('vehicleName') || share.title || 'No share title is set.';
        const description = valueOf('description') || share.description || 'No description is set.';
        const steps = [
            { label: 'shareimage.jpg', ok: Boolean(share.hasShareImage), url: share.shareImageUrl || '' },
            { label: 'Hero slide', ok: Boolean(hero && hero.hasSlide !== false && hero.slideUrl), url: hero?.slideUrl || '' },
            { label: 'folderthumb.jpg', ok: Boolean(share.hasFolderThumb), url: share.folderThumbUrl || '' },
        ];
        const effective = share.effectiveShareImage || steps.find((step) => step.ok)?.url || '';
        const card = el('article', {
            class: effective ? 'share-card' : 'share-card is-text-only',
            title: 'Share card preview — how this page looks when the link is shared',
        });
        if (effective) {
            const media = el('div', { class: 'share-card-media' });
            const image = el('img', { src: safeResource(effective), alt: title });
            image.addEventListener('error', () => {
                media.replaceChildren(el('p', { class: 'share-card-text', text: 'The share image file is missing.' }));
            });
            media.append(image);
            card.append(media);
        }
        const cardBody = el('div', { class: 'share-card-body' });
        cardBody.append(el('p', { class: 'share-card-title', text: title }));
        cardBody.append(el('p', { class: 'share-card-text', text: description }));
        cardBody.append(el('p', { class: 'share-card-host', text: siteHost() }));
        card.append(cardBody);
        const used = steps.find((step) => step.ok && sameResource(step.url, effective));
        const chain = el('ol', { class: 'chain' });
        for (const step of steps) {
            const item = el('li');
            const usedHere = used && used.label === step.label;
            item.title = `${step.label} — ${step.ok ? 'on disk' : 'missing'}${usedHere ? ', currently used' : ''}. Fallback order: shareimage.jpg, then the hero slide, then folderthumb.jpg.`;
            item.append(el('span', {
                class: step.ok ? 'dot is-ok' : 'dot',
                title: step.ok ? `${step.label} is on disk` : `${step.label} is missing`,
            }));
            const copy = el('div');
            copy.append(el('div', { class: 'chain-label', text: step.label }));
            copy.append(el('div', { class: 'chain-url', text: step.url || 'Not on disk' }));
            item.append(copy);
            item.append(el('span', { class: 'used-tag', text: used && used.label === step.label ? 'Used' : '' }));
            chain.append(item);
        }
        const requested = valueOf('image') || valueOf('searchImage');
        const wrap = el('div', { class: 'share-block' });
        wrap.append(el('h3', { text: 'Share card' }));
        wrap.append(card);
        wrap.append(el('p', { class: 'field-hint', text: used ? `Fallback in use: ${used.label}.` : 'No share image file is available yet.' }));
        if (requested) wrap.append(el('p', { class: 'field-hint', text: `Page image: ${requested}` }));
        wrap.append(chain);
        return wrap;
    }

    function shell(name, title, { idle = false, badge = '', tone = '', body = null, add = null, extraClass = '', status = '' } = {}) {
        const classes = ['widget', 'widget-card', idle ? 'is-idle' : 'is-active'];
        if (extraClass) classes.push(extraClass);
        if (isCollapsed(name)) classes.push('is-collapsed');
        const section = el('section', {
            class: classes.join(' '),
            'data-widget': name,
            'data-state': idle ? 'idle' : 'active',
        });
        const head = el('header', { class: 'widget-head' });
        const collapsed = isCollapsed(name);
        const toggle = el('button', {
            class: 'widget-toggle',
            type: 'button',
            text: collapsed ? '▸' : '▾',
            'aria-expanded': collapsed ? 'false' : 'true',
            'aria-label': `${collapsed ? 'Expand' : 'Collapse'} ${title}`,
            title: `${collapsed ? 'Expand' : 'Collapse'} ${title}`,
        });
        if (body) {
            body.id = `widget-body-${name}`;
            toggle.setAttribute('aria-controls', body.id);
            if (collapsed) body.hidden = true;
        }
        toggle.addEventListener('click', () => {
            setWidgetCollapsed(name, !isCollapsed(name));
            applyCollapse(section);
        });
        head.append(shelfOrder.handle(title), toggle, el('h2', { text: title, title: HEAD_TIPS[name] || title }));
        if (badge) {
            const pill = el('span', { class: 'pill', text: badge, title: `${HEAD_TIPS[name] || title}: ${badge}` });
            if (tone) pill.dataset.tone = tone;
            if (status) pill.dataset.status = status;
            head.append(pill);
        }
        if (add) head.append(add);
        section.append(head);
        if (body) section.append(body);
        return section;
    }

    function addButton(widget, extra) {
        const button = el('button', {
            class: 'btn widget-add',
            type: 'button',
            text: '+ Add Widget',
            title: 'Insert this support block into the page',
            'data-action': widget,
        });
        button.addEventListener('click', () => {
            void submit(widget, { adding: true, enabled: true, ...extra });
        });
        return button;
    }

    function actionButton(label, widget, extra = {}, primary = true) {
        const button = el('button', {
            class: primary ? 'btn btn-primary' : 'btn',
            type: 'button',
            text: label,
            title: ACTION_TIPS[label] || label,
            'data-action': widget,
        });
        button.addEventListener('click', () => { void submit(widget, extra); });
        return button;
    }

    function jumpButton(label, needle) {
        const button = el('button', { class: 'btn', type: 'button', text: label, title: `Jump to this block in the code editor — ${label}` });
        button.addEventListener('click', () => jumpTo(needle));
        return button;
    }

    function jumpTo(needle) {
        const content = ctx.getSource?.() || doc?.content || '';
        const at = content.indexOf(needle);
        if (at < 0) {
            ctx.onAnnounce?.('That comment is not in the current source.');
            return;
        }
        ctx.onJump?.({ startOffset: at, endOffset: at + needle.length, label: needle.slice(0, 48) });
    }

    function textField(label, key, value, { type = 'text', hint = '', share = false, readonly = false, hintAttr = '' } = {}) {
        const wrap = el('label', { class: 'field' });
        wrap.append(document.createTextNode(label));
        const input = el('input', {
            type,
            'data-field': key,
            value,
            autocomplete: 'off',
            spellcheck: 'false',
        });
        if (share) input.setAttribute('data-share-source', 'true');
        if (readonly) input.readOnly = true;
        if (FIELD_TIPS[key]) wrap.title = FIELD_TIPS[key];
        wrap.append(input);
        if (hint) {
            const node = el('p', { class: 'field-hint', text: hint });
            if (hintAttr) node.setAttribute(hintAttr, 'true');
            wrap.append(node);
        }
        return wrap;
    }

    function areaField(label, key, value, { share = false } = {}) {
        const wrap = el('label', { class: 'field' });
        wrap.append(document.createTextNode(label));
        const input = el('textarea', { 'data-field': key, rows: '4' });
        input.value = value;
        if (share) input.setAttribute('data-share-source', 'true');
        if (FIELD_TIPS[key]) wrap.title = FIELD_TIPS[key];
        wrap.append(input);
        return wrap;
    }

    function selectField(label, key, value, options, { labels = {} } = {}) {
        const wrap = el('label', { class: 'field' });
        wrap.append(document.createTextNode(label));
        const input = el('select', { 'data-field': key });
        for (const optionValue of options) {
            const option = el('option', { value: optionValue, text: labels[optionValue] || optionValue || '(any / blank)' });
            if (optionValue === value) option.selected = true;
            input.append(option);
        }
        if (FIELD_TIPS[key]) wrap.title = FIELD_TIPS[key];
        wrap.append(input);
        return wrap;
    }

    function makeField(label, key, value) {
        const field = textField(label, key, value);
        const input = field.querySelector('input');
        input.setAttribute('list', 'widget-makes');
        if (!root.querySelector('#widget-makes')) {
            const list = el('datalist', { id: 'widget-makes' });
            const names = new Set(KNOWN_MAKES);
            if (value) names.add(value);
            for (const name of ctx.getMakes?.() || []) if (name) names.add(name);
            for (const name of [...names].sort((a, b) => a.localeCompare(b))) {
                list.append(el('option', { value: name }));
                if (name.includes(' ')) list.append(el('option', { value: name.replace(/ /g, '_') }));
            }
            root.append(list);
        }
        return field;
    }

    function imageField(value, { key = 'image', label = 'Image' } = {}) {
        const field = textField(label, key, value, { hint: 'A file beside the page, or a slide path such as slides/hero.jpg.' });
        const input = field.querySelector('input');
        const listId = key === 'image' ? 'widget-images' : 'widget-search-images';
        input.setAttribute('list', listId);
        const list = el('datalist', { id: listId });
        for (const option of imageOptions(doc, value)) list.append(el('option', { value: option }));
        field.append(list);
        return field;
    }

    function checkField(label, key, on) {
        const wrap = el('label', { class: 'field check' });
        const input = el('input', { type: 'checkbox', 'data-field': key });
        input.checked = on;
        if (FIELD_TIPS[key]) wrap.title = FIELD_TIPS[key];
        wrap.append(input, document.createTextNode(` ${label}`));
        return wrap;
    }

    function setOrder(list) {
        shelfOrder.setOrder(list);
        if (root.querySelector('.widget-card')) render();
    }

    return {
        setDoc,
        noteSource,
        readData,
        showWidgets,
        setCollapsed,
        collapsedMap,
        setOrder,
        widgetOrder: () => shelfOrder.ids(),
    };
}

function isMarkupPage(current) {
    return /\.(php|html?|shtml)$/i.test(String(current?.path || ''));
}

function supportOf(current) {
    return parseSupport(current?.content || '', current);
}

function shelfSnapshot(current) {
    const parsed = supportOf(current);
    return JSON.stringify({
        path: current?.path || '',
        sales: isSalesVehicle(current),
        errors: (current?.errors || []).map((item) => `${item.severity || ''}:${item.title || ''}`).join('|'),
        page: parsed.pageValues,
        related: parsed.relatedSales,
        admin: parsed.adminTracking,
        google: parsed.googleTracking,
        extreme: parsed.extremeTracking,
        index: parsed.indexSales,
        project: parsed.projectInventory,
        search: parsed.searchImage,
        archive: parsed.newsletterArchive,
        subscribe: parsed.newsletterSubscribe,
        listing: parsed.salesListing,
    });
}

function isSalesVehicle(current) {
    return /^\/sales\/[^/]+\/index\.php$/i.test(current?.path || '');
}

function inheritedOnly(current, name, present) {
    return isSalesVehicle(current) && INHERITED.has(name) && !present;
}

function defaultPid(current, parsed) {
    if (isSalesVehicle(current)) {
        const id = parsed?.salesListing?.inventoryNumber || '';
        if (/^\d+$/.test(id)) return id;
    }
    return PID_FOR_SECTION[current?.section] || 'articles';
}

function parseSupport(text, current) {
    const source = String(text || '');
    const server = current?.widgets || {};
    const parsed = {
        pageValues: parsePageValues(source),
        relatedSales: parseRelated(source),
        adminTracking: parseHeader(source),
        googleTracking: parseGoogle(source),
        extremeTracking: parseExtreme(source),
        indexSales: parseIndexLinks(source),
        projectInventory: parseNn(source),
        searchImage: parseSearchImage(source),
        newsletterArchive: parseArchive(source),
        newsletterSubscribe: parseSubscribe(source),
        salesListing: parseSalesListing(source, current),
    };
    if (!source) applyServer(parsed, server, current);
    return parsed;
}

function applyServer(parsed, server, current) {
    const page = server.pageValues || {};
    const related = server.relatedSales || {};
    const google = server.googleTracking || {};
    const extreme = server.extremeTracking || {};
    const admin = server.adminTracking || {};
    const details = current?.details || {};
    parsed.pageValues = {
        ...parsed.pageValues,
        present: Boolean(page.present),
        title: page.title || '',
        description: page.description || '',
        image: page.image || '',
        type: page.type || 'Article',
        date: page.date || '',
    };
    parsed.relatedSales = {
        present: Boolean(related.present || related.hasInclude),
        hasInclude: Boolean(related.hasInclude),
        vehicleType: related.vehicleType || '',
        makeType: related.makeType || '',
        vehicleName: related.vehicleName || '',
        overrideIds: related.overrideIds || '',
    };
    parsed.googleTracking = {
        present: Boolean(google.present) && !google.inheritedFromInventoryPhp,
        measurementId: google.measurementId || gaId(),
    };
    parsed.extremeTracking = {
        present: Boolean(extreme.present) && !extreme.inheritedFromInventoryPhp,
        login: extreme.login || extremeLogin(),
        pid: extreme.pid || '',
        hasNoscript: Boolean(extreme.hasNoscript),
    };
    parsed.adminTracking = {
        present: Boolean(admin.hasHeaderInclude) && !admin.inheritedFromInventoryPhp,
        legacy: false,
        recordsViaLogview: Boolean(admin.recordsViaLogview),
        hasTrackJs: Boolean(admin.hasTrackJs),
    };
    if (details.vehicleName || details.inventoryNumber) {
        parsed.salesListing = {
            present: true,
            vehicleName: details.vehicleName || '',
            inventoryNumber: details.inventoryNumber || '',
            price: details.price || '',
            vehicleType: details.vehicleType || 'car',
            makeType: details.makeType || '',
            doNotRender: Boolean(details.doNotRender),
            sold: Boolean(details.sold),
        };
    }
}

function parsePageValues(text) {
    const block = commentBlock(text, 'page-values');
    const title = block ? commentField(block.body, 'title') : '';
    const description = block ? commentField(block.body, 'description') : '';
    const seo = seoStatus(text, title, description);
    return {
        present: Boolean(block),
        title,
        description,
        image: block ? commentField(block.body, 'image') : '',
        type: (block && commentField(block.body, 'type')) || 'Article',
        date: block ? commentField(block.body, 'date') : '',
        seo: seo.text,
        seoTone: seo.tone,
        seoToneLabel: seo.label,
    };
}

function parseRelated(text) {
    const block = commentBlock(text, 'related-sales');
    const hasInclude = /<!--#include\s+virtual="[^"]*similar_vehicles\.php[^"]*"\s*-->/i.test(text);
    return {
        present: Boolean(block) || hasInclude,
        hasInclude,
        vehicleType: block ? commentField(block.body, 'vehicleType') : '',
        makeType: block ? commentField(block.body, 'makeType') : '',
        vehicleName: block ? commentField(block.body, 'vehicleName') : '',
        overrideIds: block ? commentField(block.body, 'Override IDs') : '',
    };
}

function parseHeader(text) {
    const present = /\/includes\/header\.html/i.test(text) || /traffic_public_header/.test(text);
    return {
        present,
        legacy: /trackerforfeatures\.html/i.test(text) || /\/includes\/mainmenu\.html/i.test(text),
        recordsViaLogview: present || /logview\.php/.test(text),
        hasTrackJs: present || /\/includes\/track\.js/.test(text),
    };
}

function parseGoogle(text) {
    const id = text.match(/googletagmanager\.com\/gtag\/js\?id=([A-Za-z0-9_-]+)/i)
        || text.match(/gtag\(\s*['"]config['"]\s*,\s*['"]([A-Za-z0-9_-]+)['"]/i);
    return {
        present: /<!--\s*Google tag \(gtag\.js\)\s*-->/i.test(text) || /googletagmanager\.com\/gtag\/js/i.test(text),
        measurementId: id ? id[1] : gaId(),
    };
}

function parseExtreme(text) {
    const login = text.match(/[?&]login=([A-Za-z0-9_-]+)/);
    const pid = text.match(/[?&]pid=([A-Za-z0-9_-]+)/);
    return {
        present: /extreme-dm\.com/i.test(text) || /eXTReMe Pro Tracker/.test(text),
        login: login ? login[1] : extremeLogin(),
        pid: pid ? pid[1] : '',
        hasNoscript: /<noscript[\s\S]{0,700}extreme-dm\.com/i.test(text),
    };
}

function parseIndexLinks(text) {
    const items = [];
    const re = /<!--#include\s+virtual="\/php\/index_sales_links\.php\?page=([^"]*)"\s*-->/gi;
    let match = re.exec(text);
    while (match) {
        items.push({ page: match[1], token: match[0] });
        match = re.exec(text);
    }
    return { present: items.length > 0, items };
}

function parseNn(text) {
    const items = [];
    const re = /<!--\s*NN\s+(\d+)\s*-->/g;
    let match = re.exec(text);
    while (match) {
        items.push({ id: match[1] });
        match = re.exec(text);
    }
    return { present: items.length > 0, items };
}

function parseSearchImage(text) {
    const match = text.match(/<!--\s*SEARCH_IMAGE:\s*([\s\S]*?)\s*-->/i);
    return { present: Boolean(match), path: match ? match[1].trim() : '' };
}

function parseArchive(text) {
    return { present: /<!--\s*newsletter-archive\s*-->/i.test(text) };
}

function parseSubscribe(text) {
    return { present: /\/includes\/subscribe\.html/i.test(text) };
}

function parseSalesListing(text, current) {
    const present = /\$vehicleName\s*=/.test(text);
    if (!present) {
        const details = current?.details || {};
        return {
            present: Boolean(details.vehicleName || details.inventoryNumber),
            vehicleName: details.vehicleName || '',
            inventoryNumber: String(details.inventoryNumber || ''),
            price: details.price || '',
            vehicleType: details.vehicleType || 'car',
            makeType: details.makeType || '',
            doNotRender: Boolean(details.doNotRender),
            sold: Boolean(details.sold),
        };
    }
    const price = phpAssign(text, 'price');
    return {
        present: true,
        vehicleName: phpAssign(text, 'vehicleName'),
        inventoryNumber: phpAssign(text, 'inventoryNumber'),
        price,
        vehicleType: phpAssign(text, 'vehicleType') || 'car',
        makeType: phpAssign(text, 'makeType'),
        doNotRender: /\$doNotRender\s*=\s*true\b/i.test(text),
        sold: /^sold$/i.test(price),
    };
}

function phpAssign(text, name) {
    const match = text.match(new RegExp(`\\$${name}\\s*=\\s*(?:'([^']*)'|"([^"]*)"|([^;\\n]+))`));
    if (!match) return '';
    return String(match[1] ?? match[2] ?? match[3] ?? '').trim();
}

function commentBlock(text, name) {
    const match = text.match(new RegExp(`<!--\\s*${name}\\b([\\s\\S]*?)-->`));
    if (!match) return null;
    return { body: match[1], index: match.index };
}

function commentField(body, key) {
    const escaped = key.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const match = body.match(new RegExp(`(?:^|\\n)\\s*${escaped}\\s*:\\s*([^\\n]*)`));
    return match ? match[1].trim() : '';
}

function seoStatus(text, title, description) {
    const open = text.indexOf('<!-- generated-seo -->');
    const close = text.indexOf('<!-- /generated-seo -->');
    if (open < 0 || close < open) return { text: 'Generated SEO is missing.', label: 'SEO missing', tone: 'danger' };
    const block = text.slice(open, close);
    if (!title) return { text: 'Generated SEO is waiting for a title.', label: 'SEO waiting', tone: 'warn' };
    const titleOk = block.includes(title) || block.includes(escapeHtml(title));
    const descriptionOk = !description || block.includes(description) || block.includes(escapeHtml(description));
    if (titleOk && descriptionOk) return { text: 'Generated SEO matches these page values.', label: 'SEO in sync', tone: 'ok' };
    return { text: 'Generated SEO is out of date. Apply Page Values to refresh it.', label: 'SEO out of date', tone: 'warn' };
}

function escapeHtml(value) {
    return String(value || '')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#x27;');
}

function summarize(current) {
    const list = Array.isArray(current?.errors) ? current.errors : [];
    if (!current) return { label: 'No page', tone: '' };
    if (!current.explicitErrors && !list.length) return { label: 'Not checked', tone: '' };
    const errors = list.filter((item) => item.severity !== 'warning').length;
    if (!list.length) return { label: '0 Issues', tone: 'ok' };
    if (errors) return { label: `${errors} Error${errors === 1 ? '' : 's'}`, tone: 'danger' };
    return { label: `${list.length} Warning${list.length === 1 ? '' : 's'}`, tone: 'warn' };
}

function widgetForError(error) {
    const blob = `${error?.id || ''} ${error?.title || ''} ${error?.message || ''}`.toLowerCase();
    if (/google|gtag|measurement/.test(blob)) return 'googleTracking';
    if (/extreme|xtreme/.test(blob)) return 'extremeTracking';
    if (/admin|logview|track\.js|hit\.php|header/.test(blob)) return 'adminTracking';
    if (/similar|override|related/.test(blob)) return 'relatedSales';
    if (/slide|thumb|unlisted/.test(blob)) return 'gallery';
    return 'pageValues';
}

function withCurrent(options, current) {
    const list = options.slice();
    if (current && !list.includes(current)) list.push(current);
    return list;
}

function typeOptions(current) {
    return withCurrent(PAGE_TYPES, current);
}

function pidOptions(current) {
    const values = PIDS.slice();
    if (current && !values.includes(current)) values.unshift(current);
    return values;
}

function imageOptions(current, value) {
    const values = new Set();
    if (value) values.add(value);
    values.add('shareimage.jpg');
    values.add('folderthumb.jpg');
    const images = [...(current?.images || []), ...(current?.unlistedSlides || [])];
    for (const image of images) {
        const name = image?.name;
        if (!name) continue;
        values.add(name.includes('/') ? name : `slides/${name}`);
    }
    return [...values];
}

function checkItem(ok, known, label, detail) {
    const item = el('li');
    const state = !known ? { mark: '–', word: 'Not loaded', off: false } : (ok ? { mark: '✓', word: 'Present', off: false } : { mark: '–', word: 'Missing', off: true });
    item.title = `${label}: ${state.word}. ${detail}`;
    item.append(el('span', { class: state.off ? 'check-mark is-off' : 'check-mark', text: state.mark, 'aria-hidden': 'true' }));
    const copy = el('div');
    copy.append(el('div', { class: 'check-label', text: `${label} — ${state.word}` }));
    copy.append(el('div', { class: 'chain-url', text: detail }));
    item.append(copy);
    return item;
}

function safeResource(url) {
    if (!url || /^(https?:|data:)/i.test(url)) return url || '';
    return String(url).split('/').map((part, index) => {
        if (!part) return index === 0 ? '' : part;
        try { return encodeURIComponent(decodeURIComponent(part)); } catch { return encodeURIComponent(part); }
    }).join('/');
}

function sameResource(left, right) {
    if (!left || !right) return false;
    try {
        const a = new URL(left, window.location.origin);
        const b = new URL(right, window.location.origin);
        return decodeURIComponent(a.pathname) === decodeURIComponent(b.pathname);
    } catch {
        return left === right;
    }
}
