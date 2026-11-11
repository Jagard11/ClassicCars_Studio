import { formatPriceLabel } from './details.js';
import { el } from './dom.js';

const SHOW_LIMIT = 80;
const TYPE_FILTERS = [
    ['all', 'All Types'],
    ['car', 'Cars'],
    ['motorcycle', 'Motorcycles'],
    ['truck', 'Trucks'],
];

const pickerUi = {
    query: '',
    status: 'sale',
    type: 'all',
    matchWidget: false,
};

let catalog = null;
let catalogTask = null;
let pendingPath = '';
let pendingOverride = null;
let hoverCard = null;
let hoverAnchor = null;
let hideTimer = 0;
let hoverListening = false;

export function mountOverrideIds({ value = '', path = '', tip = '', onChange } = {}) {
    listenForHover();
    hideHover();
    const wrap = el('div', { class: 'field override-ids' });
    if (tip) wrap.title = tip;
    wrap.append(document.createTextNode('Override IDs'));
    const hidden = el('input', { type: 'hidden', 'data-field': 'overrideIds' });
    const field = el('div', {
        class: 'override-ids-field',
        role: 'group',
        'aria-label': 'Override IDs',
    });
    const input = el('input', {
        class: 'override-ids-inline-input',
        type: 'text',
        placeholder: 'Type ID # or search…',
        autocomplete: 'off',
        spellcheck: 'false',
        'aria-label': 'Add an inventory number',
    });
    field.append(input);
    const suggest = el('div', { class: 'override-id-suggest', hidden: true });
    const hint = el('p', {
        class: 'field-hint',
        text: 'Type an inventory number, then press Enter. Hover a chip for the name, photo, and ID. Listed order is the pin order.',
    });
    const picker = buildPicker();
    wrap.append(hidden, field, suggest, hint, picker.root);

    let ids = parseIds(shownValue(value, path));
    let suggestions = [];
    let suggestIndex = -1;
    let loadError = '';
    hidden.value = ids.join(', ');

    function publish(next) {
        const serialized = next.join(', ');
        if (serialized === ids.join(', ')) return;
        ids = next;
        pendingOverride = serialized;
        hidden.value = serialized;
        paintChips();
        paintList();
        onChange?.(serialized);
    }

    function addIds(tokens) {
        const next = ids.slice();
        let changed = false;
        for (const token of tokens) {
            const key = inventoryKey(token);
            if (!key || next.includes(key)) continue;
            next.push(key);
            changed = true;
        }
        if (changed) publish(next);
    }

    function reorder(fromId, toId) {
        if (!fromId || fromId === toId) return;
        const from = ids.indexOf(fromId);
        const to = ids.indexOf(toId);
        if (from < 0 || to < 0) return;
        const next = ids.slice();
        next.splice(from, 1);
        next.splice(to, 0, fromId);
        publish(next);
    }

    function move(id, delta) {
        const index = ids.indexOf(id);
        const target = index + delta;
        if (index < 0 || target < 0 || target >= ids.length) return;
        reorder(id, ids[target]);
    }

    function chooseVehicle(vehicle) {
        input.value = '';
        closeSuggest();
        if (vehicle?.idKey) {
            if (ids.includes(vehicle.idKey)) publish(ids.filter((item) => item !== vehicle.idKey));
            else publish(ids.concat(vehicle.idKey));
        }
        input.focus();
    }

    function commitNumeric() {
        const text = input.value.trim();
        if (!/^\d+$/.test(text)) return false;
        input.value = '';
        addIds([text]);
        closeSuggest();
        return true;
    }

    function closeSuggest() {
        suggestions = [];
        suggestIndex = -1;
        suggest.hidden = true;
        suggest.replaceChildren();
    }

    function paintSuggest() {
        const query = input.value.trim();
        suggestions = query ? matchingVehicles(query).slice(0, 8) : [];
        suggest.replaceChildren();
        if (!suggestions.length) {
            suggest.hidden = true;
            suggestIndex = -1;
            return;
        }
        if (suggestIndex >= suggestions.length) suggestIndex = suggestions.length - 1;
        suggest.hidden = false;
        suggestions.forEach((vehicle, index) => {
            const button = el('button', {
                type: 'button',
                class: index === suggestIndex ? 'override-suggest is-active' : 'override-suggest',
            });
            const img = el('img', { alt: '' });
            bindThumb(img, vehicle, { hero: true });
            const copy = el('span', { class: 'override-suggest-copy' });
            copy.append(
                el('span', { class: 'override-vehicle-name', text: vehicle.vehicleName }),
                el('span', { class: 'override-id-badge', text: `#${vehicle.idKey}` }),
            );
            button.append(img, copy);
            button.addEventListener('mousedown', (event) => event.preventDefault());
            button.addEventListener('click', () => chooseVehicle(vehicle));
            suggest.append(button);
        });
    }

    function onKeyDown(event) {
        if (event.key === 'ArrowDown' && suggestions.length) {
            event.preventDefault();
            suggestIndex = Math.min(suggestions.length - 1, suggestIndex + 1);
            paintSuggest();
            return;
        }
        if (event.key === 'ArrowUp' && suggestions.length) {
            event.preventDefault();
            suggestIndex = suggestIndex <= 0 ? -1 : suggestIndex - 1;
            paintSuggest();
            return;
        }
        if (event.key === 'Escape') {
            closeSuggest();
            return;
        }
        if (event.key === 'Backspace' && input.value === '' && ids.length) {
            event.preventDefault();
            publish(ids.slice(0, -1));
            return;
        }
        const numeric = /^\d+$/.test(input.value.trim());
        if (event.key === 'Enter' || event.key === ',' || (event.key === ' ' && numeric)) {
            event.preventDefault();
            if (numeric) {
                commitNumeric();
                return;
            }
            if (event.key === 'Enter' && suggestIndex >= 0 && suggestions[suggestIndex]) {
                chooseVehicle(suggestions[suggestIndex]);
                return;
            }
            if (event.key === 'Enter' && suggestions.length === 1) {
                chooseVehicle(suggestions[0]);
                return;
            }
            if (event.key === 'Enter' && input.value.trim()) {
                pickerUi.query = input.value.trim();
                picker.search.value = pickerUi.query;
                paintList({ resetScroll: true });
                picker.search.focus();
            }
            return;
        }
        if (event.key === 'Tab' && numeric) commitNumeric();
    }

    input.addEventListener('input', () => {
        suggestIndex = -1;
        paintSuggest();
    });
    input.addEventListener('keydown', onKeyDown);
    input.addEventListener('paste', (event) => {
        const text = event.clipboardData?.getData('text') || '';
        if (!/[\s,]/.test(text) && !/^\d+$/.test(text.trim())) return;
        event.preventDefault();
        const numeric = [];
        const rest = [];
        for (const part of text.split(/[\s,]+/)) {
            if (!part) continue;
            if (/^\d+$/.test(part)) numeric.push(part);
            else rest.push(part);
        }
        input.value = rest.join(' ');
        if (numeric.length) addIds(numeric);
        paintSuggest();
    });
    input.addEventListener('blur', () => {
        if (suggest.contains(document.activeElement)) return;
        commitNumeric();
        closeSuggest();
    });
    field.addEventListener('click', (event) => {
        const button = event.target.closest('button');
        const chip = event.target.closest('.override-id-chip');
        if (button && chip) {
            const id = chip.dataset.inventoryId || '';
            if (button.classList.contains('override-id-remove')) publish(ids.filter((item) => item !== id));
            else if (button.dataset.dir === 'prev') move(id, -1);
            else if (button.dataset.dir === 'next') move(id, 1);
            return;
        }
        if (event.target.closest('button, input, .override-id-drag')) return;
        input.focus();
    });

    function renderChip(id, index) {
        const key = inventoryKey(id);
        const vehicle = key && catalog ? catalog.byId.get(key) || null : null;
        const state = !catalog ? 'pending' : (vehicle ? 'valid' : 'invalid');
        const chip = el('span', {
            class: `override-id-chip is-${state}`,
            'data-inventory-id': id,
        });
        const label = key ? `#${key}` : id;
        if (vehicle) {
            const img = el('img', { class: 'override-id-thumb', alt: '' });
            bindThumb(img, vehicle, { hero: false });
            chip.append(img);
        }
        chip.append(el('span', {
            class: 'override-id-label',
            text: state === 'invalid' ? `${label} · Unknown ID` : label,
        }));
        if (vehicle) {
            chip.append(el('span', { class: 'override-id-name', text: vehicle.vehicleName }));
            chip.append(el('span', {
                class: `override-id-dot ${vehicle.sold ? 'is-sold' : 'is-sale'}`,
                'aria-hidden': 'true',
            }));
        }
        const handle = el('span', {
            class: 'override-id-drag',
            draggable: 'true',
            title: 'Drag to reorder',
            'aria-hidden': 'true',
            text: '⋮⋮',
        });
        handle.addEventListener('dragstart', (event) => {
            event.dataTransfer.setData('text/plain', id);
            event.dataTransfer.effectAllowed = 'move';
            chip.classList.add('is-dragging');
        });
        handle.addEventListener('dragend', () => chip.classList.remove('is-dragging'));
        const earlier = el('button', {
            type: 'button',
            class: 'override-id-move',
            'data-dir': 'prev',
            text: '←',
            'aria-label': `Move ${label} earlier`,
            disabled: index === 0,
        });
        const later = el('button', {
            type: 'button',
            class: 'override-id-move',
            'data-dir': 'next',
            text: '→',
            'aria-label': `Move ${label} later`,
            disabled: index === ids.length - 1,
        });
        const remove = el('button', {
            type: 'button',
            class: 'override-id-remove',
            text: '×',
            'aria-label': `Remove ${label}`,
        });
        chip.append(handle, earlier, later, remove);
        chip.addEventListener('dragover', (event) => event.preventDefault());
        chip.addEventListener('drop', (event) => {
            event.preventDefault();
            reorder(event.dataTransfer.getData('text/plain'), id);
        });
        chip.addEventListener('mouseenter', () => showHover(chip));
        chip.addEventListener('mouseleave', (event) => {
            if (chip.contains(event.relatedTarget)) return;
            scheduleHide();
        });
        chip.addEventListener('focusin', () => showHover(chip));
        chip.addEventListener('focusout', (event) => {
            if (chip.contains(event.relatedTarget)) return;
            scheduleHide();
        });
        if (state === 'invalid') chip.title = `No vehicle in sales/ has inventory number ${id}.`;
        return chip;
    }

    function paintChips() {
        hideHover();
        for (const node of [...field.querySelectorAll('.override-id-chip')]) node.remove();
        const nodes = ids.map((id, index) => renderChip(id, index));
        if (nodes.length) input.before(...nodes);
    }

    function guidance() {
        const body = wrap.closest('.widget-body');
        return {
            make: body?.querySelector('[data-field="relatedMakeType"]')?.value || '',
            type: body?.querySelector('[data-field="relatedVehicleType"]')?.value || '',
        };
    }

    function filteredVehicles() {
        if (!catalog) return [];
        const query = pickerUi.query.trim().toLowerCase();
        const guide = guidance();
        const wantMake = pickerUi.matchWidget ? normMake(guide.make) : '';
        const wantType = pickerUi.matchWidget ? guide.type.trim().toLowerCase() : '';
        const rows = catalog.vehicles.filter((vehicle) => {
            if (pickerUi.status === 'sale' && vehicle.sold) return false;
            if (pickerUi.type !== 'all' && vehicle.vehicleType !== pickerUi.type) return false;
            if (wantMake && normMake(vehicle.makeType) !== wantMake) return false;
            if (wantType && vehicle.vehicleType !== wantType) return false;
            return queryMatches(vehicle, query);
        });
        rows.sort((a, b) => rank(a, query) - rank(b, query) || a.vehicleName.localeCompare(b.vehicleName));
        return rows;
    }

    function renderRow(vehicle, picked) {
        const row = el('button', {
            type: 'button',
            class: picked ? 'override-vehicle-row is-picked' : 'override-vehicle-row',
            'aria-pressed': picked ? 'true' : 'false',
        });
        if (!vehicle.idKey) row.disabled = true;
        const img = el('img', { class: 'override-vehicle-thumb', alt: '' });
        bindThumb(img, vehicle, { hero: true });
        const copy = el('span', { class: 'override-vehicle-copy' });
        const price = formatPriceLabel(vehicle.price, vehicle.sold);
        const meta = el('span', { class: 'override-vehicle-meta' });
        meta.append(el('span', { class: 'override-id-badge', text: vehicle.idKey ? `#${vehicle.idKey}` : 'No ID' }));
        meta.append(el('span', {
            class: 'override-price-badge',
            'data-tone': vehicle.sold ? 'warn' : price.tone,
            text: vehicle.sold ? 'SOLD' : (price.short || price.label),
        }));
        copy.append(el('span', { class: 'override-vehicle-name', text: vehicle.vehicleName }), meta);
        row.append(img, copy, el('span', { class: 'override-vehicle-pick', text: picked ? '✓ Picked' : '+ Pick' }));
        row.addEventListener('click', () => {
            if (!vehicle.idKey) return;
            if (ids.includes(vehicle.idKey)) publish(ids.filter((item) => item !== vehicle.idKey));
            else publish(ids.concat(vehicle.idKey));
        });
        return row;
    }

    function paintFilters() {
        for (const button of picker.statusHost.querySelectorAll('[data-status]')) {
            button.setAttribute('aria-pressed', button.dataset.status === pickerUi.status ? 'true' : 'false');
        }
        picker.matchButton.setAttribute('aria-pressed', pickerUi.matchWidget ? 'true' : 'false');
        if (picker.typeSelect.value !== pickerUi.type) picker.typeSelect.value = pickerUi.type;
    }

    function paintList({ resetScroll = false } = {}) {
        const top = resetScroll ? 0 : picker.list.scrollTop;
        paintFilters();
        picker.list.replaceChildren();
        if (loadError) {
            picker.count.textContent = '';
            picker.list.append(el('p', { class: 'field-hint', text: loadError }));
            const retry = el('button', { class: 'btn', type: 'button', text: 'Retry' });
            retry.addEventListener('click', () => {
                loadError = '';
                paintList();
                void ensureCatalog();
            });
            picker.list.append(retry);
            return;
        }
        if (!catalog) {
            picker.count.textContent = '';
            picker.list.append(el('p', { class: 'field-hint', text: 'Loading sales vehicles…' }));
            return;
        }
        const rows = filteredVehicles();
        const shown = rows.slice(0, SHOW_LIMIT);
        const picked = new Set(ids);
        picker.count.textContent = rows.length === catalog.vehicles.length
            ? `${catalog.vehicles.length} vehicles`
            : `${rows.length} match${rows.length === 1 ? '' : 'es'}`;
        if (!shown.length) {
            picker.list.append(el('p', { class: 'field-hint', text: 'No vehicles match.' }));
            return;
        }
        for (const vehicle of shown) picker.list.append(renderRow(vehicle, picked.has(vehicle.idKey)));
        if (rows.length > shown.length) {
            picker.list.append(el('p', {
                class: 'field-hint',
                text: `Showing ${shown.length} of ${rows.length}. Refine the filter.`,
            }));
        }
        picker.list.scrollTop = top;
    }

    function ensureCatalog() {
        return loadSalesCatalog().then(() => {
            loadError = '';
            if (!wrap.isConnected) return;
            paintChips();
            paintList();
        }).catch((error) => {
            loadError = error?.message || 'Could not load sales vehicles';
            if (!wrap.isConnected) return;
            paintList();
        });
    }

    picker.search.value = pickerUi.query;
    picker.search.addEventListener('input', () => {
        pickerUi.query = picker.search.value;
        paintList({ resetScroll: true });
    });
    picker.statusHost.addEventListener('click', (event) => {
        const button = event.target.closest('[data-status]');
        if (!button) return;
        pickerUi.status = button.dataset.status === 'all' ? 'all' : 'sale';
        paintList({ resetScroll: true });
    });
    picker.typeSelect.addEventListener('change', () => {
        pickerUi.type = picker.typeSelect.value || 'all';
        paintList({ resetScroll: true });
    });
    picker.matchButton.addEventListener('click', () => {
        pickerUi.matchWidget = !pickerUi.matchWidget;
        paintList({ resetScroll: true });
    });
    queueMicrotask(() => {
        const body = wrap.closest('.widget-body');
        if (!body) return;
        const onGuide = (event) => {
            const key = event.target?.getAttribute?.('data-field') || '';
            if (pickerUi.matchWidget && (key === 'relatedMakeType' || key === 'relatedVehicleType')) paintList();
        };
        body.addEventListener('input', onGuide);
        body.addEventListener('change', onGuide);
    });

    paintChips();
    paintList();
    void ensureCatalog();
    return wrap;
}

function buildPicker() {
    const root = el('div', { class: 'override-vehicle-picker' });
    const head = el('div', { class: 'override-picker-head' });
    head.append(el('span', { class: 'override-picker-title', text: 'Vehicle filter' }));
    const count = el('span', { class: 'override-picker-count' });
    head.append(count);
    const search = el('input', {
        class: 'override-vehicle-search',
        type: 'search',
        placeholder: 'Name, year, make, or ID…',
        autocomplete: 'off',
        spellcheck: 'false',
        'aria-label': 'Filter vehicles by name, year, make, or ID',
    });
    const statusHost = el('div', { class: 'override-filter-row', role: 'group', 'aria-label': 'Sale status' });
    statusHost.append(
        filterPill('sale', 'For Sale Only'),
        filterPill('all', 'All (Include Sold)'),
    );
    const typeSelect = el('select', { 'aria-label': 'Vehicle type filter' });
    for (const [value, label] of TYPE_FILTERS) {
        const option = el('option', { value, text: label });
        if (value === pickerUi.type) option.selected = true;
        typeSelect.append(option);
    }
    const matchButton = el('button', {
        type: 'button',
        class: 'override-filter-pill',
        text: 'Match widget Make/Type',
        'aria-pressed': pickerUi.matchWidget ? 'true' : 'false',
    });
    const filters = el('div', { class: 'override-filter-row' });
    filters.append(typeSelect, matchButton);
    const list = el('div', { class: 'override-vehicle-list' });
    root.append(head, search, statusHost, filters, list);
    return { root, search, statusHost, typeSelect, matchButton, count, list };
}

function filterPill(status, label) {
    return el('button', {
        type: 'button',
        class: 'override-filter-pill',
        'data-status': status,
        text: label,
        'aria-pressed': pickerUi.status === status ? 'true' : 'false',
    });
}

function shownValue(value, path) {
    if (path !== pendingPath) {
        pendingPath = path;
        pendingOverride = null;
    }
    const source = parseIds(value).join(', ');
    if (pendingOverride != null && source === pendingOverride) pendingOverride = null;
    return pendingOverride != null ? pendingOverride : source;
}

function loadSalesCatalog() {
    if (catalog) return Promise.resolve(catalog);
    if (!catalogTask) {
        catalogTask = fetch('/__studio/api/folders?section=sales', {
            headers: { Accept: 'application/json' },
            cache: 'no-store',
        })
            .then(async (response) => {
                const data = await response.json().catch(() => null);
                if (!response.ok || data?.ok === false) {
                    throw new Error(data?.error || data?.message || 'Could not load sales vehicles');
                }
                catalog = indexCatalog(Array.isArray(data?.folders) ? data.folders : []);
                catalogTask = null;
                return catalog;
            })
            .catch((error) => {
                catalogTask = null;
                throw error;
            });
    }
    return catalogTask;
}

function indexCatalog(folders) {
    const byId = new Map();
    const vehicles = [];
    for (const raw of folders) {
        const slug = String(raw?.slug || '').trim();
        if (!slug) continue;
        const idKey = inventoryKey(raw?.inventoryNumber);
        const vehicleName = String(raw?.vehicleName || slug);
        const makeType = String(raw?.makeType || '');
        const vehicleType = String(raw?.vehicleType || '').trim().toLowerCase();
        const price = String(raw?.price || '');
        const year = (vehicleName.match(/\b(?:18|19|20)\d{2}\b/) || [''])[0];
        const vehicle = {
            slug,
            vehicleName,
            inventoryNumber: String(raw?.inventoryNumber ?? '').trim(),
            idKey,
            makeType,
            vehicleType,
            price,
            sold: Boolean(raw?.sold) || /^sold$/i.test(price),
            doNotRender: Boolean(raw?.doNotRender),
            heroThumbUrl: String(raw?.heroThumbUrl || ''),
            haystack: [
                vehicleName,
                makeType,
                makeType.replace(/_/g, ' '),
                String(raw?.inventoryNumber ?? ''),
                idKey,
                year,
                vehicleType,
            ].join(' ').toLowerCase(),
        };
        vehicles.push(vehicle);
        if (!idKey) continue;
        const existing = byId.get(idKey);
        if (!existing || listingScore(vehicle) > listingScore(existing)) byId.set(idKey, vehicle);
    }
    vehicles.sort((a, b) => a.vehicleName.localeCompare(b.vehicleName));
    return { byId, vehicles };
}

function listingScore(vehicle) {
    return (vehicle.doNotRender ? 0 : 2) + (vehicle.sold ? 0 : 1);
}

function inventoryKey(raw) {
    const text = String(raw ?? '').trim();
    if (!/^\d+$/.test(text)) return '';
    const trimmed = text.replace(/^0+/, '');
    return trimmed === '' ? '0' : trimmed;
}

function parseIds(raw) {
    const seen = new Set();
    const ids = [];
    for (const part of String(raw || '').split(',')) {
        const token = part.trim();
        if (!token) continue;
        const key = inventoryKey(token) || token;
        const dedupe = key.toLowerCase();
        if (seen.has(dedupe)) continue;
        seen.add(dedupe);
        ids.push(key);
    }
    return ids;
}

function normMake(value) {
    return String(value || '').trim().toLowerCase().replace(/[_-]+/g, ' ').replace(/\s+/g, ' ');
}

function rank(vehicle, query) {
    if (!query) return 1;
    if (vehicle.idKey === query) return 0;
    if (vehicle.idKey.startsWith(query)) return 1;
    if (vehicle.vehicleName.toLowerCase().startsWith(query)) return 2;
    return 3;
}

function queryMatches(vehicle, query) {
    if (!query) return true;
    if (vehicle.haystack.includes(query) || vehicle.idKey.startsWith(query)) return true;
    const key = inventoryKey(query);
    return Boolean(key) && (vehicle.idKey === key || vehicle.idKey.startsWith(key));
}

function matchingVehicles(query) {
    const q = query.trim().toLowerCase();
    if (!q || !catalog) return [];
    const rows = catalog.vehicles.filter((vehicle) => queryMatches(vehicle, q));
    rows.sort((a, b) => rank(a, q) - rank(b, q) || a.vehicleName.localeCompare(b.vehicleName));
    return rows;
}

function thumbChain(vehicle, hero) {
    const folder = `/sales/${encodeURIComponent(vehicle.slug)}/folderthumb.jpg`;
    const share = `/sales/${encodeURIComponent(vehicle.slug)}/shareimage.jpg`;
    const urls = [];
    const push = (url) => {
        if (url && !urls.includes(url)) urls.push(url);
    };
    if (hero) push(vehicle.heroThumbUrl);
    push(folder);
    push(share);
    return urls;
}

function bindThumb(img, vehicle, { hero = false } = {}) {
    const chain = thumbChain(vehicle, hero);
    let step = 0;
    img.alt = '';
    img.decoding = 'async';
    if (!hero) img.loading = 'lazy';
    img.src = chain[0] || '';
    img.onerror = () => {
        step += 1;
        if (step < chain.length) img.src = chain[step];
        else img.onerror = null;
    };
}

function statusLine(vehicle) {
    if (vehicle.sold) return { tone: 'warn', text: 'SOLD (skipped by suggestion strip)' };
    const price = formatPriceLabel(vehicle.price, false);
    const make = vehicle.makeType ? vehicle.makeType.replace(/_/g, ' ') : '';
    const badge = [make, vehicle.vehicleType].filter(Boolean).join(' · ');
    return { tone: price.tone, text: badge ? `${price.label} · ${badge}` : price.label };
}

function ensureCard() {
    if (hoverCard?.isConnected) return hoverCard;
    hoverCard = el('div', { class: 'override-id-hover-card', role: 'tooltip' });
    document.body.append(hoverCard);
    return hoverCard;
}

function fillHover(id, vehicle) {
    const card = ensureCard();
    card.replaceChildren();
    if (!vehicle) {
        const body = el('div', { class: 'override-id-hover-body' });
        body.append(
            el('p', { class: 'override-id-hover-id', text: `ID #${id}` }),
            el('p', {
                class: 'override-id-hover-status',
                'data-tone': catalog ? 'warn' : '',
                text: catalog
                    ? `No vehicle in sales/ has inventory number ${id}.`
                    : 'Looking up this inventory number…',
            }),
        );
        card.append(body);
        return;
    }
    const media = el('div', { class: 'override-id-hover-media' });
    const img = el('img', { alt: '' });
    bindThumb(img, vehicle, { hero: true });
    img.addEventListener('load', () => {
        if (hoverAnchor) placeHover(hoverAnchor);
    });
    media.append(img);
    const status = statusLine(vehicle);
    const body = el('div', { class: 'override-id-hover-body' });
    body.append(
        el('p', { class: 'override-id-hover-name', text: vehicle.vehicleName }),
        el('p', { class: 'override-id-hover-id', text: `ID #${vehicle.idKey}` }),
        el('p', { class: 'override-id-hover-status', 'data-tone': status.tone, text: status.text }),
    );
    card.append(media, body);
}

function placeHover(anchor) {
    const card = ensureCard();
    card.classList.add('is-open');
    card.style.visibility = 'hidden';
    const margin = 8;
    const anchorRect = anchor.getBoundingClientRect();
    const cardRect = card.getBoundingClientRect();
    let left = anchorRect.left;
    if (left + cardRect.width > window.innerWidth - margin) left = window.innerWidth - cardRect.width - margin;
    if (left < margin) left = margin;
    let top = anchorRect.top - cardRect.height - margin;
    if (top < margin) top = Math.min(anchorRect.bottom + margin, window.innerHeight - cardRect.height - margin);
    card.style.left = `${Math.round(left)}px`;
    card.style.top = `${Math.round(Math.max(margin, top))}px`;
    card.style.visibility = 'visible';
}

function showHover(chip) {
    window.clearTimeout(hideTimer);
    hoverAnchor = chip;
    const id = chip.dataset.inventoryId || '';
    const key = inventoryKey(id);
    const vehicle = key && catalog ? catalog.byId.get(key) || null : null;
    fillHover(id, vehicle);
    placeHover(chip);
}

function scheduleHide() {
    window.clearTimeout(hideTimer);
    hideTimer = window.setTimeout(hideHover, 80);
}

function hideHover() {
    window.clearTimeout(hideTimer);
    hoverAnchor = null;
    if (!hoverCard) return;
    hoverCard.classList.remove('is-open');
    hoverCard.style.visibility = '';
}

function onViewportMove() {
    if (!hoverAnchor || !hoverAnchor.isConnected) {
        hideHover();
        return;
    }
    placeHover(hoverAnchor);
}

function listenForHover() {
    if (hoverListening || typeof document === 'undefined') return;
    hoverListening = true;
    document.addEventListener('scroll', onViewportMove, true);
    window.addEventListener('resize', onViewportMove);
}
