import { getStudioConfig } from './config.js';

export function mountShare(parent, ctx) {
    const root = el('div', {
        class: 'view view-share',
        id: 'view-share',
        role: 'tabpanel',
        'aria-labelledby': 'mode-share',
        hidden: true,
    });
    parent.append(root);
    let doc = null;

    function setDoc(next) {
        doc = next;
        render();
    }

    function refresh() {
        doc = ctx.getDoc() || doc;
        render();
    }

    function setActive(active) {
        root.hidden = !active;
        if (active) refresh();
    }

    function render() {
        root.replaceChildren();
        const layout = el('div', { class: 'share-layout' });
        layout.append(el('h2', { text: 'Share card' }));
        if (!doc) {
            layout.append(el('p', { class: 'empty', text: 'Open a folder to inspect its share card.' }));
            root.append(layout);
            return;
        }
        const share = doc.share || {};
        const hero = Array.isArray(doc.images) ? doc.images[0] : null;
        const steps = [
            { label: '1. shareimage.jpg', ok: Boolean(share.hasShareImage), url: share.shareImageUrl || '' },
            {
                label: '2. Hero Slide #1',
                ok: Boolean(hero && hero.hasSlide !== false && hero.slideUrl),
                url: hero?.slideUrl || '',
            },
            { label: '3. folderthumb.jpg', ok: Boolean(share.hasFolderThumb), url: share.folderThumbUrl || '' },
        ];
        const effective = share.effectiveShareImage || steps.find((step) => step.ok)?.url || '';
        const card = el('article', {
            class: effective ? 'share-card' : 'share-card is-text-only',
            title: 'Share card preview — how this page looks when the link is shared',
        });
        if (effective) {
            const media = el('div', { class: 'share-card-media' });
            const image = el('img', { src: effective, alt: share.title || 'Share image' });
            image.addEventListener('error', () => {
                media.replaceChildren(el('p', { class: 'share-card-text', text: 'The share image file is missing.' }));
            });
            media.append(image);
            card.append(media);
        }
        const body = el('div', { class: 'share-card-body' });
        body.append(el('p', { class: 'share-card-title', text: share.title || 'No share title is set.' }));
        body.append(el('p', {
            class: 'share-card-text',
            text: share.description || (effective ? 'No description is set.' : 'The share image file is missing.'),
        }));
        body.append(el('p', { class: 'share-card-host', text: getStudioConfig().siteHost || '' }));
        card.append(body);

        const chain = el('ol', { class: 'chain' });
        for (const step of steps) {
            const used = step.ok && sameResource(step.url, effective);
            const item = el('li', {
                title: `${step.label} — ${step.ok ? 'on disk' : 'missing'}${used ? ', currently used' : ''}. Fallback order: shareimage.jpg, then the hero slide, then folderthumb.jpg.`,
            });
            item.append(el('span', {
                class: step.ok ? 'dot is-ok' : 'dot',
                title: step.ok ? `${step.label} is on disk` : `${step.label} is missing`,
            }));
            const copy = el('div');
            copy.append(el('div', { class: 'chain-label', text: step.label }));
            copy.append(el('div', { class: 'chain-url', text: step.url || 'Not on disk' }));
            item.append(copy);
            item.append(el('span', { class: 'used-tag', text: used ? 'Used' : '' }));
            chain.append(item);
        }
        layout.append(
            el('p', { class: 'field-hint', text: 'The public card uses the first file that exists: share image, then the hero slide, then the folder thumbnail.' }),
            card,
            chain,
        );
        root.append(layout);
    }

    return { setDoc, setActive, refresh };
}

function sameResource(left, right) {
    if (!left || !right) return false;
    try {
        const a = new URL(left, window.location.origin);
        const b = new URL(right, window.location.origin);
        return decodeURIComponent(a.pathname) === decodeURIComponent(b.pathname) && a.search === b.search;
    } catch {
        return left === right;
    }
}

function el(tag, props = {}, children = []) {
    const node = document.createElement(tag);
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
