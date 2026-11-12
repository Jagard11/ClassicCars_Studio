import { formatPriceLabel } from './details.js';
import { createTreeRename } from './tree-rename.js';

export function jumpMatches(sections, query) {
    const q = String(query || '').trim().toLowerCase();
    const items = [];
    for (const section of sections || []) {
        for (const file of section.rootFiles || []) {
            if (q && !textHit(file, q)) continue;
            items.push({
                path: file.path,
                title: file.name,
                meta: section.label,
                sectionId: section.id,
            });
        }
        for (const folder of section.folders || []) {
            if (q && !textHit(folder, q)) {
                for (const file of folder.files || []) {
                    if (!textHit(file, q)) continue;
                    items.push({
                        path: file.path,
                        title: file.name,
                        meta: `${folder.name || folder.slug} · ${section.label}`,
                        sectionId: section.id,
                        folder,
                    });
                }
                continue;
            }
            if (folder.primaryFile) {
                items.push({
                    path: folder.primaryFile,
                    title: folder.name || folder.slug,
                    meta: section.label,
                    sectionId: section.id,
                    folder,
                });
            }
        }
    }
    items.sort((a, b) => String(a.title || '').localeCompare(String(b.title || ''), undefined, { numeric: true, sensitivity: 'base' }));
    return items.slice(0, 40);
}

export function createTree(container, handlers) {
    const renameUi = createTreeRename(container, {
        onOpen: (path, detail) => handlers.onOpen?.(path, detail),
        onToggleRow: (row) => toggle(row),
        onRename: (payload) => handlers.onRename?.(payload),
        onDuplicate: (info) => handlers.onDuplicate?.(info),
        onCut: (info) => handlers.onCut?.(info),
        onCopy: (info) => handlers.onCopy?.(info),
        onPaste: (info) => handlers.onPaste?.(info),
        onDelete: (info) => handlers.onDelete?.(info),
        onNewPage: (info) => handlers.onNewPage?.(info),
        onNewFolder: (info) => handlers.onNewFolder?.(info),
        onUndo: () => handlers.onUndo?.(),
        onRedo: () => handlers.onRedo?.(),
        getOpsState: () => handlers.getOpsState?.(),
        onAnnounce: (msg) => handlers.onAnnounce?.(msg),
    });

    container.addEventListener('click', (event) => {
        if (event.target.closest('.tree-rename-box')) return;
        const row = event.target.closest('.tree-item');
        if (!row || !container.contains(row)) return;
        if (event.target.closest('[data-tree-action="add"]')) {
            event.preventDefault();
            event.stopPropagation();
            focusKey(row.dataset.treeKey, { scroll: false });
            handlers.onNewPage?.(renameUi.rowInfo(row));
            return;
        }
        if (event.target.closest('[data-tree-action="rename"]')) {
            event.preventDefault();
            event.stopPropagation();
            focusKey(row.dataset.treeKey, { scroll: false });
            renameUi.startRename(row);
            return;
        }
        const twist = event.target.closest('[data-twist]');
        if (twist || row.dataset.kind === 'section') {
            toggle(row);
            return;
        }
        if (row.dataset.path) handlers.onOpen(row.dataset.path, { pin: event.detail > 1 });
        else if (row.dataset.kind === 'folder') toggle(row);
    });
    container.addEventListener('dblclick', (event) => {
        if (event.target.closest('.tree-rename-box') || event.target.closest('[data-tree-action]')) return;
        const row = event.target.closest('.tree-item');
        if (!row || !container.contains(row)) return;
        if (event.target.closest('[data-twist]')) return;
        if (!row.dataset.path) return;
        event.preventDefault();
        handlers.onOpen(row.dataset.path, { pin: true });
    });
    container.addEventListener('contextmenu', (event) => {
        if (event.target.closest('.tree-rename-input')) return;
        const row = event.target.closest('.tree-item');
        if (row && !container.contains(row)) return;
        event.preventDefault();
        event.stopPropagation();
        if (row) focusKey(row.dataset.treeKey, { scroll: false });
        renameUi.openContextMenu(row || null, event.clientX, event.clientY);
    });
    container.addEventListener('keydown', (event) => {
        if (event.target.closest?.('.tree-rename-input')) return;
        const row = event.target.closest?.('.tree-item');
        if (!row || !container.contains(row)) return;
        const mod = event.ctrlKey || event.metaKey;
        const key = event.key.toLowerCase();
        const isModifiableRow = row.dataset.kind === 'file'
            || row.dataset.kind === 'folder'
            || (row.dataset.kind === 'section' && row.dataset.section !== '/');
        if (event.key === 'F2' && !mod && !event.altKey && !event.shiftKey) {
            event.preventDefault();
            event.stopPropagation();
            renameUi.startRename(row);
            return;
        }
        if (event.key === 'Delete' && !mod && !event.altKey && !event.shiftKey) {
            if (isModifiableRow) {
                event.preventDefault();
                event.stopPropagation();
                handlers.onDelete?.(renameUi.rowInfo(row));
            }
            return;
        }
        if (event.key === 'Escape' && !mod && !event.altKey && !event.shiftKey) {
            if (handlers.onClearCut?.()) {
                event.preventDefault();
                event.stopPropagation();
                return;
            }
        }
        if (mod && !event.altKey) {
            if (key === 'd' && !event.shiftKey) {
                if (isModifiableRow) {
                    event.preventDefault();
                    event.stopPropagation();
                    handlers.onDuplicate?.(renameUi.rowInfo(row));
                }
                return;
            }
            if (key === 'x' && !event.shiftKey) {
                if (isModifiableRow) {
                    event.preventDefault();
                    event.stopPropagation();
                    handlers.onCut?.(renameUi.rowInfo(row));
                }
                return;
            }
            if (key === 'c' && !event.shiftKey) {
                if (isModifiableRow) {
                    event.preventDefault();
                    event.stopPropagation();
                    handlers.onCopy?.(renameUi.rowInfo(row));
                }
                return;
            }
            if (key === 'v' && !event.shiftKey) {
                event.preventDefault();
                event.stopPropagation();
                handlers.onPaste?.(renameUi.rowInfo(row));
                return;
            }
            if (key === 'z' && !event.shiftKey) {
                event.preventDefault();
                event.stopPropagation();
                handlers.onUndo?.();
                return;
            }
            if ((key === 'y' && !event.shiftKey) || (key === 'z' && event.shiftKey)) {
                event.preventDefault();
                event.stopPropagation();
                handlers.onRedo?.();
                return;
            }
        }
        if ((event.key === 'Enter' || event.key === ' ') && !event.target.closest?.('[data-tree-action]')) {
            event.preventDefault();
            if (row.dataset.kind === 'section') {
                toggle(row);
                return;
            }
            if (row.dataset.path) handlers.onOpen(row.dataset.path, { pin: event.key === 'Enter' });
            else if (row.dataset.kind === 'folder') toggle(row);
            return;
        }
        if (event.key === 'ArrowDown' || event.key === 'ArrowUp' || event.key === 'Home' || event.key === 'End') {
            event.preventDefault();
            moveFocus(row, event.key);
            return;
        }
        if (event.key === 'ArrowRight' && row.dataset.kind !== 'file' && row.getAttribute('aria-expanded') !== 'true') {
            event.preventDefault();
            toggle(row);
            return;
        }
        if (event.key === 'ArrowLeft' && row.dataset.kind !== 'file') {
            event.preventDefault();
            if (row.getAttribute('aria-expanded') === 'true') toggle(row);
            else focusKey(row.dataset.parent);
        }
    });

    function toggle(row) {
        if (row.dataset.kind === 'section') handlers.onToggleSection(row.dataset.id);
        else if (row.dataset.kind === 'folder') handlers.onToggleFolder(row.dataset.id);
    }

    function moveFocus(row, key) {
        const items = [...container.querySelectorAll('.tree-item')];
        const index = items.indexOf(row);
        let next = index;
        if (key === 'ArrowDown') next = Math.min(items.length - 1, index + 1);
        if (key === 'ArrowUp') next = Math.max(0, index - 1);
        if (key === 'Home') next = 0;
        if (key === 'End') next = items.length - 1;
        items.forEach((item, itemIndex) => { item.tabIndex = itemIndex === next ? 0 : -1; });
        items[next]?.focus();
    }

    function focusKey(key, { scroll = false } = {}) {
        if (!key) return false;
        const match = [...container.querySelectorAll('.tree-item')].find((item) => item.dataset.treeKey === key);
        if (!match) return false;
        for (const item of container.querySelectorAll('.tree-item')) item.tabIndex = item === match ? 0 : -1;
        if (scroll) match.scrollIntoView({ block: 'nearest' });
        match.focus({ preventScroll: !scroll });
        return true;
    }

    function startRenameForSelection() {
        const focused = container.contains(document.activeElement)
            ? document.activeElement.closest?.('.tree-item')
            : null;
        const target = focused || container.querySelector('.tree-item[aria-current="true"]');
        if (!target) return false;
        return renameUi.startRename(target);
    }

    function render(model) {
        renameUi.closeMenu();
        const focusKeyValue = model.focusTreeKey
            || (container.contains(document.activeElement) ? document.activeElement.closest?.('.tree-item')?.dataset.treeKey : '');
        const scroll = container.scrollTop;
        const prepared = prepareTree(model.sections || [], model.query, model.filter);
        const frag = document.createDocumentFragment();
        if (!prepared.length) {
            frag.append(paragraph(model.sections?.length ? 'No files match.' : 'No files loaded.'));
        } else {
            for (const section of prepared) frag.append(renderSection(section, model));
        }
        container.replaceChildren(frag);
        const items = [...container.querySelectorAll('.tree-item')];
        const cutKey = model.cutTreeKey || '';
        const preferred = items.find((item) => item.dataset.treeKey === focusKeyValue)
            || items.find((item) => item.getAttribute('aria-current') === 'true')
            || items[0];
        for (const item of items) {
            item.tabIndex = item === preferred ? 0 : -1;
            if (cutKey && item.dataset.treeKey === cutKey) {
                item.classList.add('is-cut');
            }
        }
        container.scrollTop = scroll;
        if (model.reveal) {
            const revealTarget = (model.focusTreeKey && items.find((item) => item.dataset.treeKey === model.focusTreeKey))
                || container.querySelector('[aria-current="true"]');
            revealTarget?.scrollIntoView({ block: 'nearest' });
        }
        if (focusKeyValue) items.find((item) => item.dataset.treeKey === focusKeyValue)?.focus({ preventScroll: true });
    }

    function renderSection(section, model) {
        const group = document.createElement('div');
        group.className = 'tree-section';
        group.setAttribute('role', 'group');
        const isRoot = section.id === '/';
        if (isRoot) {
            for (const file of section.rootFiles || []) {
                group.append(fileButton(file, {
                    depth: 0,
                    parent: '',
                    activePath: model.activePath,
                    sectionId: '/',
                }));
            }
            return group;
        }
        const open = section.forceOpen || model.expandedSections.has(section.id);
        const count = section.totalFolders ?? section.folders.length;
        const dirName = section.id;
        const childActive = (section.rootFiles || []).some((file) => file.path === model.activePath)
            || (section.folders || []).some((folder) => folder.primaryFile === model.activePath
                || (folder.files || []).some((file) => file.path === model.activePath));
        const button = rowButton({
            kind: 'section',
            id: section.id,
            section: section.id,
            name: dirName,
            folderPath: `/${section.id}`,
            treeKey: `section:${section.id}`,
            depth: 0,
            open,
            current: childActive && !open,
            title: `Click to expand or collapse ${dirName} · ${count} item${count === 1 ? '' : 's'}`,
        });
        button.append(
            twist(open, 'Expand or collapse directory'),
            icon('folder'),
            nameSpan(dirName),
            addActionButton(`New page or file in /${section.id}`),
            renameActionButton(`Rename folder ${section.id} (F2)`),
            badgeSpan([countBadge(count)]),
        );
        group.append(button);
        if (!open) return group;
        const kids = document.createElement('div');
        kids.setAttribute('role', 'group');
        if (!section.rootFiles.length && !section.folders.length) kids.append(paragraph('No pages in this section.'));
        const grouped = groupFolders(section.folders);
        for (const folder of grouped.roots) {
            kids.append(renderFolder(section, folder, model, grouped.children, 1, `section:${section.id}`));
        }
        for (const file of section.rootFiles) {
            kids.append(fileButton(file, {
                depth: 1,
                parent: `section:${section.id}`,
                activePath: model.activePath,
                sectionId: section.id,
            }));
        }
        group.append(kids);
        return group;
    }

    function renderFolder(section, folder, model, childMap, depth, parentKey) {
        const key = `${section.id}/${folder.slug}`;
        const open = folder.forceOpen || model.expandedFolders.has(key);
        const folderKey = `folder:${key}`;
        const direct = childMap.get(String(folder.slug || '')) || [];
        const childActive = (folder.files || []).some((file) => file.path === model.activePath) || folder.primaryFile === model.activePath;
        const filename = folderFilename(folder) || 'Untitled';
        const leafName = fileBase(folder.slug) || filename;
        const human = String(folder.name || '').trim();
        const subtitle = human && compareFilenames(human, filename) !== 0 ? human : '';
        const wrap = document.createElement('div');
        const button = rowButton({
            kind: 'folder',
            id: key,
            section: section.id,
            slug: folder.slug,
            name: leafName,
            folderPath: `/${section.id}/${folder.slug}`,
            treeKey: folderKey,
            path: folder.primaryFile || '',
            depth,
            parent: parentKey,
            open,
            current: childActive && !open,
            title: folderRowTitle(folder, filename, human),
        });
        button.append(
            twist(open, 'Expand or collapse folder files'),
            icon('folder'),
            nameSpan(filename, subtitle),
            addActionButton(`New page or file in ${leafName}`),
            renameActionButton(`Rename folder ${leafName} (F2)`),
            badgeSpan(folderBadges(folder)),
        );
        wrap.append(button);
        if (open) {
            const kids = document.createElement('div');
            kids.setAttribute('role', 'group');
            const files = folder.files?.length ? folder.files : [];
            if (!direct.length && !files.length) kids.append(paragraph('No files in this folder.'));
            for (const child of direct) {
                kids.append(renderFolder(section, child, model, childMap, depth + 1, folderKey));
            }
            for (const file of files) {
                kids.append(fileButton(file, {
                    depth: depth + 1,
                    parent: folderKey,
                    activePath: model.activePath,
                    folder,
                    sectionId: section.id,
                }));
            }
            wrap.append(kids);
        }
        const frag = document.createDocumentFragment();
        frag.append(wrap);
        if (!open) {
            // Collapsed folders stay in the list; their subfolders stay visible beside them.
            for (const child of direct) {
                frag.append(renderFolder(section, child, model, childMap, depth, parentKey));
            }
        }
        return frag;
    }

    function getSelectedRowInfo() {
        const focused = container.contains(document.activeElement)
            ? document.activeElement.closest?.('.tree-item')
            : null;
        const target = focused || container.querySelector('.tree-item[aria-current="true"]');
        return target ? renameUi.rowInfo(target) : null;
    }

    return {
        render,
        focusTreeKey: focusKey,
        startRenameForSelection,
        getSelectedRowInfo,
        containsFocus: () => container.contains(document.activeElement),
        isRenaming: () => renameUi.isRenaming(),
    };
}

function fileButton(file, { depth, parent, activePath, folder = null, sectionId = '' }) {
    const type = fileTypeKey(file);
    const fname = file.name || fileBase(file.path);
    const button = rowButton({
        kind: 'file',
        section: sectionId,
        slug: folder?.slug || '',
        name: fname,
        treeKey: `file:${file.path}`,
        path: file.path,
        depth,
        parent,
        current: file.path === activePath,
        title: fileRowTitle(file, type),
    });
    button.classList.add('tree-file');
    if (type) button.dataset.fileType = type;
    button.append(
        fileBar(),
        spacer(),
        icon('file'),
        nameSpan(file.name),
        renameActionButton(`Rename ${fname} (F2)`),
        badgeSpan(fileBadges(file, folder)),
    );
    return button;
}

function fileBar() {
    const span = document.createElement('span');
    span.className = 'tree-file-bar';
    span.setAttribute('aria-hidden', 'true');
    return span;
}

function fileRowTitle(file, type) {
    const parts = [];
    if (file?.path) parts.push(file.path);
    const label = fileTypeLabel(type);
    if (label) parts.push(label);
    parts.push('Single-click to preview · Double-click to keep tab open');
    return parts.join(' — ');
}

function fileTypeLabel(type) {
    const names = {
        php: 'PHP file',
        html: 'HTML file',
        htm: 'HTM file',
        shtml: 'SHTML file',
        txt: 'Text file',
        json: 'JSON file',
        css: 'CSS file',
        js: 'JavaScript file',
        jpg: 'JPEG image',
        png: 'PNG image',
        gif: 'GIF image',
        webp: 'WebP image',
        svg: 'SVG image',
        image: 'Image',
    };
    if (!type) return '';
    return names[type] || `${type.toUpperCase()} file`;
}

function fileBadges(file, folder) {
    const badges = [];
    const issues = fileIssueCount(file, folder);
    if (fileIsDirty(file, folder)) badges.push(dirtyDot());
    if (issues > 0) badges.push(issueBadge(issues));
    return badges;
}

function fileTypeKey(file) {
    const name = String(file?.name || file?.path || '');
    const slash = Math.max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
    const base = slash >= 0 ? name.slice(slash + 1) : name;
    const dot = base.lastIndexOf('.');
    if (dot > 0 && dot < base.length - 1) {
        const ext = base.slice(dot + 1).toLowerCase();
        if (ext === 'jpeg') return 'jpg';
        return ext;
    }
    const type = String(file?.type || '').toLowerCase();
    if (!type || type === 'file') return '';
    if (type === 'jpeg') return 'jpg';
    return type;
}

function fileIssueCount(file, folder) {
    if (file && file.issueCount != null && file.issueCount !== '') return Number(file.issueCount) || 0;
    if (fileCarriesFolderCounts(file, folder)) return Number(folder.issueCount) || 0;
    return 0;
}

function fileIsDirty(file, folder) {
    if (file && file.dirty != null && file.dirty !== '') return Boolean(file.dirty);
    if (fileCarriesFolderCounts(file, folder)) return Boolean(folder.dirty);
    return false;
}

function fileCarriesFolderCounts(file, folder) {
    if (!file || !folder) return false;
    const path = String(file.path || '');
    if (folder.primaryFile && path === folder.primaryFile) return true;
    const pages = (folder.files || []).filter((item) => /\.(php|html|shtml|htm)$/i.test(String(item.name || item.path || '')));
    if (pages.length === 1 && pages[0].path === path) return true;
    return /\/sales\/[^/]+\/index\.php$/i.test(path);
}

function issueBadge(count) {
    const badge = counterBadge(count, 'issue', 'issues', false, 'danger');
    badge.classList.add('tree-badge', 'is-issue');
    return badge;
}

function setTip(node, text) {
    const value = String(text || '').trim();
    if (node.hasAttribute('title')) node.removeAttribute('title');
    if (!value) {
        node.removeAttribute('data-tooltip');
        return;
    }
    node.setAttribute('data-tooltip', value);
    if (node.getAttribute('aria-hidden') === 'true' || node.hasAttribute('aria-label') || node.hasAttribute('aria-labelledby')) return;
    const visible = (node.textContent || '').replace(/\s+/g, ' ').trim();
    if (visible.length === 1) node.setAttribute('aria-label', value);
}

function dirtyDot() {
    const span = document.createElement('span');
    span.className = 'tree-dirty';
    span.textContent = '●';
    span.setAttribute('aria-label', 'Modified');
    setTip(span, 'Uncommitted Git changes in this file');
    return span;
}

function addActionButton(title) {
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'tree-rename-btn tree-add-btn';
    btn.dataset.treeAction = 'add';
    btn.tabIndex = -1;
    btn.textContent = '+';
    btn.setAttribute('aria-label', title || 'New Page / File');
    setTip(btn, title || 'New Page / File');
    return btn;
}

function renameActionButton(title) {
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'tree-rename-btn';
    btn.dataset.treeAction = 'rename';
    btn.tabIndex = -1;
    btn.textContent = '✎';
    btn.setAttribute('aria-label', title || 'Rename (F2)');
    setTip(btn, title || 'Rename (F2)');
    return btn;
}

function rowButton({ kind, id, section, slug, name, folderPath, treeKey, path, depth, parent, open, current, title }) {
    const button = document.createElement('div');
    button.className = 'tree-item';
    button.dataset.kind = kind;
    button.dataset.treeKey = treeKey;
    if (id) button.dataset.id = id;
    if (section) button.dataset.section = section;
    if (slug) button.dataset.slug = slug;
    if (name) button.dataset.name = name;
    if (folderPath) button.dataset.folderPath = folderPath;
    if (path) button.dataset.path = path;
    if (parent) button.dataset.parent = parent;
    button.style.setProperty('--depth', String(depth || 0));
    button.setAttribute('role', 'treeitem');
    setTip(button, title);
    if (kind !== 'file') button.setAttribute('aria-expanded', open ? 'true' : 'false');
    if (current) button.setAttribute('aria-current', 'true');
    return button;
}

function folderBadges(folder) {
    const badges = [];
    const match = folder.match || null;
    const reveal = Boolean(folder.revealCounts);
    const photo = counterBadge(
        folder.photoCount,
        'photo',
        'photos',
        Boolean(match?.photo),
    );
    const issue = counterBadge(
        folder.issueCount,
        'issue',
        'issues',
        Boolean(match?.issue),
        Number(folder.issueCount) > 0 ? 'danger' : '',
    );
    if (reveal) {
        badges.push(photo, issue);
        if (match?.count) badges.push(matchBadge(match.count));
    } else if (folder.photoCount > 0 || folder.unlistedCount > 0) {
        badges.push(photo);
    }
    if (folder.price) {
        const price = formatPriceLabel(folder.price, folder.sold);
        badges.push(mini(price.short, price.tone, price.label));
    }
    if (folder.doNotRender) badges.push(mini('Hidden', '', 'Hidden from the public site ($doNotRender)'));
    if (folder.dirty) badges.push(mini('Modified', 'warn', 'Uncommitted Git changes in this folder'));
    if (Number(folder.unlistedCount) > 0) {
        const count = Number(folder.unlistedCount);
        badges.push(mini(
            `${count} unlisted`,
            'warn',
            count === 1 ? '1 slide on disk not listed in gallery' : `${count} slides on disk not listed in gallery`,
        ));
    }
    if (!reveal && Number(folder.issueCount) > 0) badges.push(issue);
    return badges;
}

function prepareTree(sections, query, filter) {
    const q = String(query || '').trim().toLowerCase();
    const filtering = Boolean(q) || (filter && filter !== 'all');
    const output = [];
    for (const section of sections) {
        const folders = [];
        for (const folder of section.folders || []) {
            if (filter === 'modified' && !folder.dirty && !(folder.files || []).some((file) => file.dirty)) continue;
            if (filter === 'issues' && !(Number(folder.issueCount) > 0 || Number(folder.unlistedCount) > 0 || (folder.files || []).some((file) => Number(file.issueCount) > 0))) continue;
            const meta = Boolean(q) && folderMetaHit(folder, q);
            const matchedFiles = q ? folderFileHits(folder, q) : [];
            const issueHits = q ? matchingIssues(folder, q) : [];
            const photoHits = q ? matchingPhotos(folder, q) : [];
            if (q && !meta && !matchedFiles.length && !issueHits.length && !photoHits.length) continue;
            const broad = meta || issueHits.length > 0 || photoHits.length > 0;
            folders.push({
                ...folder,
                files: sortByFilename(q && !broad ? matchedFiles : (folder.files || []), fileFilename),
                revealCounts: filtering,
                match: q ? {
                    count: (meta ? 1 : 0) + matchedFiles.length + issueHits.length + photoHits.length,
                    photo: matchedFiles.length > 0 || photoHits.length > 0,
                    issue: issueHits.length > 0,
                } : null,
                forceOpen: false,
            });
        }
        folders.sort((a, b) => compareFilenames(folderFilename(a), folderFilename(b)));
        const rootFiles = sortByFilename((section.rootFiles || []).filter((file) => {
            if (filter === 'modified' && !file.dirty) return false;
            if (filter === 'issues' && !(Number(file.issueCount) > 0)) return false;
            return !q || textHit(file, q);
        }), fileFilename);
        if (filtering && !folders.length && !rootFiles.length) continue;
        output.push({
            ...section,
            folders,
            rootFiles,
            totalFolders: section.totalFolders ?? (section.folders || []).length,
            forceOpen: filtering,
        });
    }
    output.sort((a, b) => {
        const aRootFiles = a.id === '/';
        const bRootFiles = b.id === '/';
        if (aRootFiles !== bRootFiles) return aRootFiles ? 1 : -1;
        return compareFilenames(a.id, b.id);
    });
    return output;
}

function textHit(item, query) {
    if (!item || !query) return false;
    if (folderMetaHit(item, query)) return true;
    if (fileQueryHit(item, query)) return true;
    if (fileCandidateHit(item.primaryFile, query) || fileCandidateHit(item.path, query)) return true;
    if (matchingIssues(item, query).length) return true;
    if (matchingPhotos(item, query).length) return true;
    return false;
}

function folderMetaHit(folder, query) {
    return [
        folder?.name,
        folder?.slug,
        folder?.title,
        folder?.vehicleName,
        folder?.inventoryNumber,
        folder?.makeType,
        folder?.vehicleType,
        folder?.price,
    ].some((value) => fieldHit(value, query));
}

function folderFileHits(folder, query) {
    const hits = [];
    const seen = new Set();
    for (const file of folder?.files || []) {
        if (!fileQueryHit(file, query)) continue;
        hits.push(file);
        const pathKey = String(file.path || '').toLowerCase();
        const nameKey = String(file.name || fileBase(file.path) || '').toLowerCase();
        if (pathKey) seen.add(pathKey);
        if (nameKey) seen.add(nameKey);
    }
    for (const candidate of [folder?.primaryFile, folder?.path]) {
        if (!fileCandidateHit(candidate, query)) continue;
        const pathKey = String(candidate || '').toLowerCase();
        const nameKey = fileBase(candidate).toLowerCase();
        if (seen.has(pathKey) || seen.has(nameKey)) continue;
        hits.push({ name: fileBase(candidate), path: candidate });
        if (pathKey) seen.add(pathKey);
        if (nameKey) seen.add(nameKey);
    }
    return hits;
}

function fileCandidateHit(path, query) {
    const name = fileBase(path);
    if (!name.includes('.')) return false;
    return fileQueryHit({ name, path }, query);
}

function fileQueryHit(file, query) {
    if (!file || !query) return false;
    if (fieldHit(file.name, query)) return true;
    const path = String(file.path || '');
    const base = fileBase(path);
    if (base && base !== String(file.name || '') && fieldHit(base, query)) return true;
    return query.includes('/') && fieldHit(path, query);
}

function matchingIssues(folder, query) {
    const found = [];
    for (const list of [folder?.warnings, folder?.errors, folder?.issues]) {
        if (!Array.isArray(list)) continue;
        for (const item of list) {
            if (fieldHit(issueItemText(item), query)) found.push(item);
        }
    }
    return found;
}

function issueItemText(item) {
    if (item == null) return '';
    if (typeof item === 'string' || typeof item === 'number') return String(item);
    return ['message', 'title', 'text', 'detail', 'id'].map((key) => item?.[key] || '').filter(Boolean).join(' ');
}

function matchingPhotos(folder, query) {
    const found = [];
    for (const list of [folder?.images, folder?.photos, folder?.slides]) {
        if (!Array.isArray(list)) continue;
        for (const item of list) {
            const text = typeof item === 'string' || typeof item === 'number'
                ? String(item)
                : `${item?.name || ''} ${item?.path || ''}`;
            if (fieldHit(text, query)) found.push(item);
        }
    }
    return found;
}

function fieldHit(value, query) {
    const text = String(value ?? '').toLowerCase();
    if (!text || !query) return false;
    if (text.includes(query)) return true;
    const needle = String(query).replace(/^#/, '');
    return Boolean(needle && needle !== query && text.includes(needle));
}

function fileBase(path) {
    const parts = String(path || '').split(/[\\/]/).filter(Boolean);
    return parts.length ? parts[parts.length - 1] : '';
}

function compareFilenames(a, b) {
    return String(a || '').localeCompare(String(b || ''), undefined, { numeric: true, sensitivity: 'base' });
}

function folderFilename(folder) {
    return folder?.slug || fileBase(folder?.primaryFile || folder?.path) || folder?.name || '';
}

function folderParentSlug(slug) {
    const text = String(slug || '');
    const cut = text.lastIndexOf('/');
    return cut === -1 ? '' : text.slice(0, cut);
}

function groupFolders(folders) {
    const slugs = new Set((folders || []).map((folder) => String(folder.slug || '')));
    const children = new Map();
    const roots = [];
    for (const folder of folders || []) {
        const parent = folderParentSlug(folder.slug);
        if (parent && slugs.has(parent)) {
            const list = children.get(parent);
            if (list) list.push(folder);
            else children.set(parent, [folder]);
        } else {
            roots.push(folder);
        }
    }
    return { roots, children };
}

function fileFilename(file) {
    return file?.name || fileBase(file?.path) || '';
}

function sortByFilename(items, key) {
    return items.slice().sort((a, b) => compareFilenames(key(a), key(b)));
}

function folderRowTitle(folder, filename, human) {
    const parts = [];
    const path = folder.primaryFile || folder.path || filename;
    if (path) parts.push(path);
    if (human) parts.push(human);
    if (folder.inventoryNumber) parts.push(`Inventory #${String(folder.inventoryNumber).replace(/^#/, '')}`);
    if (folder.price) parts.push(formatPriceLabel(folder.price, folder.sold).label);
    else if (folder.sold) parts.push('SOLD');
    if (folder.doNotRender) parts.push('Hidden');
    parts.push('Single-click to preview · Double-click to keep tab open');
    return parts.join(' — ');
}

function counterBadge(count, kind, label, hit, tone) {
    const value = Number(count) || 0;
    let title = `${value} ${label}`;
    if (kind === 'photo') title = value === 1 ? '1 photo in gallery' : `${value} photos in gallery`;
    else if (kind === 'issue') {
        title = value === 1
            ? '1 admin dashboard error or warning'
            : `${value} admin dashboard errors or warnings`;
    }
    if (hit) title += ' · search matches in this folder';
    return mini(String(value), tone || '', title, { hit, kind });
}

function matchBadge(count) {
    const label = count === 1 ? 'Match' : `${count} matches`;
    const title = count === 1 ? 'Search match in this folder' : `${count} search matches in this folder`;
    return mini(label, '', title, { kind: 'match' });
}

function twist(open, title) {
    const span = document.createElement('span');
    span.className = 'twist';
    span.dataset.twist = 'true';
    span.setAttribute('aria-hidden', 'true');
    span.textContent = open ? '▾' : '▸';
    if (title) setTip(span, title);
    return span;
}

function spacer() {
    const span = document.createElement('span');
    span.className = 'twist twist-spacer';
    span.setAttribute('aria-hidden', 'true');
    return span;
}

function icon(kind) {
    const span = document.createElement('span');
    span.className = `tree-icon tree-icon-${kind}`;
    span.setAttribute('aria-hidden', 'true');
    span.innerHTML = kind === 'folder'
        ? '<svg viewBox="0 0 24 24" width="16" height="16"><path d="M3 7h6l2 2h10v10H3z" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"></path></svg>'
        : '<svg viewBox="0 0 24 24" width="16" height="16"><path d="M7 3h7l5 5v13H7z" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"></path></svg>';
    return span;
}

function nameSpan(text, subtitle) {
    const span = document.createElement('span');
    span.className = 'tree-name';
    if (!subtitle) {
        span.textContent = text || '';
        return span;
    }
    span.classList.add('has-sub');
    const file = document.createElement('span');
    file.className = 'tree-name-file';
    file.textContent = text || '';
    const title = document.createElement('span');
    title.className = 'tree-name-title';
    title.textContent = subtitle;
    span.append(file, title);
    return span;
}

function badgeSpan(badges) {
    const span = document.createElement('span');
    span.className = 'tree-badges';
    for (const badge of badges) if (badge) span.append(badge);
    return span;
}

function countBadge(count) {
    const value = Number(count) || 0;
    return mini(String(value), '', value === 1 ? '1 item' : `${value} items`);
}

function mini(text, tone, title, flags) {
    const span = document.createElement('span');
    span.className = 'mini';
    if (tone) span.dataset.tone = tone;
    if (flags?.hit) span.dataset.hit = 'true';
    if (flags?.kind) span.dataset.kind = flags.kind;
    span.textContent = text;
    if (title) setTip(span, title);
    return span;
}

function paragraph(text) {
    const node = document.createElement('p');
    node.className = 'tree-empty';
    node.textContent = text;
    return node;
}

export function mountOpenEditors(section, handlers) {
    const list = section.querySelector('.open-editors-list');
    const toggle = section.querySelector('.open-editors-toggle');
    const closeAll = section.querySelector('.open-editors-close');
    const count = section.querySelector('.open-editors-count');
    const chevron = toggle?.querySelector('.twist');

    toggle?.addEventListener('click', () => handlers.onToggle());
    closeAll?.addEventListener('click', () => handlers.onCloseAll());
    list?.addEventListener('mousedown', (event) => {
        if (event.button === 1) event.preventDefault();
    });
    list?.addEventListener('click', (event) => {
        const row = event.target.closest('.open-editor');
        if (!row || !list.contains(row)) return;
        if (event.target.closest('.open-editor-close')) {
            event.preventDefault();
            handlers.onClose(row.dataset.path);
            return;
        }
        if (event.detail > 1) handlers.onPin(row.dataset.path);
        else handlers.onActivate(row.dataset.path);
    });
    list?.addEventListener('dblclick', (event) => {
        const row = event.target.closest('.open-editor');
        if (!row || !list.contains(row) || event.target.closest('.open-editor-close')) return;
        event.preventDefault();
        handlers.onPin(row.dataset.path);
    });
    list?.addEventListener('auxclick', (event) => {
        if (event.button !== 1) return;
        const row = event.target.closest('.open-editor');
        if (!row || !list.contains(row)) return;
        event.preventDefault();
        handlers.onClose(row.dataset.path);
    });
    list?.addEventListener('keydown', (event) => {
        const row = event.target.closest('.open-editor');
        if (!row || !list.contains(row)) return;
        if (event.key === 'Enter' && row.dataset.preview === 'true') {
            event.preventDefault();
            handlers.onPin(row.dataset.path);
        }
    });

    function render(model) {
        const open = model.open !== false;
        const tabs = model.tabs || [];
        section.dataset.open = open ? 'true' : 'false';
        toggle?.setAttribute('aria-expanded', open ? 'true' : 'false');
        if (chevron) chevron.textContent = open ? '▾' : '▸';
        if (count) {
            count.textContent = String(tabs.length);
            setTip(count, tabs.length === 1 ? '1 open editor' : `${tabs.length} open editors`);
        }
        if (toggle) {
            setTip(toggle, `Open Editors — click to ${open ? 'collapse' : 'expand'} · ${tabs.length} open`);
        }
        if (closeAll) closeAll.disabled = tabs.length === 0;
        if (!list) return;
        const scroll = list.scrollTop;
        const focusPath = list.contains(document.activeElement) ? document.activeElement.dataset?.path : '';
        list.replaceChildren();
        if (!tabs.length) {
            list.append(paragraph('No editors open.'));
        } else {
            for (const tab of tabs) list.append(editorRow(tab));
        }
        list.scrollTop = scroll;
        if (focusPath) [...list.querySelectorAll('.open-editor')].find((node) => node.dataset.path === focusPath)?.focus({ preventScroll: true });
    }

    return { render };
}

function splitEditorPath(tab) {
    const fullPath = String(tab.path || tab.title || '').trim();
    const parts = fullPath.split('/').filter(Boolean);
    if (parts.length > 1) {
        return {
            fullPath,
            fileName: parts[parts.length - 1],
            dirPath: parts.slice(0, -1).join('/'),
        };
    }
    const fileName = parts[0] || String(tab.label || fullPath).trim();
    const hint = String(tab.hint || '').trim();
    const dirPath = hint && hint !== fileName ? hint : '';
    return { fullPath: fullPath || fileName, fileName, dirPath };
}

function editorRow(tab) {
    const { fullPath, fileName, dirPath } = splitEditorPath(tab);
    const row = document.createElement('div');
    row.className = 'open-editor';
    if (tab.unsaved) row.classList.add('is-unsaved');
    row.dataset.path = tab.path;
    row.dataset.preview = tab.isPreview ? 'true' : 'false';
    row.tabIndex = tab.active ? 0 : -1;
    const preview = tab.isPreview
        ? 'Preview tab (italic) — double-click to keep open'
        : 'Pinned editor — click to switch';
    const rowTip = fullPath ? `${fullPath} — ${preview}` : preview;
    setTip(row, rowTip);
    row.title = rowTip;
    if (tab.active) row.setAttribute('aria-current', 'true');
    const text = document.createElement('span');
    text.className = 'open-editor-text';
    if (fullPath) text.title = fullPath;
    const label = document.createElement('span');
    label.className = 'open-editor-label';
    label.textContent = fileName || tab.label || tab.path;
    text.append(label);
    if (dirPath) {
        const hint = document.createElement('span');
        hint.className = 'open-editor-hint';
        hint.textContent = dirPath;
        if (fullPath) hint.title = fullPath;
        text.append(hint);
    }
    const meta = document.createElement('span');
    meta.className = 'open-editor-meta';
    if (Number(tab.issues) > 0) {
        const badge = document.createElement('span');
        badge.className = 'mini';
        badge.dataset.tone = 'danger';
        badge.textContent = String(tab.issues);
        setTip(badge, tab.issues === 1
            ? '1 admin dashboard issue on this page'
            : `${tab.issues} admin dashboard issues on this page`);
        meta.append(badge);
    }
    const closer = document.createElement('button');
    closer.type = 'button';
    closer.className = 'open-editor-close';
    setTip(closer, 'Close tab (Ctrl+W)');
    closer.setAttribute('aria-label', `Close ${fileName || tab.label || 'editor'}`);
    const dot = document.createElement('span');
    dot.className = 'open-editor-dot';
    dot.textContent = '●';
    dot.setAttribute('aria-hidden', 'true');
    if (tab.unsaved) setTip(dot, 'Has unsaved changes');
    const mark = document.createElement('span');
    mark.className = 'open-editor-x';
    mark.textContent = '×';
    mark.setAttribute('aria-hidden', 'true');
    closer.append(dot, mark);
    row.append(text);
    if (meta.childNodes.length) row.append(meta);
    row.append(closer);
    return row;
}

