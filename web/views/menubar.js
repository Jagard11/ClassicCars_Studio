import { el } from './dom.js';

const MENUS = [
    {
        id: 'file',
        label: 'File',
        mnemonic: 'f',
        items: [
            { action: 'new-page', label: 'New Page…', accel: 'Ctrl+N', keys: 'Control+N' },
            { action: 'open-page', label: 'Open Page / Quick Jump…', accel: 'Ctrl+O', keys: 'Control+O', title: 'Ctrl+O or Ctrl+K' },
            { action: 'open-folder', label: 'Open Folder…', accel: 'Ctrl+Shift+O', keys: 'Control+Shift+O', title: 'Ctrl+Shift+O or Ctrl+K Ctrl+O' },
            { label: 'Open Recent', recent: true },
            { sep: true },
            { action: 'save', label: 'Save', accel: 'Ctrl+S', keys: 'Control+S' },
            { action: 'save-sync', label: 'Save & Sync SEO (Page Values)', accel: 'Ctrl+Shift+S', keys: 'Control+Shift+S' },
            { action: 'revert', label: 'Revert Uncommitted Changes…' },
            { sep: true },
            { action: 'close-tab', label: 'Close Editor', accel: 'Ctrl+W', keys: 'Control+W' },
            { action: 'reopen-closed-tab', label: 'Reopen Closed Editor', accel: 'Ctrl+Shift+T', keys: 'Control+Shift+T' },
            { sep: true },
            { action: 'reload', label: 'Reload Preview', accel: 'F5', keys: 'F5' },
            { sep: true },
            { action: 'exit', label: 'Exit', accel: 'Alt+F4', keys: 'Alt+F4', title: 'Alt+F4 or Ctrl+Q' },
        ],
    },
    {
        id: 'edit',
        label: 'Edit',
        mnemonic: 'e',
        items: [
            { action: 'undo', label: 'Undo', accel: 'Ctrl+Z', keys: 'Control+Z' },
            { action: 'redo', label: 'Redo', accel: 'Ctrl+Y', keys: 'Control+Y', title: 'Ctrl+Y or Ctrl+Shift+Z' },
            { action: 'undo-history', label: 'Undo History…', accel: 'Ctrl+Shift+H', keys: 'Control+Shift+H' },
            { sep: true },
            { action: 'cut', label: 'Cut', accel: 'Ctrl+X', keys: 'Control+X' },
            { action: 'copy', label: 'Copy', accel: 'Ctrl+C', keys: 'Control+C' },
            { action: 'paste', label: 'Paste', accel: 'Ctrl+V', keys: 'Control+V' },
            { action: 'select-all', label: 'Select All', accel: 'Ctrl+A', keys: 'Control+A' },
            { sep: true },
            { action: 'find', label: 'Find in Code Editor…', accel: 'Ctrl+F', keys: 'Control+F' },
            { action: 'find-in-project', label: 'Find & Replace in Project…', accel: 'Ctrl+Shift+F', keys: 'Control+Shift+F' },
            { action: 'toggle-comment', label: 'Toggle Comment', accel: 'Ctrl+/', keys: 'Control+/' },
            { action: 'toggle-preview-edit', label: 'Toggle Viewport Inline Text Edit', accel: 'Ctrl+E', keys: 'Control+E', check: true },
            { sep: true },
            {
                label: 'Insert HTML Block',
                submenu: [
                    { action: 'insert-block', arg: 'feature-single', label: 'Feature Single Image (<div class="articleFeatureSingle">)' },
                    { action: 'insert-block', arg: 'feature-landscape', label: 'Feature Landscape Pair (<div class="articleFeaturePair--landscape">)' },
                    { action: 'insert-block', arg: 'feature-portrait', label: 'Feature Portrait Pair (<div class="articleFeaturePair--portrait">)' },
                    { action: 'insert-block', arg: 'gallery-section', label: 'Lightbox Gallery Section (<section class="articleGallerySection">)' },
                ],
            },
        ],
    },
    {
        id: 'view',
        label: 'View',
        mnemonic: 'v',
        items: [
            { action: 'toggle-tree', label: 'Toggle Left Shelf', accel: 'Ctrl+B', keys: 'Control+B', check: true },
            {
                label: 'Left Shelf Mode',
                submenu: [
                    { action: 'shelf-view', arg: 'files', label: 'Files (File Tree)', accel: 'Ctrl+Shift+E', keys: 'Control+Shift+E', radio: 'shelf' },
                    { action: 'shelf-view', arg: 'search', label: 'Project Find & Replace', accel: 'Ctrl+Shift+F', keys: 'Control+Shift+F', radio: 'shelf' },
                    { action: 'shelf-view', arg: 'git', label: 'Version Control (Git)', accel: 'Ctrl+Shift+G', keys: 'Control+Shift+G', radio: 'shelf' },
                ],
            },
            { action: 'toggle-inspector', label: 'Toggle Right Widgets Panel', accel: 'Ctrl+J', keys: 'Control+J', check: true },
            { action: 'toggle-terminal', label: 'Toggle Terminal', accel: 'Ctrl+`', keys: 'Control+Backquote', check: true },
            { action: 'keep-open', label: 'Keep Editor Open' },
            { sep: true },
            {
                label: 'Top Editor Mode',
                submenu: [
                    { action: 'mode-code', label: 'Code Editor (HTML / PHP)', radio: 'mode' },
                    { action: 'mode-gallery', label: 'Gallery Order & Captions', radio: 'mode' },
                ],
            },
            { action: 'word-wrap', label: 'Word Wrap', accel: 'Alt+Z', keys: 'Alt+Z', check: true },
            {
                label: 'Viewport Size',
                submenu: [
                    { action: 'viewport', arg: 'desktop', label: 'Desktop (1280px)', radio: 'viewport' },
                    { action: 'viewport', arg: 'column', label: 'Site Column (1040px)', radio: 'viewport' },
                    { action: 'viewport', arg: 'tablet', label: 'Tablet (768px)', radio: 'viewport' },
                    { action: 'viewport', arg: 'mobile', label: 'Mobile (390px)', radio: 'viewport' },
                    { action: 'viewport', arg: 'fit', label: 'Fit Width', radio: 'viewport' },
                ],
            },
            { action: 'scroll-gallery', label: 'Scroll Preview to Gallery' },
            { action: 'full-preview', label: 'Full Window Preview', check: true },
            { action: 'refresh-preview', label: 'Refresh Preview', accel: 'Ctrl+R', keys: 'Control+R' },
            { sep: true },
            { action: 'zoom-in', label: 'Zoom In', accel: 'Ctrl+Wheel Up', keys: 'Control+=' },
            { action: 'zoom-out', label: 'Zoom Out', accel: 'Ctrl+Wheel Down', keys: 'Control+-' },
            { action: 'zoom-reset', label: 'Reset Zoom', accel: 'Ctrl+0', keys: 'Control+0' },
            { sep: true },
            {
                label: 'Theme',
                submenu: [
                    { action: 'theme', arg: 'gold', label: 'Shappy Gold (Site Dark)', radio: 'theme' },
                    { action: 'theme', arg: 'slate', label: 'Slate Studio (Neutral Dark)', radio: 'theme' },
                    { action: 'theme', arg: 'daylight', label: 'Daylight (Light)', radio: 'theme' },
                    { action: 'theme', arg: 'contrast', label: 'High Contrast (AAA)', radio: 'theme' },
                    { sep: true },
                    { action: 'theme-custom', label: 'Customize Theme Colors…' },
                ],
            },
        ],
    },
    {
        id: 'tools',
        label: 'Tools',
        mnemonic: 't',
        items: [
            { action: 'error-check', label: 'Run Common Error Check' },
            { action: 'project-macros', label: 'Project Macros & Page Tools…' },
            { action: 'new-macro', label: 'New Project Macro…' },
            { sep: true },
            { action: 'sync-seo', label: 'Sync Page Values & SEO Block' },
            { action: 'suggestion', label: 'Configure Suggestion Strip…' },
            { action: 'tracking', label: 'Verify Tracking Tags (Google / eXTReMe / Admin)' },
            { sep: true },
            {
                label: 'Legacy Converters',
                submenu: [
                    { action: 'convert-jalbum', label: 'jAlbum → Modern Lightbox Page…' },
                ],
            },
            { sep: true },
            { action: 'append-unlisted', label: 'Append All Unlisted Slides' },
            { sep: true },
            {
                label: 'Config',
                submenu: [
                    { action: 'git-auto-fetch-settings', label: 'Git Periodic Remote Check (App-Wide)…' },
                    { sep: true },
                    { action: 'open-config', label: 'Open Config File in Editor' },
                    { action: 'open-config-folder', label: 'Open Config File Folder' },
                    { sep: true },
                    { action: 'backup-config', label: 'Backup Config' },
                    { action: 'recover-config', label: 'Recover Config' },
                    { sep: true },
                    { action: 'clear-config', label: 'Clear Config…' },
                ],
            },
        ],
    },
    {
        id: 'help',
        label: 'Help',
        mnemonic: 'h',
        items: [
            { action: 'shortcuts', label: 'Keyboard Shortcuts & Workflow Guide…' },
            { action: 'check-updates', label: 'Check for Updates…' },
            { sep: true },
            { action: 'about', label: 'About ClassicCars Studio' },
        ],
    },
];

const SHORTCUT_GROUPS = [
    ['File', [
        ['Ctrl+N', 'New Page'],
        ['Ctrl+O', 'Open Page / Quick Jump'],
        ['Ctrl+K', 'Open Page / Quick Jump'],
        ['Ctrl+Shift+O', 'Open Folder'],
        ['Ctrl+K Ctrl+O', 'Open Folder'],
        ['Ctrl+S', 'Save the current page and gallery'],
        ['Ctrl+W', 'Close the active editor'],
        ['Ctrl+Shift+T', 'Reopen the last closed editor'],
        ['Ctrl+Tab', 'Next open editor'],
        ['Ctrl+Shift+Tab', 'Previous open editor'],
        ['Ctrl+PageDown', 'Next open editor'],
        ['Ctrl+PageUp', 'Previous open editor'],
        ['Ctrl+Shift+S', 'Save and sync SEO (page values)'],
        ['F5', 'Reload the rendered preview'],
        ['Alt+F4', 'Exit'],
        ['Ctrl+Q', 'Exit'],
    ]],
    ['Edit', [
        ['Ctrl+Z', 'Undo the last document or gallery change'],
        ['Ctrl+Y / Ctrl+Shift+Z', 'Redo the next document or gallery change'],
        ['Ctrl+Shift+H', 'Open the interactive Undo History viewer'],
        ['Ctrl+X', 'Cut'],
        ['Ctrl+C', 'Copy'],
        ['Ctrl+V', 'Paste'],
        ['Ctrl+A', 'Select all'],
        ['Ctrl+F', 'Find in the code editor'],
        ['Ctrl+Shift+F', 'Find and replace across the project'],
        ['Ctrl+/', 'Toggle comment on the current line or selection'],
        ['Ctrl+E', 'Toggle viewport inline text edit'],
    ]],
    ['View', [
        ['Keep Editor Open', 'Pin the preview tab so the next file opens beside it'],
        ['Alt+Z', 'Toggle word wrap in the code editor'],
        ['Ctrl+B', 'Show or hide the left shelf'],
        ['Ctrl+Shift+E', 'Switch left shelf to Files'],
        ['Ctrl+Shift+F', 'Switch left shelf to Project Find & Replace'],
        ['Ctrl+Shift+G', 'Switch left shelf to Git Version Control'],
        ['Ctrl+J', 'Show or hide the right widgets panel'],
        ['Ctrl+`', 'Show or hide the bottom terminal'],
        ['Ctrl+R', 'Refresh the rendered preview'],
        ['Ctrl+Wheel Up', 'Zoom in'],
        ['Ctrl+Wheel Down', 'Zoom out'],
        ['Ctrl+0', 'Reset zoom to 100%'],
    ]],
    ['Preview', [
        ['Right-click', 'Jump to code block ▸ on the element under the pointer'],
        ['Alt', 'Hold Alt to outline the preview element under the pointer'],
    ]],
    ['Tools', [
        ['Legacy Converters', 'jAlbum → Modern Lightbox Page…'],
        ['Config', 'Open, back up, recover, or clear state/config.json'],
    ]],
    ['Gallery', [
        ['Arrow keys', 'Move between photos'],
        ['Shift+Arrows', 'Extend the selection'],
        ['Space', 'Pick up the selection, then drop it'],
        ['Alt+Arrow', 'Move the selection immediately'],
        ['Enter', 'Edit the focused caption'],
    ]],
    ['File Tree', [
        ['F2', 'Rename selected file or folder'],
        ['Ctrl+D', 'Duplicate selected file or folder'],
        ['Ctrl+X / Ctrl+C / Ctrl+V', 'Cut, copy, and paste files or folders'],
        ['Delete', 'Move selected file or folder to trash'],
        ['Ctrl+Z / Ctrl+Y', 'Undo or redo file tree operation'],
    ]],
];

const ITEM_TIPS = {
    'new-page': 'Create a new blank or templated page',
    'open-page': 'Quick-jump to any vehicle, article, award, or project by name or inventory #',
    'open-folder': 'Choose the project folder Studio edits',
    save: 'Save the active code buffer and gallery changes to disk',
    'save-sync': 'Save and write page values into the generated SEO block',
    revert: 'Revert uncommitted Git changes in this folder back to HEAD',
    'close-tab': 'Close the active editor tab',
    'reopen-closed-tab': 'Reopen the most recently closed editor tab',
    reload: 'Reload the PHP + SSI preview while preserving scroll position',
    exit: 'Quit ClassicCars Studio',
    undo: 'Undo the last edit in the current document or active text field',
    redo: 'Redo the next edit in the current document or active text field',
    'undo-history': 'Open the interactive Undo History timeline to inspect or restore any state',
    cut: 'Cut the selection',
    copy: 'Copy the selection',
    paste: 'Paste from the clipboard',
    'select-all': 'Select all text in the active field',
    find: 'Find text in the code editor',
    'find-in-project': 'Search and replace across all files in the project using Literal, Regex, or Wildcard patterns',
    'shelf-view:files': 'Show Open Editors and the Project File Tree in the left shelf',
    'shelf-view:search': 'Show Project Find & Replace in the left shelf',
    'shelf-view:git': 'Show Git Version Control in the left shelf',
    'toggle-comment': 'Toggle comment appropriate for the code at the cursor or selection in the code editor',
    'toggle-preview-edit': 'Toggle inline edit mode in the rendered preview',
    'insert-block:feature-single': 'Insert single featured image block',
    'insert-block:feature-landscape': 'Insert landscape photo pair',
    'insert-block:feature-portrait': 'Insert portrait photo pair',
    'insert-block:gallery-section': 'Insert Lightbox photo gallery section',
    'toggle-tree': 'Toggle the Left File Tree sidebar',
    'toggle-inspector': 'Toggle the Right Supporting Widgets and Error Check shelf',
    'toggle-terminal': 'Toggle the integrated bottom Terminal panel',
    'keep-open': 'Pin the preview tab so the next file opens beside it',
    'mode-code': 'Edit raw HTML/PHP source code with syntax highlighting and IntelliSense',
    'mode-gallery': 'Visual drag-and-drop photo order, Hero #1 slot, and caption editor',
    'word-wrap': 'Wrap long lines in the code editor',
    'viewport:desktop': 'Preview at a desktop width of 1280px',
    'viewport:column': 'Preview at the site column width of 1040px',
    'viewport:tablet': 'Preview at a tablet width of 768px',
    'viewport:mobile': 'Preview at a mobile width of 390px',
    'viewport:fit': 'Fit the preview to the available width',
    'scroll-gallery': 'Scroll the preview directly to the photo gallery section',
    'full-preview': 'Expand the rendered preview to fill the entire Studio window',
    'refresh-preview': 'Reload the PHP + SSI preview while preserving scroll position',
    'zoom-in': 'Zoom in (Ctrl + middle mouse wheel up or Ctrl+=)',
    'zoom-out': 'Zoom out (Ctrl + middle mouse wheel down or Ctrl+-)',
    'zoom-reset': 'Reset zoom back to 100% (Ctrl+0)',
    'theme:gold': 'Switch Studio color theme to Shappy Gold',
    'theme:slate': 'Switch Studio color theme to Slate Studio',
    'theme:daylight': 'Switch Studio color theme to Daylight',
    'theme:contrast': 'Switch Studio color theme to High Contrast AAA',
    'theme-custom': 'Customize theme colors and check WCAG contrast ratios',
    'error-check': 'Run Common Error Check and show the admin dashboard warnings',
    'project-macros': 'Create, customize, and manage project-specific macros and right-panel page tools',
    'new-macro': 'Create a new customizable macro for this project',
    'sync-seo': 'Sync page values into the generated SEO block',
    suggestion: 'Open Suggestion Widget Settings for the similar-vehicles strip',
    tracking: 'Verify Google, eXTReMe, and Admin Dashboard tracking tags',
    'append-unlisted': 'Add orphaned slide images from slides/ into the gallery data',
    'git-auto-fetch-settings': 'Configure app-wide periodic Git remote checks (saved in state/app-config.json)',
    'open-config': 'Open state/config.json in the Studio code editor',
    'open-config-folder': 'Open the folder containing state/config.json in the system file manager',
    'backup-config': 'Save a backup copy of state/config.json to state/config.backup.json',
    'recover-config': 'Restore state/config.json from state/config.backup.json',
    'clear-config': 'Clear tracking keys in state/config.json back to an empty template',
    'convert-jalbum': 'Open the jAlbum converter. It rewrites the open editor and preview only, and leaves the file on disk until Save.',
    shortcuts: 'Open the keyboard shortcuts and workflow guide',
    'check-updates': 'Check GitHub Releases for a newer version of ClassicCars Studio',
    about: 'About ClassicCars Studio, including library versions and the PHP preview port',
    'clear-recent': 'Clear the Open Recent list',
};

function tipFor(spec) {
    if (spec.flyout) return `Open the ${spec.label} submenu`;
    if (spec.recentItem || spec.action === 'open-recent') {
        const path = spec.arg || '';
        return path ? `Open recent page — ${path}` : (spec.label || 'Recent page');
    }
    if (!spec.action) return spec.title || spec.label || '';
    const key = spec.arg ? `${spec.action}:${spec.arg}` : spec.action;
    const base = ITEM_TIPS[key] || ITEM_TIPS[spec.action] || spec.label || '';
    if (spec.title && /ctrl|alt|shift|\+/i.test(spec.title) && !base.includes(spec.title)) {
        return `${base} (${spec.title})`;
    }
    if (spec.accel && !base.includes(spec.accel)) return `${base} (${spec.accel})`;
    return base;
}

export function mountMenuBar(root, handlers) {
    if (!root) return { refresh() {}, close() {}, isOpen: () => false };
    root.classList.add('menu-bar');
    root.setAttribute('role', 'menubar');
    root.setAttribute('aria-label', 'Application menu');
    const menus = MENUS.map((spec) => buildMenu(spec));
    let openId = '';
    let flyTimer = 0;
    let recentSig = '';
    let switchedFrom = '';
    let suppressClick = false;
    const helpDialog = buildHelpDialog();
    const aboutDialog = buildAboutDialog();

    function buildMenu(spec) {
        const button = el('button', {
            class: 'menu-top',
            role: 'menuitem',
            'aria-haspopup': 'menu',
            'aria-expanded': 'false',
            'data-menu': spec.id,
            'aria-controls': `menu-panel-${spec.id}`,
        });
        button.setAttribute('aria-label', spec.label);
        button.title = `${spec.label} menu (Alt+${spec.mnemonic.toUpperCase()})`;
        button.append(el('span', { class: 'menu-mnem', text: spec.label.slice(0, 1) }), spec.label.slice(1));
        const panel = el('div', {
            class: 'menu-drop',
            id: `menu-panel-${spec.id}`,
            role: 'menu',
            hidden: true,
            'aria-label': spec.label,
        });
        for (const item of spec.items) {
            if (item.sep) panel.append(separator());
            else if (item.recent || item.submenu) panel.append(submenuHost(item));
            else panel.append(itemButton(item));
        }
        button.addEventListener('pointerdown', (event) => {
            if (event.button !== 0) return;
            suppressClick = true;
            if (switchedFrom && switchedFrom !== spec.id) {
                openMenu(spec.id);
                switchedFrom = '';
                return;
            }
            switchedFrom = '';
            if (openId === spec.id) closeAll();
            else openMenu(spec.id);
        });
        button.addEventListener('click', (event) => {
            if (suppressClick) {
                suppressClick = false;
                event.preventDefault();
                return;
            }
            if (openId === spec.id) closeAll();
            else openMenu(spec.id);
        });
        button.addEventListener('pointerenter', () => {
            if (openId && openId !== spec.id) {
                switchedFrom = openId;
                openMenu(spec.id);
            }
        });
        root.append(button, panel);
        return { id: spec.id, mnemonic: spec.mnemonic, button, panel };
    }

    function submenuHost(spec) {
        const host = el('div', { class: 'menu-subhost' });
        if (spec.recent) host.setAttribute('data-recent', 'true');
        const button = itemButton({ label: spec.label, flyout: true });
        button.setAttribute('aria-haspopup', 'menu');
        button.setAttribute('aria-expanded', 'false');
        const fly = el('div', { class: 'menu-fly', role: 'menu', hidden: true, 'aria-label': spec.label });
        if (!spec.recent) {
            for (const child of spec.submenu) {
                if (child.sep) fly.append(separator());
                else fly.append(itemButton(child));
            }
        }
        host.append(button, fly);
        host.addEventListener('pointerenter', () => {
            if (!openId) return;
            openFlyout(host);
        });
        host.addEventListener('pointerleave', (event) => {
            if (host.contains(event.relatedTarget)) return;
            scheduleCloseFlyout(host);
        });
        return host;
    }

    function itemButton(spec) {
        const role = spec.radio ? 'menuitemradio' : spec.check ? 'menuitemcheckbox' : 'menuitem';
        const button = el('button', {
            class: spec.recentItem ? 'menu-item is-recent' : 'menu-item',
            role,
            'data-action': spec.action || '',
            'data-arg': spec.arg || '',
            'data-label': spec.label || '',
            title: tipFor(spec),
            disabled: Boolean(spec.disabled),
        });
        if (role !== 'menuitem') button.setAttribute('aria-checked', 'false');
        if (spec.keys) button.setAttribute('aria-keyshortcuts', spec.keys);
        button.append(el('span', { class: 'menu-check', 'aria-hidden': 'true' }));
        button.append(el('span', { class: 'menu-label', text: spec.label || '' }));
        if (spec.accel) button.append(el('span', { class: 'menu-accel', text: spec.accel }));
        if (spec.flyout) button.append(el('span', { class: 'menu-caret', text: '▸', 'aria-hidden': 'true' }));
        button.addEventListener('click', (event) => {
            event.stopPropagation();
            runItem(button, { keyboard: event.detail === 0 });
        });
        button.addEventListener('pointerover', () => setActive(button));
        return button;
    }

    function separator() {
        return el('div', { class: 'menu-sep', role: 'separator' });
    }

    function refresh() {
        const snap = handlers.snapshot();
        for (const button of root.querySelectorAll('.menu-item[data-action]')) {
            if (button.closest('[data-recent]')) continue;
            applyFlags(button, flagFor(button.dataset.action, button.dataset.arg || '', snap));
        }
        const recent = root.querySelector('[data-recent]');
        if (recent) fillRecent(recent, snap.recent || []);
    }

    function fillRecent(host, items) {
        const fly = host.querySelector('.menu-fly');
        const sig = items.map((item) => `${item.path}\0${item.label}`).join('\n');
        if (sig === recentSig && fly.childElementCount) return;
        recentSig = sig;
        const wasOpen = !fly.hidden;
        fly.replaceChildren();
        if (!items.length) fly.append(itemButton({ label: 'No recent pages', disabled: true, title: 'No pages have been opened recently' }));
        else {
            for (const item of items) {
                fly.append(itemButton({
                    action: 'open-recent',
                    arg: item.path,
                    label: item.label || item.path,
                    title: item.path,
                    recentItem: true,
                }));
            }
        }
        fly.append(separator());
        fly.append(itemButton({
            action: 'clear-recent',
            label: 'Clear Recent Files',
            disabled: items.length === 0,
        }));
        if (wasOpen) placeFlyout(host);
    }

    function applyFlags(button, flags) {
        button.disabled = Boolean(flags.disabled);
        const check = button.querySelector('.menu-check');
        const on = Boolean(flags.checked);
        if (check) check.textContent = on ? '✓' : '';
        if (button.getAttribute('role') === 'menuitemcheckbox' || button.getAttribute('role') === 'menuitemradio') {
            button.setAttribute('aria-checked', on ? 'true' : 'false');
        }
    }

    function openMenu(id, { focusItem = false, fromEnd = false } = {}) {
        window.clearTimeout(flyTimer);
        closeFlyouts();
        openId = id;
        refresh();
        for (const menu of menus) {
            const on = menu.id === id;
            menu.panel.hidden = !on;
            menu.button.setAttribute('aria-expanded', on ? 'true' : 'false');
            menu.button.classList.toggle('is-open', on);
            if (on) placeDrop(menu);
        }
        const menu = menus.find((item) => item.id === id);
        if (!menu) return;
        if (focusItem) {
            const items = listItems(menu.panel);
            setActive(fromEnd ? items[items.length - 1] : items[0], { focus: true });
        } else setActive(null);
    }

    function closeAll({ focusTop = false } = {}) {
        const was = openId;
        window.clearTimeout(flyTimer);
        closeFlyouts();
        openId = '';
        switchedFrom = '';
        for (const menu of menus) {
            menu.panel.hidden = true;
            menu.button.setAttribute('aria-expanded', 'false');
            menu.button.classList.remove('is-open');
        }
        setActive(null);
        if (focusTop && was) menus.find((menu) => menu.id === was)?.button.focus();
    }

    function closeFlyouts() {
        for (const fly of root.querySelectorAll('.menu-fly')) {
            fly.hidden = true;
            fly.parentElement?.querySelector(':scope > .menu-item')?.setAttribute('aria-expanded', 'false');
        }
    }

    function openFlyout(host) {
        window.clearTimeout(flyTimer);
        for (const fly of root.querySelectorAll('.menu-fly')) {
            if (fly.parentElement === host) continue;
            fly.hidden = true;
            fly.parentElement?.querySelector(':scope > .menu-item')?.setAttribute('aria-expanded', 'false');
        }
        placeFlyout(host);
    }

    function scheduleCloseFlyout(host) {
        window.clearTimeout(flyTimer);
        flyTimer = window.setTimeout(() => {
            const fly = host.querySelector('.menu-fly');
            if (!fly || fly.hidden || fly.contains(document.activeElement)) return;
            fly.hidden = true;
            host.querySelector(':scope > .menu-item')?.setAttribute('aria-expanded', 'false');
        }, 180);
    }

    function placeDrop(menu) {
        const panel = menu.panel;
        const rect = menu.button.getBoundingClientRect();
        const width = panel.offsetWidth;
        const height = panel.offsetHeight;
        const margin = 4;
        let left = rect.left;
        let top = rect.bottom;
        if (left + width > window.innerWidth - margin) left = Math.max(margin, window.innerWidth - width - margin);
        if (top + height > window.innerHeight - margin) top = Math.max(margin, rect.top - height);
        panel.style.left = `${Math.round(left)}px`;
        panel.style.top = `${Math.round(top)}px`;
    }

    function placeFlyout(host) {
        const fly = host.querySelector('.menu-fly');
        const button = host.querySelector(':scope > .menu-item');
        if (!fly || !button) return;
        fly.hidden = false;
        button.setAttribute('aria-expanded', 'true');
        const anchor = button.getBoundingClientRect();
        const width = fly.offsetWidth;
        const height = fly.offsetHeight;
        const margin = 4;
        let left = anchor.right - 6;
        let top = anchor.top - 4;
        if (left + width > window.innerWidth - margin) left = Math.max(margin, anchor.left - width + 6);
        if (top + height > window.innerHeight - margin) top = Math.max(margin, window.innerHeight - height - margin);
        fly.style.left = `${Math.round(left)}px`;
        fly.style.top = `${Math.round(top)}px`;
    }

    function listItems(panel) {
        if (!panel) return [];
        const selector = panel.classList.contains('menu-fly')
            ? ':scope > .menu-item'
            : ':scope > .menu-item, :scope > .menu-subhost > .menu-item';
        return [...panel.querySelectorAll(selector)].filter((item) => !item.disabled);
    }

    function listFor(item) {
        const fly = item?.closest?.('.menu-fly');
        if (fly && !fly.hidden) return fly;
        return root.querySelector('.menu-drop:not([hidden])');
    }

    function activeItem() {
        return root.querySelector('.menu-item.is-active');
    }

    function setActive(item, { focus = false } = {}) {
        for (const node of root.querySelectorAll('.menu-item.is-active')) node.classList.remove('is-active');
        if (!item) return;
        item.classList.add('is-active');
        if (focus) item.focus({ preventScroll: true });
    }

    function move(delta) {
        const items = listItems(listFor(activeItem()));
        if (!items.length) return;
        const index = items.indexOf(activeItem());
        const next = index < 0
            ? (delta > 0 ? items[0] : items[items.length - 1])
            : items[(index + delta + items.length) % items.length];
        setActive(next, { focus: true });
    }

    function stepTop(delta) {
        if (!menus.length) return;
        const index = Math.max(0, menus.findIndex((menu) => menu.id === openId));
        const next = menus[(index + delta + menus.length) % menus.length];
        openMenu(next.id, { focusItem: true });
    }

    function closeFlyoutToParent() {
        const fly = root.querySelector('.menu-fly:not([hidden])');
        const current = activeItem();
        if (!fly || !current || !fly.contains(current)) return false;
        const parent = fly.parentElement?.querySelector(':scope > .menu-item');
        fly.hidden = true;
        parent?.setAttribute('aria-expanded', 'false');
        setActive(parent, { focus: true });
        return true;
    }

    function runItem(button, { keyboard = false } = {}) {
        if (!button || button.disabled) return;
        if (button.getAttribute('aria-haspopup') === 'menu') {
            const host = button.closest('.menu-subhost');
            openFlyout(host);
            if (keyboard) {
                const first = listItems(host.querySelector('.menu-fly'))[0];
                if (first) setActive(first, { focus: true });
            }
            return;
        }
        const action = button.dataset.action;
        if (!action) return;
        const arg = button.dataset.arg || '';
        closeAll();
        if (action === 'shortcuts') openHelp();
        else if (action === 'about') openAbout();
        else handlers.perform(action, arg);
    }

    function onKey(event) {
        if (document.querySelector('dialog[open]')) return;
        const key = event.key;
        if (!openId) {
            if (event.altKey && !event.ctrlKey && !event.metaKey && !event.shiftKey && !event.repeat && key.length === 1) {
                const menu = menus.find((item) => item.mnemonic === key.toLowerCase());
                if (!menu) return;
                event.preventDefault();
                event.stopPropagation();
                openMenu(menu.id, { focusItem: true });
            }
            const top = event.target?.closest?.('.menu-top');
            if (!top) return;
            if (key === 'ArrowRight' || key === 'ArrowLeft') {
                event.preventDefault();
                const index = menus.findIndex((menu) => menu.button === top);
                const next = menus[(index + (key === 'ArrowRight' ? 1 : -1) + menus.length) % menus.length];
                next.button.focus();
            } else if (key === 'ArrowDown' || key === 'ArrowUp') {
                event.preventDefault();
                openMenu(top.dataset.menu, { focusItem: true, fromEnd: key === 'ArrowUp' });
            }
            return;
        }
        if (event.altKey && !event.ctrlKey && !event.metaKey && !event.shiftKey && !event.repeat && key.length === 1) {
            const menu = menus.find((item) => item.mnemonic === key.toLowerCase());
            if (!menu) return;
            event.preventDefault();
            event.stopPropagation();
            openMenu(menu.id, { focusItem: true });
            return;
        }
        if (key === 'Escape') {
            event.preventDefault();
            event.stopPropagation();
            if (!closeFlyoutToParent()) closeAll({ focusTop: true });
            return;
        }
        if (key === 'ArrowDown') {
            event.preventDefault();
            event.stopPropagation();
            move(1);
            return;
        }
        if (key === 'ArrowUp') {
            event.preventDefault();
            event.stopPropagation();
            move(-1);
            return;
        }
        if (key === 'ArrowRight') {
            event.preventDefault();
            event.stopPropagation();
            const current = activeItem();
            if (current?.getAttribute('aria-haspopup') === 'menu') {
                runItem(current, { keyboard: true });
                return;
            }
            if (current?.closest('.menu-fly')) return;
            stepTop(1);
            return;
        }
        if (key === 'ArrowLeft') {
            event.preventDefault();
            event.stopPropagation();
            if (closeFlyoutToParent()) return;
            stepTop(-1);
            return;
        }
        if (key === 'Home' || key === 'End') {
            event.preventDefault();
            event.stopPropagation();
            const items = listItems(listFor(activeItem()));
            setActive(key === 'Home' ? items[0] : items[items.length - 1], { focus: true });
            return;
        }
        if (key === 'Tab') {
            closeAll();
            return;
        }
        if (key.length === 1 && !event.ctrlKey && !event.metaKey && !event.altKey) {
            const items = listItems(listFor(activeItem()));
            const start = Math.max(0, items.indexOf(activeItem()));
            for (let step = 1; step <= items.length; step += 1) {
                const candidate = items[(start + step) % items.length];
                const label = candidate?.dataset.label || '';
                if (label.trim().toLowerCase().startsWith(key.toLowerCase())) {
                    event.preventDefault();
                    event.stopPropagation();
                    setActive(candidate, { focus: true });
                    return;
                }
            }
        }
        if (openId && (key === 'F5' || (event.altKey && key === 'F4') || ((event.ctrlKey || event.metaKey) && key.length === 1))) {
            closeAll();
        }
    }

    function openHelp() {
        if (!helpDialog.open) helpDialog.showModal();
        helpDialog.querySelector('.btn')?.focus();
    }

    function openAbout() {
        const snap = handlers.snapshot();
        const version = snap.version ? `v${String(snap.version).replace(/^v/i, '')}` : 'v0.0.x';
        aboutDialog.querySelector('[data-about="version"]').textContent = `ClassicCars Studio ${version}`;
        fillAboutLibraries(aboutDialog.querySelector('[data-about="libraries"]'), snap.libraries);
        aboutDialog.querySelector('[data-about="port"]').textContent = snap.phpPort ? String(snap.phpPort) : 'Unavailable';
        const online = snap.apiOnline === true ? 'Online' : snap.apiOnline === false ? 'Offline' : 'Checking';
        const detail = snap.status ? ` · ${snap.status}` : '';
        aboutDialog.querySelector('[data-about="status"]').textContent = `${online}${detail}`;
        if (!aboutDialog.open) aboutDialog.showModal();
    }

    document.addEventListener('pointerdown', (event) => {
        if (!openId || root.contains(event.target)) return;
        closeAll();
    }, true);
    window.addEventListener('keydown', onKey, true);
    window.addEventListener('resize', () => {
        if (!openId) return;
        const menu = menus.find((item) => item.id === openId);
        if (menu && !menu.panel.hidden) placeDrop(menu);
        const host = root.querySelector('.menu-fly:not([hidden])')?.parentElement;
        if (host) placeFlyout(host);
    });
    refresh();
    return {
        refresh,
        close: () => closeAll(),
        isOpen: () => Boolean(openId),
        openAbout,
    };
}

function flagFor(action, arg, snap) {
    if (action === 'save') return { disabled: !snap.canSave };
    if (action === 'close-tab') return { disabled: !snap.canCloseTab };
    if (action === 'reopen-closed-tab') return { disabled: !snap.canReopenClosedTab };
    if (action === 'keep-open') return { disabled: !snap.canKeepOpen };
    if (action === 'save-sync' || action === 'sync-seo') return { disabled: !snap.canSync };
    if (action === 'revert') return { disabled: !snap.canRevert };
    if (action === 'copy') return { disabled: !(snap.canCopy ?? snap.canEdit) };
    if (action === 'select-all') return { disabled: !(snap.canSelectAll ?? snap.canEdit) };
    if (action === 'undo') return { disabled: !(snap.canUndo ?? snap.canEdit) };
    if (action === 'redo') return { disabled: !(snap.canRedo ?? snap.canEdit) };
    if (action === 'undo-history') return { disabled: !snap.canUndoHistory };
    if (action === 'cut' || action === 'paste') {
        return { disabled: !snap.canEdit };
    }
    if (action === 'find') return { disabled: !snap.canFind };
    if (action === 'toggle-comment') return { disabled: !snap.canComment };
    if (action === 'insert-block') return { disabled: !snap.canInsert };
    if (action === 'toggle-tree') return { checked: snap.treeOpen };
    if (action === 'toggle-inspector') return { checked: snap.inspectorOpen };
    if (action === 'toggle-terminal') return { checked: Boolean(snap.terminalOpen) };
    if (action === 'toggle-preview-edit') return { checked: snap.previewEdit, disabled: !snap.canPreviewEdit };
    if (action === 'full-preview') return { checked: snap.previewFull };
    if (action === 'mode-code') return { checked: snap.mode !== 'gallery' };
    if (action === 'mode-gallery') return { disabled: !snap.hasGallery, checked: snap.mode === 'gallery' };
    if (action === 'word-wrap') return { checked: Boolean(snap.wordWrap) };
    if (action === 'shelf-view') return { checked: (snap.leftShelfTab || 'files') === arg };
    if (action === 'viewport') return { checked: snap.viewport === arg };
    if (action === 'theme') return { checked: snap.theme === arg };
    if (action === 'append-unlisted') return { disabled: !(snap.unlisted > 0) };
    if (action === 'convert-jalbum') return { disabled: !snap.isJalbum };
    if (action === 'zoom-in') return { disabled: snap.canZoomIn === false };
    if (action === 'zoom-out') return { disabled: snap.canZoomOut === false };
    if (action === 'zoom-reset') return { disabled: !snap.canResetZoom };
    if (action === 'reload' || action === 'scroll-gallery' || action === 'refresh-preview') return { disabled: !snap.canPreview };
    return {};
}

function buildHelpDialog() {
    const dialog = el('dialog', { class: 'help-dialog', 'aria-labelledby': 'help-dialog-title' });
    const head = el('div', { class: 'dialog-head' });
    head.append(el('h2', { id: 'help-dialog-title', text: 'Keyboard Shortcuts & Workflow' }));
    const close = el('button', { class: 'btn', type: 'button', text: 'Close', title: 'Close the keyboard shortcuts guide' });
    close.addEventListener('click', () => dialog.close());
    head.append(close);
    const body = el('div', { class: 'dialog-body' });
    body.append(el('p', {
        class: 'field-hint',
        text: 'Menus follow the desktop pattern: click a title, hover across the bar to switch, and use the arrow keys, Enter, and Escape.',
    }));
    for (const [title, rows] of SHORTCUT_GROUPS) {
        const group = el('section', { class: 'shortcut-group' });
        group.append(el('h3', { text: title }));
        for (const [keys, label] of rows) {
            const row = el('div', { class: 'shortcut-row' });
            row.append(el('span', { text: label }));
            row.append(el('kbd', { text: keys }));
            group.append(row);
        }
        body.append(group);
    }
    dialog.append(head, body);
    document.body.append(dialog);
    return dialog;
}

function fillAboutLibraries(host, libraries) {
    if (!host) return;
    host.replaceChildren();
    if (!Array.isArray(libraries)) {
        host.append(el('dt', { text: 'Libraries' }), el('dd', { text: 'The studio API did not report library versions.' }));
        return;
    }
    if (!libraries.length) {
        host.append(el('dt', { text: 'Libraries' }), el('dd', { text: 'The studio API reported no library versions.' }));
        return;
    }
    for (const item of libraries) {
        const name = item && typeof item.name === 'string' ? item.name.trim() : '';
        const version = item && typeof item.version === 'string' ? item.version.trim() : '';
        const error = item && typeof item.error === 'string' ? item.error.trim() : '';
        host.append(
            el('dt', { text: name || 'Library' }),
            el('dd', { text: version || error || 'Version was not reported.' }),
        );
    }
}

function buildAboutDialog() {
    const dialog = el('dialog', { class: 'about-dialog', 'aria-labelledby': 'about-dialog-title' });
    const head = el('div', { class: 'dialog-head' });
    head.append(el('h2', { id: 'about-dialog-title', text: 'About ClassicCars Studio' }));
    const close = el('button', { class: 'btn', type: 'button', text: 'Close', title: 'Close About ClassicCars Studio' });
    close.addEventListener('click', () => dialog.close());
    head.append(close);
    const body = el('div', { class: 'dialog-body' });
    body.append(el('p', { class: 'about-version', 'data-about': 'version', text: 'ClassicCars Studio v0.0.x' }));
    body.append(el('p', {
        class: 'field-hint',
        text: 'Local editor for inventory, articles, galleries, and page values on this ClassicCars checkout.',
    }));
    body.append(el('p', { class: 'field-hint', text: 'Libraries' }));
    body.append(el('dl', { class: 'about-dl', 'data-about': 'libraries' }));
    const list = el('dl', { class: 'about-dl' });
    list.append(el('dt', { text: 'PHP preview' }), el('dd', { 'data-about': 'port', text: 'Unavailable' }));
    list.append(el('dt', { text: 'Status' }), el('dd', { 'data-about': 'status', text: 'Checking' }));
    body.append(list);
    dialog.append(head, body);
    document.body.append(dialog);
    return dialog;
}
