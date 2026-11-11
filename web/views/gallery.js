export function captionIsReal(caption, vehicleName, imageName) {
    const text = String(caption ?? '').trim();
    if (!text) return false;
    const vehicle = String(vehicleName ?? '').trim();
    if (vehicle && text.toLowerCase() === vehicle.toLowerCase()) return false;
    if (/^photo\s*\d+$/i.test(text)) return false;
    if (vehicle) {
        const escaped = vehicle.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
        if (new RegExp(`^${escaped}\\s*[-–—:]\\s*photo\\s*\\d+$`, 'i').test(text)) return false;
    }
    const file = String(imageName ?? '').trim();
    const base = file.replace(/\.[^.]+$/, '');
    if (file && text.toLowerCase() === file.toLowerCase()) return false;
    if (base && text.toLowerCase() === base.toLowerCase()) return false;
    if (/\.(?:jpe?g|png|gif|webp|bmp|tiff?)$/i.test(text)) return false;
    return true;
}

export function bulletAssignments(images, vehicleName) {
    const byImage = new Map();
    const seen = new Set();
    let count = 0;
    for (let index = 1; index < images.length; index += 1) {
        const image = images[index];
        const caption = String(image?.caption ?? '').trim();
        if (!captionIsReal(caption, vehicleName, image?.name)) continue;
        const key = caption.toLowerCase();
        if (seen.has(key)) continue;
        if (count >= 4) break;
        count += 1;
        seen.add(key);
        byImage.set(image, count);
    }
    return { byImage, count };
}

export function captionRole(image, index, assignment, vehicleName) {
    const trimmed = String(image?.caption ?? '').trim();
    const bullet = assignment.byImage.get(image) || 0;
    if (!trimmed) {
        return { tone: 'muted', text: 'No caption yet. The lightbox falls back to the vehicle name.' };
    }
    if (index === 0) {
        return { tone: 'hero', text: 'Hero caption. Shown on the large photo and as this photo’s lightbox title.' };
    }
    if (bullet) {
        return {
            tone: 'bullet',
            text: `Bullet #${bullet}. Promoted to the list at the top of the vehicle page, and used as this photo’s lightbox title.`,
        };
    }
    if (captionIsReal(trimmed, vehicleName, image?.name)) {
        return { tone: 'caption', text: 'Lightbox caption. Shown when this photo is opened. It is not one of the four top bullets.' };
    }
    return {
        tone: 'muted',
        text: 'Shown in the lightbox, but not promoted. The public page skips captions that repeat the vehicle name, the file name, or a “Photo N” placeholder.',
    };
}

export function relocate(images, selected, finalStart) {
    const moving = [];
    const rest = [];
    for (const image of images) {
        if (selected.has(image)) moving.push(image);
        else rest.push(image);
    }
    if (!moving.length) return images;
    const start = Math.max(0, Math.min(rest.length, finalStart));
    let contiguous = true;
    const currentStart = images.indexOf(moving[0]);
    for (let index = 0; index < moving.length; index += 1) {
        if (images[currentStart + index] !== moving[index]) contiguous = false;
    }
    if (contiguous && currentStart === start) return images;
    const next = rest.slice();
    next.splice(start, 0, ...moving);
    return next;
}

export function finalStartForInsert(images, selected, insertBefore) {
    let movingBefore = 0;
    const end = Math.max(0, Math.min(images.length, insertBefore));
    for (let index = 0; index < end; index += 1) {
        if (selected.has(images[index])) movingBefore += 1;
    }
    return insertBefore - movingBefore;
}

export function formatBytes(bytes) {
    const value = Number(bytes);
    if (!Number.isFinite(value) || value <= 0) return '';
    if (value < 1024) return `${Math.round(value)} B`;
    if (value < 1024 * 1024) return `${(value / 1024).toFixed(1)} KB`;
    return `${(value / (1024 * 1024)).toFixed(2)} MB`;
}

export function formatDimensions(width, height) {
    const w = Number(width) || 0;
    const h = Number(height) || 0;
    if (!w || !h) return '';
    return `${w} × ${h}`;
}

const EQUALS_ERROR = '= is reserved by image_data.txt. Captions are stored as filename=caption, so saving is paused until the equals sign is removed.';
const HTML_GALLERY_SECTIONS = new Set(['articles', 'awards', 'projects']);

export function attachGallerySource(doc) {
    if (!doc || typeof doc !== 'object') return doc;
    const section = doc.section || '';
    const path = String(doc.path || '');
    const isHtml = /\.s?html?$/i.test(path);
    const html = typeof doc.content === 'string' ? doc.content : '';
    const parsed = HTML_GALLERY_SECTIONS.has(section) && isHtml
        ? (parseThumbnailGallery(html, { section, slug: doc.slug || '' }) || parseJalbumGallery(html, { section, slug: doc.slug || '' }))
        : null;
    if (parsed) {
        const merged = mergeParsedImages(parsed.images, doc.images, doc.unlistedSlides, html);
        doc.gallerySource = 'html';
        doc.hasGallery = true;
        doc.images = merged.images;
        doc.unlistedSlides = merged.unlistedSlides;
        return doc;
    }
    if (section === 'sales' || (Array.isArray(doc.images) && doc.images.length > 0)) doc.gallerySource = 'data';
    else if (doc.gallerySource !== 'html') doc.gallerySource = 'none';
    doc.unlistedSlides = omitListedSlides(doc.unlistedSlides, doc.images, html);
    return doc;
}

export function parseThumbnailGallery(html, { section = '', slug = '' } = {}) {
    const block = findThumbsBlock(String(html || ''));
    if (!block) return null;
    const images = [];
    const re = /<div\b[^>]*\bclass\s*=\s*("[^"]*\bthumbnail-wrapper\b[^"]*"|'[^']*\bthumbnail-wrapper\b[^']*')[^>]*>[\s\S]*?<\/div>/gi;
    let match;
    while ((match = re.exec(block.inner))) {
        const node = match[0];
        const anchor = firstTag(node, 'a');
        const imageTag = firstTag(node, 'img');
        const href = attrValue(anchor, 'href');
        const thumbSrc = attrValue(imageTag, 'src');
        const caption = attrValue(anchor, 'data-title') || attrValue(imageTag, 'alt') || attrValue(imageTag, 'title');
        const name = baseName(href || thumbSrc);
        if (!name) continue;
        images.push({
            name,
            caption,
            originalCaption: caption,
            originalHtml: node,
            href: href || `slides/${name}`,
            thumbSrc: thumbSrc || `thumbs/${name}`,
            lightbox: attrValue(anchor, 'data-lightbox'),
            slideUrl: publicAsset(section, slug, href || `slides/${name}`),
            thumbUrl: publicAsset(section, slug, thumbSrc || `thumbs/${name}`),
            width: 0,
            height: 0,
            thumbWidth: 0,
            thumbHeight: 0,
            fileSize: 0,
            hasSlide: true,
            hasThumb: Boolean(thumbSrc),
            isBullet: false,
        });
    }
    if (!images.length) return null;
    return { images, indent: block.indent, lightbox: images.find((image) => image.lightbox)?.lightbox || '' };
}

export function writeThumbnailGallery(html, images) {
    const source = String(html || '');
    const block = findThumbsBlock(source);
    if (!block) return writeJalbumGallery(source, images);
    const list = Array.isArray(images) ? images : [];
    const lightbox = list.find((image) => image?.lightbox)?.lightbox || blockLightbox(block.inner) || 'gallery';
    const kept = replaceChangedWrappers(block.inner, list, lightbox);
    const inner = kept == null ? renderThumbsInner(block, list, lightbox) : kept;
    return source.slice(0, block.innerStart) + inner + source.slice(block.innerEnd);
}

function parseJalbumGallery(html, { section = '', slug = '' } = {}) {
    const source = String(html || '');
    const re = /<div\b[^>]*>/gi;
    const images = [];
    let match;
    let rangeStart = -1;
    let rangeEnd = -1;
    while ((match = re.exec(source))) {
        if (insideStyleOrScript(source, match.index)) continue;
        if (!classList(match[0]).includes('card')) continue;
        const closeAt = closingDiv(source, match.index);
        if (closeAt < 0) continue;
        const endTag = source.indexOf('>', closeAt);
        const endPos = endTag >= 0 ? endTag + 1 : closeAt;
        const node = source.slice(match.index, endPos);
        const anchor = firstTag(node, 'a');
        if (!classList(anchor).includes('thumb')) {
            re.lastIndex = endPos;
            continue;
        }
        const href = attrValue(anchor, 'href');
        const imageTag = firstTag(node, 'img');
        const thumbSrc = attrValue(imageTag, 'src');
        const name = slideFileName(href || thumbSrc);
        const usesSlide = /(?:^|[/\\])(?:slides|thumbs)[/\\]/i.test(href) || /(?:^|[/\\])(?:slides|thumbs)[/\\]/i.test(thumbSrc);
        if (!name || !usesSlide) {
            re.lastIndex = endPos;
            continue;
        }
        const comment = node.match(/<div\b[^>]*\bclass\s*=\s*(?:"[^"]*\bcomment\b[^"]*"|'[^']*\bcomment\b[^']*')[^>]*>([\s\S]*?)<\/div>/i);
        const caption = comment
            ? plainComment(comment[1])
            : (attrValue(anchor, 'data-title') || attrValue(imageTag, 'alt') || attrValue(imageTag, 'title'));
        if (rangeStart < 0) rangeStart = match.index;
        rangeEnd = endPos;
        images.push({
            name,
            caption,
            originalCaption: caption,
            originalHtml: node,
            href: href || `slides/${name}`,
            thumbSrc: thumbSrc || `thumbs/${name}`,
            lightbox: attrValue(anchor, 'data-lightbox') || 'gallery',
            slideUrl: publicAsset(section, slug, href || `slides/${name}`),
            thumbUrl: publicAsset(section, slug, thumbSrc || `thumbs/${name}`),
            width: Number(attrValue(imageTag, 'width')) || 0,
            height: Number(attrValue(imageTag, 'height')) || 0,
            thumbWidth: 0,
            thumbHeight: 0,
            fileSize: 0,
            hasSlide: true,
            hasThumb: Boolean(thumbSrc),
            isBullet: false,
        });
        re.lastIndex = endPos;
    }
    if (!images.length || rangeStart < 0) return null;
    const lineStart = source.lastIndexOf('\n', rangeStart) + 1;
    const indent = (source.slice(lineStart, rangeStart).match(/^[ \t]*/) || [''])[0];
    return { images, indent, rangeStart, rangeEnd, jalbum: true };
}

function plainComment(html) {
    return decodeEntities(String(html || '').replace(/<br\s*\/?>/gi, ' ').replace(/<[^>]+>/g, ' ')).replace(/\s+/g, ' ').trim();
}

function writeJalbumGallery(html, images) {
    const parsed = parseJalbumGallery(html);
    if (!parsed) return null;
    const list = Array.isArray(images) ? images : [];
    const unchanged = parsed.images.length === list.length && parsed.images.every((image, index) => (
        fileKey(image.name) === fileKey(list[index]?.name)
        && String(image.caption ?? '') === String(list[index]?.caption ?? '')
    ));
    if (unchanged) return String(html || '');
    const body = list.map((image, index) => jalbumCard(image, index, list.length)).join(`\n${parsed.indent}`);
    return String(html || '').slice(0, parsed.rangeStart) + body + String(html || '').slice(parsed.rangeEnd);
}

function jalbumCard(image, index, total) {
    if (image?.originalHtml && String(image.caption ?? '') === String(image.originalCaption ?? '')) {
        return renumberCard(String(image.originalHtml).trim(), index, total);
    }
    const name = slideFileName(image?.name) || 'photo.jpg';
    const file = encodeURIComponent(name);
    const caption = escapeAttr(image?.caption || '');
    const comment = caption ? `<div class="comment">${caption}</div>` : '';
    return `<div class="card preload image hascaption landscape"><a class="thumb" href="slides/${file}"><img src="thumbs/${file}" width="265" height="212" alt="${caption}"></a><div class="caption">${comment}<div class="nr">${index + 1} / ${total}</div></div></div>`;
}

function renumberCard(card, index, total) {
    return card.replace(/(<div\b[^>]*\bclass\s*=\s*(?:"[^"]*\bnr\b[^"]*"|'[^']*\bnr\b[^']*')[^>]*>)[\s\S]*?(<\/div>)/i, `$1${index + 1} / ${total}$2`);
}

function replaceChangedWrappers(inner, images, lightbox) {
    const names = [];
    const re = /<div\b[^>]*\bclass\s*=\s*("[^"]*\bthumbnail-wrapper\b[^"]*"|'[^']*\bthumbnail-wrapper\b[^']*')[^>]*>[\s\S]*?<\/div>/gi;
    let match;
    while ((match = re.exec(inner))) names.push(baseName(attrValue(firstTag(match[0], 'a'), 'href') || attrValue(firstTag(match[0], 'img'), 'src')));
    if (names.length !== images.length || names.some((name, index) => name !== images[index]?.name)) return null;
    let next = inner;
    for (const image of images) {
        if (!image?.originalHtml || String(image.caption ?? '') === String(image.originalCaption ?? '')) continue;
        const fresh = wrapperHtml(image, lightbox);
        if (!next.includes(image.originalHtml)) return null;
        next = next.replace(image.originalHtml, fresh);
    }
    return next;
}

function renderThumbsInner(block, images, lightbox) {
    const lines = images.map((image) => `${block.indent}${wrapperHtml(image, lightbox)}`);
    return lines.length ? `\n${lines.join('\n')}\n${block.wrapperIndent}` : `\n${block.wrapperIndent}`;
}

function blockLightbox(inner) {
    return attrValue(firstTag(inner, 'a'), 'data-lightbox');
}

function mergeParsedImages(parsed, dataImages, unlistedSlides, html) {
    const pool = [...(Array.isArray(dataImages) ? dataImages : []), ...(Array.isArray(unlistedSlides) ? unlistedSlides : [])];
    const byName = new Map();
    for (const item of pool) {
        const key = fileKey(item?.name || item?.slideUrl || item?.thumbUrl || '');
        if (key && !byName.has(key)) byName.set(key, item);
    }
    const images = parsed.map((image) => {
        const key = fileKey(image.name);
        const extra = key ? byName.get(key) : null;
        if (!extra) return image;
        return {
            ...image,
            width: Number(extra.width) || image.width || 0,
            height: Number(extra.height) || image.height || 0,
            thumbWidth: Number(extra.thumbWidth) || 0,
            thumbHeight: Number(extra.thumbHeight) || 0,
            fileSize: Number(extra.fileSize) || 0,
            hasSlide: extra.hasSlide !== false,
            hasThumb: extra.hasThumb != null ? Boolean(extra.hasThumb) : image.hasThumb,
            slideUrl: extra.slideUrl || image.slideUrl,
            thumbUrl: extra.thumbUrl || image.thumbUrl,
        };
    });
    return { images, unlistedSlides: omitListedSlides(pool, images, html) };
}

function findThumbsBlock(html) {
    const openRe = /<div\b[^>]*>/gi;
    let match;
    while ((match = openRe.exec(html))) {
        if (insideStyleOrScript(html, match.index)) continue;
        if (!classList(match[0]).includes('thumbs')) continue;
        const closeAt = closingDiv(html, match.index);
        if (closeAt < 0) continue;
        const innerStart = match.index + match[0].length;
        const inner = html.slice(innerStart, closeAt);
        if (!/\bthumbnail-wrapper\b/.test(inner)) continue;
        const lineStart = html.lastIndexOf('\n', match.index) + 1;
        const wrapperIndent = (html.slice(lineStart, match.index).match(/^[ \t]*/) || [''])[0];
        const itemIndent = inner.match(/\n([ \t]*)<div\b[^>]*\bthumbnail-wrapper\b/i);
        return {
            innerStart,
            innerEnd: closeAt,
            inner,
            wrapperIndent,
            indent: itemIndent ? itemIndent[1] : `${wrapperIndent}  `,
        };
    }
    return null;
}

function closingDiv(html, openIndex) {
    const tagRe = /<\/?div\b[^>]*>/gi;
    tagRe.lastIndex = openIndex;
    let depth = 0;
    let match;
    while ((match = tagRe.exec(html))) {
        if (match[0].startsWith('</')) {
            depth -= 1;
            if (depth === 0) return match.index;
        } else depth += 1;
    }
    return -1;
}

function insideStyleOrScript(html, index) {
    const head = html.slice(0, index).toLowerCase();
    const style = head.lastIndexOf('<style');
    const styleEnd = head.lastIndexOf('</style');
    if (style > styleEnd) return true;
    const script = head.lastIndexOf('<script');
    const scriptEnd = head.lastIndexOf('</script');
    return script > scriptEnd;
}

function classList(tag) {
    const match = /\bclass\s*=\s*("([^"]*)"|'([^']*)')/i.exec(tag);
    return (match?.[2] || match?.[3] || '').split(/\s+/).filter(Boolean);
}

function firstTag(html, name) {
    const match = new RegExp(`<${name}\\b[^>]*>`, 'i').exec(html);
    return match ? match[0] : '';
}

function attrValue(tag, name) {
    const match = new RegExp(`\\b${name}\\s*=\\s*("([^"]*)"|'([^']*)')`, 'i').exec(tag || '');
    if (!match) return '';
    return decodeEntities(match[2] ?? match[3] ?? '');
}

function decodeEntities(text) {
    return String(text || '')
        .replace(/&quot;/gi, '"')
        .replace(/&#34;/g, '"')
        .replace(/&apos;/gi, "'")
        .replace(/&#39;/g, "'")
        .replace(/&lt;/gi, '<')
        .replace(/&gt;/gi, '>')
        .replace(/&amp;/gi, '&');
}

function escapeAttr(value) {
    return String(value ?? '')
        .replace(/&/g, '&amp;')
        .replace(/"/g, '&quot;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;');
}

function wrapperHtml(image, fallbackLightbox) {
    if (image?.originalHtml && String(image.caption ?? '') === String(image.originalCaption ?? '')) {
        return String(image.originalHtml).trim();
    }
    const name = image?.name || 'photo.jpg';
    const href = image?.href || `slides/${name}`;
    const src = image?.thumbSrc || `thumbs/${name}`;
    const caption = escapeAttr(image?.caption || '');
    const lightbox = escapeAttr(image?.lightbox || fallbackLightbox || 'gallery');
    return `<div class="thumbnail-wrapper"><a href="${escapeAttr(href)}" data-lightbox="${lightbox}" data-title="${caption}"><img src="${escapeAttr(src)}" class="thumbnail" alt="${caption}" title="${caption}" loading="lazy" decoding="async"></a></div>`;
}

function publicAsset(section, slug, rel) {
    const text = String(rel || '').trim();
    if (!text) return '';
    if (/^(https?:|data:|\/)/i.test(text)) return text;
    const clean = text.replace(/^\.\//, '');
    if (section && slug) return `/${section}/${slug}/${clean}`.replace(/\/{2,}/g, '/');
    return `/${clean}`;
}

function fileKey(value) {
    return baseName(value).toLowerCase();
}

export function mountGallery(parent, sheetParent, ctx) {
    const root = el('div', {
        class: 'view view-gallery gallery-captioner',
        id: 'view-gallery',
        role: 'tabpanel',
        'aria-labelledby': 'mode-gallery',
    });
    const top = el('div', { class: 'gallery-top' });
    const tools = el('div', { class: 'gallery-tools' });
    const hero = buildHero();
    const toolbar = buildToolbar();
    const help = el('p', {
        id: 'grid-help',
        class: 'sr-only',
        text: 'Arrow keys move between photos. Shift plus arrows extends the selection. Space picks up the selection and drops it. Alt plus an arrow moves the selection immediately. Enter edits the caption.',
    });
    const wrap = el('div', { class: 'sheet-wrap' });
    const sheet = el('ul', {
        class: 'sheet',
        role: 'listbox',
        'aria-multiselectable': 'true',
        'aria-label': 'Gallery photos',
        'aria-describedby': 'grid-help',
        'data-density': ctx.density(),
    });
    const indicator = el('div', { class: 'drop-indicator', hidden: true });
    wrap.append(sheet, indicator);
    const unlistedRoot = el('div', { class: 'gallery-unlisted' });
    const inspector = buildInspector();
    tools.append(hero.root, inspector.root);
    top.append(tools, unlistedRoot);
    root.append(top);
    parent.append(root);
    const sheetRoot = el('div', {
        class: 'view view-gallery gallery-sheet-view',
        id: 'view-gallery-sheet',
        role: 'region',
        'aria-label': 'Gallery contact sheet',
    });
    sheetRoot.append(toolbar.root, help, wrap);
    (sheetParent || parent).append(sheetRoot);

    const ghost = el('div', { class: 'drag-ghost', hidden: true });
    document.body.append(ghost);
    const lightbox = buildLightbox();

    let images = [];
    let unlisted = [];
    let sourceKind = 'data';
    let selected = new Set();
    let anchor = null;
    let focusItem = null;
    let inspectorItem = null;
    let grabbed = false;
    let grabbedMoved = false;
    let drag = null;
    let suppressClick = false;
    let lightboxIndex = 0;

    hero.edit.addEventListener('click', () => focusCaption(0));
    hero.frame.addEventListener('click', () => { if (images[0]) openLightbox(0); });
    toolbar.undo.addEventListener('click', () => ctx.onUndo?.());
    toolbar.redo.addEventListener('click', () => ctx.onRedo?.());
    toolbar.history.addEventListener('click', () => ctx.onOpenHistory?.());
    toolbar.front.addEventListener('click', () => moveTo(0));
    toolbar.end.addEventListener('click', () => moveTo(images.length));
    toolbar.clear.addEventListener('click', clearSelection);
    toolbar.unlisted.addEventListener('click', () => {
        document.getElementById('unlisted-tray')?.scrollIntoView({ block: 'nearest' });
    });
    for (const button of toolbar.densityButtons) {
        button.addEventListener('click', () => {
            setDensity(button.dataset.density);
            ctx.onDensity(button.dataset.density);
        });
    }
    inspector.promote.addEventListener('click', () => {
        if (!inspectorItem) return;
        selected = new Set([inspectorItem]);
        anchor = inspectorItem;
        focusItem = inspectorItem;
        applyOrder(relocate(images, selected, 0));
    });
    inspector.earlier.addEventListener('click', () => nudge(-1));
    inspector.later.addEventListener('click', () => nudge(1));
    inspector.end.addEventListener('click', () => moveTo(images.length));
    inspector.previewBtn.addEventListener('click', () => {
        const index = images.indexOf(inspectorItem);
        if (index >= 0) openLightbox(index);
    });
    inspector.textarea.addEventListener('input', () => {
        if (!inspectorItem) return;
        inspectorItem.caption = inspector.textarea.value;
        paintTiles();
        syncSelectionUi();
        ctx.onGalleryEdit({
            kind: 'gallery',
            label: `Reordered / edited gallery slides (Caption: ${inspectorItem.name || 'slide'})`,
            coalesce: true,
            coalesceKey: `gallery-caption:${inspectorItem.name || images.indexOf(inspectorItem)}`,
        });
    });
    sheet.addEventListener('pointerdown', onPointerDown);
    sheet.addEventListener('pointermove', onPointerMove);
    sheet.addEventListener('pointerup', onPointerUp);
    sheet.addEventListener('pointercancel', onPointerCancel);
    sheet.addEventListener('click', onClick);
    sheet.addEventListener('keydown', onGridKey);
    window.addEventListener('keydown', (event) => {
        if (event.key !== 'Escape' || document.querySelector('dialog[open]')) return;
        if (drag) {
            drag.cancelled = true;
            drag = null;
            hideDragVisuals();
            suppressClick = true;
        }
        if (grabbed) {
            grabbed = false;
            grabbedMoved = false;
            paintTiles();
            ctx.announce('Drop cancelled.');
        }
    });
    lightbox.prev.addEventListener('click', () => stepLightbox(-1));
    lightbox.next.addEventListener('click', () => stepLightbox(1));
    lightbox.close.addEventListener('click', () => lightbox.dialog.close());
    lightbox.dialog.addEventListener('keydown', (event) => {
        if (event.key === 'ArrowLeft') { event.preventDefault(); stepLightbox(-1); }
        if (event.key === 'ArrowRight') { event.preventDefault(); stepLightbox(1); }
    });
    lightbox.dialog.addEventListener('click', (event) => {
        if (event.target === lightbox.dialog) lightbox.dialog.close();
    });

    function setDoc(doc) {
        sourceKind = doc?.gallerySource === 'html' ? 'html' : 'data';
        images = Array.isArray(doc?.images) ? doc.images.map((image) => ({ ...image, caption: image.caption ?? '' })) : [];
        unlisted = omitListedSlides(doc?.unlistedSlides, images, doc?.content || '');
        if (doc) {
            doc.images = images;
            doc.unlistedSlides = unlisted;
        }
        selected = new Set(images[0] ? [images[0]] : []);
        anchor = images[0] || null;
        focusItem = images[0] || null;
        inspectorItem = images[0] || null;
        grabbed = false;
        grabbedMoved = false;
        renderHero();
        renderUnlisted();
        renderGrid();
        fillInspector(true);
        syncSelectionUi();
        setDensity(ctx.density());
    }

    function setActive(active) {
        root.hidden = !active;
        sheetRoot.hidden = !active;
    }

    function setDensity(value) {
        const density = ['compact', 'standard', 'large'].includes(value) ? value : 'standard';
        sheet.dataset.density = density;
        for (const button of toolbar.densityButtons) {
            button.setAttribute('aria-pressed', button.dataset.density === density ? 'true' : 'false');
        }
    }

    function blocked() {
        if (sourceKind === 'html') return false;
        return images.some((image) => String(image.caption ?? '').includes('='));
    }

    function payloadImages() {
        return images.map((image) => ({ name: image.name, caption: image.caption ?? '' }));
    }

    function renderHero() {
        const image = images[0];
        const share = ctx.getDoc()?.share || {};
        hero.shareImage.textContent = share.hasShareImage ? 'shareimage.jpg' : 'No shareimage.jpg';
        hero.shareImage.dataset.tone = share.hasShareImage ? 'ok' : 'warn';
        hero.shareImage.title = share.hasShareImage
            ? 'shareimage.jpg is on disk — preferred image for the share card'
            : 'shareimage.jpg is missing — the share card falls through to the hero slide, then folderthumb.jpg';
        hero.folderThumb.textContent = share.hasFolderThumb ? 'folderthumb.jpg' : 'No folderthumb.jpg';
        hero.folderThumb.dataset.tone = share.hasFolderThumb ? 'ok' : 'warn';
        hero.folderThumb.title = share.hasFolderThumb
            ? 'folderthumb.jpg is on disk — last fallback in the share image chain'
            : 'folderthumb.jpg is missing — last fallback in the share image chain';
        if (!image) {
            hero.frame.hidden = true;
            hero.empty.hidden = false;
            hero.caption.textContent = 'No hero photo';
            hero.caption.title = 'No hero photo yet. The first gallery photo is the #1 Hero.';
            hero.file.textContent = '';
            hero.file.title = '';
            return;
        }
        hero.empty.hidden = true;
        hero.frame.hidden = false;
        const src = image.slideUrl || image.thumbUrl || '';
        if (hero.image.dataset.src !== src) {
            hero.image.dataset.src = src;
            hero.image.src = src;
        }
        hero.image.alt = String(image.caption || '').trim() || image.name || 'Hero photo';
        hero.file.textContent = image.name || '';
        hero.file.title = image.name ? `Hero file — ${image.name}` : 'Hero file name';
        const captionText = String(image.caption || '').trim();
        hero.caption.textContent = captionText || 'No hero caption';
        hero.caption.title = captionText
            ? `Hero caption — ${captionText}. Shown on the pinned #1 Hero.`
            : 'No hero caption yet. The first photo is the #1 Hero.';
    }

    function renderGrid({ restoreFocus = false } = {}) {
        const bullets = bulletAssignments(images, nameOf());
        const frag = document.createDocumentFragment();
        if (!images.length) {
            const empty = el('li', {
                class: 'empty',
                text: unlisted.length
                    ? 'This gallery has no photos yet. Append an unlisted slide to start it.'
                    : 'This gallery has no photos yet.',
            });
            frag.append(empty);
        } else {
            images.forEach((image, index) => frag.append(buildTile(image, index, bullets)));
        }
        sheet.replaceChildren(frag);
        if (restoreFocus) sheet.querySelector(`[data-index="${images.indexOf(focusItem)}"]`)?.focus();
    }

    function buildTile(image, index, bullets) {
        const tile = el('li', {
            class: 'tile gallery-tile',
            role: 'option',
            'data-index': String(index),
        });
        const media = el('div', { class: 'tile-media' });
        const src = image.thumbUrl || image.slideUrl || '';
        if (src) {
            const img = el('img', {
                src,
                alt: image.caption || image.name || `Photo ${index + 1}`,
                loading: 'lazy',
                decoding: 'async',
            });
            img.draggable = false;
            img.addEventListener('error', () => img.replaceWith(el('span', { class: 'thumb-fallback', text: 'No preview' })));
            media.append(img);
        } else {
            media.append(el('span', { class: 'thumb-fallback', text: 'No preview' }));
        }
        media.append(el('span', { class: 'tile-top' }), el('span', { class: 'tile-bottom' }));
        const input = el('input', {
            class: 'gallery-tile-caption-input',
            type: 'text',
            placeholder: 'Add caption…',
            'aria-label': captionInputLabel(image, index),
            title: 'Caption for this photo',
            autocomplete: 'off',
            spellcheck: 'true',
        });
        input.value = image.caption || '';
        input.draggable = false;
        bindCaptionInput(input);
        tile.append(media, input);
        decorateTile(tile, image, index, bullets);
        return tile;
    }

    function bindCaptionInput(input) {
        const stopPointer = (event) => event.stopPropagation();
        input.addEventListener('pointerdown', stopPointer);
        input.addEventListener('mousedown', stopPointer);
        input.addEventListener('click', stopPointer);
        input.addEventListener('dragstart', (event) => {
            event.preventDefault();
            event.stopPropagation();
        });
        input.addEventListener('keydown', onCaptionKeyDown);
        input.addEventListener('input', onTileCaptionInput);
        input.addEventListener('focus', onCaptionFocus);
    }

    function onCaptionFocus(event) {
        const input = event.currentTarget;
        const image = images[Number(input.closest('.tile')?.dataset.index)];
        if (!image || !activateCaptionTile(image)) return;
        paintTiles();
        fillInspector(true);
        syncSelectionUi();
    }

    function onCaptionKeyDown(event) {
        if (event.key === 'Tab') {
            const inputs = [...sheet.querySelectorAll('.gallery-tile-caption-input')];
            const index = inputs.indexOf(event.currentTarget);
            const next = index + (event.shiftKey ? -1 : 1);
            if (index >= 0 && next >= 0 && next < inputs.length) {
                event.preventDefault();
                inputs[next].focus();
            }
            return;
        }
        if (captionKeyShouldStop(event)) event.stopPropagation();
    }

    function captionKeyShouldStop(event) {
        const key = event.key;
        if (key === 'ArrowLeft' || key === 'ArrowRight' || key === 'ArrowUp' || key === 'ArrowDown') return true;
        if (key === ' ' || key === 'Spacebar' || event.code === 'Space') return true;
        if (key === 'Backspace' || key === 'Delete' || key === 'Home' || key === 'End' || key === 'Enter') return true;
        return (event.ctrlKey || event.metaKey) && key.toLowerCase() === 'a';
    }

    function onTileCaptionInput(event) {
        const input = event.currentTarget;
        const image = images[Number(input.closest('.tile')?.dataset.index)];
        if (!image) return;
        const start = input.selectionStart;
        const end = input.selectionEnd;
        const direction = input.selectionDirection;
        image.caption = input.value;
        activateCaptionTile(image);
        paintTiles();
        fillInspector(false);
        syncSelectionUi();
        if (document.activeElement === input && !event.isComposing && start != null && end != null
            && (input.selectionStart !== start || input.selectionEnd !== end)) {
            const max = input.value.length;
            input.setSelectionRange(Math.min(start, max), Math.min(end, max), direction || 'none');
        }
        ctx.onGalleryEdit({
            kind: 'gallery',
            label: `Reordered / edited gallery slides (Caption: ${image.name || 'slide'})`,
            coalesce: true,
            coalesceKey: `gallery-caption:${image.name || Number(input.closest('.tile')?.dataset.index)}`,
        });
    }

    function activateCaptionTile(image) {
        if (!image) return false;
        const same = inspectorItem === image && focusItem === image && anchor === image
            && selected.size === 1 && selected.has(image) && !grabbed;
        if (same) return false;
        selected = new Set([image]);
        anchor = image;
        focusItem = image;
        inspectorItem = image;
        grabbed = false;
        grabbedMoved = false;
        return true;
    }

    function captionInvalid(image) {
        return sourceKind !== 'html' && String(image?.caption ?? '').includes('=');
    }

    function captionInputLabel(image, index) {
        const name = String(image?.name || '').trim();
        const photo = `Caption for photo #${index + 1}`;
        return name ? `${photo} ${name}` : photo;
    }

    function syncCaptionInput(input, image, index) {
        if (!input) return;
        const next = image?.caption ?? '';
        if (input !== document.activeElement && input.value !== next) input.value = next;
        const label = captionInputLabel(image, index);
        if (input.getAttribute('aria-label') !== label) input.setAttribute('aria-label', label);
        const tip = `${label}. Up to 4 captioned thumbnails are promoted to bullet points at the top of the vehicle page.`;
        if (input.getAttribute('data-tooltip') !== tip && input.title !== tip) input.title = tip;
        const invalid = captionInvalid(image);
        input.classList.toggle('is-invalid', invalid);
        input.setAttribute('aria-invalid', invalid ? 'true' : 'false');
        if (invalid) input.setAttribute('aria-errormessage', 'caption-error');
        else input.removeAttribute('aria-errormessage');
    }

    function decorateTile(tile, image, index, bullets) {
        tile.classList.toggle('is-selected', selected.has(image));
        tile.classList.toggle('is-hero', index === 0);
        tile.classList.toggle('is-grabbed', grabbed && selected.has(image));
        tile.classList.toggle('is-dragging-source', Boolean(drag?.active && selected.has(image)));
        tile.classList.toggle('has-caption-error', captionInvalid(image));
        tile.setAttribute('aria-selected', selected.has(image) ? 'true' : 'false');
        const nextTab = image === focusItem ? 0 : -1;
        if (tile.tabIndex !== nextTab) tile.tabIndex = nextTab;
        const label = tileLabel(image, index, bullets);
        if (tile.getAttribute('aria-label') !== label) tile.setAttribute('aria-label', label);
        const tileTip = `${image.name || `Photo ${index + 1}`} — Drag to reorder · Click to inspect & edit caption · Shift+Click for range · Alt+Arrows to move`;
        if (tile.getAttribute('data-tooltip') !== tileTip && tile.title !== tileTip) tile.title = tileTip;
        const mediaImg = tile.querySelector('.tile-media img');
        if (mediaImg) {
            const alt = String(image.caption || '').trim() || image.name || `Photo ${index + 1}`;
            if (mediaImg.alt !== alt) mediaImg.alt = alt;
        }
        const bullet = bullets.byImage.get(image) || 0;
        const top = tile.querySelector('.tile-top');
        const bottom = tile.querySelector('.tile-bottom');
        top.replaceChildren();
        bottom.replaceChildren();
        if (bullet) top.append(badge(`Bullet #${bullet}`, 'badge badge-bullet', `Bullet #${bullet} — captioned thumbnail promoted to a bullet point at the top of the vehicle page`));
        else if (String(image.caption ?? '').trim()) top.append(badge('Captioned', 'badge badge-caption', 'Captioned — this photo has a lightbox caption'));
        else top.append(badge('No caption', 'badge badge-empty', 'No caption yet'));
        if (index === 0) bottom.append(badge('Hero #1', 'badge badge-hero', '#1 Hero — first photo in the gallery and the page hero'));
        else {
            bottom.append(badge(`#${index + 1}`, 'badge', `Gallery position ${index + 1}`));
            bottom.append(badge(`Grid #${index}`, 'badge', `Grid slot ${index}`));
        }
        syncCaptionInput(tile.querySelector('.gallery-tile-caption-input'), image, index);
    }

    function paintTiles() {
        const bullets = bulletAssignments(images, nameOf());
        for (const tile of sheet.querySelectorAll('.tile')) {
            const index = Number(tile.dataset.index);
            const image = images[index];
            if (image) decorateTile(tile, image, index, bullets);
        }
        hero.caption.textContent = String(images[0]?.caption ?? '').trim() || (images[0] ? 'No hero caption' : 'No hero photo');
        toolbar.bullets.textContent = `${bullets.count} / 4 top bullet captions active`;
        updateRole();
    }

    function renderUnlisted() {
        unlistedRoot.replaceChildren();
        toolbar.unlisted.hidden = unlisted.length === 0;
        toolbar.unlisted.textContent = `${unlisted.length} unlisted`;
        toolbar.unlisted.title = 'Show slides on disk that are not listed in the gallery';
        if (!unlisted.length) return;
        const box = el('section', {
            class: 'unlisted',
            id: 'unlisted-tray',
            title: 'Slides on disk in slides/ that are not listed in the gallery data',
        });
        const head = el('div', { class: 'unlisted-head' });
        head.append(el('h3', { text: `Unlisted slides (${unlisted.length})` }));
        const all = el('button', {
            class: 'btn btn-primary',
            type: 'button',
            text: `Append All (+${unlisted.length})`,
            title: `Add all ${unlisted.length} orphaned slide images from slides/ into deep-data.json and image_data.txt`,
        });
        all.addEventListener('click', () => appendSlides(unlisted.slice()));
        head.append(all);
        const row = el('div', { class: 'unlisted-row' });
        for (const slide of unlisted) {
            const card = el('article', { class: 'unlisted-card' });
            const src = slide.thumbUrl || slide.slideUrl || '';
            if (src) {
                const image = el('img', { src, alt: '', loading: 'lazy', decoding: 'async' });
                image.addEventListener('error', () => image.replaceWith(el('span', { class: 'thumb-fallback', text: 'No preview' })));
                card.append(image);
            } else {
                card.append(el('span', { class: 'thumb-fallback', text: 'No preview' }));
            }
            card.append(el('p', { text: slide.name || 'Untitled' }));
            const dims = formatDimensions(slide.width, slide.height);
            if (dims) card.append(el('p', { text: dims }));
            const append = el('button', {
                class: 'btn',
                type: 'button',
                text: 'Append',
                title: `Add ${slide.name || 'this slide'} from slides/ into deep-data.json and image_data.txt`,
            });
            append.addEventListener('click', () => appendSlides([slide]));
            card.append(append);
            row.append(card);
        }
        box.append(head, row);
        unlistedRoot.append(box);
    }

    function appendSlides(slides) {
        if (!slides.length) return;
        const doc = ctx.getDoc();
        const lightbox = images.find((image) => image.lightbox)?.lightbox || 'gallery';
        const next = appendUnlisted(images, unlisted, slides, lightbox, doc?.content || '');
        if (!next.added.length) return;
        images = next.images;
        unlisted = next.unlisted;
        const added = next.added;
        if (doc) {
            doc.unlistedSlides = unlisted;
            doc.images = images;
        }
        selected = new Set(added);
        anchor = added[0];
        focusItem = added[0];
        inspectorItem = added[0];
        renderHero();
        renderUnlisted();
        renderGrid({ restoreFocus: true });
        fillInspector(true);
        syncSelectionUi();
        ctx.onGalleryEdit({
            kind: 'gallery',
            label: `Reordered / edited gallery slides (Appended ${added.length} unlisted)`,
            coalesce: false,
        });
        ctx.onStructureChange();
    }

    function fillInspector(resetValue) {
        const index = images.indexOf(inspectorItem);
        const empty = index < 0;
        inspector.empty.hidden = !empty;
        inspector.previewBtn.hidden = empty;
        inspector.body.hidden = empty;
        if (empty) return;
        const image = inspectorItem;
        const src = image.slideUrl || image.thumbUrl || '';
        if (inspector.image.dataset.src !== src) {
            inspector.image.dataset.src = src;
            inspector.image.src = src;
        }
        inspector.image.alt = String(image.caption || '').trim() || image.name || 'Selected photo';
        inspector.file.textContent = image.name || 'Untitled';
        inspector.dims.textContent = formatDimensions(image.width, image.height) || 'Unknown';
        inspector.size.textContent = formatBytes(image.fileSize) || 'Unknown';
        inspector.position.textContent = `#${index + 1} of ${images.length}`;
        inspector.file.title = `File — ${inspector.file.textContent}`;
        inspector.dims.title = `Dimensions — ${inspector.dims.textContent}`;
        inspector.size.title = `File size — ${inspector.size.textContent}`;
        inspector.position.title = `Gallery position — ${inspector.position.textContent}`;
        if (resetValue || document.activeElement !== inspector.textarea) {
            inspector.textarea.value = image.caption ?? '';
        }
        updateRole();
        updateActionStates();
    }

    function updateRole() {
        const index = images.indexOf(inspectorItem);
        if (index < 0) return;
        const role = captionRole(inspectorItem, index, bulletAssignments(images, nameOf()), nameOf());
        inspector.role.textContent = role.text;
        inspector.role.title = role.text;
        inspector.role.dataset.tone = role.tone;
        const invalid = captionInvalid(inspectorItem);
        inspector.error.hidden = !invalid;
        inspector.error.textContent = invalid ? EQUALS_ERROR : '';
        inspector.error.title = invalid ? EQUALS_ERROR : '';
    }

    function syncSelectionUi() {
        toolbar.count.textContent = `${selected.size} selected`;
        toolbar.count.title = selected.size === 1
            ? '1 photo selected for a batch move'
            : `${selected.size} photos selected for a batch move`;
        const bullets = bulletAssignments(images, nameOf());
        toolbar.bullets.textContent = `${bullets.count} / 4 top bullet captions active`;
        toolbar.bullets.title = 'Up to 4 captioned thumbnails are promoted to bullet points at the top of the vehicle page';
        inspector.note.textContent = selected.size > 1
            ? `${selected.size} photos selected. Caption edits apply to this photo only.`
            : selected.size === 1 ? '1 selected' : 'No photo selected';
        inspector.note.title = selected.size > 1
            ? 'Range selection. Caption edits apply to the inspected photo only.'
            : selected.size === 1
                ? 'One photo is selected in the gallery'
                : 'No photo is selected';
        updateActionStates();
    }

    function updateActionStates() {
        const indexes = images.map((image, index) => selected.has(image) ? index : -1).filter((index) => index >= 0);
        const has = indexes.length > 0;
        toolbar.front.disabled = !has;
        toolbar.end.disabled = !has;
        toolbar.clear.disabled = !has;
        inspector.promote.disabled = !inspectorItem || images[0] === inspectorItem;
        inspector.earlier.disabled = !has || relocate(images, selected, indexes[0] - 1) === images;
        inspector.later.disabled = !has || relocate(images, selected, indexes[0] + 1) === images;
        inspector.end.disabled = !has || relocate(images, selected, images.length) === images;
        toolbar.front.disabled = !has || relocate(images, selected, 0) === images;
        toolbar.end.disabled = !has || relocate(images, selected, images.length) === images;
    }

    function moveTo(start) {
        ensureSelection();
        applyOrder(relocate(images, selected, start));
    }

    function nudge(delta) {
        ensureSelection();
        const start = images.findIndex((image) => selected.has(image));
        if (start < 0) return;
        applyOrder(relocate(images, selected, start + delta));
    }

    function ensureSelection() {
        if (selected.size) return;
        const image = focusItem || inspectorItem || images[0];
        if (!image) return;
        selected = new Set([image]);
        anchor = image;
        inspectorItem = image;
        focusItem = image;
    }

    function clearSelection() {
        selected = new Set();
        anchor = null;
        inspectorItem = null;
        grabbed = false;
        focusItem = images[0] || null;
        paintTiles();
        fillInspector(true);
        syncSelectionUi();
    }

    function focusCaption(index) {
        const image = images[index];
        if (!image) return;
        selected = new Set([image]);
        anchor = image;
        focusItem = image;
        inspectorItem = image;
        paintTiles();
        fillInspector(true);
        syncSelectionUi();
        inspector.textarea.focus();
    }

    function applyOrder(next, { save = true, quiet = false } = {}) {
        if (next === images) {
            if (!quiet) ctx.announce('The selection is already at that position.');
            return false;
        }
        images = next;
        const doc = ctx.getDoc();
        if (doc) doc.images = images;
        if (!images.includes(focusItem)) focusItem = [...selected][0] || images[0] || null;
        renderHero();
        renderGrid({ restoreFocus: true });
        fillInspector(false);
        syncSelectionUi();
        ctx.onStructureChange();
        if (save) {
            ctx.onGalleryEdit({
                kind: 'gallery',
                label: 'Reordered / edited gallery slides',
                coalesce: false,
            });
        }
        const position = images.indexOf(inspectorItem);
        if (!quiet && position >= 0) ctx.announce(`Moved to position ${position + 1} of ${images.length}.`);
        return true;
    }

    function onPointerDown(event) {
        if (event.target.closest?.('.gallery-tile-caption-input')) return;
        const tile = event.target.closest?.('.tile');
        if (!tile || event.button !== 0 || event.shiftKey || event.metaKey || event.ctrlKey) return;
        const image = images[Number(tile.dataset.index)];
        if (!image) return;
        drag = {
            pointerId: event.pointerId,
            startX: event.clientX,
            startY: event.clientY,
            img: image,
            active: false,
            insertAt: images.indexOf(image),
            cancelled: false,
        };
        sheet.setPointerCapture(event.pointerId);
    }

    function onPointerMove(event) {
        if (!drag || event.pointerId !== drag.pointerId || drag.cancelled) return;
        const dx = event.clientX - drag.startX;
        const dy = event.clientY - drag.startY;
        if (!drag.active) {
            if (dx * dx + dy * dy < 16) return;
            drag.active = true;
            if (!selected.has(drag.img)) {
                selected = new Set([drag.img]);
                anchor = drag.img;
                inspectorItem = drag.img;
                focusItem = drag.img;
            }
            document.body.classList.add('is-dragging');
            showGhost(event);
            paintTiles();
        }
        moveGhost(event);
        drag.insertAt = insertionIndex(event.clientX, event.clientY);
        placeIndicator(drag.insertAt);
    }

    function onPointerUp(event) {
        if (!drag || event.pointerId !== drag.pointerId) return;
        const info = drag;
        drag = null;
        hideDragVisuals();
        if (info.cancelled || !info.active) return;
        suppressClick = true;
        const start = finalStartForInsert(images, selected, info.insertAt);
        applyOrder(relocate(images, selected, start), { quiet: true });
        window.setTimeout(() => { suppressClick = false; }, 0);
    }

    function onPointerCancel(event) {
        if (!drag || event.pointerId !== drag.pointerId) return;
        drag = null;
        hideDragVisuals();
        suppressClick = true;
    }

    function onClick(event) {
        if (event.target.closest?.('.gallery-tile-caption-input')) return;
        const tile = event.target.closest?.('.tile');
        if (!tile) return;
        if (suppressClick) {
            suppressClick = false;
            return;
        }
        const image = images[Number(tile.dataset.index)];
        if (!image) return;
        if (event.shiftKey && anchor) {
            const start = images.indexOf(anchor);
            const end = images.indexOf(image);
            const [lo, hi] = start < end ? [start, end] : [end, start];
            selected = new Set(images.slice(lo, hi + 1));
            inspectorItem = image;
        } else if (event.metaKey || event.ctrlKey) {
            if (selected.has(image)) selected.delete(image);
            else selected.add(image);
            anchor = image;
            inspectorItem = selected.has(image) ? image : ([...selected].pop() || null);
        } else {
            selected = new Set([image]);
            anchor = image;
            inspectorItem = image;
        }
        focusItem = image;
        grabbed = false;
        paintTiles();
        fillInspector(true);
        syncSelectionUi();
        tile.focus();
    }

    function onGridKey(event) {
        if (event.target.closest?.('.gallery-tile-caption-input')) return;
        if (event.key === ' ' || event.code === 'Space') {
            event.preventDefault();
            const image = focusItem || inspectorItem;
            if (!image) return;
            if (!grabbed) {
                if (!selected.has(image)) {
                    selected = new Set([image]);
                    anchor = image;
                    inspectorItem = image;
                }
                grabbed = true;
                grabbedMoved = false;
                paintTiles();
                ctx.announce(`Picked up ${selected.size} photo${selected.size === 1 ? '' : 's'}. Arrow keys move. Space drops.`);
                return;
            }
            grabbed = false;
            paintTiles();
            if (grabbedMoved) {
                ctx.onGalleryEdit({
                    kind: 'gallery',
                    label: 'Reordered / edited gallery slides',
                    coalesce: false,
                });
            }
            grabbedMoved = false;
            ctx.announce('Dropped.');
            return;
        }
        if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'a') {
            event.preventDefault();
            selected = new Set(images);
            anchor = images[0] || null;
            inspectorItem = images[images.length - 1] || inspectorItem;
            paintTiles();
            fillInspector(true);
            syncSelectionUi();
            return;
        }
        if (event.key === 'Enter') {
            event.preventDefault();
            if (focusItem) {
                inspectorItem = focusItem;
                fillInspector(true);
            }
            inspector.textarea.focus();
            return;
        }
        const arrows = ['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Home', 'End'];
        if (!arrows.includes(event.key)) return;
        event.preventDefault();
        const cols = columnCount();
        let delta = event.key === 'ArrowLeft' ? -1 : event.key === 'ArrowRight' ? 1 : event.key === 'ArrowUp' ? -cols : cols;
        if (event.key === 'Home') delta = -images.length;
        if (event.key === 'End') delta = images.length;
        if (event.altKey || grabbed) {
            ensureSelection();
            const start = images.findIndex((image) => selected.has(image));
            if (start < 0) return;
            const moved = applyOrder(relocate(images, selected, start + delta), { save: event.altKey && !grabbed });
            if (grabbed && moved) grabbedMoved = true;
            return;
        }
        const current = Math.max(0, images.indexOf(focusItem));
        let nextIndex = event.key === 'Home' ? 0 : event.key === 'End' ? images.length - 1 : current + delta;
        nextIndex = Math.max(0, Math.min(images.length - 1, nextIndex));
        const image = images[nextIndex];
        if (!image) return;
        focusItem = image;
        if (event.shiftKey && anchor) {
            const start = images.indexOf(anchor);
            const [lo, hi] = start < nextIndex ? [start, nextIndex] : [nextIndex, start];
            selected = new Set(images.slice(lo, hi + 1));
        } else {
            selected = new Set([image]);
            anchor = image;
        }
        inspectorItem = image;
        paintTiles();
        fillInspector(true);
        syncSelectionUi();
        sheet.querySelector(`[data-index="${nextIndex}"]`)?.focus();
    }

    function insertionIndex(clientX, clientY) {
        const tiles = [...sheet.querySelectorAll('.tile')];
        if (!tiles.length) return 0;
        let best = null;
        for (const tile of tiles) {
            const rect = tile.getBoundingClientRect();
            const dx = clientX - (rect.left + rect.width / 2);
            const dy = clientY - (rect.top + rect.height / 2);
            const dist = dx * dx + dy * dy;
            if (!best || dist < best.dist) best = { dist, tile, rect };
        }
        const index = Number(best.tile.dataset.index);
        return clientX >= best.rect.left + best.rect.width / 2 ? index + 1 : index;
    }

    function placeIndicator(insertBefore) {
        const tiles = [...sheet.querySelectorAll('.tile')];
        if (!tiles.length || insertBefore == null) {
            indicator.hidden = true;
            return;
        }
        const wrapRect = wrap.getBoundingClientRect();
        const tile = tiles[Math.min(insertBefore, tiles.length - 1)];
        const rect = tile.getBoundingClientRect();
        const after = insertBefore >= tiles.length;
        indicator.hidden = false;
        indicator.style.top = `${rect.top - wrapRect.top}px`;
        indicator.style.left = `${(after ? rect.right : rect.left) - wrapRect.left - 2}px`;
        indicator.style.height = `${rect.height}px`;
    }

    function showGhost(event) {
        ghost.hidden = false;
        ghost.replaceChildren();
        const src = drag.img.thumbUrl || drag.img.slideUrl;
        if (src) ghost.append(el('img', { src, alt: '' }));
        ghost.append(el('span', { text: selected.size > 1 ? `${selected.size} photos` : (drag.img.name || 'Photo') }));
        moveGhost(event);
    }

    function moveGhost(event) {
        ghost.style.left = `${event.clientX}px`;
        ghost.style.top = `${event.clientY}px`;
    }

    function hideDragVisuals() {
        document.body.classList.remove('is-dragging');
        ghost.hidden = true;
        indicator.hidden = true;
        paintTiles();
    }

    function openLightbox(index) {
        lightboxIndex = index;
        showLightbox();
        if (!lightbox.dialog.open) lightbox.dialog.showModal();
        lightbox.close.focus();
    }

    function stepLightbox(delta) {
        if (!images.length) return;
        lightboxIndex = Math.max(0, Math.min(images.length - 1, lightboxIndex + delta));
        showLightbox();
    }

    function showLightbox() {
        const image = images[lightboxIndex];
        if (!image) return;
        const full = image.hasSlide === false ? '' : (image.slideUrl || '');
        lightbox.image.src = full || image.thumbUrl || '';
        lightbox.image.alt = String(image.caption || '').trim() || image.name || 'Slide';
        lightbox.caption.textContent = `${image.name || 'Photo'} · ${formatDimensions(image.width, image.height) || 'size unknown'} · #${lightboxIndex + 1} of ${images.length}`;
        lightbox.caption.title = lightbox.caption.textContent;
        lightbox.note.hidden = image.hasSlide !== false;
        lightbox.prev.disabled = lightboxIndex <= 0;
        lightbox.next.disabled = lightboxIndex >= images.length - 1;
    }

    function columnCount() {
        const value = getComputedStyle(sheet).gridTemplateColumns;
        return Math.max(1, value.split(' ').filter((part) => part && part !== 'none').length);
    }

    function tileLabel(image, index, bullets) {
        const bullet = bullets.byImage.get(image);
        const bits = [`Photo ${index + 1} of ${images.length}`];
        bits.push(index === 0 ? 'hero' : `grid ${index}`);
        if (bullet) bits.push(`bullet ${bullet}`);
        else if (String(image.caption ?? '').trim()) bits.push('captioned');
        if (image.name) bits.push(image.name);
        if (selected.has(image)) bits.push('selected');
        return bits.join(', ');
    }

    function nameOf() { return ctx.vehicleName() || ''; }

    function selectByName(name) {
        const file = baseName(name).toLowerCase();
        if (!file) return false;
        const image = images.find((item) => [item?.name, item?.slideUrl, item?.thumbUrl]
            .some((value) => baseName(value).toLowerCase() === file));
        if (!image) return false;
        selected = new Set([image]);
        anchor = image;
        focusItem = image;
        inspectorItem = image;
        grabbed = false;
        grabbedMoved = false;
        paintTiles();
        fillInspector(true);
        syncSelectionUi();
        const index = images.indexOf(image);
        const tile = sheet.querySelector(`[data-index="${index}"]`);
        if (tile) {
            tile.focus({ preventScroll: true });
            tile.scrollIntoView({ block: 'nearest' });
        }
        return true;
    }

    function setCaptions(names, caption) {
        const wanted = new Set((Array.isArray(names) ? names : []).map((name) => baseName(name).toLowerCase()).filter(Boolean));
        if (!wanted.size) return false;
        const next = String(caption ?? '');
        let changed = false;
        for (const image of images) {
            const keys = [image?.name, image?.slideUrl, image?.thumbUrl].map((value) => baseName(value).toLowerCase());
            if (!keys.some((key) => wanted.has(key))) continue;
            if (String(image.caption ?? '') === next) continue;
            image.caption = next;
            changed = true;
        }
        if (!changed) return false;
        renderHero();
        paintTiles();
        syncSelectionUi();
        if (inspectorItem && document.activeElement !== inspector.textarea) inspector.textarea.value = inspectorItem.caption ?? '';
        return true;
    }

    return {
        setDoc,
        setActive,
        setDensity,
        blocked,
        payloadImages,
        imageCount: () => images.length,
        unlistedCount: () => unlisted.length,
        appendAllUnlisted() {
            if (!unlisted.length) return 0;
            const count = unlisted.length;
            appendSlides(unlisted.slice());
            return count;
        },
        selectByName,
        setCaptions,
        repaint() {
            renderHero();
            paintTiles();
            syncSelectionUi();
        },
        syncUndoUi({ canUndo = false, canRedo = false, count = 0, hasHistory = false } = {}) {
            toolbar.undo.disabled = !canUndo;
            toolbar.redo.disabled = !canRedo;
            toolbar.history.disabled = !hasHistory;
            toolbar.historyBadge.hidden = !(count > 0);
            toolbar.historyBadge.textContent = String(count);
        },
    };

    function buildHero() {
        const frame = el('button', { class: 'hero-frame', type: 'button', 'aria-label': 'Open the hero slide', title: 'Open the #1 Hero slide' });
        const image = el('img', { alt: '' });
        frame.append(image);
        const empty = el('p', { class: 'empty', text: 'No photos in this gallery yet.', hidden: true });
        const caption = el('p', { class: 'hero-caption', text: 'No hero caption' });
        const file = el('p', { class: 'hero-file' });
        const edit = el('button', { class: 'btn', type: 'button', text: 'Edit Hero Caption', title: 'Edit the hero caption in the photo inspector' });
        const shareImage = el('p', { class: 'pill', text: 'shareimage.jpg', title: 'shareimage.jpg status in the share-card fallback chain' });
        const folderThumb = el('p', { class: 'pill', text: 'folderthumb.jpg', title: 'folderthumb.jpg status in the share-card fallback chain' });
        const copy = el('div', { class: 'hero-copy' });
        copy.append(el('h2', { text: 'Pinned hero' }), caption, file, edit, el('div', { class: 'hero-pills' }, [shareImage, folderThumb]));
        const banner = el('section', {
            class: 'hero-banner',
            'aria-label': 'Hero photo',
            title: 'Pinned hero — photo #1 is the page hero and the first gallery slot',
        });
        banner.append(el('div', {}, [frame, empty]), copy);
        return { root: banner, frame, image, empty, caption, file, edit, shareImage, folderThumb };
    }

    function buildToolbar() {
        const bar = el('div', { class: 'toolbar' });
        const undoCluster = el('div', { class: 'undo-cluster' });
        const undo = el('button', {
            class: 'btn btn-undo-step',
            type: 'button',
            text: '↶',
            disabled: true,
            'aria-label': 'Undo',
            title: 'Undo last change (Ctrl+Z)',
        });
        const redo = el('button', {
            class: 'btn btn-redo-step',
            type: 'button',
            text: '↷',
            disabled: true,
            'aria-label': 'Redo',
            title: 'Redo next change (Ctrl+Y / Ctrl+Shift+Z)',
        });
        const history = el('button', {
            class: 'btn btn-undo-history',
            type: 'button',
            disabled: true,
            title: 'Open Undo History viewer (Ctrl+Shift+H)',
        });
        const historyBadge = el('span', { class: 'uh-count-badge', hidden: true, text: '0' });
        history.append(el('span', { text: 'History' }), historyBadge);
        undoCluster.append(undo, redo, history);
        const density = el('div', { class: 'seg', role: 'group', 'aria-label': 'Grid density' });
        const densityButtons = [
            ['compact', 'Compact 6-col'],
            ['standard', 'Standard 4-col'],
            ['large', 'Large 3-col'],
        ].map(([value, label]) => {
            const densityTips = {
                compact: 'Compact 6-column grid',
                standard: 'Standard 4-column grid',
                large: 'Large 3-column grid',
            };
            const button = el('button', {
                type: 'button',
                text: label,
                'data-density': value,
                'aria-pressed': value === 'standard' ? 'true' : 'false',
                title: densityTips[value] || label,
            });
            density.append(button);
            return button;
        });
        const count = el('span', { text: '0 selected', title: '0 photos selected for a batch move' });
        const front = el('button', { class: 'btn', type: 'button', text: 'Move to Front', title: 'Move selected photos to front' });
        const end = el('button', { class: 'btn', type: 'button', text: 'Move to End', title: 'Move selected photos to end' });
        const clear = el('button', { class: 'btn', type: 'button', text: 'Clear Selection', title: 'Clear selection' });
        const unlistedButton = el('button', { class: 'btn', type: 'button', text: '0 unlisted', hidden: true, title: 'Show slides on disk that are not listed in the gallery' });
        const bullets = el('p', {
            class: 'bullet-count',
            text: '0 / 4 top bullet captions active',
            title: 'Up to 4 captioned thumbnails are promoted to bullet points at the top of the vehicle page',
        });
        bar.append(undoCluster, density, count, front, end, clear, unlistedButton, bullets);
        return { root: bar, undo, redo, history, historyBadge, densityButtons, count, front, end, clear, unlisted: unlistedButton, bullets };
    }

    function buildInspector() {
        const section = el('section', { class: 'inspector', 'aria-label': 'Photo inspector' });
        const previewBtn = el('button', { class: 'inspector-preview', type: 'button', 'aria-label': 'Open the full-size slide', title: 'Open the full-size slide' });
        const image = el('img', { alt: '' });
        previewBtn.append(image);
        const body = el('div');
        const meta = el('dl', { class: 'meta-dl' });
        const file = metaPair(meta, 'File');
        const dims = metaPair(meta, 'Dimensions');
        const size = metaPair(meta, 'Size');
        const position = metaPair(meta, 'Position');
        const actions = el('div', { class: 'inspector-actions' });
        const promote = el('button', { class: 'btn', type: 'button', text: 'Promote to Hero (#1)', title: 'Promote to Hero (#1) — move this photo to the first slot' });
        const earlier = el('button', { class: 'btn', type: 'button', text: 'Move Earlier', title: 'Move this photo earlier in the gallery order' });
        const later = el('button', { class: 'btn', type: 'button', text: 'Move Later', title: 'Move this photo later in the gallery order' });
        const end = el('button', { class: 'btn', type: 'button', text: 'Move to End', title: 'Move this photo to the end of the gallery' });
        actions.append(promote, earlier, later, end);
        const note = el('p', { class: 'field-hint', text: 'No photo selected' });
        const label = el('label', { class: 'field', for: 'photo-caption', text: 'Caption' });
        const textarea = el('textarea', {
            id: 'photo-caption',
            rows: '4',
            'aria-describedby': 'caption-error caption-role',
            title: 'Caption for the selected photo. Up to 4 captioned thumbnails are promoted to bullet points at the top of the vehicle page.',
        });
        const error = el('p', { class: 'field-error', id: 'caption-error', role: 'alert', hidden: true });
        const role = el('p', { class: 'role-hint', id: 'caption-role' });
        label.append(textarea);
        body.append(meta, actions, note, label, error, role);
        const empty = el('p', { class: 'empty', text: 'Select a photo to edit its caption.', hidden: true });
        section.append(previewBtn, body, empty);
        return { root: section, previewBtn, image, body, file, dims, size, position, promote, earlier, later, end, note, textarea, error, role, empty };
    }

    function buildLightbox() {
        const dialog = el('dialog', { class: 'lightbox', 'aria-label': 'Full-size slide' });
        const bar = el('div', { class: 'lightbox-bar' });
        const prev = el('button', { class: 'btn', type: 'button', text: 'Previous', title: 'Show the previous slide' });
        const next = el('button', { class: 'btn', type: 'button', text: 'Next', title: 'Show the next slide' });
        const close = el('button', { class: 'btn', type: 'button', text: 'Close', title: 'Close the full-size slide' });
        bar.append(prev, next, close);
        const image = el('img', { alt: '' });
        const note = el('p', {
            class: 'field-error',
            text: 'The full-size slide is missing. Showing the thumbnail.',
            title: 'The full-size slide is missing. Showing the thumbnail.',
            hidden: true,
        });
        const caption = el('p', { class: 'lightbox-caption' });
        dialog.append(bar, image, note, caption);
        document.body.append(dialog);
        return { dialog, image, caption, note, prev, next, close };
    }
}

export function slideFileName(value) {
    let text = String(value || '').trim();
    if (!text) return '';
    text = text.split('#')[0].split('?')[0].trim();
    try { text = decodeURIComponent(text.replace(/\+/g, ' ')); } catch { /* keep the original name */ }
    text = text.trim();
    const marked = text.match(/(?:^|[/\\])(?:slides|thumbs)[/\\]([^/\\]+)$/i);
    if (marked) return marked[1].trim();
    const parts = text.split(/[/\\]/);
    return (parts[parts.length - 1] || '').trim();
}

function baseName(value) {
    return slideFileName(value);
}

export function htmlSlideKeys(html) {
    const keys = new Set();
    const text = String(html || '');
    const re = /(?:slides|thumbs)\//gi;
    let match;
    while ((match = re.exec(text))) {
        const prev = match.index > 0 ? text[match.index - 1] : '';
        if (/[A-Za-z0-9_-]/.test(prev)) continue;
        let end;
        if (prev === '"' || prev === "'") {
            end = text.indexOf(prev, match.index + match[0].length);
            if (end < 0) end = text.length;
        } else {
            const rest = text.slice(match.index + match[0].length);
            const cut = rest.search(/[\s"'<>?#)&]/);
            end = match.index + match[0].length + (cut < 0 ? rest.length : cut);
        }
        const name = slideFileName(text.slice(match.index, end));
        if (name && /\.(?:jpe?g|png|gif|webp|bmp|tiff?)$/i.test(name)) keys.add(name.toLowerCase());
    }
    return keys;
}

export function omitListedSlides(unlisted, images, html) {
    const used = htmlSlideKeys(html);
    for (const image of Array.isArray(images) ? images : []) {
        const key = fileKey(image?.name || image?.href || image?.slideUrl || image?.thumbSrc || image?.thumbUrl || '');
        if (key) used.add(key);
    }
    const seen = new Set();
    const out = [];
    for (const slide of Array.isArray(unlisted) ? unlisted : []) {
        const key = fileKey(slide?.name || slide?.href || slide?.slideUrl || slide?.thumbUrl || '');
        if (!key || used.has(key) || seen.has(key)) continue;
        seen.add(key);
        out.push(slide);
    }
    return out;
}

export function appendUnlisted(images, unlisted, slides, lightbox = 'gallery', html = '') {
    const list = Array.isArray(slides) ? slides.filter(Boolean) : [];
    const added = list.map((slide) => fromUnlisted(slide, lightbox));
    const nextImages = (Array.isArray(images) ? images : []).concat(added);
    const drop = new Set(list);
    const nextUnlisted = omitListedSlides(
        (Array.isArray(unlisted) ? unlisted : []).filter((slide) => !drop.has(slide)),
        nextImages,
        html,
    );
    return { images: nextImages, unlisted: nextUnlisted, added };
}

function fromUnlisted(slide, lightbox) {
    const name = slideFileName(slide?.name || slide?.slideUrl || slide?.thumbUrl);
    return {
        name,
        caption: '',
        href: name ? `slides/${name}` : '',
        thumbSrc: name ? `thumbs/${name}` : '',
        lightbox: lightbox || 'gallery',
        slideUrl: slide.slideUrl || '',
        thumbUrl: slide.thumbUrl || slide.slideUrl || '',
        width: slide.width || 0,
        height: slide.height || 0,
        thumbWidth: slide.thumbWidth || 0,
        thumbHeight: slide.thumbHeight || 0,
        fileSize: slide.fileSize || 0,
        hasSlide: slide.hasSlide !== false,
        hasThumb: Boolean(slide.hasThumb || slide.thumbUrl),
        isBullet: false,
    };
}

function metaPair(meta, term) {
    const dd = el('dd', { text: '—' });
    meta.append(el('dt', { text: term }), dd);
    return dd;
}

function badge(text, className, title) {
    return el('span', { class: className, text, title });
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
