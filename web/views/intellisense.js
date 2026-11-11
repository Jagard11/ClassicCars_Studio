import { el } from './dom.js';

const VOID_TAGS = new Set(['area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input', 'link', 'meta', 'param', 'source', 'track', 'wbr']);
const MAX_ITEMS = 60;
const STORE_KEY = 'studio.intellisense';

const HTML_TAGS = [
    ['div', 'Generic block. The usual wrapper for a feature, a listing row, or a gallery.'],
    ['section', 'A named region, such as the photo gallery at the end of an article.'],
    ['figure', 'One photo plus its caption.'],
    ['figcaption', 'The caption under a figure. Pair it with bodyCaption.'],
    ['p', 'A paragraph of story text.'],
    ['a', 'A link. Use articleLightboxLink when the link opens a photo.'],
    ['img', 'An image. Lead photos use slides/; gallery thumbs use thumbs/.'],
    ['h1', 'Page title. Articles center it with centerText.'],
    ['h2', 'Section heading, such as the photo gallery title.'],
    ['h3', 'Subtitle. Show reports use it for the dateline.'],
    ['h4', 'Smaller heading.'],
    ['h5', 'Byline heading. Articles use it with articleByline.'],
    ['ul', 'A bulleted list. Sales specifications are list items inside the description.'],
    ['li', 'One item in a list.'],
    ['hr', 'A horizontal rule between the story and the gallery.'],
    ['span', 'Inline wrapper, including bookmark-anchor jump targets.'],
    ['table', 'A table. Prefer the article layout classes for new pages.'],
    ['tr', 'A table row.'],
    ['td', 'A table cell.'],
    ['script', 'A script. The lightbox bundle is usually a separate script tag, not an inline block.'],
    ['style', 'An embedded stylesheet. 2026 articles keep their layout rules here.'],
    ['link', 'A stylesheet link, such as classicCarsGeneric.css or lightbox.css.'],
    ['meta', 'Metadata. Page title and description are written from the page-values comment.'],
];

const TAG_SIGNATURES = {
    a: '<a href class data-lightbox data-title target rel>',
    img: '<img src alt title class loading fetchpriority decoding width height>',
    div: '<div class id aria-label>',
    section: '<section class id aria-label>',
    h1: '<h1 class id aria-label>',
    h2: '<h2 class id aria-label>',
    h3: '<h3 class id aria-label>',
    h4: '<h4 class id aria-label>',
    h5: '<h5 class id aria-label>',
    figcaption: '<figcaption class id aria-label>',
};

const BLOCK_ATTR_TAGS = new Set(['div', 'section', 'h1', 'h2', 'h3', 'h4', 'h5', 'figcaption']);

const MAKES = [
    'Cadillac', 'Duesenberg', 'Packard', 'Indian', 'Harley-Davidson', 'Flying_Merkel',
    'Pope', 'Mercer', 'Locomobile', 'Pierce-Arrow', 'Cameron', 'Thor', 'Crocker',
];

const SPECS = [
    ['ENGINE NO: ', 'Engine number, the value collectors use to identify the vehicle.'],
    ['ENGINE: ', 'Engine description, such as the cylinder count and displacement.'],
    ['HP: ', 'Horsepower.'],
    ['CHASSIS NO: ', 'Chassis or frame number.'],
    ['BODY: ', 'Body style and coachbuilder.'],
    ['BRAKES: ', 'Brake type.'],
    ['TOP SPEED: ', 'Top speed.'],
];

const ENTITIES = [
    ['&shy;', 'Soft hyphen. It is used throughout vehicle names and descriptions so a long word can break without showing a hyphen until it wraps.'],
    ['&bull;', 'Bullet.'],
    ['&ndash;', 'En dash, for ranges such as years.'],
    ['&mdash;', 'Em dash.'],
    ['&ldquo;', 'Opening double quote.'],
    ['&rdquo;', 'Closing double quote.'],
    ['&lsquo;', 'Opening single quote.'],
    ['&rsquo;', 'Closing single quote, also used as an apostrophe.'],
    ['&quot;', 'Straight double quote.'],
    ['&amp;', 'Ampersand. Required when the text itself contains &.'],
];

const CLASSES = [
    ['articleContainer', 'Page column for a 2026 article. It holds the title, the story, and the gallery.', '2026 article stylesheet. Full width of the column, with 10px of side padding.'],
    ['articleSubtitle', 'Dateline under the article title.', 'Tightens the gap between the title and the place-and-date line.'],
    ['articleByline', 'Byline under the subtitle.', 'Adds space before the first paragraph.'],
    ['articleFeatureSingle', 'One large photo in the story, about 82% of the column.', 'Featured clickable image. The figure inside uses articleLightboxLink.'],
    ['articleFeaturePair', 'Two photos side by side.', 'A two-column grid. Add --landscape or --portrait for the crop.'],
    ['articleFeaturePair--landscape', 'Landscape crop (4:3) for a photo pair.', 'Used with articleFeaturePair. Stacks to one column on a phone.'],
    ['articleFeaturePair--portrait', 'Narrower portrait crop (3:4) for a photo pair.', 'Used with articleFeaturePair. The pair is about 76% of the column.'],
    ['articleLightboxLink', 'White-framed photo that opens Lightbox2. The frame turns teal on hover.', 'Put this class on the <a> around a feature image.'],
    ['articleGallerySection', 'The photo gallery block at the end of an article.', 'Holds the gallery heading, the note, and the thumbnail grid.'],
    ['articleGalleryNote', 'The short note under the gallery heading.', 'The usual text is "Click any photo to view full size."'],
    ['centerText', 'Centers text and stretches the line to the full column.', 'From classicCarsGeneric.css. Used on headings, captions, and the video index.'],
    ['textJustify', 'Justifies text and turns on automatic hyphenation.', 'Often set on <body>. Index cards also use it.'],
    ['bodyCaption', 'White caption type under a photo.', 'The figcaption class on feature photos and gallery notes.'],
    ['D_35p_1f', 'Desktop listing row: an image column beside a text column.', 'classicCarsGeneric.css. The sales index and the article index use this grid.'],
    ['list', 'Extra space under a listing row.', 'Pair it with D_35p_1f on index cards.'],
    ['D_35p_1f list', 'Listing row plus the list spacing used on index pages.', 'Both classes together, the way the sales and article indexes write them.'],
    ['bookmark-anchor', 'Invisible jump target that clears the sticky header.', 'Used on the video index so a #hash lands below the header.'],
    ['redFont', 'Red warning text.', 'classicCarsGeneric.css uses it for the JavaScript-disabled notice.'],
    ['thumbs-wrapper', 'Centers the thumbnail grid.', 'salesDetails.css and the 2026 article gallery.'],
    ['thumbs', 'Wrapping row of gallery thumbnails.', 'The direct parent of the thumbnail-wrapper items.'],
    ['thumbnail-wrapper', 'One rounded thumbnail frame, four across on a desktop.', 'The frame turns teal when the photo is hovered.'],
    ['thumbnail', 'The image that fills a thumbnail frame.', 'Absolutely positioned inside thumbnail-wrapper.'],
    ['hero-wrapper', '16:9 frame for the sales hero photo.', 'salesDetails.css. The first gallery image is the hero.'],
    ['hero', 'The hero image, cropped to the frame.', 'The teal hover border is drawn by hero-wrapper.'],
    ['photoCaptions', 'Caption list under the sales gallery.', 'Hidden on a wide screen and shown on a phone.'],
    ['photoMore', 'Collapsible control for a long phone gallery.', 'Closed, the extra thumbnails stay tucked away. Open, the strip expands.'],
    ['photoMoreShow', 'Label shown while the extra photos are hidden.', 'salesDetails.css hides it once photoMore is open.'],
    ['photoMoreHide', 'Label shown while the extra photos are open.', 'Hidden until photoMore is open.'],
];

const FIXED_PATHS = [
    ['/awards/index.html', 'Awards index', 'The awards landing page.'],
    ['/video/index.html', 'Video index', 'The video landing page.'],
    ['/style/classicCarsGeneric.css', 'Shared site stylesheet', 'Loaded by articles and most public pages.'],
    ['/JS/lightbox2-2.11.3/dist/css/lightbox.css', 'Lightbox2 stylesheet', 'The lightbox skin used by feature photos and galleries.'],
    ['/JS/lightbox2-2.11.3/dist/js/lightbox-plus-jquery.js', 'Lightbox2 script', 'jQuery build of Lightbox2. Articles load it at the bottom of the body.'],
];

const ATTRS = {
    a: [
        ['href=""', 'Link target. A site path, or slides/<file> for a lightbox photo.'],
        ['class="articleLightboxLink"', 'Opens the photo in Lightbox2 with the white article frame.'],
        ['data-lightbox=""', 'Lightbox2 group name. Photos that share a name form one gallery.'],
        ['data-title=""', 'Caption shown inside the lightbox.'],
        ['target="_blank"', 'Open the link in a new tab.'],
        ['rel="noopener"', 'Keep a new tab from reaching back into this page.'],
    ],
    img: [
        ['src=""', 'Image path. slides/<file> for a lead photo, thumbs/<file> for a gallery thumb.'],
        ['alt=""', 'Text used when the image cannot be seen.'],
        ['title=""', 'Tooltip. Feature photos repeat the lightbox caption here.'],
        ['class="thumbnail"', 'Fill a thumbnail-wrapper frame. Used in the gallery grid, not on feature photos.'],
        ['loading="lazy"', 'Wait to load this image until it is near the screen. Skip it on the lead photo.'],
        ['fetchpriority="high"', 'Load this image first. Use it on the lead photo only.'],
        ['decoding="async"', 'Decode the image off the main thread.'],
        ['width=""', 'Pixel width. Optional; the article layouts size the image with CSS.'],
        ['height=""', 'Pixel height. Optional; the article layouts size the image with CSS.'],
    ],
    block: [
        ['class=""', 'One or more site classes. Suggestions open inside the quotes.'],
        ['id=""', 'Fragment id, used by jump links and bookmark-anchor targets.'],
        ['aria-label=""', 'Accessible name when the element has no visible heading of its own.'],
    ],
};

export function suggestAt(value, caret, context, options = {}) {
    const manual = options.manual === true;
    const text = String(value ?? '');
    const at = clampIndex(caret, text.length);
    const facts = normalizeFacts(context);
    const state = analyze(text, at);
    return buildSuggestions(text, at, facts, state, manual);
}

export function pairKey(value, caret, key, options = {}) {
    const text = String(value ?? '');
    const at = clampIndex(caret, text.length);
    const end = clampIndex(options.selectionEnd == null ? at : options.selectionEnd, text.length);
    const from = Math.min(at, end);
    const to = Math.max(at, end);
    if (key === '>' && from === to) return pairClose(text, from);
    if (key === '"' || key === "'") return pairQuote(text, from, to, key);
    return null;
}

export function describeAt(value, offset, context) {
    const text = String(value ?? '');
    const at = clampIndex(offset, text.length);
    const facts = normalizeFacts(context);
    const token = tokenAt(text, at);
    if (!token.text) return null;
    const entity = ENTITIES.find((item) => item[0] === token.text || item[0] === `${token.text};` || item[0] === token.text.replace(/;$/, ''));
    if (entity || token.text.startsWith('&')) {
        const found = ENTITIES.find((item) => item[0] === token.text || item[0].startsWith(token.text));
        if (found && (token.text === found[0] || token.text === found[0].replace(/;$/, '') || found[0].startsWith(token.text))) {
            return { kind: 'entity', title: found[0], detail: found[1] };
        }
    }
    const phpName = token.text.startsWith('$') ? token.text : (text[token.start - 1] === '$' ? `$${token.text}` : '');
    if (phpName && /\.php$/i.test(facts.path)) {
        const known = phpDocs(phpName);
        if (known) return { kind: 'php', title: phpName, detail: known };
    }
    const classHit = CLASSES.find((item) => item[0] === token.text || item[0].split(/\s+/).includes(token.text));
    if (classHit && classHit[0] === token.text) return { kind: 'class', title: classHit[0], detail: classHit[2] || classHit[1] };
    const boundary = text[token.start - 1] || '';
    if (boundary === '<' || (boundary === '/' && text[token.start - 2] === '<')) {
        const tag = HTML_TAGS.find((item) => item[0] === token.text.toLowerCase());
        if (tag) return { kind: 'tag', title: tag[0], detail: tag[1], signature: TAG_SIGNATURES[tag[0]] || '' };
    }
    const file = imageLookup(facts, token.text);
    if (file) return { kind: 'asset', title: file.path, detail: file.detail };
    return classHit ? { kind: 'class', title: token.text, detail: classHit[1] } : null;
}

export function mountIntelliSense({ textarea, toolbar }) {
    const toggle = el('button', {
        class: 'btn is-toggle',
        type: 'button',
        title: 'Toggle automatic code completions (Ctrl+Space to trigger manually)',
        'aria-pressed': 'true',
        'aria-keyshortcuts': 'Control+Space',
    });
    const toggleLabel = el('span', { class: 'is-toggle-label', text: 'IntelliSense: ON' });
    const toggleHint = el('kbd', { text: 'Ctrl+Space' });
    toggle.append(toggleLabel, toggleHint);

    const popup = el('div', {
        class: 'is-popup',
        id: 'code-intellisense',
        role: 'listbox',
        hidden: true,
        'aria-label': 'Suggestions',
    });
    const list = el('div', { class: 'is-list' });
    const docs = el('div', { class: 'is-docs' });
    const docsKind = el('span', { class: 'is-kind', text: 'docs' });
    const docsTitle = el('p', { class: 'is-docs-title' });
    const docsSig = el('p', { class: 'is-docs-sig' });
    const docsBody = el('p', { class: 'is-docs-body' });
    const docsPre = el('pre', { class: 'is-docs-pre', hidden: true });
    const docsCount = el('p', { class: 'is-docs-count' });
    docs.append(docsKind, docsTitle, docsSig, docsBody, docsPre, docsCount);
    popup.append(list, docs);

    const hover = el('div', { class: 'is-hover', hidden: true, role: 'tooltip' });
    const hoverKind = el('span', { class: 'is-kind' });
    const hoverTitle = el('p', { class: 'is-hover-title' });
    const hoverBody = el('p', { class: 'is-hover-body' });
    hover.append(hoverKind, hoverTitle, hoverBody);

    const mirror = el('div', { class: 'is-mirror', 'aria-hidden': 'true' });
    const mark = el('span', { class: 'is-caret-mark', text: '\u200b' });
    document.body.append(popup, hover, mirror);

    const anchor = toolbar.children[2] || null;
    toolbar.insertBefore(toggle, anchor);

    let automatic = readAutomatic();
    let enabled = false;
    let facts = normalizeFacts();
    let isOpen = false;
    let items = [];
    let active = 0;
    let query = '';
    let total = 0;
    let armed = false;
    let refreshOnInput = false;
    let suppress = false;
    let manualSession = false;
    let hoverTimer = 0;

    paintToggle();
    textarea.setAttribute('aria-autocomplete', 'list');
    textarea.setAttribute('aria-controls', popup.id);
    textarea.setAttribute('aria-expanded', 'false');

    toggle.addEventListener('click', () => {
        automatic = !automatic;
        writeAutomatic(automatic);
        paintToggle();
        if (!automatic) hide();
        if (enabled) textarea.focus();
    });

    textarea.addEventListener('keydown', onKeyDown, true);
    textarea.addEventListener('input', onInput);
    textarea.addEventListener('scroll', () => {
        if (isOpen) positionPopup();
        hideHover();
    });
    textarea.addEventListener('mousemove', (event) => {
        if (!enabled || isOpen) {
            hideHover();
            return;
        }
        const x = event.clientX;
        const y = event.clientY;
        window.clearTimeout(hoverTimer);
        hoverTimer = window.setTimeout(() => showHover(x, y), 280);
    });
    textarea.addEventListener('mouseleave', () => {
        window.clearTimeout(hoverTimer);
        hideHover();
    });
    popup.addEventListener('mousemove', (event) => {
        const row = event.target.closest?.('.is-item');
        if (!row || !list.contains(row)) return;
        const index = Number(row.dataset.index);
        if (Number.isFinite(index) && index !== active) setActive(index);
    });
    popup.addEventListener('mousedown', (event) => {
        const row = event.target.closest?.('.is-item');
        if (!row || !list.contains(row)) {
            event.preventDefault();
            return;
        }
        event.preventDefault();
        accept(Number(row.dataset.index));
    });
    document.addEventListener('mousedown', (event) => {
        if (!isOpen) return;
        if (popup.contains(event.target) || toggle.contains(event.target)) return;
        hide();
    }, true);
    window.addEventListener('resize', () => {
        if (isOpen) positionPopup();
    });
    const pane = textarea.closest('#code-mount');
    if (pane) {
        new MutationObserver(() => {
            if (pane.hidden) hide();
        }).observe(pane, { attributes: true, attributeFilter: ['hidden'] });
    }

    function onKeyDown(event) {
        if (!enabled || event.isComposing) return;
        if ((event.ctrlKey || event.metaKey) && !event.altKey && event.key === ' ') {
            event.preventDefault();
            event.stopImmediatePropagation();
            refresh(true);
            return;
        }
        if (isOpen && event.key === 'ArrowDown') {
            event.preventDefault();
            event.stopImmediatePropagation();
            setActive(active + 1);
            return;
        }
        if (isOpen && event.key === 'ArrowUp') {
            event.preventDefault();
            event.stopImmediatePropagation();
            setActive(active - 1);
            return;
        }
        if (isOpen && event.key === 'Enter' && !event.shiftKey && !event.ctrlKey && !event.metaKey && !event.altKey) {
            event.preventDefault();
            event.stopImmediatePropagation();
            accept(active);
            return;
        }
        if (isOpen && event.key === 'Tab' && !event.shiftKey && !event.ctrlKey && !event.metaKey && !event.altKey) {
            event.preventDefault();
            event.stopImmediatePropagation();
            accept(active);
            return;
        }
        if (isOpen && event.key === 'Escape') {
            event.preventDefault();
            event.stopImmediatePropagation();
            hide();
            return;
        }
        if (isOpen && (event.key === 'ArrowLeft' || event.key === 'ArrowRight' || event.key === 'Home' || event.key === 'End' || (event.key === 'Enter' && event.shiftKey) || (event.key === 'Tab' && event.shiftKey))) {
            hide();
        }
        if (event.key === 'Backspace' || event.key === 'Delete') refreshOnInput = isOpen;
        if (event.key.length !== 1 || event.ctrlKey || event.metaKey || event.altKey) return;
        if (automatic && (event.key === '>' || event.key === '"' || event.key === "'")) {
            const action = pairKey(textarea.value, textarea.selectionStart, event.key, { selectionEnd: textarea.selectionEnd });
            if (action) {
                event.preventDefault();
                event.stopImmediatePropagation();
                applyAction(action);
                refresh(false);
                return;
            }
        }
        if (automatic) armed = true;
    }

    function onInput() {
        if (suppress) return;
        const should = armed || refreshOnInput || isOpen;
        armed = false;
        refreshOnInput = false;
        if (!should || !enabled) return;
        refresh(false);
    }

    function refresh(manual) {
        if (!enabled || textarea.disabled || textarea.closest('[hidden]')) {
            hide();
            return;
        }
        if (manual) manualSession = true;
        const result = suggestAt(textarea.value, textarea.selectionStart, facts, { manual: manual || (isOpen && manualSession) });
        const allow = manual || (isOpen && result.items.length) || (automatic && result.auto);
        if (!result.items.length || !allow) {
            hide();
            return;
        }
        query = result.query || '';
        total = result.total || result.items.length;
        items = result.items;
        if (active >= items.length) active = 0;
        openMenu();
    }

    function openMenu() {
        isOpen = true;
        hideHover();
        renderList();
        popup.style.visibility = 'hidden';
        popup.hidden = false;
        textarea.setAttribute('aria-expanded', 'true');
        positionPopup();
    }

    function hide() {
        manualSession = false;
        if (!isOpen && popup.hidden) return;
        isOpen = false;
        items = [];
        active = 0;
        popup.hidden = true;
        list.replaceChildren();
        textarea.setAttribute('aria-expanded', 'false');
        textarea.removeAttribute('aria-activedescendant');
    }

    function renderList() {
        list.replaceChildren();
        items.forEach((item, index) => {
            const button = el('div', {
                class: index === active ? 'is-item is-active' : 'is-item',
                role: 'option',
                id: `code-intellisense-opt-${index}`,
                'aria-selected': index === active ? 'true' : 'false',
            });
            button.dataset.index = String(index);
            const badge = el('span', { class: 'is-kind', 'data-kind': item.kind, text: item.kind });
            const label = labelNode(item.label, item.needle == null ? query : item.needle);
            const detail = el('span', { class: 'is-detail', text: item.detail || '' });
            button.append(badge, label, detail);
            if (item.dims) button.append(el('span', { class: 'is-dims', text: item.dims }));
            list.append(button);
        });
        const current = list.children[active];
        if (current) revealRow(current);
        fillDocs(items[active]);
        if (current) textarea.setAttribute('aria-activedescendant', current.id);
    }

    function fillDocs(item) {
        docsKind.textContent = item?.kind || '';
        docsKind.dataset.kind = item?.kind || '';
        docsTitle.textContent = item?.label || '';
        docsSig.hidden = !item?.signature;
        docsSig.textContent = item?.signature || '';
        docsBody.textContent = item?.docs || item?.detail || '';
        const preview = item && item.insert && (item.insert.includes('\n') || item.insert !== item.label);
        docsPre.hidden = !preview;
        docsPre.textContent = preview ? item.insert.split('\n').slice(0, 12).join('\n') : '';
        docsCount.textContent = total > items.length ? `${items.length} of ${total}` : '';
    }

    function setActive(index) {
        if (!items.length) return;
        active = (index + items.length) % items.length;
        const rows = list.children;
        for (let i = 0; i < rows.length; i += 1) {
            const on = i === active;
            rows[i].classList.toggle('is-active', on);
            rows[i].setAttribute('aria-selected', on ? 'true' : 'false');
        }
        const current = rows[active];
        if (current) {
            revealRow(current);
            textarea.setAttribute('aria-activedescendant', current.id);
        }
        fillDocs(items[active]);
    }

    function accept(index) {
        const item = items[index];
        if (!item) {
            hide();
            return;
        }
        const from = item.replaceFrom;
        const to = item.replaceTo;
        hide();
        applyEdit(item.insert, from, to, item.cursor, item.selectTo);
    }

    function applyAction(action) {
        if (action.skip) {
            const next = textarea.selectionStart + action.skip;
            textarea.setSelectionRange(next, next);
            return;
        }
        const from = textarea.selectionStart;
        const to = action.replaceTo == null ? textarea.selectionEnd : action.replaceTo;
        applyEdit(action.insert, Math.min(from, to), Math.max(from, to), action.cursor, action.selectTo);
    }

    function applyEdit(insert, from, to, cursor, selectTo) {
        suppress = true;
        try {
            textarea.focus();
            textarea.setRangeText(insert, from, to, 'end');
            const start = from + (cursor == null ? insert.length : cursor);
            const stop = selectTo == null ? start : from + selectTo;
            const lo = Math.max(0, Math.min(start, stop, textarea.value.length));
            const hi = Math.max(lo, Math.min(Math.max(start, stop), textarea.value.length));
            textarea.setSelectionRange(lo, hi);
            textarea.dispatchEvent(new InputEvent('input', { bubbles: true, inputType: 'insertText', data: insert }));
        } finally {
            suppress = false;
        }
    }

    let placing = false;
    function positionPopup() {
        if (placing) return;
        placing = true;
        try {
            revealCaret(textarea.selectionStart);
            placePopup(caretPoint(textarea.selectionStart));
        } finally {
            placing = false;
        }
    }

    function placePopup(point) {
        popup.style.visibility = 'hidden';
        popup.hidden = false;
        const margin = 8;
        const width = popup.offsetWidth;
        const height = popup.offsetHeight;
        let left = point.x;
        let top = point.bottom + 2;
        if (top + height > window.innerHeight - margin) {
            const above = point.y - height - 2;
            top = above >= margin ? above : Math.max(margin, window.innerHeight - height - margin);
        }
        if (left + width > window.innerWidth - margin) left = window.innerWidth - width - margin;
        if (left < margin) left = margin;
        popup.style.left = `${Math.round(left)}px`;
        popup.style.top = `${Math.round(top)}px`;
        popup.style.visibility = 'visible';
    }

    function caretPoint(pos) {
        const style = getComputedStyle(textarea);
        copyMirrorStyle(mirror, style);
        mirror.replaceChildren(document.createTextNode(textarea.value.slice(0, pos)), mark);
        const host = textarea.getBoundingClientRect();
        const mirrorBox = mirror.getBoundingClientRect();
        const markBox = mark.getBoundingClientRect();
        const borderLeft = parseFloat(style.borderLeftWidth) || 0;
        const borderTop = parseFloat(style.borderTopWidth) || 0;
        const localX = markBox.left - mirrorBox.left;
        const localY = markBox.top - mirrorBox.top;
        const x = host.left + borderLeft + localX - textarea.scrollLeft;
        const y = host.top + borderTop + localY - textarea.scrollTop;
        const lineHeight = markBox.height || parseFloat(style.lineHeight) || (parseFloat(style.fontSize) || 13) * 1.5;
        return { x, y, bottom: y + lineHeight, lineHeight, contentX: localX - borderLeft, contentY: localY - borderTop };
    }

    function revealCaret(pos) {
        const point = caretPoint(pos);
        const style = getComputedStyle(textarea);
        const padLeft = parseFloat(style.paddingLeft) || 0;
        const padTop = parseFloat(style.paddingTop) || 0;
        const padBottom = parseFloat(style.paddingBottom) || 0;
        const slack = 48;
        if (point.contentX < textarea.scrollLeft + padLeft || point.contentX > textarea.scrollLeft + textarea.clientWidth - slack) {
            textarea.scrollLeft = Math.max(0, point.contentX - slack);
        }
        if (point.contentY < textarea.scrollTop + padTop || point.contentY + point.lineHeight > textarea.scrollTop + textarea.clientHeight - padBottom) {
            textarea.scrollTop = Math.max(0, point.contentY - Math.max(padTop, textarea.clientHeight / 3));
        }
    }

    function showHover(x, y) {
        if (!enabled || isOpen || textarea.closest('[hidden]')) return;
        const offset = offsetFromPoint(x, y);
        if (offset < 0) {
            hideHover();
            return;
        }
        const info = describeAt(textarea.value, offset, facts);
        if (!info) {
            hideHover();
            return;
        }
        hoverKind.textContent = info.kind;
        hoverKind.dataset.kind = info.kind;
        hoverTitle.textContent = info.signature ? `${info.title}  ${info.signature}` : info.title;
        hoverBody.textContent = info.detail || '';
        hover.hidden = false;
        const width = hover.offsetWidth;
        const height = hover.offsetHeight;
        let left = x + 12;
        let top = y + 16;
        if (left + width > window.innerWidth - 8) left = Math.max(8, x - width - 12);
        if (top + height > window.innerHeight - 8) top = Math.max(8, y - height - 12);
        hover.style.left = `${Math.round(left)}px`;
        hover.style.top = `${Math.round(top)}px`;
    }

    function hideHover() {
        hover.hidden = true;
    }

    function offsetFromPoint(clientX, clientY) {
        const style = getComputedStyle(textarea);
        const rect = textarea.getBoundingClientRect();
        if (clientX < rect.left || clientX > rect.right || clientY < rect.top || clientY > rect.bottom) return -1;
        const lineHeight = parseFloat(style.lineHeight) || (parseFloat(style.fontSize) || 13) * 1.5;
        const borderTop = parseFloat(style.borderTopWidth) || 0;
        const paddingTop = parseFloat(style.paddingTop) || 0;
        const y = clientY - rect.top - borderTop - paddingTop + textarea.scrollTop;
        const lineIndex = Math.max(0, Math.floor(y / lineHeight));
        const lines = textarea.value.split('\n');
        if (lineIndex >= lines.length) return -1;
        let lineStart = 0;
        for (let i = 0; i < lineIndex; i += 1) lineStart += lines[i].length + 1;
        const line = lines[lineIndex];
        let lo = 0;
        let hi = line.length;
        while (lo < hi) {
            const mid = Math.floor((lo + hi) / 2);
            const point = caretPoint(lineStart + mid);
            if (point.x < clientX) lo = mid + 1;
            else hi = mid;
        }
        return lineStart + Math.max(0, lo - (lo > 0 && caretPoint(lineStart + lo).x - clientX > clientX - caretPoint(lineStart + lo - 1).x ? 1 : 0));
    }

    function paintToggle() {
        toggleLabel.textContent = automatic ? 'IntelliSense: ON' : 'IntelliSense: OFF';
        toggle.setAttribute('aria-pressed', automatic ? 'true' : 'false');
        toggle.title = automatic
            ? 'IntelliSense: ON — toggle automatic code completions (Ctrl+Space to trigger manually)'
            : 'IntelliSense: OFF — toggle automatic code completions (Ctrl+Space to trigger manually)';
        toggle.disabled = !enabled;
    }

    function setContext(next) {
        const pathChanged = next && next.path != null && next.path !== facts.path;
        facts = {
            path: next?.path != null ? String(next.path) : facts.path,
            section: next?.section != null ? String(next.section) : facts.section,
            slug: next?.slug != null ? String(next.slug) : facts.slug,
            images: Array.isArray(next?.images) ? next.images : (pathChanged ? [] : facts.images),
            unlistedSlides: Array.isArray(next?.unlistedSlides) ? next.unlistedSlides : (pathChanged ? [] : facts.unlistedSlides),
            folders: Array.isArray(next?.folders) ? next.folders : facts.folders,
        };
        if (pathChanged) hide();
    }

    function setEnabled(on) {
        enabled = Boolean(on);
        paintToggle();
        if (!enabled) hide();
    }

    return { setContext, setEnabled, dismiss: hide };
}

function buildSuggestions(text, caret, facts, state, manual) {
    const empty = { items: [], auto: false, query: '', total: 0 };
    const tag = state.tagStart >= 0 && state.commentStart < 0 ? inspectTag(text.slice(state.tagStart, caret)) : null;
    const valued = valueContext(text, caret, state, tag);

    if (valued && valued.type === 'class') {
        return pack(classItems(valued.query, valued.token, valued.from, caret), valued.token, true);
    }
    if (valued && valued.type === 'path') {
        return pack(pathItems(facts, valued.query, valued.from, caret), valued.query, true);
    }
    if (valued && valued.type === 'other') {
        const entity = entityQuery(text, caret);
        if (entity) return pack(entityItems(entity), entity.query, true);
        return empty;
    }

    if (state.commentKind) {
        const comment = commentItems(text, caret, facts, state.commentKind);
        if (comment.items.length) return comment;
        const entity = entityQuery(text, caret);
        if (entity) return pack(entityItems(entity), entity.query, true);
        return empty;
    }

    if (!state.commentStart && !state.tagStart) {
        const image = imageLine(text, caret);
        if (image) return pack(pathItems(facts, image.query, image.from, caret), image.query, true);
    }

    if (tag && tag.mode === 'attr') return pack(attrItems(text, state, tag, caret), tag.query, true);
    if (tag && tag.mode === 'close') return pack(closeItems(text, state, tag, caret), tag.query, true);
    if (tag && (tag.mode === 'name' || tag.mode === 'bang')) return pack(openItems(facts, state, tag, caret), tag.query, true);

    if (isPhp(facts) && state.commentStart < 0 && !state.inRaw) {
        const php = phpItems(text, caret, manual);
        if (php) return php;
    }

    if (state.commentStart < 0) {
        const entity = entityQuery(text, caret);
        if (entity) return pack(entityItems(entity), entity.query, true);
    }

    if (!manual || state.commentStart >= 0 || state.tagStart >= 0) return empty;
    return pack(fallbackItems(text, caret, facts), currentWord(text, caret).text, false);
}

function valueContext(text, caret, state, tag) {
    if (tag && tag.mode === 'value') {
        const attr = String(tag.attr || '').toLowerCase();
        const from = state.tagStart + tag.valueAt;
        if (attr === 'class') {
            const word = (tag.query.match(/(\S*)$/) || ['', ''])[1];
            return { type: 'class', query: tag.query, token: word, from: from + tag.query.length - word.length };
        }
        if (attr === 'src' || attr === 'href') return { type: 'path', query: tag.query, from };
        return { type: 'other' };
    }
    if (state.commentStart >= 0 || state.tagStart >= 0) return null;
    const tail = text.slice(Math.max(0, caret - 240), caret);
    let match = tail.match(/(?:^|[^A-Za-z0-9_-])class\s*=\s*(["'])([^"']*)$/i);
    if (match) {
        const word = (match[2].match(/(\S*)$/) || ['', ''])[1];
        return { type: 'class', query: match[2], token: word, from: caret - word.length };
    }
    match = tail.match(/(?:^|[^A-Za-z0-9_-])(?:src|href)\s*=\s*(["'])([^"']*)$/i);
    if (match) return { type: 'path', query: match[2], from: caret - match[2].length };
    return null;
}

function commentItems(text, caret, facts, kind) {
    const lineStart = text.lastIndexOf('\n', Math.max(0, caret - 1)) + 1;
    const line = text.slice(lineStart, caret);
    const opener = kind === 'page-values' ? /<!--\s*page-values\s*$/i : /<!--\s*related-sales\s*$/i;
    if (opener.test(line)) {
        const items = kindFields(kind).map((field) => ({
            ...field,
            replaceFrom: caret,
            replaceTo: caret,
            insert: `\n  ${field.insert}`,
            cursor: null,
            needle: '',
        }));
        const packed = pack(items, '', true);
        packed.consumed = true;
        return packed;
    }
    if (/<!--/.test(line) || /-->/.test(line)) return { items: [], auto: false, query: '', total: 0, consumed: true };

    const image = line.match(/^\s*image:\s*(.*)$/i);
    if (image && kind === 'page-values') {
        const query = image[1];
        const packed = pack(pathItems(facts, query, caret - query.length, caret), query, true);
        packed.consumed = true;
        return packed;
    }

    const typed = line.trim();
    const colon = typed.indexOf(':');
    if (colon >= 0) {
        const name = typed.slice(0, colon).trim();
        const value = typed.slice(colon + 1).trimStart();
        const needsSpace = value.length === 0 && !/\s$/.test(line);
        return valueFields(kind, name, value, caret - value.length, caret, needsSpace);
    }

    const indent = (line.match(/^\s*/) || [''])[0].length;
    const query = typed;
    const items = kindFields(kind).filter((field) => fieldMatches(field, query)).map((field) => ({
        ...field,
        replaceFrom: lineStart + indent,
        replaceTo: caret,
        needle: query,
    }));
    const packed = pack(items, query, query.length > 0 && items.length > 0);
    packed.consumed = query.length > 0 || items.length > 0;
    return packed;
}

function valueFields(kind, name, value, from, caret, needsSpace) {
    const key = name.toLowerCase().replace(/\s+/g, '');
    let choices = [];
    if (kind === 'page-values' && key === 'type') {
        choices = [
            ['Article', 'Schema.org Article. Use for a story or show report.'],
            ['NewsArticle', 'Schema.org NewsArticle. Use when the page is a news item.'],
            ['CollectionPage', 'Schema.org CollectionPage. Use for an index or a gallery collection.'],
        ];
    } else if (kind === 'page-values' && key === 'date') {
        choices = [[todayStamp(), 'Publication date in YYYY-MM-DD.']];
    } else if (kind === 'related-sales' && key === 'vehicletype') {
        choices = [
            ['car', 'Suggest automobiles.'],
            ['motorcycle', 'Suggest motorcycles.'],
            ['truck', 'Suggest trucks.'],
            ['sidecar', 'Suggest sidecars.'],
        ];
    } else if (kind === 'related-sales' && key === 'maketype') {
        choices = MAKES.map((make) => [make, `Make key ${make}, used by the sales index and the suggestion strip.`]);
    } else {
        return { items: [], auto: false, query: value, total: 0 };
    }
    const items = choices.filter((choice) => !value || choice[0].toLowerCase().startsWith(value.toLowerCase())).map((choice) => {
        const lead = needsSpace ? ' ' : '';
        const insert = `${lead}${choice[0]}`;
        return {
            kind: 'comment',
            label: `${name}: ${choice[0]}`,
            detail: choice[1],
            docs: choice[1],
            insert,
            needle: value,
            replaceFrom: from,
            replaceTo: caret,
            cursor: insert.length,
        };
    });
    return pack(items, value, items.length > 0);
}

function kindFields(kind) {
    if (kind === 'page-values') {
        const stamp = todayStamp();
        return [
            commentField('title: ', 'Share title and browser title.', 'Written into the browser tab, the share card, and the generated <title>.'),
            commentField('description: ', 'Meta description and share text.', '<!-- generated-seo --> is rebuilt from this text.'),
            commentField('image: ', 'Share image path.', 'Usually slides/<file> in this folder. Image paths are suggested after the colon.'),
            commentField('type: Article', 'Schema.org Article.', 'Use for a story or a show report.'),
            commentField('type: NewsArticle', 'Schema.org NewsArticle.', 'Use when the page is a news item.'),
            commentField('type: CollectionPage', 'Schema.org CollectionPage.', 'Use for an index or a gallery collection.'),
            commentField(`date: ${stamp}`, `Publication date. Inserts ${stamp}.`, 'YYYY-MM-DD. The suggestion label shows the required format.', { label: 'date: YYYY-MM-DD' }),
        ];
    }
    return [
        commentField('vehicleType: car', 'Suggest automobiles.', 'Matches sales pages whose vehicle type is car.'),
        commentField('vehicleType: motorcycle', 'Suggest motorcycles.', 'Matches sales pages whose vehicle type is motorcycle.'),
        commentField('vehicleType: truck', 'Suggest trucks.', 'Matches sales pages whose vehicle type is truck.'),
        commentField('vehicleType: sidecar', 'Suggest sidecars.', 'Matches sales pages whose vehicle type is sidecar.'),
        commentField('makeType: ', 'Make key.', 'Examples: Harley-Davidson, Flying_Merkel, Cadillac. The sales index groups vehicles by this key.'),
        commentField('vehicleName: ', 'Vehicle name on the suggestion strip.', 'The name shown when this page recommends a specific vehicle.'),
        commentField('Override IDs: ', 'Force specific inventory numbers.', 'Comma-separated inventory numbers. They override the make and type match.'),
    ];
}

function commentField(insert, detail, docs, extra = {}) {
    return {
        kind: 'comment',
        label: extra.label || insert,
        detail,
        docs,
        signature: extra.label && extra.label !== insert ? `${extra.label} → ${insert}` : insert,
        insert,
        cursor: insert.length,
        replaceFrom: 0,
        replaceTo: 0,
    };
}

function classItems(query, token, from, caret) {
    const have = new Set(String(query || '').split(/\s+/).map((part) => part.trim()).filter((part) => part && part !== token));
    return CLASSES.filter((item) => classMatches(item[0], token) && !item[0].split(/\s+/).every((part) => have.has(part))).map((item) => ({
        kind: 'class',
        label: item[0],
        detail: item[1],
        docs: item[2] || item[1],
        signature: `.${item[0].split(/\s+/).join(' .')}`,
        insert: item[0],
        needle: token,
        replaceFrom: from,
        replaceTo: caret,
        cursor: item[0].length,
    }));
}

function pathItems(facts, query, from, caret) {
    const items = [];
    for (const image of collectImages(facts)) {
        const slideDims = dims(image.image.width, image.image.height);
        const thumbDims = dims(image.image.thumbWidth, image.image.thumbHeight);
        const caption = String(image.image.caption || '').trim();
        const role = image.unlisted ? 'Unlisted slide' : 'Slide';
        if (image.image.hasSlide !== false) {
            items.push(pathItem(`slides/${image.name}`, [role, caption].filter(Boolean).join(' · '), slideDocs(image, caption, 'Full-size slide'), slideDims, query, from, caret));
        }
        if (image.image.hasThumb !== false) {
            const thumbRole = image.unlisted ? 'Unlisted thumbnail' : 'Thumbnail';
            items.push(pathItem(`thumbs/${image.name}`, thumbRole, slideDocs(image, caption, 'Gallery thumbnail'), thumbDims, query, from, caret));
        }
    }
    for (const site of sitePaths(facts, query)) {
        items.push(pathItem(site.path, site.detail, site.docs, '', query, from, caret));
    }
    return items.filter((item) => item.score < 9);
}

function pathItem(path, detail, docs, badge, query, from, caret) {
    return {
        kind: 'asset',
        label: path,
        detail,
        docs,
        signature: path,
        insert: path,
        needle: query,
        dims: badge,
        score: pathScore(path, query),
        replaceFrom: from,
        replaceTo: caret,
        cursor: path.length,
    };
}

function sitePaths(facts, query) {
    const list = [];
    const seen = new Set();
    const add = (path, detail, docs) => {
        if (!path || seen.has(path)) return;
        seen.add(path);
        list.push({ path, detail, docs });
    };
    for (const [path, detail, docs] of FIXED_PATHS) add(path, detail, docs);
    if (facts.section === 'sales' && facts.slug) add(`/sales/${facts.slug}/index.php`, 'This sales page', 'Public URL for the vehicle open in the editor.');
    if (facts.section === 'projects' && facts.slug) add(`/projects/${facts.slug}/index.html`, 'This project page', 'Public URL for the project open in the editor.');
    if (facts.path && facts.path.startsWith('/')) add(facts.path, 'This file', 'The file open in the editor.');
    const q = String(query || '').toLowerCase();
    if (q.startsWith('/') || q.startsWith('sales/') || q.startsWith('projects/') || q.startsWith('articles/') || q.startsWith('awards/')) {
        for (const folder of facts.folders) add(folderPath(folder), folderLabel(folder), `${folder.section || 'page'} / ${folder.slug || ''}`);
    }
    return list;
}

function attrItems(text, state, tag, caret) {
    const name = String(tag.name || '').toLowerCase();
    const catalog = name === 'a' ? ATTRS.a : name === 'img' ? ATTRS.img : BLOCK_ATTR_TAGS.has(name) ? ATTRS.block : ATTRS.block.slice(0, 2);
    const from = state.tagStart + (tag.attrAt == null ? tag.query.length : tag.attrAt);
    const existing = existingAttrs(text.slice(state.tagStart, caret), tag.attrAt || 0);
    return catalog.filter((item) => attrMatches(item[0], tag.query) && !existing.has(attrName(item[0]))).map((item) => {
        const cursor = cursorInAttr(item[0]);
        return {
            kind: 'attr',
            label: item[0],
            detail: item[1],
            docs: item[1],
            signature: item[0],
            insert: item[0],
            needle: tag.query,
            replaceFrom: from,
            replaceTo: caret,
            cursor,
        };
    });
}

function closeItems(text, state, tag, caret) {
    const query = tag.query || '';
    const nextCloses = text[caret] === '>';
    const seen = new Set();
    const items = [];
    for (let index = state.stack.length - 1; index >= 0; index -= 1) {
        const tagInfo = state.stack[index];
        if (seen.has(tagInfo.name)) continue;
        if (query && !tagInfo.source.toLowerCase().startsWith(query.toLowerCase())) continue;
        seen.add(tagInfo.name);
        const nearest = items.length === 0;
        items.push({
            kind: 'tag',
            label: `</${tagInfo.source}>`,
            detail: nearest ? 'Nearest unclosed tag' : 'Unclosed ancestor',
            docs: `Closes the open <${tagInfo.source}> ${nearest ? 'that wraps the caret' : 'further up the page'}.`,
            signature: `</${tagInfo.source}>`,
            insert: nextCloses ? tagInfo.source : `${tagInfo.source}>`,
            needle: query,
            score: nearest ? 0 : 1,
            replaceFrom: state.tagStart + 2,
            replaceTo: caret,
            cursor: nextCloses ? tagInfo.source.length : tagInfo.source.length + 1,
        });
    }
    return items;
}

function openItems(facts, state, tag, caret) {
    const query = tag.mode === 'bang' ? tag.query : tag.query;
    const items = [];
    if (tag.mode !== 'bang') {
        for (const entry of HTML_TAGS) {
            if (query && !entry[0].startsWith(query.toLowerCase())) continue;
            items.push({
                kind: 'tag',
                label: entry[0],
                detail: entry[1],
                docs: entry[1],
                signature: TAG_SIGNATURES[entry[0]] || `<${entry[0]}>`,
                insert: entry[0],
                needle: query,
                replaceFrom: state.tagStart + 1,
                replaceTo: caret,
                cursor: entry[0].length,
            });
        }
    }
    for (const snippet of snippetsFor(facts)) {
        if (!snippetMatch(snippet.label, query, snippet.aliases)) continue;
        const placed = placeholder(snippet.insert);
        items.push({
            kind: snippet.kind || 'snippet',
            label: snippet.label,
            detail: snippet.detail,
            docs: snippet.docs,
            signature: snippet.label,
            insert: snippet.insert,
            needle: query,
            replaceFrom: state.tagStart,
            replaceTo: caret,
            cursor: placed.cursor,
            selectTo: placed.selectTo,
        });
    }
    if (isPhp(facts)) {
        for (const spec of SPECS) {
            const insert = `<li>${spec[0]}</li>`;
            if (!snippetMatch(insert, query, [spec[0]])) continue;
            items.push({
                kind: 'php',
                label: insert,
                detail: spec[1],
                docs: spec[1],
                signature: insert,
                insert,
                needle: query,
                replaceFrom: state.tagStart,
                replaceTo: caret,
                cursor: `<li>${spec[0]}`.length,
            });
        }
    }
    return items;
}

function phpItems(text, caret, manual) {
    const assignment = phpAssignment(text, caret);
    if (assignment) {
        const items = assignment.choices.filter((choice) => !assignment.query || choice[0].toLowerCase().startsWith(assignment.query.toLowerCase())).map((choice) => ({
            kind: 'php',
            label: `${assignment.left}${choice[0]}${assignment.right}`,
            detail: choice[1],
            docs: choice[1],
            signature: `${assignment.left}${choice[0]}${assignment.right}`,
            insert: choice[0],
            needle: assignment.query,
            replaceFrom: assignment.from,
            replaceTo: caret,
            cursor: choice[0].length,
        }));
        if (items.length) return pack(items, assignment.query, true);
    }
    const strict = text.slice(text.lastIndexOf('\n', Math.max(0, caret - 1)) + 1, caret).match(/^\s*(\$[\w]*)$/);
    const loose = manual ? phpLoose(text, caret) : null;
    const token = strict ? { query: strict[1], from: caret - strict[1].length } : loose;
    if (!token) return null;
    const items = phpCatalog().filter((item) => phpMatches(item, token.query)).map((item) => ({
        ...item,
        needle: token.query,
        replaceFrom: token.from,
        replaceTo: caret,
    }));
    if (!items.length) return null;
    return pack(items, token.query, Boolean(strict));
}

function phpCatalog() {
    const items = [
        phpStatement("$vehicleType = 'car';", 'Automobile.', 'Schema and inventory type. Sales pages use car or motorcycle.'),
        phpStatement("$vehicleType = 'motorcycle';", 'Motorcycle.', 'Schema and inventory type. A motorcycle uses the Motorcycle schema.'),
        phpStatement("$makeType = '';", 'Make key.', 'The sales index and the suggestion strip group vehicles by this key.', { label: "$makeType = '...';", cursor: "$makeType = '".length }),
    ];
    for (const make of MAKES) {
        items.push(phpStatement(`$makeType = '${make}';`, `Make key ${make}.`, 'Used by the sales index filters and the related-sales strip.'));
    }
    items.push(
        phpStatement('$doNotRender = false;', 'Publish this vehicle.', 'false keeps the vehicle on the public inventory. true hides the page from the index.'),
        phpStatement("$vehicleName = '';", 'Name shown on the sales page and the index.', 'The heading collectors see. Soft hyphens (&shy;) belong in long names.', { label: "$vehicleName = '...';", cursor: "$vehicleName = '".length }),
        phpStatement('$inventoryNumber = ;', 'Inventory number.', 'The integer printed on the page and accepted by Override IDs.', { label: '$inventoryNumber = ...;', cursor: '$inventoryNumber = '.length }),
        phpStatement("$price = 'Price on Request';", 'Asking price is not published.', 'Any price other than SOLD is treated as still for sale.'),
        phpStatement("$price = 'SOLD';", 'Mark the vehicle sold.', 'SOLD moves the page to the sold archive.'),
        phpStatement('$description = <<<DESC\n\nDESC;', 'Vehicle description heredoc.', 'HTML in the heredoc is the sales description. The cursor is left on the blank line.', { label: '$description = <<<DESC', cursor: '$description = <<<DESC\n'.length }),
    );
    for (const spec of SPECS) {
        const insert = `<li>${spec[0]}</li>`;
        items.push({
            kind: 'php',
            label: insert,
            detail: spec[1],
            docs: spec[1],
            signature: insert,
            insert,
            cursor: `<li>${spec[0]}`.length,
        });
    }
    return items;
}

function phpStatement(insert, detail, docs, extra = {}) {
    return {
        kind: 'php',
        label: extra.label || insert,
        detail,
        docs,
        signature: extra.label || insert,
        insert,
        cursor: extra.cursor == null ? insert.length : extra.cursor,
    };
}

function phpAssignment(text, caret) {
    const line = text.slice(text.lastIndexOf('\n', Math.max(0, caret - 1)) + 1, caret);
    const match = line.match(/^\s*(\$(?:makeType|vehicleType|price))\s*=\s*(['"])([^'"]*)$/);
    if (!match) return null;
    const name = match[1];
    let choices = [];
    if (name === '$makeType') choices = MAKES.map((make) => [make, `Make key ${make}.`]);
    else if (name === '$vehicleType') choices = [['car', 'Automobile.'], ['motorcycle', 'Motorcycle.']];
    else choices = [['Price on Request', 'Asking price is not published.'], ['SOLD', 'Mark the vehicle sold.']];
    return {
        query: match[3],
        from: caret - match[3].length,
        left: `${name} = ${match[2]}`,
        right: `${match[2]};`,
        choices,
    };
}

function entityItems(entity) {
    return ENTITIES.filter((item) => item[0].toLowerCase().startsWith(entity.query.toLowerCase())).map((item) => ({
        kind: 'entity',
        label: item[0],
        detail: item[1],
        docs: item[1],
        signature: item[0],
        insert: item[0],
        needle: entity.query,
        replaceFrom: entity.from,
        replaceTo: entity.from + entity.query.length,
        cursor: item[0].length,
    }));
}

function fallbackItems(text, caret, facts) {
    const word = currentWord(text, caret);
    const query = word.text;
    const from = word.from;
    const items = [];
    if (isPhp(facts)) {
        for (const item of phpCatalog()) {
            if (query && !phpMatches(item, query) && !phpMatches(item, query.startsWith('$') ? query : `$${query}`)) continue;
            items.push({ ...item, needle: query, replaceFrom: from, replaceTo: caret });
        }
    }
    for (const entry of HTML_TAGS) {
        if (query && !entry[0].startsWith(query.toLowerCase())) continue;
        const paired = VOID_TAGS.has(entry[0]) ? `<${entry[0]}>` : `<${entry[0]}></${entry[0]}>`;
        const cursor = VOID_TAGS.has(entry[0]) ? paired.length - 1 : entry[0].length + 2;
        items.push({
            kind: 'tag',
            label: entry[0],
            detail: entry[1],
            docs: entry[1],
            signature: TAG_SIGNATURES[entry[0]] || paired,
            insert: paired,
            needle: query,
            replaceFrom: from,
            replaceTo: caret,
            cursor,
        });
    }
    for (const snippet of snippetsFor(facts)) {
        if (query && !snippetMatch(snippet.label, query, snippet.aliases)) continue;
        const placed = placeholder(snippet.insert);
        items.push({
            kind: 'snippet',
            label: snippet.label,
            detail: snippet.detail,
            docs: snippet.docs,
            insert: snippet.insert,
            needle: query,
            replaceFrom: from,
            replaceTo: caret,
            cursor: placed.cursor,
            selectTo: placed.selectTo,
        });
    }
    return items;
}

function snippetsFor(facts) {
    const page = facts.path && facts.path.startsWith('/') ? facts.path : '...';
    const feature = (modifier, left, right) => `    <div class="articleFeaturePair ${modifier}">\n${figureBlock(left)}\n${figureBlock(right)}\n    </div>`;
    return [
        snippet('<div class="articleFeatureSingle">', featureSingleBlock(), 'Full featured single photo, with a Lightbox2 link and a figcaption.', 'One slide, framed, with a caption under it.', ['articleFeatureSingle']),
        snippet('<div class="articleFeaturePair articleFeaturePair--landscape">', feature('articleFeaturePair--landscape', 'photo-a.jpg', 'photo-b.jpg'), 'Two-column landscape photo pair.', 'Two 4:3 photos. Each opens in the lightbox.', ['articleFeaturePair', 'articleFeaturePair--landscape']),
        snippet('<div class="articleFeaturePair articleFeaturePair--portrait">', feature('articleFeaturePair--portrait', 'photo-c.jpg', 'photo-d.jpg'), 'Two-column portrait photo pair.', 'Two 3:4 photos. Each opens in the lightbox.', ['articleFeaturePair', 'articleFeaturePair--portrait']),
        snippet('<div class="thumbnail-wrapper">', '<div class="thumbnail-wrapper"><a href="slides/photo.jpg" data-lightbox="gallery" data-title="Caption"><img src="thumbs/photo.jpg" class="thumbnail" alt="Caption" title="Caption" loading="lazy" decoding="async"></a></div>', 'Single gallery thumbnail, slides/ in the link and thumbs/ on the image.', 'One cell of the photo gallery grid.', ['thumbnail-wrapper']),
        snippet('<section class="articleGallerySection">', galleryBlock(), 'Full photo gallery section.', 'Heading, note, and one thumbnail row. Duplicate the thumbnail-wrapper for each photo.', ['articleGallerySection']),
        snippet('<!--#include virtual="/includes/header.html"-->', '<!--#include virtual="/includes/header.html"-->', 'Site header and the admin traffic logger.', 'Server-side include. The header also records the page view.', ['include', 'header']),
        snippet('<!--#include virtual="/php/similar_vehicles.php?embed=1" -->', '<!--#include virtual="/php/similar_vehicles.php?embed=1" -->', 'Related sales suggestion strip.', 'SSI. The related-sales comment chooses which vehicles appear.', ['include', 'similar']),
        snippet('<!--#include virtual="/php/index_sales_links.php?page=..." -->', `<!--#include virtual="/php/index_sales_links.php?page=${page}" -->`, 'Index for-sale and sold links.', page === '...' ? 'SSI. Set page= to the article or award this card links to.' : `SSI for ${page}.`, ['include', 'index_sales']),
    ];
}

function snippet(label, insert, detail, docs, aliases) {
    return { kind: 'snippet', label, insert, detail, docs, aliases };
}

function featureSingleBlock() {
    return `    <div class="articleFeatureSingle">
      <figure>
        <a class="articleLightboxLink" href="slides/photo.jpg" data-lightbox="gallery" data-title="Caption">
          <img src="slides/photo.jpg" alt="Caption" title="Caption" decoding="async">
        </a>
        <figcaption class="bodyCaption centerText">Caption</figcaption>
      </figure>
    </div>`;
}

function figureBlock(file) {
    return `      <figure>
        <a class="articleLightboxLink" href="slides/${file}" data-lightbox="gallery" data-title="Caption">
          <img src="thumbs/${file}" alt="Caption" title="Caption" loading="lazy" decoding="async">
        </a>
        <figcaption class="bodyCaption centerText">Caption</figcaption>
      </figure>`;
}

function galleryBlock() {
    return `    <section class="articleGallerySection" aria-label="Photo Gallery">
      <h2 class="centerText">Photo Gallery</h2>
      <p class="centerText bodyCaption articleGalleryNote">Click any photo to view full size.</p>
      <div class="thumbs-wrapper">
        <div class="thumbs">
          <div class="thumbnail-wrapper"><a href="slides/photo.jpg" data-lightbox="gallery" data-title="Caption"><img src="thumbs/photo.jpg" class="thumbnail" alt="Caption" title="Caption" loading="lazy" decoding="async"></a></div>
        </div>
      </div>
    </section>`;
}

function analyze(value, caret) {
    const stack = [];
    let quote = '';
    let tagStart = -1;
    let commentStart = -1;
    const limit = clampIndex(caret, value.length);
    let i = 0;
    while (i < limit) {
        if (commentStart >= 0) {
            if (value.startsWith('-->', i)) {
                commentStart = -1;
                i += 3;
                continue;
            }
            i += 1;
            continue;
        }
        if (tagStart >= 0) {
            const ch = value[i];
            if (quote) {
                if (ch === quote) quote = '';
                i += 1;
                continue;
            }
            if (i === tagStart + 1 && !/[A-Za-z/!]/.test(ch)) {
                tagStart = -1;
            } else {
                if (ch === '"' || ch === "'") quote = ch;
                else if (ch === '>') {
                    commitTag(value.slice(tagStart, i), stack);
                    tagStart = -1;
                    quote = '';
                }
                i += 1;
                continue;
            }
        }
        if (value.startsWith('<!--', i)) {
            commentStart = i;
            i += 4;
            continue;
        }
        if (value[i] === '<') {
            const raw = rawBlock(value, i, limit, stack);
            if (raw && raw.action === 'inside') {
                return finishState(value, limit, stack, raw.tagStart, '', -1, raw.inRaw);
            }
            if (raw && raw.action === 'skip') {
                i = raw.index;
                continue;
            }
            tagStart = i;
        }
        i += 1;
    }
    return finishState(value, limit, stack, tagStart, quote, commentStart, '');
}

function rawBlock(value, index, limit, stack) {
    const ahead = value.slice(index, limit);
    const raw = /^<(script|style)\b/i.exec(ahead);
    if (!raw) return null;
    const gt = indexOfUnquoted(value, '>', index);
    if (gt < 0 || gt >= limit) return null;
    if (/\/\s*$/.test(value.slice(index, gt))) return null;
    const closeAt = findCloseTag(value, gt + 1, raw[1]);
    if (closeAt >= 0 && closeAt < limit) {
        const matched = /^<\/\s*[A-Za-z][\w:-]*\s*>/.exec(value.slice(closeAt));
        if (!matched) return null;
        return { action: 'skip', index: closeAt + matched[0].length };
    }
    stack.push({ name: raw[1].toLowerCase(), source: raw[1] });
    const lt = value.lastIndexOf('<', limit - 1);
    const inner = lt >= gt + 1 && value.slice(lt, limit).startsWith('</') ? lt : -1;
    return { action: 'inside', tagStart: inner, inRaw: raw[1].toLowerCase() };
}

function finishState(value, limit, stack, tagStart, quote, commentStart, inRaw) {
    let commentKind = '';
    if (commentStart >= 0) {
        const head = value.slice(commentStart, Math.min(limit, commentStart + 96));
        if (/<!--\s*page-values\b/i.test(head)) commentKind = 'page-values';
        else if (/<!--\s*related-sales\b/i.test(head)) commentKind = 'related-sales';
    }
    return {
        stack,
        tagStart,
        quote: tagStart >= 0 ? quote : '',
        commentStart,
        commentKind,
        inRaw,
    };
}

function commitTag(raw, stack) {
    if (raw.startsWith('<!') || raw.startsWith('<?')) return;
    if (raw.startsWith('</')) {
        const match = /^<\/\s*([A-Za-z][\w:-]*)/.exec(raw);
        if (!match) return;
        const name = match[1].toLowerCase();
        for (let index = stack.length - 1; index >= 0; index -= 1) {
            if (stack[index].name === name) {
                stack.length = index;
                break;
            }
        }
        return;
    }
    const match = /^<([A-Za-z][\w:-]*)/.exec(raw);
    if (!match) return;
    const name = match[1].toLowerCase();
    if (/\/\s*$/.test(raw) || VOID_TAGS.has(name)) return;
    stack.push({ name, source: match[1] });
}

function inspectTag(slice) {
    if (slice.startsWith('</')) {
        const match = /^<\/([A-Za-z][\w:-]*)?/.exec(slice);
        const query = match && match[1] ? match[1] : '';
        return { mode: 'close', name: query, query };
    }
    if (slice.startsWith('<!')) return { mode: 'bang', name: '', query: slice.slice(1) };
    const nameMatch = /^<([A-Za-z][\w:-]*)/.exec(slice);
    if (!nameMatch) return { mode: 'name', name: '', query: slice.slice(1), attrAt: 1 };
    const name = nameMatch[1];
    if (nameMatch[0].length >= slice.length) return { mode: 'name', name, query: name, attrAt: 1 };
    let quote = '';
    let attrAt = -1;
    let valueAt = -1;
    let sawEquals = false;
    for (let i = nameMatch[0].length; i < slice.length; i += 1) {
        const ch = slice[i];
        if (quote) {
            if (ch === quote) quote = '';
            continue;
        }
        if ((ch === '"' || ch === "'") && sawEquals) {
            quote = ch;
            valueAt = i + 1;
            sawEquals = false;
            attrAt = -1;
            continue;
        }
        if (ch === '=') {
            sawEquals = true;
            continue;
        }
        if (/\s/.test(ch)) {
            attrAt = -1;
            if (!sawEquals) continue;
            continue;
        }
        if (attrAt < 0 && /[A-Za-z_:]/.test(ch)) {
            attrAt = i;
            sawEquals = false;
        }
    }
    if (quote && valueAt >= 0) {
        return { mode: 'value', name, attr: attrNameBefore(slice, valueAt), query: slice.slice(valueAt), valueAt, attrAt: valueAt };
    }
    if (attrAt >= 0) return { mode: 'attr', name, query: slice.slice(attrAt), attrAt };
    return { mode: 'attr', name, query: '', attrAt: slice.length };
}

function pairClose(value, caret) {
    const state = analyze(value, caret);
    if (state.tagStart < 0 || state.quote || state.commentStart >= 0) return null;
    const slice = value.slice(state.tagStart, caret);
    if (!slice.startsWith('<') || slice.startsWith('</') || slice.startsWith('<!') || slice.startsWith('<?')) return null;
    if (/\/\s*$/.test(slice)) return null;
    const match = /^<([A-Za-z][\w:-]*)/.exec(slice);
    if (!match || VOID_TAGS.has(match[1].toLowerCase())) return null;
    if (new RegExp(`^</${match[1]}\\s*>`, 'i').test(value.slice(caret))) return null;
    return { insert: `></${match[1]}>`, cursor: 1 };
}

function pairQuote(value, from, to, quote) {
    if (to > from) {
        const selected = value.slice(from, to);
        if (selected.length > 400) return null;
        return { insert: quote + selected + quote, cursor: 1, selectTo: 1 + selected.length, replaceTo: to };
    }
    if (value[from] === quote) return { skip: 1 };
    const state = analyze(value, from);
    if (state.quote) return null;
    if (quote === "'" && state.tagStart < 0 && value[from - 1] !== '=') return null;
    if (quote === "'" && /[\w]/.test(value[from - 1] || '')) return null;
    return { insert: quote + quote, cursor: 1 };
}

function entityQuery(value, caret) {
    let index = caret;
    while (index > 0 && /[A-Za-z]/.test(value[index - 1])) index -= 1;
    if (value[index - 1] !== '&' || value[index - 2] === '&') return null;
    return { query: value.slice(index - 1, caret), from: index - 1 };
}

function phpLoose(value, caret) {
    let index = caret;
    while (index > 0 && /\w/.test(value[index - 1])) index -= 1;
    if (value[index - 1] !== '$') return null;
    return { query: value.slice(index - 1, caret), from: index - 1 };
}

function imageLine(text, caret) {
    const line = text.slice(text.lastIndexOf('\n', Math.max(0, caret - 1)) + 1, caret);
    const match = line.match(/^\s*image:\s*(.*)$/i);
    if (!match) return null;
    return { query: match[1], from: caret - match[1].length };
}

function classMatches(name, query) {
    if (!query) return true;
    const q = query.toLowerCase();
    return name.toLowerCase().split(/\s+/).some((part) => part.startsWith(q));
}

function pathScore(path, query) {
    const q = String(query || '').toLowerCase();
    if (!q) return 1;
    const p = path.toLowerCase();
    if (p.startsWith(q)) return 0;
    const base = p.slice(p.lastIndexOf('/') + 1);
    if (!q.includes('/') && base.startsWith(q)) return 1;
    if (q.length >= 2 && p.includes(q)) return 2;
    return 9;
}

function snippetMatch(label, query, aliases) {
    if (!query) return true;
    const q = query.toLowerCase();
    const body = label.startsWith('<') ? label.slice(1).toLowerCase() : label.toLowerCase();
    if (body.startsWith(q) || label.toLowerCase().startsWith(q) || label.toLowerCase().startsWith(`<${q}`)) return true;
    return (aliases || []).some((alias) => String(alias).toLowerCase().startsWith(q));
}

function phpMatches(item, query) {
    if (!query) return true;
    const q = query.toLowerCase();
    const label = String(item.label || '').toLowerCase();
    const insert = String(item.insert || '').toLowerCase();
    if (label.startsWith(q) || insert.startsWith(q)) return true;
    if (q.startsWith('$')) return label.startsWith(q);
    return label.includes(q) || insert.includes(q);
}

function fieldMatches(field, query) {
    if (!query) return true;
    const q = query.toLowerCase();
    return field.label.toLowerCase().startsWith(q) || field.insert.toLowerCase().startsWith(q);
}

function attrMatches(text, query) {
    if (!query) return true;
    const name = attrName(text).toLowerCase();
    return name.startsWith(query.toLowerCase()) || text.toLowerCase().startsWith(query.toLowerCase());
}

function attrName(text) {
    return String(text).split('=')[0].trim().toLowerCase();
}

function existingAttrs(slice, tokenStart) {
    const head = slice.slice(0, Math.max(0, tokenStart));
    const names = new Set();
    const pattern = /([:@A-Za-z_][\w:.-]*)\s*=/g;
    let match = pattern.exec(head);
    while (match) {
        names.add(match[1].toLowerCase());
        match = pattern.exec(head);
    }
    return names;
}

function attrNameBefore(slice, valueAt) {
    let index = valueAt - 2;
    while (index >= 0 && /\s/.test(slice[index])) index -= 1;
    if (slice[index] === '=') index -= 1;
    while (index >= 0 && /\s/.test(slice[index])) index -= 1;
    const end = index + 1;
    while (index >= 0 && /[^\s=<>]/.test(slice[index])) index -= 1;
    return slice.slice(index + 1, end);
}

function cursorInAttr(text) {
    const empty = text.indexOf('=""');
    if (empty >= 0) return empty + 2;
    const single = text.indexOf("=''");
    if (single >= 0) return single + 2;
    return text.length;
}

function placeholder(insert) {
    const at = insert.indexOf('photo.jpg');
    if (at >= 0) return { cursor: at, selectTo: at + 'photo.jpg'.length };
    return { cursor: insert.length, selectTo: null };
}

function pack(items, query, auto) {
    const ranked = items.map((item, index) => ({ item, index }));
    const q = String(query || '').toLowerCase();
    ranked.sort((a, b) => {
        const as = a.item.score == null ? matchScore(a.item.label, q) : a.item.score;
        const bs = b.item.score == null ? matchScore(b.item.label, q) : b.item.score;
        if (as !== bs) return as - bs;
        return a.index - b.index;
    });
    const ordered = ranked.map((row) => row.item);
    return {
        items: ordered.slice(0, MAX_ITEMS),
        auto: Boolean(auto) && ordered.length > 0,
        query: query || '',
        total: ordered.length,
    };
}

function matchScore(label, query) {
    if (!query) return 1;
    const hay = String(label || '').toLowerCase();
    if (hay === query || hay.startsWith(query)) return 0;
    const at = hay.indexOf(query);
    return at < 0 ? 2 : 1 + at / 1000;
}

function collectImages(facts) {
    const map = new Map();
    const add = (image, unlisted) => {
        const name = imageName(image);
        if (!name) return;
        const key = name.toLowerCase();
        if (!map.has(key)) map.set(key, { name, image, unlisted });
    };
    for (const image of facts.images) add(image, false);
    for (const image of facts.unlistedSlides) add(image, true);
    return [...map.values()].sort((a, b) => a.name.localeCompare(b.name, undefined, { numeric: true }));
}

function imageName(image) {
    if (!image || typeof image !== 'object') return '';
    if (image.name) return String(image.name);
    const url = String(image.slideUrl || image.thumbUrl || '');
    const parts = url.split('/').filter(Boolean);
    return parts[parts.length - 1] || '';
}

function imageLookup(facts, token) {
    const text = String(token || '');
    const images = collectImages(facts);
    for (const image of images) {
        if (text === image.name || text.endsWith(`/${image.name}`)) {
            const path = text.includes('thumbs/') ? `thumbs/${image.name}` : `slides/${image.name}`;
            const badge = text.includes('thumbs/') ? dims(image.image.thumbWidth, image.image.thumbHeight) : dims(image.image.width, image.image.height);
            return { path, detail: [image.unlisted ? 'Unlisted' : 'Folder image', badge, image.image.caption].filter(Boolean).join(' · ') };
        }
    }
    return null;
}

function slideDocs(image, caption, role) {
    const bits = [role, image.name];
    if (caption) bits.push(caption);
    return bits.join(' — ');
}

function folderPath(folder) {
    if (!folder || typeof folder !== 'object') return '';
    if (folder.path && String(folder.path).startsWith('/')) return String(folder.path);
    if (!folder.section || !folder.slug) return '';
    if (folder.section === 'sales') return `/sales/${folder.slug}/index.php`;
    return `/${folder.section}/${folder.slug}/index.html`;
}

function folderLabel(folder) {
    return folder?.name || folder?.slug || 'Page';
}

function phpDocs(name) {
    const docs = {
        $vehicleType: 'Inventory type. car and motorcycle change the schema and the suggestion strip.',
        $makeType: 'Make key used by the sales index and related-sales, such as Cadillac or Flying_Merkel.',
        $doNotRender: 'false publishes the vehicle. true hides it from the inventory.',
        $vehicleName: 'Name shown on the sales page and the index.',
        $inventoryNumber: 'Integer inventory number. Override IDs on other pages refer to it.',
        $price: 'Asking price. SOLD archives the vehicle. Anything else is still for sale.',
        $description: 'Heredoc of the sales description. HTML in the heredoc is rendered on the page.',
    };
    return docs[name] || '';
}

function tokenAt(value, offset) {
    const isTok = (ch) => /[A-Za-z0-9_:&;./-]/.test(ch || '');
    let start = Math.max(0, Math.min(offset, value.length));
    let end = start;
    if (start < value.length && !isTok(value[start]) && start > 0 && isTok(value[start - 1])) start -= 1;
    while (start > 0 && isTok(value[start - 1])) start -= 1;
    while (end < value.length && isTok(value[end])) end += 1;
    return { text: value.slice(start, end), start, end };
}

function currentWord(value, caret) {
    let index = caret;
    while (index > 0 && /[A-Za-z0-9_:-]/.test(value[index - 1])) index -= 1;
    return { from: index, text: value.slice(index, caret) };
}

function labelNode(label, needle) {
    const span = document.createElement('span');
    span.className = 'is-label';
    const text = String(label ?? '');
    const query = String(needle ?? '');
    const at = query ? text.toLowerCase().indexOf(query.toLowerCase()) : -1;
    if (!query || at < 0) {
        span.textContent = text;
        return span;
    }
    span.append(
        document.createTextNode(text.slice(0, at)),
        el('mark', { class: 'is-hit', text: text.slice(at, at + query.length) }),
        document.createTextNode(text.slice(at + query.length)),
    );
    return span;
}

function copyMirrorStyle(mirror, style) {
    mirror.style.fontStyle = style.fontStyle;
    mirror.style.fontVariant = style.fontVariant;
    mirror.style.fontWeight = style.fontWeight;
    mirror.style.fontStretch = style.fontStretch;
    mirror.style.fontSize = style.fontSize;
    mirror.style.lineHeight = style.lineHeight;
    mirror.style.fontFamily = style.fontFamily;
    mirror.style.letterSpacing = style.letterSpacing;
    mirror.style.wordSpacing = style.wordSpacing;
    mirror.style.textTransform = style.textTransform;
    mirror.style.textIndent = style.textIndent;
    mirror.style.tabSize = style.tabSize || '4';
    mirror.style.paddingTop = style.paddingTop;
    mirror.style.paddingRight = style.paddingRight;
    mirror.style.paddingBottom = style.paddingBottom;
    mirror.style.paddingLeft = style.paddingLeft;
    mirror.style.borderTopWidth = style.borderTopWidth;
    mirror.style.borderRightWidth = style.borderRightWidth;
    mirror.style.borderBottomWidth = style.borderBottomWidth;
    mirror.style.borderLeftWidth = style.borderLeftWidth;
    mirror.style.borderStyle = 'solid';
    mirror.style.boxSizing = style.boxSizing;
    mirror.style.whiteSpace = 'pre';
}

function revealRow(node) {
    const parent = node.parentElement;
    if (!parent) return;
    const top = node.offsetTop;
    const bottom = top + node.offsetHeight;
    if (top < parent.scrollTop) parent.scrollTop = top;
    else if (bottom > parent.scrollTop + parent.clientHeight) parent.scrollTop = bottom - parent.clientHeight;
}

function findCloseTag(value, from, name) {
    const lower = name.toLowerCase();
    let index = from;
    while (index < value.length) {
        const lt = value.indexOf('<', index);
        if (lt < 0) return -1;
        const match = /^<\/\s*([A-Za-z][\w:-]*)\s*>/.exec(value.slice(lt));
        if (match && match[1].toLowerCase() === lower) return lt;
        index = lt + 1;
    }
    return -1;
}

function indexOfUnquoted(value, ch, from) {
    let quote = '';
    for (let i = from; i < value.length; i += 1) {
        const current = value[i];
        if (quote) {
            if (current === quote) quote = '';
            continue;
        }
        if (current === '"' || current === "'") {
            quote = current;
            continue;
        }
        if (current === ch) return i;
    }
    return -1;
}

function normalizeFacts(context) {
    const source = context && typeof context === 'object' ? context : {};
    return {
        path: String(source.path || ''),
        section: String(source.section || ''),
        slug: String(source.slug || ''),
        images: Array.isArray(source.images) ? source.images : [],
        unlistedSlides: Array.isArray(source.unlistedSlides) ? source.unlistedSlides : [],
        folders: Array.isArray(source.folders) ? source.folders : [],
    };
}

function isPhp(facts) {
    return /\.php$/i.test(facts.path || '');
}

function todayStamp() {
    const now = new Date();
    const month = String(now.getMonth() + 1).padStart(2, '0');
    const day = String(now.getDate()).padStart(2, '0');
    return `${now.getFullYear()}-${month}-${day}`;
}

function dims(width, height) {
    const w = Number(width);
    const h = Number(height);
    if (!Number.isFinite(w) || !Number.isFinite(h) || w <= 0 || h <= 0) return '';
    return `${w}×${h}`;
}

function clampIndex(index, length) {
    const number = Number(index);
    if (!Number.isFinite(number)) return 0;
    return Math.max(0, Math.min(number, length));
}

function readAutomatic() {
    try {
        return sessionStorage.getItem(STORE_KEY) !== 'off';
    } catch {
        return true;
    }
}

function writeAutomatic(on) {
    try {
        sessionStorage.setItem(STORE_KEY, on ? 'on' : 'off');
    } catch { /* private mode */ }
}
