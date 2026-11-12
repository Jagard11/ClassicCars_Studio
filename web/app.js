import { attachGallerySource, mountGallery, omitListedSlides, writeThumbnailGallery } from './views/gallery.js';
import { applyStudioConfig, CONFIG_PAGE_PATH, getStudioConfig } from './views/config.js';
import { mountPreviewContext } from './views/preview-context.js';
import { mountPreviewNav } from './views/preview-nav.js';
import { mountPreviewInlineEdit } from './views/preview-inline-edit.js';
import { formatPriceLabel } from './views/details.js';
import { mountCodeEditor } from './views/code-editor.js';
import { mountMacroBuilder } from './views/macro-builder.js';
import { FALLBACK_PRESETS } from './views/macro-engine.js';
import { primeEngineBundle } from './wasm/studio_engine.js';
import { mountWidgets } from './views/widgets.js';
import { sanitizeWidgetOrder } from './views/widget-order.js';
import { mountNewPage } from './views/new-page.js';
import { mountOpenFolder } from './views/open-folder.js';
import { mountMenuBar } from './views/menubar.js';
import { isJalbumPage, mountJalbumConverter } from './views/converters.js';
import { mountTooltips } from './views/tooltip.js';
import { mountZoom } from './views/zoom.js';
import { mountDocTabs } from './views/tabs.js';
import {
    beginClosedTabRestore,
    endClosedTabRestore,
    hasClosedTabs,
    rememberClosedTab,
    takeClosedTab,
} from './views/closed-tabs.js';
import { createTree, jumpMatches, mountOpenEditors } from './views/tree.js';
import { mountLeftShelf } from './views/left-shelf.js';
import { mountProjectSearch } from './views/project-search.js';
import { mountGitPanel } from './views/git-panel.js';
import { mountGitFooter } from './views/git-footer.js';
import { mountTerminal } from './views/terminal.js';
import { createUndoStack, mountUndoHistory } from './views/undo-history.js';
import { mountUpdater } from './views/updater.js';
import { el } from './views/dom.js';
import {
    bootTheme,
    getTheme,
    mountThemeEditor,
    persistLayout,
    rememberTheme,
    replaceTheme,
    setLayoutProvider,
    setPreset,
    subscribeTheme,
    wasLayoutTouched,
    wasThemeTouched,
} from './views/theme-editor.js';

const SECTIONS = [
    { id: 'articles', label: 'articles' },
    { id: 'awards', label: 'awards' },
    { id: 'contact', label: 'contact' },
    { id: 'newsletter', label: 'newsletter' },
    { id: 'parts', label: 'parts' },
    { id: 'projects', label: 'projects' },
    { id: 'sales', label: 'sales' },
    { id: 'video', label: 'video' },
    { id: 'whatsnew', label: 'whatsnew' },
];
const VIEWPORTS = { desktop: 1280, column: 1040, tablet: 768, mobile: 390, fit: 0 };
const SAVE_DELAY = 250;
const EQUALS_ERROR = 'Remove = from captions before saving. Equals signs are reserved by image_data.txt.';

const els = {};
const state = {
    sections: [],
    path: '',
    section: '',
    slug: '',
    doc: null,
    mode: 'code',
    sourceLoaded: false,
    savedContent: '',
    savedImagesSig: '',
    codeDirty: false,
    galleryDirty: false,
    treeQuery: '',
    treeFilter: 'all',
    expandedSections: new Set(),
    expandedFolders: new Set(),
    treeOpen: true,
    inspectorOpen: true,
    treeWidth: 260,
    inspectorWidth: 340,
    centerRatio: 0.58,
    galleryRatio: 0.4,
    viewport: 'desktop',
    viewportMode: 'view',
    inlineTouched: false,
    inlineNeedsValues: false,
    density: 'standard',
    previewFull: false,
    saveState: 'saved',
    saveDetail: '',
    version: '',
    phpPort: '',
    libraries: null,
    repoRoot: '',
    apiOnline: null,
    treeMessage: 'Checking studio API…',
    treeSource: '',
    tabs: [],
    openEditorsOpen: true,
    openEditorsHeight: 160,
    fileTabbarHeight: 35,
    leftShelfTab: 'files',
    terminalOpen: false,
    terminalHeight: 240,
    treeClipboard: null,
    treeUndo: { canUndo: false, canRedo: false, undoLabel: '', redoLabel: '' },
};
let codeApi = null;
let galleryApi = null;
let inlineApi = null;
let widgetsApi = null;
let treeApi = null;
let tabsApi = null;
let editorsApi = null;
let leftShelfApi = null;
let projectSearchApi = null;
let gitPanelApi = null;
let gitFooterApi = null;
let terminalApi = null;
let undoStack = null;
let undoHistoryApi = null;
let revertPendingPath = '';
let saveTimer = 0;
let saveEpoch = 0;
let stageRev = 0;
let previewStaged = false;
let saveReload = true;
let savePageValues = true;
let saveAbort = null;
let sessionWrite = Promise.resolve();
let sessionTimer = 0;
let sessionPayload = null;
let sessionChrome = null;
let sessionChromeApplied = false;
let panelTouched = false;
let editorsSized = false;
let widgetsTouched = false;
let openToken = 0;
let loadingPath = '';
let previewRestore = null;
let started = false;
let jumpDialog = null;
let jumpInput = null;
let jumpResults = null;
let jumpList = [];
let jumpActive = 0;
let menuApi = null;
let converterApi = null;
let previewNav = null;
let newPageApi = null;
let openFolderApi = null;
let themeApi = null;
let zoomApi = null;
let updaterApi = null;
let macroBuilderApi = null;
let projectMacros = [];
let macroPresets = FALLBACK_PRESETS;
let showBuiltinWidgets = true;
let folderChord = 0;
let editTarget = null;
const RECENT_KEY = 'studio.recentPages';
const LAST_PAGE_KEY = 'studio.lastPage';
const OPEN_TABS_KEY = 'studio.openTabs';
const OPEN_TABS_SET_KEY = 'studio.openTabsSet';
const OPEN_EDITORS_KEY = 'studio.openEditorsOpen';
const OPEN_EDITORS_HEIGHT_KEY = 'studio.openEditorsHeight';
const FILE_TABBAR_HEIGHT_KEY = 'studio.fileTabbarHeight';
const OPEN_EDITORS_HEIGHT_MIN = 28;
const OPEN_EDITORS_HEIGHT_MAX = 720;
const OPEN_EDITORS_HEIGHT_DEFAULT = 160;
const FILE_TABBAR_HEIGHT_MIN = 35;
const FILE_TABBAR_HEIGHT_MAX = 320;
const FILE_TABBAR_HEIGHT_DEFAULT = 35;
const TERMINAL_OPEN_KEY = 'studio.terminalOpen';
const TERMINAL_HEIGHT_KEY = 'studio.terminalHeight';
const TERMINAL_HEIGHT_MIN = 110;
const TERMINAL_HEIGHT_MAX = 640;
const TERMINAL_HEIGHT_DEFAULT = 240;
const RIGHT_PANEL_KEY = 'studio.rightPanelState';
const LANDING_PAGE = '/sales/index.php';
const DARK_PREVIEW_DOC = '<!DOCTYPE html><html><head><meta name="color-scheme" content="dark"><style>html,body{margin:0;background:#000;color-scheme:dark}</style></head><body></body></html>';

function editorFacts(extra = {}) {
    const folders = [];
    for (const section of state.sections || []) {
        for (const folder of section.folders || []) {
            if (!folder?.slug) continue;
            folders.push({
                section: section.id,
                slug: folder.slug,
                name: folder.name || folder.slug,
                path: folder.primaryFile || '',
            });
        }
    }
    const path = extra.path != null ? extra.path : state.path;
    const same = normalizePath(path || '') === normalizePath(state.path || '');
    const doc = same ? state.doc : null;
    const images = Array.isArray(extra.images) ? extra.images : (Array.isArray(doc?.images) ? doc.images : []);
    const html = typeof extra.content === 'string' ? extra.content : (typeof doc?.content === 'string' ? doc.content : '');
    const unlistedSlides = omitListedSlides(
        Array.isArray(extra.unlistedSlides) ? extra.unlistedSlides : (Array.isArray(doc?.unlistedSlides) ? doc.unlistedSlides : []),
        images,
        html,
    );
    const baselineContent = typeof extra.baselineContent === 'string'
        ? extra.baselineContent
        : (typeof doc?.gitContent === 'string'
            ? doc.gitContent
            : (typeof doc?.initialContent === 'string'
                ? doc.initialContent
                : (same ? (state.savedContent || '') : undefined)));
    return {
        path: path || '',
        section: extra.section != null ? extra.section : (state.section || ''),
        slug: extra.slug != null ? extra.slug : (same ? (state.slug || '') : ''),
        editable: extra.editable,
        message: extra.message || '',
        images,
        unlistedSlides,
        folders,
        media: extra.media !== undefined ? extra.media : (mediaDoc(doc, path)?.isMedia ? mediaFacts(mediaDoc(doc, path)) : null),
        baselineContent,
    };
}

function mediaDoc(doc, path) {
    if (!doc || normalizePath(doc.path || '') !== normalizePath(path || '')) return null;
    return doc;
}

function mediaFacts(doc) {
    if (!doc?.isMedia) return null;
    return {
        isMedia: true,
        path: doc.path || '',
        fileType: doc.fileType || '',
        mediaKind: doc.mediaKind || '',
        fileSize: Number(doc.fileSize) || 0,
        width: Number(doc.width) || 0,
        height: Number(doc.height) || 0,
    };
}

function init() {
    if (started || typeof document === 'undefined') return;
    started = true;
    mountTooltips();
    cacheElements();
    const stored = bootTheme();
    setLayoutProvider(currentLayout);
    try { state.openEditorsOpen = localStorage.getItem(OPEN_EDITORS_KEY) !== '0'; } catch { state.openEditorsOpen = true; }
    treeApi = createTree(els.treeRoot, {
        onOpen: (path, detail = {}) => { void openPath(path, { pin: Boolean(detail.pin) }); },
        onToggleSection(id) {
            if (state.expandedSections.has(id)) state.expandedSections.delete(id);
            else state.expandedSections.add(id);
            renderTree({ reveal: false });
        },
        onToggleFolder(key) {
            if (state.expandedFolders.has(key)) state.expandedFolders.delete(key);
            else state.expandedFolders.add(key);
            renderTree({ reveal: false });
        },
        onRename: (payload) => handleTreeRename(payload),
        onDuplicate: (info) => { void handleTreeDuplicate(info); },
        onCut: (info) => handleTreeCut(info),
        onCopy: (info) => handleTreeCopy(info),
        onPaste: (info) => { void handleTreePaste(info); },
        onDelete: (info) => { void handleTreeDelete(info); },
        onNewPage: (info) => handleTreeNewPage(info),
        onNewFolder: (info) => { void handleTreeNewFolder(info); },
        onUndo: () => { void handleTreeUndo(); },
        onRedo: () => { void handleTreeRedo(); },
        onClearCut: () => clearTreeCut(),
        getOpsState: () => ({
            clipboard: state.treeClipboard,
            canUndo: state.treeUndo.canUndo,
            canRedo: state.treeUndo.canRedo,
            undoLabel: state.treeUndo.undoLabel,
            redoLabel: state.treeUndo.redoLabel,
        }),
        onAnnounce: (msg) => announce(msg),
    });
    tabsApi = mountDocTabs(els.docTabs, {
        onActivate(path) { void openPath(path); },
        onPin(path) { pinTab(path); },
        onClose(path) { void closeTab(path); },
        onCloseOthers(path) { void closeOtherTabs(path); },
        onCloseSaved() { void closeSavedTabs(); },
        onCloseAll() { void closeAllTabs(); },
    });
    editorsApi = mountOpenEditors(els.openEditors, {
        onToggle() {
            state.openEditorsOpen = !state.openEditorsOpen;
            try { localStorage.setItem(OPEN_EDITORS_KEY, state.openEditorsOpen ? '1' : '0'); } catch { /* private mode */ }
            refreshEditors();
        },
        onActivate(path) { void openPath(path); },
        onPin(path) { pinTab(path); },
        onClose(path) { void closeTab(path); },
        onCloseAll() { void closeAllTabs(); },
    });
    leftShelfApi = mountLeftShelf({
        panel: els.tree,
        tabBar: els.shelfTabs,
        panes: {
            files: els.shelfPaneFiles,
            search: els.shelfPaneSearch,
            git: els.shelfPaneGit,
        },
        initialTab: state.leftShelfTab,
        onChange(tab) {
            state.leftShelfTab = tab;
            rememberPanelState();
            syncMenu();
            if (tab === 'search') projectSearchApi?.focus?.(false);
            else if (tab === 'git') void gitPanelApi?.refresh?.();
        },
    });
    projectSearchApi = mountProjectSearch(els.shelfPaneSearch, {
        apiSend,
        confirmDialog,
        announce,
        async onOpenMatch(path, match) {
            await openPath(path, { pin: true, mode: 'code' });
            setMode('code');
            if (match && Number.isFinite(match.startOffset)) {
                codeApi?.revealRange(
                    match.startOffset,
                    Number.isFinite(match.endOffset) ? match.endOffset : match.startOffset,
                    match.matchText || `Line ${match.line || 1}`,
                );
            }
        },
        async onFilesModified(modifiedPaths) {
            await syncAfterExternalChanges(modifiedPaths);
        },
        onMatchCountChange(count) {
            leftShelfApi?.setSearchBadge(count);
        },
    });
    gitPanelApi = mountGitPanel(els.shelfPaneGit, {
        apiSend,
        confirmDialog,
        announce,
        onOpenFile(path) {
            void openPath(path, { pin: true });
        },
        async onFilesChanged(reloadPaths) {
            await syncAfterExternalChanges(reloadPaths);
        },
        onStatusChange(count, statusData) {
            leftShelfApi?.setGitBadge(count);
            if (statusData) gitFooterApi?.applyStatus?.(statusData);
        },
    });
    gitFooterApi = mountGitFooter(els.gitFooter, {
        apiSend,
        announce,
        onOpenGitPanel() {
            if (!state.treeOpen) setTreeOpen(true);
            setLeftShelfTab('git', { focus: true });
        },
        onStatusUpdated(statusData) {
            gitPanelApi?.applyStatus?.(statusData);
        },
        async onFilesChanged(reloadPaths) {
            await syncAfterExternalChanges(reloadPaths);
        },
    });
    terminalApi = mountTerminal(els.terminalPanel, {
        apiSend,
        getRepoRoot: () => state.repoRoot,
        getKnownPaths: () => jumpMatches(state.sections, '').map((m) => m.path),
        onOpenFile(path) {
            void openPath(path, { pin: true });
        },
        onClose() {
            setTerminalOpen(false, { restoreFocus: true });
        },
        onCommandComplete() {
            void gitPanelApi?.refresh?.();
        },
        announce,
    });
    undoStack = createUndoStack({
        onChange() {
            syncUndoUi();
            undoHistoryApi?.refresh();
        },
    });
    undoHistoryApi = mountUndoHistory({
        getActivePath: () => state.path,
        getOpenFiles: () => state.tabs.map((tab) => ({
            path: tab.path,
            label: tabLabel(tab.path),
        })),
        getTrackedPaths: () => undoStack?.listDocumentPaths?.() || [],
        getTimeline: (path) => undoStack?.getTimeline?.(path || state.path),
        onUndo: (path) => { void performUndo(path); },
        onRedo: (path) => { void performRedo(path); },
        onJumpTo: (path, index) => { void performJumpToHistory(path, index); },
        onClear: (path) => {
            const target = normalizePath(path || state.path);
            if (!target) return;
            undoStack?.clearHistory(target);
            announce('Cleared undo history for this file.');
        },
    });
    codeApi = mountCodeEditor(els.codeMount, {
        onEdit: noteCodeEdit,
        onShowCode: () => setMode('code'),
        onUndo: () => { void performUndo(); },
        onRedo: () => { void performRedo(); },
        onOpenHistory: () => openUndoHistory(),
        onCaretChange(caret) {
            if (state.path) undoStack?.updateCaret(state.path, caret, state.mode);
        },
    });
    galleryApi = mountGallery(els.galleryMount, els.gallerySheet, galleryContext());
    previewNav = mountPreviewNav({
        frame: els.frame,
        getViewportMode: () => state.viewportMode,
        getPageMode: () => state.mode,
        getEditorPath: () => state.path,
        getEditorPreviewUrl: () => state.doc?.previewUrl || '',
        normalizePath,
        prepareUrl(cleanPath, filePath) {
            const same = filePath && normalizePath(filePath) === normalizePath(state.path);
            const raw = same ? decoratePreview(cleanPath) : stripDraftQuery(cleanPath);
            return encodePreview(raw);
        },
        previewHtml(path) {
            if (!state.doc?.isMedia) return '';
            if (normalizePath(path) !== normalizePath(state.doc.path || state.doc.previewUrl || '')) return '';
            return mediaPreviewHtml(state.doc);
        },
        onOpenInEditor(path) {
            void openPath(path, { pin: true });
        },
    });
    mountPreviewContext({
        frame: els.frame,
        stage: els.stage,
        getSource: () => (state.sourceLoaded ? codeApi.getValue() : ''),
        getFileType: () => fileType(fileName(state.path)),
        getHasGallery: () => Boolean(state.doc?.hasGallery),
        getCanGoBack: () => Boolean(previewNav?.canGoBack()),
        getCanGoForward: () => Boolean(previewNav?.canGoForward()),
        onBack() { previewNav?.goBack(); },
        onForward() { previewNav?.goForward(); },
        onJump(range) {
            const ok = codeApi.revealRange(range.startOffset, range.endOffset, range.label);
            if (!ok) announce('Open a page with source before jumping to a code block.');
        },
        onSelectPhoto(name) {
            if (!state.doc?.hasGallery) {
                announce('This page has no gallery.');
                return;
            }
            setMode('gallery');
            requestAnimationFrame(() => {
                const ok = galleryApi.selectByName(name);
                announce(ok
                    ? `Selected ${name} in Gallery Order & Captions.`
                    : `${name} is not in this gallery.`);
            });
        },
        onReloadPreview() {
            refreshPreview(true);
        },
    });
    inlineApi = mountPreviewInlineEdit({
        frame: els.frame,
        stage: els.stage,
        root: els.viewportMode,
        viewButton: els.viewportView,
        editButton: els.viewportEdit,
        getSource: () => (state.sourceLoaded ? codeApi.getValue() : ''),
        getPath: () => state.path,
        getFileType: () => fileType(fileName(state.path)),
        listPhotos: () => (state.doc?.images || []).map((image) => ({
            name: image?.name || '',
            caption: image?.caption || '',
            slideUrl: image?.slideUrl || '',
            thumbUrl: image?.thumbUrl || '',
        })),
        onHighlight(range, meta = {}) {
            if (!range) {
                codeApi.clearHighlight();
                return;
            }
            codeApi.highlightRange(range.startOffset, range.endOffset, range.label, {
                scroll: meta.scroll !== false,
            });
        },
        onSource(next, meta = {}) {
            applyInlineSource(next, meta);
        },
        onPhotoCaption(update) {
            applyInlineCaption(update);
        },
        onFlush() {
            flushQuiet();
        },
        onSave() {
            void saveCurrent();
        },
        onSaveSync() {
            void saveAndSync();
        },
        onPreviewReload() {
            void refreshPreview(true);
        },
        onCommand(key, event) {
            return runShortcut(key, event);
        },
        onEditTarget(node) {
            rememberEditTarget(node);
        },
        onMode(mode) {
            leaveOrEnterViewport(mode);
        },
        onAnnounce: announce,
    });
    inlineApi.setEnabled(false);
    widgetsApi = mountWidgets(els.widgetMount, {
        onApply: (widget, data) => applyWidget(widget, data),
        onApplyMacroSource: (result, macro, mode) => applyMacroSourceResult(result, macro, mode),
        onOpenMacroBuilder: (opts = {}) => macroBuilderApi?.open?.(opts),
        onOpenGallery() {
            setMode('gallery');
            announce('Opened the gallery editor.');
        },
        onJump(range) {
            setMode('code');
            const ok = codeApi.revealRange(range.startOffset, range.endOffset, range.label);
            if (!ok) announce('Open a page with source before jumping to that comment.');
        },
        onAnnounce: announce,
        getSource: () => (state.sourceLoaded ? codeApi.getValue() : ''),
        getCursor: () => codeApi.getCaret(),
        getMakes: collectMakes,
        initialCollapsed: readRightPanelLocal().widgetCollapsed,
        initialOrder: readRightPanelLocal().widgetOrder,
        onWidgetState() {
            widgetsTouched = true;
            rememberPanelState();
        },
    });
    macroBuilderApi = mountMacroBuilder({
        getMacros: () => projectMacros,
        getPresets: () => macroPresets,
        getShowBuiltin: () => showBuiltinWidgets,
        getProjectRoot: () => state.repoRoot,
        onSaveMacros: (payload) => saveProjectMacros(payload),
        onZoom(direction) {
            if (direction > 0) zoomApi?.zoomIn({ panel: 'macro' });
            else if (direction < 0) zoomApi?.zoomOut({ panel: 'macro' });
            else zoomApi?.resetZoom({ panel: 'macro' });
        },
        getMacroZoom: () => zoomApi?.getZoom?.('macro') || 1,
        zoomFlags() {
            return {
                canZoomIn: zoomApi ? zoomApi.canZoomIn('macro') : true,
                canZoomOut: zoomApi ? zoomApi.canZoomOut('macro') : true,
                canReset: Boolean(zoomApi?.canResetZoom?.('macro')),
            };
        },
    });
    newPageApi = mountNewPage({
        button: els.newPage,
        apiSend,
        announce,
        exists: folderExists,
        getSections: () => state.sections,
        onCreated: (result) => onCreated(result),
    });
    openFolderApi = mountOpenFolder({
        apiSend,
        getRoot: () => state.repoRoot,
        confirmDiscard: confirmSwitchProject,
        onOpened: (data) => useProjectRoot(data),
    });
    themeApi = mountThemeEditor(els.customize);
    if (stored?.layout) applyLayout(stored.layout, { silent: true });
    else {
        setTreeOpen(document.documentElement.dataset.tree !== 'closed', { silent: true });
        setInspectorOpen(document.documentElement.dataset.inspector !== 'closed', { silent: true });
        setCenterRatio(state.centerRatio);
    }
    applyLocalPanel(readRightPanelLocal());
    els.theme.value = getTheme().preset;
    els.customize.classList.toggle('has-custom', Object.keys(getTheme().custom).length > 0);
    subscribeTheme((theme) => {
        if (els.theme.value !== theme.preset) els.theme.value = theme.preset;
        els.customize.classList.toggle('has-custom', Object.keys(theme.custom).length > 0);
        syncMenu();
    });
    codeApi.setContext(editorFacts({ editable: false, message: 'Loading page…', images: [], unlistedSlides: [] }));
    menuApi = mountMenuBar(document.getElementById('menu-bar'), {
        snapshot: menuSnapshot,
        perform(action, arg) { performMenu(action, arg); },
    });
    converterApi = mountJalbumConverter({
        context: jalbumContext,
        onApply: applyConvertedPage,
    });
    updaterApi = mountUpdater({
        apiSend,
        announce,
    });
    zoomApi = mountZoom({
        frame: els.frame,
        scaleBadge: els.scale,
        onZoomChange(zoom, meta = {}) {
            if (meta.panel === 'macro') macroBuilderApi?.syncZoom?.(zoom);
            if (!meta.panel || meta.panel === 'preview') layoutPreview();
            syncMenu();
            if (meta.announce) {
                const label = meta.label ? `${meta.label} zoom` : 'Zoom';
                announce(`${label} ${Math.round(zoom * 100)}%.`);
            }
        },
        onActivePanelChange() {
            syncMenu();
        },
    });
    bindChrome();
    buildJumpDialog();
    const hash = parseHash();
    state.mode = hash.mode === 'gallery' ? 'gallery' : 'code';
    setMode(state.mode, { silent: true });
    refreshEditors();
    void bootDocument(hash);
    void loadHealth();
    void loadProjectMacros();
    void loadRemoteTheme();
    void gitPanelApi?.refresh?.();
    new ResizeObserver(() => layoutPreview()).observe(els.stage);
    layoutPreview();
}

function cacheElements() {
    els.treeToggle = document.getElementById('tree-toggle');
    els.shelfTabs = document.getElementById('left-shelf-tabs');
    els.shelfPaneFiles = document.getElementById('shelf-pane-files');
    els.shelfPaneSearch = document.getElementById('shelf-pane-search');
    els.shelfPaneGit = document.getElementById('shelf-pane-git');
    els.terminalToggle = document.getElementById('terminal-toggle');
    els.terminalPanel = document.getElementById('terminal-panel');
    els.terminalResizer = document.getElementById('terminal-resizer');
    els.inspectorToggle = document.getElementById('inspector-toggle');
    els.newPage = document.getElementById('new-page');
    els.jump = document.getElementById('jump-open');
    els.crumb = document.getElementById('crumb');
    els.save = document.getElementById('save-status-pill') || document.getElementById('save-pill');
    els.saveNow = document.getElementById('btn-save') || document.getElementById('save-now');
    els.codeTab = document.getElementById('mode-code');
    els.revert = document.getElementById('revert-btn');
    els.undoHistoryBtn = document.getElementById('undo-history-btn');
    els.undoHistoryBadge = document.getElementById('undo-history-badge');
    els.theme = document.getElementById('theme-select');
    els.customize = document.getElementById('theme-customize');
    els.version = document.getElementById('version-badge');
    els.treeClear = document.getElementById('tree-search-clear');
    els.tree = document.getElementById('tree-panel');
    els.treeResizer = document.getElementById('tree-resizer');
    els.treeSearch = document.getElementById('tree-search');
    els.treeRoot = document.getElementById('tree-root');
    els.gitFooter = document.getElementById('statusbar');
    els.filters = [...document.querySelectorAll('#tree-panel [data-filter]')];
    els.treeNewFolder = document.getElementById('tree-new-folder');
    els.center = document.getElementById('center-stack');
    els.docTabs = document.getElementById('doc-tabs');
    els.fileTabbarResizer = document.getElementById('file-tabbar-resizer');
    els.openEditors = document.getElementById('open-editors');
    els.openEditorsList = document.getElementById('open-editors-list');
    els.openEditorsResizer = document.getElementById('open-editors-resizer');
    els.editor = document.getElementById('editor-pane');
    els.preview = document.getElementById('preview-pane');
    els.hDivider = document.getElementById('h-divider');
    els.modeTabs = [...document.querySelectorAll('.mode-bar [data-mode]')];
    els.viewportMode = document.getElementById('viewport-mode');
    els.viewportView = document.getElementById('viewport-mode-view');
    els.viewportEdit = document.getElementById('viewport-mode-edit');
    els.galleryTab = document.getElementById('mode-gallery');
    els.galleryCount = document.getElementById('gallery-count');
    els.gallerySheet = document.getElementById('gallery-sheet');
    els.previewBar = document.getElementById('preview-bar');
    els.editorStatus = document.getElementById('editor-status');
    els.editorStatusText = document.getElementById('editor-status-text');
    els.editorRetry = document.getElementById('editor-retry');
    els.codeMount = document.getElementById('code-mount');
    els.galleryMount = document.getElementById('gallery-mount');
    els.inspector = document.getElementById('inspector-panel');
    els.inspectorResizer = document.getElementById('inspector-resizer');
    els.widgetMount = document.getElementById('widget-mount');
    els.viewport = document.getElementById('viewport');
    els.scale = document.getElementById('scale-badge');
    els.scrollGallery = document.getElementById('scroll-gallery');
    els.refresh = document.getElementById('btn-refresh-preview');
    els.full = document.getElementById('preview-full');
    els.stage = document.getElementById('preview-stage');
    els.frame = document.getElementById('preview-frame');
    els.live = document.getElementById('live');
}

function bindChrome() {
    document.addEventListener('contextmenu', (event) => {
        if (keepsNativeContextMenu(event.target)) return;
        event.preventDefault();
    }, true);
    els.treeToggle.addEventListener('click', () => setTreeOpen(!state.treeOpen));
    els.terminalToggle?.addEventListener('click', () => toggleTerminal());
    els.inspectorToggle.addEventListener('click', () => setInspectorOpen(!state.inspectorOpen));
    els.jump.addEventListener('click', () => { void openJump(); });
    els.treeSearch.addEventListener('input', () => {
        state.treeQuery = els.treeSearch.value;
        syncTreeClear();
        renderTree({ reveal: false });
    });
    els.treeClear?.addEventListener('click', () => {
        els.treeSearch.value = '';
        state.treeQuery = '';
        syncTreeClear();
        renderTree({ reveal: false });
        els.treeSearch.focus();
    });
    els.version?.addEventListener('click', () => menuApi?.openAbout());
    syncTreeClear();
    els.filters.forEach((button) => {
        button.addEventListener('click', () => {
            state.treeFilter = button.dataset.filter;
            for (const other of els.filters) other.setAttribute('aria-pressed', other === button ? 'true' : 'false');
            renderTree({ reveal: false });
        });
    });
    els.treeNewFolder?.addEventListener('click', () => {
        const selected = treeApi?.getSelectedRowInfo?.();
        void handleTreeNewFolder(selected || { kind: 'section', section: '/', slug: '', path: '/' });
    });
    els.modeTabs.forEach((tab) => {
        tab.addEventListener('click', () => setMode(tab.dataset.mode));
        tab.addEventListener('keydown', (event) => {
            const visible = els.modeTabs.filter((item) => !item.hidden);
            const current = visible.indexOf(tab);
            if (event.key !== 'ArrowRight' && event.key !== 'ArrowLeft') return;
            if (current < 0) return;
            event.preventDefault();
            const next = event.key === 'ArrowRight' ? (current + 1) % visible.length : (current + visible.length - 1) % visible.length;
            visible[next].focus();
            setMode(visible[next].dataset.mode);
        });
    });
    els.saveNow.addEventListener('click', () => { void onSaveClick(); });
    els.revert.addEventListener('click', () => { void revertFile(); });
    els.undoHistoryBtn?.addEventListener('click', () => openUndoHistory());
    els.editorRetry.addEventListener('click', () => {
        if (state.path) void openPath(state.path, { force: true });
        else void loadTree();
    });
    els.theme.addEventListener('change', () => {
        if (els.theme.value === 'customize') {
            els.theme.value = getTheme().preset;
            els.customize.click();
            return;
        }
        setPreset(els.theme.value);
    });
    els.viewport.addEventListener('change', () => setViewportSize(els.viewport.value));
    els.scrollGallery.addEventListener('click', scrollPreviewToGallery);
    els.refresh.addEventListener('click', () => { void refreshPreview(true); });
    els.full.addEventListener('click', () => setPreviewFull(!state.previewFull));
    els.frame.addEventListener('load', () => {
        bindPreviewFrameReloadKeys();
        const parked = state.mode === 'gallery' || (els.frame.getAttribute('src') || '') === 'about:blank';
        els.scrollGallery.disabled = parked || !state.doc?.previewUrl;
        els.refresh.disabled = parked || !(state.doc?.previewUrl || previewNav?.currentPath());
        if (parked) return;
        const snap = previewRestore;
        previewRestore = null;
        if (snap) {
            restorePreview(snap);
            requestAnimationFrame(() => restorePreview(snap));
            window.setTimeout(() => restorePreview(snap), 50);
        }
        layoutPreview();
        try {
            els.frame.contentWindow.addEventListener('pointerdown', () => menuApi?.close());
        } catch { /* preview is not same-origin yet */ }
    });
    bindPreviewFrameReloadKeys();
    document.addEventListener('focusin', (event) => rememberEditTarget(event.target));
    bindResizer(els.treeResizer, {
        onMove: (event) => {
            if (event.key) {
                if (event.key === 'ArrowUp' || event.key === 'ArrowDown') return;
                const delta = (event.shiftKey ? 32 : 16) * (event.key === 'ArrowRight' ? 1 : -1);
                if (!state.treeOpen) {
                    if (delta > 0) {
                        setTreeOpen(true, { silent: true });
                        setTreeWidth(Math.max(120, state.treeWidth || 260));
                    }
                    return;
                }
                if (state.treeWidth + delta < 96) {
                    setTreeOpen(false, { silent: true });
                    els.treeResizer?.setAttribute('aria-valuenow', '0');
                    return;
                }
                setTreeWidth(state.treeWidth + delta);
                return;
            }
            const leftEdge = (els.tree.parentElement || els.tree).getBoundingClientRect().left;
            const raw = event.clientX - leftEdge;
            if (raw <= 48) {
                if (state.treeOpen) setTreeOpen(false, { silent: true });
                els.treeResizer?.setAttribute('aria-valuenow', '0');
                return;
            }
            if (!state.treeOpen) setTreeOpen(true, { silent: true });
            setTreeWidth(raw);
        },
        onReset: () => {
            if (!state.treeOpen) setTreeOpen(true, { silent: true });
            setTreeWidth(260);
        },
        onCommit: () => rememberPanelState(),
    });
    bindResizer(els.inspectorResizer, {
        onMove: (event) => {
            if (event.key) {
                if (event.key === 'ArrowUp' || event.key === 'ArrowDown') return;
                const delta = (event.shiftKey ? 32 : 16) * (event.key === 'ArrowLeft' ? 1 : -1);
                if (!state.inspectorOpen) {
                    if (delta > 0) {
                        setInspectorOpen(true, { silent: true });
                        setInspectorWidth(Math.max(160, state.inspectorWidth || 340));
                    }
                    return;
                }
                if (state.inspectorWidth + delta < 120) {
                    setInspectorOpen(false, { silent: true });
                    els.inspectorResizer?.setAttribute('aria-valuenow', '0');
                    return;
                }
                setInspectorWidth(state.inspectorWidth + delta);
                return;
            }
            const rightEdge = (els.inspector.parentElement || els.inspector).getBoundingClientRect().right;
            const raw = rightEdge - event.clientX;
            if (raw <= 48) {
                if (state.inspectorOpen) setInspectorOpen(false, { silent: true });
                els.inspectorResizer?.setAttribute('aria-valuenow', '0');
                return;
            }
            if (!state.inspectorOpen) setInspectorOpen(true, { silent: true });
            setInspectorWidth(raw);
        },
        onReset: () => {
            if (!state.inspectorOpen) setInspectorOpen(true, { silent: true });
            setInspectorWidth(340);
        },
        onCommit: () => rememberPanelState(),
    });
    bindResizer(els.hDivider, {
        row: true,
        onMove: (event) => {
            const rect = els.center.getBoundingClientRect();
            if (event.key) {
                const delta = (event.shiftKey ? 0.08 : 0.02) * (event.key === 'ArrowUp' || event.key === 'ArrowLeft' ? 1 : -1);
                if (!['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight'].includes(event.key)) return;
                setCenterRatio(shownRatio() + delta);
            } else {
                const termH = state.terminalOpen && els.terminalPanel
                    ? (els.terminalPanel.offsetHeight + (els.terminalResizer?.offsetHeight || 0))
                    : 0;
                const availH = Math.max(1, rect.height - termH);
                setCenterRatio((event.clientY - rect.top) / availH);
            }
            layoutPreview();
        },
        onReset: () => setCenterRatio(state.mode === 'gallery' ? 0.4 : 0.58),
    });
    bindResizer(els.terminalResizer, {
        row: true,
        onMove: (event) => {
            editorsSized = true;
            if (event.key) {
                if (event.key !== 'ArrowUp' && event.key !== 'ArrowDown') return;
                const delta = (event.shiftKey ? 48 : 16) * (event.key === 'ArrowUp' ? 1 : -1);
                setTerminalHeight(state.terminalHeight + delta);
                return;
            }
            const bottom = els.center?.getBoundingClientRect().bottom || window.innerHeight;
            setTerminalHeight(bottom - event.clientY);
        },
        onReset: () => {
            editorsSized = true;
            setTerminalHeight(TERMINAL_HEIGHT_DEFAULT);
        },
        onCommit: () => rememberPanelState(),
    });
    bindResizer(els.openEditorsResizer, {
        row: true,
        onMove: (event) => {
            editorsSized = true;
            if (event.key) {
                if (event.key !== 'ArrowUp' && event.key !== 'ArrowDown') return;
                const delta = (event.shiftKey ? 48 : 16) * (event.key === 'ArrowDown' ? 1 : -1);
                setOpenEditorsHeight(state.openEditorsHeight + delta);
                return;
            }
            const listTop = els.openEditorsList?.getBoundingClientRect().top;
            const sectionTop = els.openEditors?.getBoundingClientRect().top;
            const top = Number.isFinite(listTop) ? listTop : (Number.isFinite(sectionTop) ? sectionTop : 0);
            setOpenEditorsHeight(event.clientY - top);
        },
        onReset: () => {
            editorsSized = true;
            setOpenEditorsHeight(OPEN_EDITORS_HEIGHT_DEFAULT);
        },
        onCommit: () => rememberPanelState(),
    });
    bindResizer(els.fileTabbarResizer, {
        row: true,
        onMove: (event) => {
            editorsSized = true;
            if (event.key) {
                if (event.key !== 'ArrowUp' && event.key !== 'ArrowDown') return;
                const delta = (event.shiftKey ? 32 : 16) * (event.key === 'ArrowDown' ? 1 : -1);
                setFileTabbarHeight(state.fileTabbarHeight + delta);
                return;
            }
            const top = els.docTabs?.getBoundingClientRect().top;
            setFileTabbarHeight(event.clientY - (Number.isFinite(top) ? top : 0));
        },
        onReset: () => {
            editorsSized = true;
            setFileTabbarHeight(FILE_TABBAR_HEIGHT_DEFAULT);
        },
        onCommit: () => rememberPanelState(),
    });
    window.addEventListener('resize', () => {
        paintOpenEditorsHeight();
        paintFileTabbarHeight();
        paintTerminalHeight();
    });
    window.addEventListener('hashchange', () => { void followHash(); });
    window.addEventListener('beforeunload', (event) => {
        flushSession();
        if (!anyDirty()) return;
        event.preventDefault();
        event.returnValue = '';
    });
    window.addEventListener('keydown', onPreviewReloadKey, true);
    document.addEventListener('keydown', (event) => {
        if (isPreviewReloadChord(event)) {
            onPreviewReloadKey(event);
            return;
        }
        const key = event.key.toLowerCase();
        const mod = event.ctrlKey || event.metaKey;
        if (mod && !event.altKey && (key === '`' || key === '~' || event.code === 'Backquote')) {
            if (!document.querySelector('dialog[open]')) {
                event.preventDefault();
                event.stopPropagation();
                if (!event.repeat) {
                    menuApi?.close();
                    toggleTerminal();
                }
                return;
            }
        }
        if (event.altKey && event.key === 'F4' && !mod) {
            event.preventDefault();
            void exitStudio();
            return;
        }
        if (!event.repeat && !event.isComposing && event.altKey && !event.shiftKey && !mod && (key === 'z' || event.code === 'KeyZ')) {
            if (document.querySelector('dialog[open]')) return;
            event.preventDefault();
            event.stopPropagation();
            menuApi?.close();
            toggleCodeWordWrap();
            return;
        }
        if (menuApi?.isOpen()) return;
        if (event.key === 'Escape' && codeApi?.isFindOpen() && !document.querySelector('dialog[open]')) {
            event.preventDefault();
            codeApi.closeFind();
            return;
        }
        if (event.key === 'Escape' && state.previewFull && !document.querySelector('dialog[open]')) {
            event.preventDefault();
            setPreviewFull(false);
            return;
        }
        if (event.key === 'F2' && !mod && !event.altKey && !event.shiftKey && !document.querySelector('dialog[open]')) {
            const active = document.activeElement;
            if (!isAuxiliaryInput(active) || els.treeRoot?.contains(active)) {
                if (state.treeOpen && state.leftShelfTab === 'files' && treeApi?.startRenameForSelection?.()) {
                    event.preventDefault();
                    return;
                }
            }
        }
        if (!mod || event.altKey) return;
        if (key === 'o' && event.shiftKey) {
            folderChord = 0;
            if (runShortcut('o', event)) event.preventDefault();
            return;
        }
        if (key === 'o' && folderChord && Date.now() - folderChord < 1500) {
            event.preventDefault();
            folderChord = 0;
            menuApi?.close();
            const openDialog = document.querySelector('dialog[open]');
            if (openDialog && !openDialog.classList.contains('open-folder-dialog')) openDialog.close();
            openFolderApi?.open();
            return;
        }
        if (key === 'k' && !event.shiftKey) folderChord = Date.now();
        else folderChord = 0;
        if (runShortcut(key, event)) event.preventDefault();
    });
}

function galleryContext() {
    return {
        getDoc: () => state.doc,
        vehicleName() {
            return state.doc?.details?.vehicleName || state.doc?.widgets?.pageValues?.title || '';
        },
        density: () => state.density,
        onDensity(value) {
            state.density = value;
            zoomApi?.refresh?.();
            persistLayout();
        },
        onGalleryEdit(meta) {
            noteGalleryEdit(meta);
        },
        onUndo() {
            void performUndo();
        },
        onRedo() {
            void performRedo();
        },
        onOpenHistory() {
            openUndoHistory();
        },
        onStructureChange() {
            const folder = findFolder(state.section, state.slug);
            if (folder) {
                folder.photoCount = galleryApi.imageCount();
                folder.unlistedCount = galleryApi.unlistedCount();
            }
            updateGalleryTab();
            renderTree({ reveal: false });
        },
        announce,
    };
}

function savedPagePath(path) {
    const normalized = normalizePath(path);
    if (!normalized || normalized === '/') return '';
    if (normalized.split('/').some((part) => part === '.' || part === '..')) return '';
    return normalized;
}

function sanitizeTabs(list) {
    const tabs = [];
    const seen = new Set();
    let previewUsed = false;
    for (const item of Array.isArray(list) ? list : []) {
        const path = savedPagePath(item?.path || '');
        if (!path || seen.has(path)) continue;
        seen.add(path);
        let isPreview = Boolean(item?.isPreview);
        if (isPreview) {
            if (previewUsed) isPreview = false;
            previewUsed = true;
        }
        tabs.push({ path, isPreview });
    }
    return tabs;
}

function sanitizeCollapsed(value) {
    const out = {};
    if (!value || typeof value !== 'object' || Array.isArray(value)) return out;
    for (const [key, collapsed] of Object.entries(value)) {
        if (typeof collapsed !== 'boolean') continue;
        if (!/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(key)) continue;
        out[key] = collapsed;
    }
    return out;
}

function storedHeight(key, min, max) {
    try {
        const raw = localStorage.getItem(key);
        if (raw == null || raw === '') return null;
        const n = Number(raw);
        if (!Number.isFinite(n)) return null;
        return clamp(Math.round(n), min, max);
    } catch {
        return null;
    }
}

function readRightPanelLocal() {
    const panel = {};
    try {
        const parsed = JSON.parse(localStorage.getItem(RIGHT_PANEL_KEY) || 'null');
        if (parsed && typeof parsed === 'object' && !Array.isArray(parsed)) {
            if (typeof parsed.rightPanelOpen === 'boolean') panel.rightPanelOpen = parsed.rightPanelOpen;
            if (typeof parsed.leftPanelOpen === 'boolean') panel.leftPanelOpen = parsed.leftPanelOpen;
            if (typeof parsed.leftPanelWidth === 'number' && Number.isFinite(parsed.leftPanelWidth)) {
                panel.leftPanelWidth = clamp(Math.round(parsed.leftPanelWidth), 120, 800);
            }
            if (typeof parsed.rightPanelWidth === 'number' && Number.isFinite(parsed.rightPanelWidth)) {
                panel.rightPanelWidth = clamp(Math.round(parsed.rightPanelWidth), 160, 800);
            }
            if (typeof parsed.openEditorsHeight === 'number' && Number.isFinite(parsed.openEditorsHeight)) {
                panel.openEditorsHeight = clamp(Math.round(parsed.openEditorsHeight), OPEN_EDITORS_HEIGHT_MIN, OPEN_EDITORS_HEIGHT_MAX);
            }
            if (typeof parsed.fileTabbarHeight === 'number' && Number.isFinite(parsed.fileTabbarHeight)) {
                panel.fileTabbarHeight = clamp(Math.round(parsed.fileTabbarHeight), FILE_TABBAR_HEIGHT_MIN, FILE_TABBAR_HEIGHT_MAX);
            }
            if (['files', 'search', 'git'].includes(parsed.leftShelfTab)) {
                panel.leftShelfTab = parsed.leftShelfTab;
            }
            if (typeof parsed.terminalOpen === 'boolean') panel.terminalOpen = parsed.terminalOpen;
            if (typeof parsed.terminalHeight === 'number' && Number.isFinite(parsed.terminalHeight)) {
                panel.terminalHeight = clamp(Math.round(parsed.terminalHeight), TERMINAL_HEIGHT_MIN, TERMINAL_HEIGHT_MAX);
            }
            if (parsed.widgetCollapsed && typeof parsed.widgetCollapsed === 'object') {
                panel.widgetCollapsed = sanitizeCollapsed(parsed.widgetCollapsed);
            }
            if (Array.isArray(parsed.widgetOrder)) {
                const order = sanitizeWidgetOrder(parsed.widgetOrder);
                if (order.length) panel.widgetOrder = order;
            }
        }
    } catch { /* private mode or bad json */ }
    const editorsHeight = storedHeight(OPEN_EDITORS_HEIGHT_KEY, OPEN_EDITORS_HEIGHT_MIN, OPEN_EDITORS_HEIGHT_MAX);
    if (editorsHeight != null) panel.openEditorsHeight = editorsHeight;
    const tabHeight = storedHeight(FILE_TABBAR_HEIGHT_KEY, FILE_TABBAR_HEIGHT_MIN, FILE_TABBAR_HEIGHT_MAX);
    if (tabHeight != null) panel.fileTabbarHeight = tabHeight;
    const termHeight = storedHeight(TERMINAL_HEIGHT_KEY, TERMINAL_HEIGHT_MIN, TERMINAL_HEIGHT_MAX);
    if (termHeight != null) panel.terminalHeight = termHeight;
    try {
        const rawTermOpen = localStorage.getItem(TERMINAL_OPEN_KEY);
        if (rawTermOpen === '1' || rawTermOpen === '0') panel.terminalOpen = rawTermOpen === '1';
    } catch { /* private mode */ }
    return panel;
}

function rightPanelPayload() {
    const body = {
        rightPanelOpen: Boolean(state.inspectorOpen),
        leftPanelOpen: Boolean(state.treeOpen),
        leftPanelWidth: state.treeWidth,
        rightPanelWidth: state.inspectorWidth,
        openEditorsHeight: state.openEditorsHeight,
        fileTabbarHeight: state.fileTabbarHeight,
        leftShelfTab: state.leftShelfTab || 'files',
        terminalOpen: Boolean(state.terminalOpen),
        terminalHeight: state.terminalHeight,
    };
    if (sessionChromeApplied || widgetsTouched) {
        const collapsed = widgetsApi?.collapsedMap?.();
        body.widgetCollapsed = collapsed || sanitizeCollapsed(sessionChrome?.widgetCollapsed);
        const order = sanitizeWidgetOrder(widgetsApi?.widgetOrder?.());
        if (order.length) body.widgetOrder = order;
    }
    return body;
}

function writeRightPanelLocal() {
    const body = rightPanelPayload();
    if (!body.widgetCollapsed || !body.widgetOrder) {
        const previous = readRightPanelLocal();
        if (!body.widgetCollapsed && previous.widgetCollapsed) body.widgetCollapsed = previous.widgetCollapsed;
        if (!body.widgetOrder && previous.widgetOrder) body.widgetOrder = previous.widgetOrder;
    }
    try { localStorage.setItem(RIGHT_PANEL_KEY, JSON.stringify(body)); } catch { /* private mode */ }
    return body;
}

function applyLocalPanel(panel) {
    if (!panel) return;
    if (typeof panel.leftPanelOpen === 'boolean') setTreeOpen(panel.leftPanelOpen, { silent: true });
    if (typeof panel.rightPanelOpen === 'boolean') setInspectorOpen(panel.rightPanelOpen, { silent: true });
    if (typeof panel.leftPanelWidth === 'number') setTreeWidth(panel.leftPanelWidth);
    if (typeof panel.rightPanelWidth === 'number') setInspectorWidth(panel.rightPanelWidth);
    if (typeof panel.openEditorsHeight === 'number') setOpenEditorsHeight(panel.openEditorsHeight, { persistLocal: false });
    if (typeof panel.fileTabbarHeight === 'number') setFileTabbarHeight(panel.fileTabbarHeight, { persistLocal: false });
    if (typeof panel.terminalHeight === 'number') setTerminalHeight(panel.terminalHeight, { persistLocal: false });
    if (typeof panel.terminalOpen === 'boolean') setTerminalOpen(panel.terminalOpen, { silent: true });
    if (['files', 'search', 'git'].includes(panel.leftShelfTab)) setLeftShelfTab(panel.leftShelfTab, { silent: true });
}

function applySessionChrome(saved) {
    if (!saved || sessionChromeApplied) return;
    sessionChromeApplied = true;
    sessionChrome = saved;
    if (!panelTouched) {
        if (typeof saved.leftPanelOpen === 'boolean') setTreeOpen(saved.leftPanelOpen, { silent: true });
        if (typeof saved.rightPanelOpen === 'boolean') setInspectorOpen(saved.rightPanelOpen, { silent: true });
        if (typeof saved.leftPanelWidth === 'number') setTreeWidth(saved.leftPanelWidth);
        if (typeof saved.rightPanelWidth === 'number') setInspectorWidth(saved.rightPanelWidth);
        if (typeof saved.terminalOpen === 'boolean') setTerminalOpen(saved.terminalOpen, { silent: true });
        if (['files', 'search', 'git'].includes(saved.leftShelfTab)) setLeftShelfTab(saved.leftShelfTab, { silent: true });
    }
    if (!editorsSized) {
        if (typeof saved.openEditorsHeight === 'number') setOpenEditorsHeight(saved.openEditorsHeight);
        if (typeof saved.fileTabbarHeight === 'number') setFileTabbarHeight(saved.fileTabbarHeight);
        if (typeof saved.terminalHeight === 'number') setTerminalHeight(saved.terminalHeight);
    }
    if (saved.widgetCollapsed && !widgetsTouched) widgetsApi?.setCollapsed(saved.widgetCollapsed);
    if (saved.widgetOrder && !widgetsTouched) widgetsApi?.setOrder(saved.widgetOrder);
}

function attachPanel(session, data) {
    if (!session || !data || typeof data !== 'object') return session;
    if (typeof data.rightPanelOpen === 'boolean') session.rightPanelOpen = data.rightPanelOpen;
    if (typeof data.leftPanelOpen === 'boolean') session.leftPanelOpen = data.leftPanelOpen;
    if (typeof data.leftPanelWidth === 'number' && Number.isFinite(data.leftPanelWidth)) {
        session.leftPanelWidth = clamp(Math.round(data.leftPanelWidth), 120, 800);
    }
    if (typeof data.rightPanelWidth === 'number' && Number.isFinite(data.rightPanelWidth)) {
        session.rightPanelWidth = clamp(Math.round(data.rightPanelWidth), 160, 800);
    }
    if (typeof data.openEditorsHeight === 'number' && Number.isFinite(data.openEditorsHeight)) {
        session.openEditorsHeight = clamp(Math.round(data.openEditorsHeight), OPEN_EDITORS_HEIGHT_MIN, OPEN_EDITORS_HEIGHT_MAX);
    }
    if (typeof data.fileTabbarHeight === 'number' && Number.isFinite(data.fileTabbarHeight)) {
        session.fileTabbarHeight = clamp(Math.round(data.fileTabbarHeight), FILE_TABBAR_HEIGHT_MIN, FILE_TABBAR_HEIGHT_MAX);
    }
    if (['files', 'search', 'git'].includes(data.leftShelfTab)) {
        session.leftShelfTab = data.leftShelfTab;
    }
    if (typeof data.terminalOpen === 'boolean') session.terminalOpen = data.terminalOpen;
    if (typeof data.terminalHeight === 'number' && Number.isFinite(data.terminalHeight)) {
        session.terminalHeight = clamp(Math.round(data.terminalHeight), TERMINAL_HEIGHT_MIN, TERMINAL_HEIGHT_MAX);
    }
    if (data.widgetCollapsed && typeof data.widgetCollapsed === 'object' && !Array.isArray(data.widgetCollapsed)) {
        session.widgetCollapsed = sanitizeCollapsed(data.widgetCollapsed);
    }
    if (Array.isArray(data.widgetOrder)) {
        const order = sanitizeWidgetOrder(data.widgetOrder);
        if (order.length) session.widgetOrder = order;
    }
    return session;
}

function sessionHasState(data) {
    if (!data || typeof data !== 'object') return false;
    if (savedPagePath(data.lastPage || '')) return true;
    if (Array.isArray(data.openTabs)) return true;
    if (typeof data.rightPanelOpen === 'boolean') return true;
    if (typeof data.leftPanelOpen === 'boolean') return true;
    if (typeof data.leftPanelWidth === 'number' && Number.isFinite(data.leftPanelWidth)) return true;
    if (typeof data.rightPanelWidth === 'number' && Number.isFinite(data.rightPanelWidth)) return true;
    if (typeof data.openEditorsHeight === 'number' && Number.isFinite(data.openEditorsHeight)) return true;
    if (typeof data.fileTabbarHeight === 'number' && Number.isFinite(data.fileTabbarHeight)) return true;
    if (data.widgetCollapsed && typeof data.widgetCollapsed === 'object' && !Array.isArray(data.widgetCollapsed)) return true;
    if (sanitizeWidgetOrder(data.widgetOrder).length) return true;
    return false;
}

function sessionFromPayload(data, { listed = false } = {}) {
    const lastPage = savedPagePath(data?.lastPage || '');
    const hasList = listed || Array.isArray(data?.openTabs);
    if (!hasList) {
        return {
            lastPage,
            openTabs: lastPage ? [{ path: lastPage, isPreview: false }] : [],
            explicitEmpty: false,
        };
    }
    const openTabs = sanitizeTabs(data.openTabs);
    return {
        lastPage,
        openTabs,
        explicitEmpty: openTabs.length === 0 && Boolean(lastPage),
    };
}

async function readSavedSession() {
    const localPanel = readRightPanelLocal();
    try {
        const data = await apiSend('GET', '/__studio/api/session');
        if (sessionHasState(data)) {
            const session = attachPanel(sessionFromPayload(data), data);
            if (!Object.prototype.hasOwnProperty.call(session, 'widgetCollapsed') && localPanel.widgetCollapsed) {
                session.widgetCollapsed = localPanel.widgetCollapsed;
            }
            if (!session.widgetOrder && localPanel.widgetOrder) session.widgetOrder = localPanel.widgetOrder;
            if (typeof session.rightPanelOpen !== 'boolean' && typeof localPanel.rightPanelOpen === 'boolean') {
                session.rightPanelOpen = localPanel.rightPanelOpen;
            }
            if (typeof session.leftPanelOpen !== 'boolean' && typeof localPanel.leftPanelOpen === 'boolean') {
                session.leftPanelOpen = localPanel.leftPanelOpen;
            }
            if (typeof session.leftPanelWidth !== 'number' && typeof localPanel.leftPanelWidth === 'number') {
                session.leftPanelWidth = localPanel.leftPanelWidth;
            }
            if (typeof session.rightPanelWidth !== 'number' && typeof localPanel.rightPanelWidth === 'number') {
                session.rightPanelWidth = localPanel.rightPanelWidth;
            }
            if (typeof session.openEditorsHeight !== 'number' && typeof localPanel.openEditorsHeight === 'number') {
                session.openEditorsHeight = localPanel.openEditorsHeight;
            }
            if (typeof session.fileTabbarHeight !== 'number' && typeof localPanel.fileTabbarHeight === 'number') {
                session.fileTabbarHeight = localPanel.fileTabbarHeight;
            }
            return session;
        }
    } catch { /* local storage is the fallback */ }
    try {
        const lastPage = savedPagePath(localStorage.getItem(LAST_PAGE_KEY) || '');
        const listed = localStorage.getItem(OPEN_TABS_SET_KEY) === '1';
        const raw = listed ? JSON.parse(localStorage.getItem(OPEN_TABS_KEY) || '[]') : null;
        return attachPanel(sessionFromPayload({ lastPage, openTabs: raw }, { listed }), localPanel);
    } catch {
        return { lastPage: '', openTabs: [], explicitEmpty: false };
    }
}

function currentOpenTabs() {
    return state.tabs
        .map((tab) => ({ path: savedPagePath(tab.path), isPreview: Boolean(tab.isPreview) }))
        .filter((tab) => tab.path);
}

function queueSession(lastPage) {
    const body = rightPanelPayload();
    if (lastPage) {
        body.lastPage = lastPage;
        body.openTabs = currentOpenTabs();
    }
    sessionPayload = body;
    window.clearTimeout(sessionTimer);
    sessionTimer = window.setTimeout(() => flushSession(), 160);
}

function rememberPanelState() {
    panelTouched = true;
    writeRightPanelLocal();
    queueSession(savedPagePath(state.path) || '');
}

function rememberSession(fallbackPath) {
    const lastPage = savedPagePath(state.path) || savedPagePath(fallbackPath) || '';
    try {
        localStorage.setItem(OPEN_TABS_KEY, JSON.stringify(currentOpenTabs()));
        localStorage.setItem(OPEN_TABS_SET_KEY, '1');
        if (lastPage) localStorage.setItem(LAST_PAGE_KEY, lastPage);
    } catch { /* private mode */ }
    writeRightPanelLocal();
    queueSession(lastPage);
}

function flushSession() {
    window.clearTimeout(sessionTimer);
    const body = sessionPayload;
    if (!body) return;
    sessionWrite = sessionWrite
        .catch(() => {})
        .then(() => fetch('/__studio/api/session', {
            method: 'POST',
            keepalive: true,
            cache: 'no-store',
            headers: { Accept: 'application/json', 'Content-Type': 'application/json' },
            body: JSON.stringify(body),
        }))
        .catch(() => {});
}

async function bootDocument(hash) {
    const treePromise = loadTree();
    const savedPromise = hash.legacy ? Promise.resolve(null) : readSavedSession();
    if (hash.legacy) {
        await treePromise;
        await openLegacy(hash, { recover: true });
        return;
    }
    const saved = await savedPromise;
    applySessionChrome(saved);
    if (saved?.openTabs?.length) installTabs(saved.openTabs);
    await treePromise;
    let target = hash.path || '';
    if (!target && saved?.openTabs?.length) {
        target = saved.openTabs.some((tab) => tab.path === saved.lastPage)
            ? saved.lastPage
            : saved.openTabs[saved.openTabs.length - 1].path;
    }
    if (!target && !saved?.explicitEmpty) target = saved?.lastPage || pickProjectLanding() || LANDING_PAGE;
    if (!target) {
        clearEditor();
        return;
    }
    const known = findTab(target);
    const remembered = Boolean(hash.path || saved?.lastPage);
    await openPath(target, {
        mode: hash.path ? hash.mode : 'code',
        recover: true,
        pin: known ? !known.isPreview : remembered,
        preview: known ? known.isPreview : !remembered,
    });
}

async function followHash() {
    const hash = parseHash();
    if (hash.legacy) {
        await openLegacy(hash);
        return;
    }
    if (hash.path && hash.path !== state.path) await openPath(hash.path, { mode: hash.mode });
    else if ((hash.mode === 'gallery' ? 'gallery' : 'code') !== state.mode) setMode(hash.mode);
}

async function loadTree({ focusTreeKey = '', reveal = null } = {}) {
    try {
        const data = await apiSend('GET', '/__studio/api/tree');
        state.sections = normalizeTree(data);
        state.treeSource = 'tree';
        state.treeMessage = 'File tree loaded';
        apiSend('GET', '/__studio/api/fs/history').then(syncTreeOpsHistory).catch(() => {});
    } catch {
        state.sections = await loadFoldersFallback();
        state.treeSource = 'folders';
        const any = state.sections.some((section) => section.folders.length || section.rootFiles.length);
        state.treeMessage = any
            ? 'Folder list loaded. The file tree API is not available yet.'
            : 'Studio API is offline. The file tree needs the studio server.';
    }
    renderTree({
        reveal: reveal != null ? Boolean(reveal) : Boolean(state.path || focusTreeKey),
        focusTreeKey,
    });
    refreshEditors();
}

async function loadFoldersFallback() {
    const loaded = await Promise.all(SECTIONS.map(async (meta) => {
        try {
            const data = await apiSend('GET', `/__studio/api/folders?section=${encodeURIComponent(meta.id)}`);
            const folders = (Array.isArray(data.folders) ? data.folders : [])
                .map((folder) => normalizeFolder(meta.id, { ...folder, name: folder.name || folder.vehicleName || folder.slug }))
                .sort((a, b) => String(a.slug).localeCompare(String(b.slug), undefined, { numeric: true, sensitivity: 'base' }));
            return { id: meta.id, label: meta.label, rootFiles: [], folders, totalFolders: folders.length };
        } catch {
            return emptySection(meta);
        }
    }));
    loaded.sort((a, b) => byFilename(a.id, b.id));
    return loaded;
}

async function openLegacy(hash, { recover = false } = {}) {
    if (hash.section) state.expandedSections.add(hash.section);
    if (!hash.slug) {
        const section = state.sections.find((item) => item.id === hash.section);
        renderTree({ reveal: true });
        if (section?.rootFiles?.[0]) await openPath(section.rootFiles[0].path, { mode: hash.mode, recover });
        else if (recover) await openPath(LANDING_PAGE, { force: true });
        return;
    }
    const section = state.sections.find((item) => item.id === hash.section);
    const folder = section?.folders.find((item) => item.slug === hash.slug);
    await openPath(folder?.primaryFile || defaultPrimary(hash.section, hash.slug), { mode: hash.mode, recover });
}

function findTab(path) {
    const needle = normalizePath(path);
    if (!needle) return null;
    return state.tabs.find((tab) => normalizePath(tab.path) === needle) || null;
}

function blankTab(path, isPreview) {
    return {
        path,
        isPreview: Boolean(isPreview),
        loaded: false,
        doc: null,
        content: '',
        savedContent: '',
        savedImagesSig: '',
        sourceLoaded: false,
        codeDirty: false,
        galleryDirty: false,
        selectionStart: 0,
        selectionEnd: 0,
        scrollTop: 0,
        scrollLeft: 0,
        previewScrollX: 0,
        previewScrollY: 0,
        previewMoreOpen: false,
        mode: 'code',
    };
}

function createTab(path, isPreview) {
    if (isPreview) {
        for (const tab of state.tabs) tab.isPreview = false;
    }
    return blankTab(path, isPreview);
}

function resetTab(tab, path, isPreview) {
    const next = blankTab(path, isPreview);
    Object.assign(tab, next);
    return tab;
}

function installTabs(list) {
    state.tabs = [];
    for (const item of sanitizeTabs(list)) state.tabs.push(createTab(item.path, item.isPreview));
}

function reusePreviewSlot(path) {
    const slot = state.tabs.find((tab) => tab.isPreview);
    if (!slot || normalizePath(slot.path) === path) return slot || null;
    const live = normalizePath(slot.path) === normalizePath(state.path);
    const dirty = live ? (state.codeDirty || state.galleryDirty) : (slot.codeDirty || slot.galleryDirty);
    if (dirty) {
        slot.isPreview = false;
        return null;
    }
    if (live) {
        window.clearTimeout(saveTimer);
        saveEpoch += 1;
        if (saveAbort) {
            saveAbort.abort();
            saveAbort = null;
        }
    }
    return resetTab(slot, path, true);
}

async function openPath(path, { force = false, mode, recover = false, pin = false, preview = true, insertAt } = {}) {
    const normalized = normalizePath(path);
    if (!normalized) return;
    let tab = findTab(normalized);
    if (tab && !force) {
        if (pin) tab.isPreview = false;
        await showTab(tab, { recover, mode });
        return;
    }
    if (!tab) {
        const wantPreview = !pin && preview !== false;
        const placed = Number.isInteger(insertAt);
        if (wantPreview && !placed) tab = reusePreviewSlot(normalized);
        if (!tab) {
            tab = createTab(normalized, wantPreview);
            const activeIndex = state.tabs.findIndex((item) => normalizePath(item.path) === normalizePath(state.path));
            const at = Number.isInteger(insertAt)
                ? Math.max(0, Math.min(insertAt, state.tabs.length))
                : (activeIndex >= 0 ? activeIndex + 1 : state.tabs.length);
            state.tabs.splice(at, 0, tab);
        }
    } else if (pin) tab.isPreview = false;
    if (mode) tab.mode = mode === 'gallery' ? 'gallery' : 'code';
    await showTab(tab, { force: true, recover, mode });
}

async function showTab(tab, { force = false, recover = false, mode } = {}) {
    if (!tab) return;
    if (mode) tab.mode = mode === 'gallery' ? 'gallery' : 'code';
    const same = normalizePath(state.path) === normalizePath(tab.path);
    if (same && !force && (tab.loaded || loadingPath === normalizePath(tab.path))) {
        if (mode && tab.loaded) setMode(tab.mode);
        revealPath(tab.path);
        writeHash();
        renderTree({ reveal: true });
        updateChrome();
        rememberSession();
        return;
    }
    if (!same) leaveActiveTab();
    state.path = tab.path;
    const loc = locate(tab.path);
    state.section = loc.section;
    state.slug = loc.slug;
    revealPath(tab.path);
    writeHash();
    renderTree({ reveal: true });
    refreshEditors();
    rememberSession();
    if (tab.loaded && tab.doc && !force) {
        restoreTab(tab);
        return;
    }
    state.mode = tab.mode === 'gallery' ? 'gallery' : 'code';
    await loadIntoTab(tab, { recover });
}

function leaveActiveTab() {
    if (!state.path) return;
    window.clearTimeout(saveTimer);
    captureActiveTab();
    if ((state.codeDirty || state.galleryDirty) && state.path) {
        const revision = bumpStageRev();
        const body = stageBody(revision);
        previewStaged = true;
        void apiSend('POST', '/__studio/api/preview/stage', body).catch(() => {});
    }
    saveEpoch += 1;
    if (saveAbort) {
        saveAbort.abort();
        saveAbort = null;
    }
}

function captureActiveTab() {
    const tab = findTab(state.path);
    if (!tab || !tab.loaded) return null;
    tab.content = state.sourceLoaded ? codeApi.getValue() : (tab.content || '');
    tab.savedContent = state.savedContent;
    tab.savedImagesSig = state.savedImagesSig;
    tab.sourceLoaded = state.sourceLoaded;
    tab.codeDirty = state.codeDirty;
    tab.galleryDirty = state.galleryDirty;
    tab.doc = state.doc;
    tab.mode = state.mode === 'gallery' ? 'gallery' : 'code';
    const caret = codeApi.getCaret();
    tab.selectionStart = caret.selectionStart;
    tab.selectionEnd = caret.selectionEnd;
    tab.scrollTop = caret.scrollTop;
    tab.scrollLeft = caret.scrollLeft;
    undoStack?.updateCaret(state.path, caret, tab.mode);
    const preview = capturePreview();
    if (preview) {
        tab.previewScrollX = preview.x;
        tab.previewScrollY = preview.y;
        tab.previewMoreOpen = preview.open;
    }
    return tab;
}

function syncActiveTabCache() {
    const tab = findTab(state.path);
    if (!tab) return;
    tab.path = state.path;
    tab.loaded = Boolean(state.doc);
    tab.doc = state.doc;
    tab.content = state.sourceLoaded ? codeApi.getValue() : '';
    tab.savedContent = state.savedContent;
    tab.savedImagesSig = state.savedImagesSig;
    tab.sourceLoaded = state.sourceLoaded;
    tab.codeDirty = state.codeDirty;
    tab.galleryDirty = state.galleryDirty;
    tab.mode = state.mode === 'gallery' ? 'gallery' : 'code';
    if (!codeApi?.getCaret) return;
    const caret = codeApi.getCaret();
    tab.selectionStart = caret.selectionStart;
    tab.selectionEnd = caret.selectionEnd;
    tab.scrollTop = caret.scrollTop;
    tab.scrollLeft = caret.scrollLeft;
}

function fillTabFromDoc(tab, doc) {
    tab.loaded = true;
    tab.doc = doc;
    tab.path = doc.path || tab.path;
    tab.content = doc.sourceLoaded ? String(doc.content || '') : '';
    tab.savedContent = tab.content;
    tab.savedImagesSig = imageSignature(doc.images);
    tab.sourceLoaded = Boolean(doc.sourceLoaded);
    tab.codeDirty = false;
    tab.galleryDirty = false;
    tab.selectionStart = 0;
    tab.selectionEnd = 0;
    tab.scrollTop = 0;
    tab.scrollLeft = 0;
    if (!tab.mode) tab.mode = 'code';
    undoStack?.initDocument(tab.path, currentDocSnapshot(doc), {
        label: 'Initial state (Opened)',
        saved: true,
    });
}

function restoreTab(tab) {
    const doc = tab.doc;
    if (doc?.engine && typeof tab.content === 'string') {
        primeEngineBundle(tab.content, doc.engine);
    }
    state.doc = doc;
    state.path = tab.path;
    state.section = doc?.section || locate(tab.path).section;
    state.slug = doc?.slug || locate(tab.path).slug;
    state.sourceLoaded = Boolean(tab.sourceLoaded);
    state.savedContent = tab.savedContent || '';
    state.savedImagesSig = tab.savedImagesSig || imageSignature(doc?.images);
    state.codeDirty = Boolean(tab.codeDirty);
    state.galleryDirty = Boolean(tab.galleryDirty);
    state.mode = tab.mode === 'gallery' ? 'gallery' : 'code';
    codeApi.setValue(state.sourceLoaded ? tab.content : '');
    codeApi.setContext(editorFacts({
        path: state.path,
        section: state.section,
        slug: state.slug,
        editable: state.sourceLoaded,
        message: state.sourceLoaded ? '' : (doc?.sourceMessage || ''),
    }));
    codeApi.setCaret({
        selectionStart: tab.selectionStart || 0,
        selectionEnd: tab.selectionEnd || 0,
        scrollTop: tab.scrollTop || 0,
        scrollLeft: tab.scrollLeft || 0,
    });
    galleryApi.setDoc(doc);
    widgetsApi.setDoc(doc, { force: true });
    undoStack?.initDocument(state.path, currentDocSnapshot(), {
        label: 'Initial state (Opened)',
        saved: !state.codeDirty && !state.galleryDirty,
    });
    if (doc) patchFolderFromDoc(doc);
    updateGalleryTab();
    setMode(state.mode, { silent: true });
    const snap = {
        x: tab.previewScrollX || 0,
        y: tab.previewScrollY || 0,
        open: Boolean(tab.previewMoreOpen),
    };
    const navigated = presentPreview();
    if (snap.x || snap.y || snap.open) {
        if (navigated) previewRestore = snap;
        else {
            restorePreview(snap);
            requestAnimationFrame(() => restorePreview(snap));
        }
    }
    revealPath(state.path);
    renderTree({ reveal: true });
    writeHash();
    setEditorStatus('');
    if (state.codeDirty || state.galleryDirty) scheduleSave();
    else {
        setSaveState(doc?.dirty ? 'git' : 'saved', doc?.dirty ? 'This file differs from the last git commit.' : 'Loaded from the open editor.');
        updateChrome();
    }
    syncActiveTabCache();
}

async function loadIntoTab(tab, { recover = false } = {}) {
    const requested = normalizePath(tab.path);
    const token = ++openToken;
    loadingPath = requested;
    setEditorStatus('');
    updateChrome();
    codeApi.setContext(editorFacts({
        path: requested,
        section: state.section,
        slug: state.slug,
        editable: false,
        message: 'Loading page…',
        images: [],
        unlistedSlides: [],
        media: null,
    }));
    try {
        const doc = await loadPage(requested);
        if (token !== openToken || normalizePath(tab.path) !== requested) return;
        if (normalizePath(state.path) !== requested) {
            fillTabFromDoc(tab, doc);
            refreshEditors();
            return;
        }
        adoptDocument(doc);
    } catch (error) {
        if (token !== openToken || normalizePath(tab.path) !== requested) return;
        if (normalizePath(state.path) !== requested) return;
        const fallback = pickProjectLanding() || LANDING_PAGE;
        if (recover && requested !== fallback) {
            tab.loaded = false;
            state.tabs = state.tabs.filter((item) => item !== tab);
            await openPath(fallback, { force: true, pin: true });
            return;
        }
        tab.loaded = false;
        tab.doc = null;
        state.doc = null;
        state.sourceLoaded = false;
        codeApi.setValue('');
        codeApi.setContext(editorFacts({
            path: requested,
            section: state.section,
            slug: state.slug,
            editable: false,
            message: error.message || 'Could not open this file.',
            images: [],
            unlistedSlides: [],
            media: null,
        }));
        galleryApi.setDoc(null);
        widgetsApi.setDoc(null, { force: true });
        setPreviewUrl('');
        setEditorStatus(error.message || 'Could not open this file.', true);
        updateChrome();
    } finally {
        if (loadingPath === requested && token === openToken) loadingPath = '';
    }
}

function pinActiveTab() {
    const tab = findTab(state.path);
    if (!tab || !tab.isPreview) return false;
    tab.isPreview = false;
    refreshEditors();
    rememberSession();
    syncMenu();
    return true;
}

function pinTab(path) {
    const tab = findTab(path);
    if (!tab) return;
    if (tab.isPreview) tab.isPreview = false;
    if (normalizePath(state.path) !== normalizePath(tab.path)) {
        void showTab(tab);
        return;
    }
    refreshEditors();
    rememberSession();
    syncMenu();
}

async function closeTab(path) {
    const tab = findTab(path);
    if (!tab) return;
    if (tabIsBufferDirty(tab)) {
        const discard = await confirmDialog(
            'Close without saving?',
            'This file has unsaved changes. Closing it discards those edits.',
            'Discard',
        );
        if (!discard) return;
    }
    const closing = normalizePath(state.path) === normalizePath(tab.path);
    const closedPath = tab.path;
    tab.codeDirty = false;
    tab.galleryDirty = false;
    if (closing) {
        state.codeDirty = false;
        state.galleryDirty = false;
        window.clearTimeout(saveTimer);
    }
    await clearPreviewDraft(closedPath, bumpStageRev());
    const index = state.tabs.indexOf(tab);
    if (index < 0) return;
    rememberClosedTab({
        path: tab.path,
        index,
        isPreview: tab.isPreview,
        mode: (closing ? state.mode : tab.mode) === 'gallery' ? 'gallery' : 'code',
    });
    state.tabs.splice(index, 1);
    if (!closing) {
        refreshEditors();
        renderTree({ reveal: false });
        rememberSession();
        syncMenu();
        return;
    }
    const next = state.tabs[index] || state.tabs[index - 1] || null;
    if (next) await showTab(next);
    else clearEditor(closedPath);
}

async function reopenClosedTab() {
    const entry = takeClosedTab();
    if (!entry) {
        announce('No closed editors to reopen.');
        syncMenu();
        return;
    }
    beginClosedTabRestore(entry.path);
    try {
        await openPath(entry.path, {
            pin: !entry.isPreview,
            preview: entry.isPreview,
            insertAt: entry.index,
            mode: entry.mode,
        });
        announce(`Reopened ${shortTabLabel(entry.path)}.`);
    } finally {
        endClosedTabRestore();
        syncMenu();
    }
}

async function closeOtherTabs(keepPath) {
    const keep = normalizePath(keepPath);
    const paths = state.tabs.map((tab) => tab.path).filter((item) => normalizePath(item) !== keep);
    for (const item of paths) {
        if (!findTab(item)) continue;
        await closeTab(item);
    }
}

async function closeSavedTabs() {
    captureActiveTab();
    const paths = state.tabs.filter((tab) => !tabIsBufferDirty(tab)).map((tab) => tab.path);
    for (const item of paths) {
        if (!findTab(item)) continue;
        await closeTab(item);
    }
}

async function closeAllTabs() {
    const paths = state.tabs.map((tab) => tab.path);
    for (const item of paths) {
        if (!findTab(item)) continue;
        await closeTab(item);
    }
}

function tabIsBufferDirty(tab) {
    if (!tab) return false;
    if (normalizePath(tab.path) === normalizePath(state.path)) return Boolean(state.codeDirty || state.galleryDirty);
    return Boolean(tab.codeDirty || tab.galleryDirty);
}

function clearEditor(fallbackPath) {
    window.clearTimeout(saveTimer);
    saveEpoch += 1;
    if (saveAbort) {
        saveAbort.abort();
        saveAbort = null;
    }
    openToken += 1;
    loadingPath = '';
    state.path = '';
    state.section = '';
    state.slug = '';
    state.doc = null;
    state.sourceLoaded = false;
    state.savedContent = '';
    state.codeDirty = false;
    state.galleryDirty = false;
    state.mode = 'code';
    codeApi.setValue('');
    codeApi.setCaret({ selectionStart: 0, selectionEnd: 0, scrollTop: 0, scrollLeft: 0 });
    codeApi.setContext(editorFacts({ editable: false, message: 'Choose a file from the tree.', images: [], unlistedSlides: [] }));
    galleryApi.setDoc(null);
    widgetsApi.setDoc(null, { force: true });
    setPreviewUrl('');
    previewNav?.clear();
    setEditorStatus('');
    setMode('code', { silent: true });
    setSaveState('saved', 'No file open.');
    if (window.location.hash) history.replaceState(null, '', `${window.location.pathname}${window.location.search}`);
    renderTree({ reveal: false });
    updateChrome();
    rememberSession(fallbackPath);
}

function anyDirty() {
    captureActiveTab();
    return state.tabs.some((tab) => tab.codeDirty || tab.galleryDirty) || state.codeDirty || state.galleryDirty;
}

function tabDirty(tab) {
    if (!tab) return false;
    if (tabIsBufferDirty(tab)) return true;
    const live = normalizePath(tab.path) === normalizePath(state.path);
    if (live && state.doc?.dirty) return true;
    if (tab.doc?.dirty) return true;
    return Boolean(folderForPath(tab.path)?.dirty);
}

function tabIssues(tab) {
    const live = normalizePath(tab.path) === normalizePath(state.path);
    const doc = live ? state.doc : tab.doc;
    if (doc && Array.isArray(doc.errors) && doc.errors.length) return doc.errors.length;
    return Number(folderForPath(tab.path)?.issueCount) || 0;
}

function folderForPath(path) {
    const loc = locate(path);
    return findFolder(loc.section, loc.slug);
}

function shortTabLabel(path) {
    const parts = normalizePath(path).split('/').filter(Boolean);
    const file = parts[parts.length - 1] || path;
    const parent = parts.length >= 3 ? parts[parts.length - 2] : '';
    return parent ? `${parent}/${file}` : file;
}

function tabLabel(path) {
    const base = shortTabLabel(path);
    const dupes = state.tabs.filter((tab) => shortTabLabel(tab.path) === base);
    if (dupes.length < 2) return base;
    const parts = normalizePath(path).split('/').filter(Boolean);
    return parts.slice(Math.max(0, parts.length - 3)).join('/') || base;
}

function tabHint(path) {
    const loc = locate(path);
    if (loc.section && loc.slug) return `${loc.section}/${loc.slug}`;
    return loc.section || '';
}

function tabExt(path) {
    const type = fileType(fileName(path));
    return type && type !== 'file' ? type : '';
}

function refreshEditors() {
    const tabs = state.tabs.map((tab) => ({
        path: tab.path,
        label: tabLabel(tab.path),
        title: normalizePath(tab.path),
        ext: tabExt(tab.path),
        hint: tabHint(tab.path),
        isPreview: Boolean(tab.isPreview),
        unsaved: tabIsBufferDirty(tab),
        dirty: tabDirty(tab),
        issues: tabIssues(tab),
        active: normalizePath(tab.path) === normalizePath(state.path),
    }));
    tabsApi?.render(tabs);
    editorsApi?.render({ open: state.openEditorsOpen, tabs });
}

function cycleTabs(delta) {
    if (state.tabs.length < 2) return;
    const index = Math.max(0, state.tabs.findIndex((tab) => normalizePath(tab.path) === normalizePath(state.path)));
    const next = state.tabs[(index + delta + state.tabs.length) % state.tabs.length];
    if (next) void showTab(next);
}

async function loadPage(path) {
    const loc = locate(path);
    let page = null;
    let pageError = null;
    try {
        page = await apiSend('GET', `/__studio/api/page?path=${encodeURIComponent(path)}`);
        if (page?.config) applyStudioConfig(page.config);
    } catch (error) {
        pageError = error;
    }
    let gallery = null;
    if (normalizePath(path) !== CONFIG_PAGE_PATH && loc.section && loc.slug) {
        try {
            gallery = await apiSend('GET', `/__studio/api/gallery?section=${encodeURIComponent(loc.section)}&slug=${encodeURIComponent(loc.slug)}`);
        } catch { /* gallery fields are optional when the page payload is complete */ }
    }
    if (!page && !gallery) throw pageError || new Error('Could not open this file.');
    const doc = finishPage(mergePageAndGallery(page, gallery, path), loc, true);
    if (doc.isMedia) doc.sourceMessage = '';
    else if (!doc.sourceLoaded) {
        doc.sourceMessage = page
            ? 'This page did not include source text.'
            : 'The page source API has not returned this file yet. Gallery order, captions, and the preview still work.';
    }
    return doc;
}

function mergePageAndGallery(page, gallery, path) {
    const merged = { ...(gallery || {}), ...(page || {}) };
    merged.path = (page && page.path) || (gallery && gallery.path) || path;
    if (!Array.isArray(page?.images) && Array.isArray(gallery?.images)) merged.images = gallery.images;
    if (!Array.isArray(page?.unlistedSlides) && Array.isArray(gallery?.unlistedSlides)) merged.unlistedSlides = gallery.unlistedSlides;
    if (!page?.details && gallery?.details) merged.details = gallery.details;
    if (!page?.previewUrl && gallery?.previewUrl) merged.previewUrl = gallery.previewUrl;
    if (typeof page?.content !== 'string' && typeof gallery?.content === 'string') merged.content = gallery.content;
    if (typeof page?.gitContent !== 'string' && typeof gallery?.gitContent === 'string') merged.gitContent = gallery.gitContent;
    if (!page?.widgets && gallery?.widgets) merged.widgets = gallery.widgets;
    if (!Array.isArray(page?.errors) && Array.isArray(gallery?.errors)) merged.errors = gallery.errors;
    if ((page?.hasGallery == null) && gallery?.hasGallery != null) merged.hasGallery = gallery.hasGallery;
    merged.unlistedSlides = omitListedSlides(
        merged.unlistedSlides,
        merged.images,
        typeof merged.content === 'string' ? merged.content : '',
    );
    return merged;
}

function adoptDocument(doc) {
    const previous = findTab(state.path);
    if (doc?.engine && typeof doc.content === 'string') {
        primeEngineBundle(doc.content, doc.engine);
    }
    state.doc = doc;
    state.path = doc.path || state.path;
    if (previous) previous.path = state.path;
    state.section = doc.section || state.section;
    state.slug = doc.slug || '';
    state.sourceLoaded = Boolean(doc.sourceLoaded);
    state.savedContent = state.sourceLoaded ? String(doc.content || '') : '';
    state.savedImagesSig = imageSignature(doc.images);
    state.codeDirty = false;
    state.galleryDirty = false;
    codeApi.setValue(state.savedContent);
    codeApi.setContext(editorFacts({
        path: state.path,
        section: state.section,
        slug: state.slug,
        editable: state.sourceLoaded,
        message: doc.sourceMessage || '',
    }));
    codeApi.setCaret({ selectionStart: 0, selectionEnd: 0, scrollTop: 0, scrollLeft: 0 });
    galleryApi.setDoc(doc);
    widgetsApi.setDoc(doc, { force: true });
    if (revertPendingPath && normalizePath(revertPendingPath) === normalizePath(state.path)) {
        revertPendingPath = '';
        undoStack?.record(state.path, currentDocSnapshot(doc), {
            kind: 'revert',
            label: 'Reverted to saved',
            saved: true,
            force: true,
        });
    } else {
        undoStack?.initDocument(state.path, currentDocSnapshot(doc), {
            label: 'Initial state (Opened)',
            saved: true,
        });
    }
    revealPath(state.path);
    patchFolderFromDoc(doc);
    updateGalleryTab();
    setMode(state.mode, { silent: true });
    presentPreview();
    renderTree({ reveal: true });
    writeHash();
    setEditorStatus('');
    previewStaged = false;
    void clearPreviewDraft(state.path, bumpStageRev());
    setSaveState(doc.dirty ? 'git' : 'saved', doc.dirty ? 'This file differs from the last git commit.' : 'Loaded from disk.');
    syncActiveTabCache();
    updateChrome();
    rememberRecent(doc);
    rememberSession();
}

function noteCodeEdit(meta = {}) {
    if (!state.sourceLoaded) return;
    pinActiveTab();
    const current = codeApi.getValue();
    if (state.doc) state.doc.content = current;
    state.codeDirty = current !== state.savedContent;
    widgetsApi?.noteSource?.(current);
    undoStack?.record(state.path, currentDocSnapshot(), {
        kind: meta?.kind || 'code',
        label: meta?.label || 'Code edit',
        coalesce: meta?.coalesce !== false,
        coalesceKey: meta?.coalesceKey || 'code-edit',
        line: meta?.line,
    });
    if (state.codeDirty || state.galleryDirty) scheduleSave();
    else releasePreviewDraft();
}

function applyInlineSource(next, meta = {}) {
    if (!state.sourceLoaded || typeof next !== 'string') return;
    const changed = codeApi.getValue() !== next;
    if (changed) {
        pinActiveTab();
        codeApi.setValue(next);
        if (state.doc) state.doc.content = next;
        state.codeDirty = next !== state.savedContent;
        state.inlineTouched = true;
        if (fileType(fileName(state.path)) === 'html') state.inlineNeedsValues = true;
    }
    if (meta.range && Number.isFinite(meta.range.startOffset)) {
        codeApi.highlightRange(meta.range.startOffset, meta.range.endOffset, meta.range.label, {
            scroll: meta.scroll !== false,
        });
    }
    widgetsApi?.noteSource?.(codeApi.getValue());
    if (changed) {
        const rangeLabel = meta?.range?.label ? ` (${meta.range.label})` : '';
        undoStack?.record(state.path, currentDocSnapshot(), {
            kind: 'inline',
            label: `Preview inline edit${rangeLabel}`,
            coalesce: !meta?.immediate,
            coalesceKey: `inline:${meta?.range?.label || 'text'}`,
            coalesceWindow: 1000,
        });
    }
    if (!state.codeDirty && !state.galleryDirty) {
        releasePreviewDraft();
        return;
    }
    if (meta.immediate) flushQuiet();
    else scheduleSave({ delay: 600, reload: false });
}

function applyInlineCaption(update) {
    const changed = galleryApi?.setCaptions?.(update?.names || [], update?.caption ?? '') === true;
    if (changed) {
        pinActiveTab();
        if (state.doc?.gallerySource === 'html') {
            writeHtmlGallery();
            state.galleryDirty = false;
        } else {
            state.galleryDirty = imageSignature(state.doc?.images) !== state.savedImagesSig;
        }
        state.inlineTouched = true;
        undoStack?.record(state.path, currentDocSnapshot(), {
            kind: 'inline',
            label: 'Preview inline edit (caption)',
            coalesce: !update?.immediate,
            coalesceKey: 'inline:caption',
            coalesceWindow: 1000,
        });
    }
    if (update?.range && Number.isFinite(update.range.startOffset)) {
        codeApi.highlightRange(update.range.startOffset, update.range.endOffset, update.range.label, {
            scroll: update.scroll !== false,
        });
    }
    if (!changed && !update?.immediate) return;
    if (update?.immediate) flushQuiet();
    else if (changed || state.codeDirty || state.galleryDirty) scheduleSave({ delay: 600, reload: false });
}

function noteGalleryEdit(meta = {}) {
    pinActiveTab();
    if (state.doc?.gallerySource === 'html') {
        writeHtmlGallery();
        state.galleryDirty = false;
        undoStack?.record(state.path, currentDocSnapshot(), {
            kind: meta?.kind || 'gallery',
            label: meta?.label || 'Reordered / edited gallery slides',
            coalesce: Boolean(meta?.coalesce),
            coalesceKey: meta?.coalesceKey || null,
            coalesceWindow: 900,
        });
        if (state.codeDirty) scheduleSave();
        else releasePreviewDraft();
        return;
    }
    state.galleryDirty = imageSignature(state.doc?.images) !== state.savedImagesSig;
    undoStack?.record(state.path, currentDocSnapshot(), {
        kind: meta?.kind || 'gallery',
        label: meta?.label || 'Reordered / edited gallery slides',
        coalesce: Boolean(meta?.coalesce),
        coalesceKey: meta?.coalesceKey || null,
        coalesceWindow: 900,
    });
    if (state.codeDirty || state.galleryDirty) scheduleSave();
    else releasePreviewDraft();
}

function writeHtmlGallery() {
    if (!state.sourceLoaded || state.doc?.gallerySource !== 'html') return false;
    const current = codeApi.getValue();
    const next = writeThumbnailGallery(current, state.doc.images || []);
    if (typeof next !== 'string') {
        setSaveState('error', 'Could not find the thumbnail gallery in this page.');
        updateChrome();
        return false;
    }
    if (next !== current) {
        codeApi.setValue(next);
        state.doc.content = next;
        widgetsApi?.noteSource?.(next);
    }
    state.codeDirty = codeApi.getValue() !== state.savedContent;
    return next !== current;
}

function refreshHtmlGalleryFromEditor() {
    if (!state.doc || !state.sourceLoaded || state.doc.gallerySource === 'data') return;
    const content = codeApi.getValue();
    const previousImages = state.doc.images;
    const previousUnlisted = state.doc.unlistedSlides;
    const before = imageSignature(previousImages);
    const beforeUnlisted = (previousUnlisted || []).map((slide) => slide?.name || '').join('\n');
    state.doc.content = content;
    attachGallerySource(state.doc);
    const after = imageSignature(state.doc.images);
    const afterUnlisted = (state.doc.unlistedSlides || []).map((slide) => slide?.name || '').join('\n');
    if (before === after && beforeUnlisted === afterUnlisted) {
        state.doc.images = previousImages;
        state.doc.unlistedSlides = previousUnlisted;
        return;
    }
    galleryApi.setDoc(state.doc);
    updateGalleryTab();
}

function flushQuiet() {
    if (!state.codeDirty && !state.galleryDirty) return;
    window.clearTimeout(saveTimer);
    void stagePreview(bumpStageRev(), { reload: false });
}

function leaveOrEnterViewport(mode) {
    state.viewportMode = mode === 'edit' ? 'edit' : 'view';
    if (state.viewportMode === 'edit') {
        announce('Edit text mode. Clicks in the preview edit text instead of following links.');
        syncMenu();
        return;
    }
    codeApi?.clearHighlight();
    announce('View mode. Preview links and the lightbox work again.');
    const touched = state.inlineTouched;
    state.inlineNeedsValues = false;
    state.inlineTouched = false;
    if (state.codeDirty || state.galleryDirty) void stagePreview(bumpStageRev(), { reload: true });
    else if (touched) reloadPreview();
    syncMenu();
}

function bumpStageRev() {
    const now = Date.now();
    stageRev = now > stageRev ? now : stageRev + 1;
    return stageRev;
}

function stageBody(revision) {
    const body = { path: state.path, revision };
    if (state.sourceLoaded) body.content = codeApi.getValue();
    if (state.slug && state.doc?.gallerySource !== 'html') body.images = galleryApi.payloadImages();
    return body;
}

async function clearPreviewDraft(path, revision) {
    const rev = revision == null ? bumpStageRev() : revision;
    try {
        const body = { revision: rev };
        if (path) body.path = path;
        await apiSend('POST', '/__studio/api/preview/clear', body);
    } catch { /* the next preview request falls back to the file on disk */ }
}

function releasePreviewDraft() {
    window.clearTimeout(saveTimer);
    const path = state.path;
    if (!previewStaged) {
        setSaveState(state.doc?.dirty ? 'git' : 'saved', state.doc?.dirty ? 'This file differs from the last git commit.' : 'Saved.');
        updateChrome();
        return;
    }
    previewStaged = false;
    const revision = bumpStageRev();
    void clearPreviewDraft(path, revision).then(() => {
        if (normalizePath(state.path) !== normalizePath(path)) return;
        if (state.codeDirty || state.galleryDirty) return;
        presentPreview({ fresh: true });
    });
    setSaveState(state.doc?.dirty ? 'git' : 'saved', state.doc?.dirty ? 'This file differs from the last git commit.' : 'Saved.');
    updateChrome();
}

async function stagePreview(revision, { reload = true } = {}) {
    if (!state.doc || !state.path) return false;
    if (normalizePath(state.path) === CONFIG_PAGE_PATH) return true;
    if (!state.codeDirty && !state.galleryDirty) return true;
    if (revision !== stageRev) return false;
    const path = state.path;
    try {
        await apiSend('POST', '/__studio/api/preview/stage', stageBody(revision));
    } catch (error) {
        if (revision !== stageRev || normalizePath(state.path) !== normalizePath(path)) return false;
        setSaveState('unsaved', error.message || 'Preview could not be updated. The file is still unsaved.');
        updateChrome();
        return false;
    }
    if (revision !== stageRev || normalizePath(state.path) !== normalizePath(path)) return false;
    previewStaged = true;
    syncActiveTabCache();
    if (reload && state.mode !== 'gallery' && inlineApi?.getMode?.() !== 'edit') {
        const navigated = presentPreview({ fresh: true });
        if (!navigated) reloadPreview();
    }
    return true;
}

function scheduleSave(options = {}) {
    const revision = bumpStageRev();
    window.clearTimeout(saveTimer);
    const reload = options.reload !== false;
    const delay = Number.isFinite(options.delay) ? options.delay : SAVE_DELAY;
    setSaveState('unsaved', 'Edits have not been written yet.');
    updateChrome();
    saveTimer = window.setTimeout(() => { void stagePreview(revision, { reload }); }, delay);
}

async function saveCurrent() {
    if (!state.doc) {
        announce('Open a page before saving.');
        return false;
    }
    const editing = inlineApi?.getMode?.() === 'edit';
    const ok = await flushSave({ reload: !editing, runPageValues: false });
    if (!ok || state.saveState === 'error') return false;
    announce(state.doc?.dirty ? 'Saved. The file differs from the last git commit.' : 'Saved.');
    return true;
}

async function saveAndSync() {
    if (!state.doc || !state.sourceLoaded || !state.path) {
        announce('Open a page with source before syncing page values.');
        return false;
    }
    const editing = inlineApi?.getMode?.() === 'edit';
    const ok = await flushSave({ reload: !editing, runPageValues: true, forceCode: true });
    if (!ok || state.saveState === 'error') return false;
    announce(state.doc?.dirty ? 'Saved and synced SEO. The file differs from the last git commit.' : 'Saved and synced SEO.');
    return true;
}

async function onSaveClick() {
    await saveCurrent();
}

async function flushSave(options = {}) {
    const reloadRequested = options.reload != null ? Boolean(options.reload) : saveReload;
    const runPageValues = options.runPageValues != null ? Boolean(options.runPageValues) : savePageValues;
    const forceCode = Boolean(options.forceCode);
    const revision = bumpStageRev();
    saveReload = true;
    savePageValues = true;
    const reload = reloadRequested && inlineApi?.getMode?.() !== 'edit';
    window.clearTimeout(saveTimer);
    if (!state.doc) return true;
    if (!forceCode && !state.codeDirty && !state.galleryDirty) return true;
    const galleryBlocked = state.galleryDirty && galleryApi.blocked();
    const doGallery = state.galleryDirty && state.doc?.gallerySource !== 'html' && Boolean(state.slug) && !galleryApi.blocked();
    const doCode = (state.codeDirty || forceCode) && state.sourceLoaded && Boolean(state.path);
    if (!doCode && !doGallery) {
        if (galleryBlocked) {
            setSaveState('error', EQUALS_ERROR);
            updateChrome();
            announce(EQUALS_ERROR);
            return false;
        }
        if (state.galleryDirty && !state.slug) {
            setSaveState('error', 'This file has no folder gallery to save.');
            updateChrome();
            return false;
        }
        return true;
    }
    const ticket = ++saveEpoch;
    if (saveAbort) saveAbort.abort();
    const controller = new AbortController();
    saveAbort = controller;
    const path = state.path;
    const section = state.section;
    const slug = state.slug;
    setSaveState('saving', 'Saving…');
    let failure = null;
    let wrote = false;
    try {
        if (doCode) {
            const sent = codeApi.getValue();
            try {
                const data = await apiSend('POST', '/__studio/api/page/save', {
                    path,
                    content: sent,
                    runPageValues,
                }, controller.signal);
                if (ticket !== saveEpoch) return false;
                adoptServer(data, { sent, path });
                wrote = true;
            } catch (error) {
                if (error.name === 'AbortError' || ticket !== saveEpoch) return false;
                failure = error.status === 404
                    ? new Error('Saving source needs the page API. Gallery changes are saved separately.')
                    : error;
            }
        }
        if (ticket !== saveEpoch) return false;
        if (doGallery) {
            try {
                await apiSend('POST', '/__studio/api/gallery/save', {
                    section,
                    slug,
                    images: galleryApi.payloadImages(),
                }, controller.signal);
                if (ticket !== saveEpoch) return false;
                state.galleryDirty = false;
                wrote = true;
                await refreshDocument({
                    replaceContent: !state.codeDirty && codeApi.getValue() === state.savedContent,
                    path,
                    signal: controller.signal,
                });
            } catch (error) {
                if (error.name === 'AbortError' || ticket !== saveEpoch) return false;
                if (!failure) failure = error;
            }
        }
        if (ticket !== saveEpoch) return false;
        if (galleryBlocked) {
            setSaveState('error', EQUALS_ERROR);
            updateChrome();
            announce(EQUALS_ERROR);
            return false;
        }
        if (failure) {
            setSaveState('error', failure.message || 'Save failed');
            updateChrome();
            announce(failure.message || 'Save failed');
            return false;
        }
        if (state.codeDirty || state.galleryDirty) {
            setSaveState('unsaved', 'Edits have not been written yet.');
            updateChrome();
            scheduleSave({ delay: 50, reload });
            return true;
        }
        previewStaged = false;
        await clearPreviewDraft(path, revision);
        if (ticket !== saveEpoch) return false;
        if (wrote) {
            state.savedImagesSig = imageSignature(state.doc?.images);
            undoStack?.record(path, currentDocSnapshot(), {
                kind: 'save',
                label: runPageValues && forceCode ? 'Saved to disk (Synced SEO)' : 'Saved to disk',
                saved: true,
            });
        }
        if (wrote && reload && state.mode !== 'gallery') {
            const navigated = presentPreview({ fresh: true });
            if (!navigated) reloadPreview();
        }
        setSaveState('saved', state.doc?.dirty ? 'Saved. This file still differs from the last git commit.' : 'Saved.');
        updateChrome();
        void gitPanelApi?.refresh?.();
        return true;
    } catch (error) {
        if (error.name === 'AbortError' || ticket !== saveEpoch) return false;
        setSaveState('error', error.message || 'Save failed');
        updateChrome();
        announce(error.message || 'Save failed');
        return false;
    }
}

async function applyWidget(widget, data) {
    if (!state.doc || !state.path) return;
    pinActiveTab();
    const writesArchive = widget === 'newsletterArchive' && Boolean(data?.clean);
    if (!state.sourceLoaded) {
        setSaveState('error', 'Load the page source before applying this widget.');
        updateChrome();
        announce('Load the page source before applying this widget.');
        return;
    }
    const ticket = ++saveEpoch;
    if (saveAbort) saveAbort.abort();
    const controller = new AbortController();
    saveAbort = controller;
    const sent = codeApi.getValue();
    setSaveState(writesArchive ? 'saving' : 'unsaved', writesArchive ? 'Cleaning newsletter archive…' : 'Applying widget…');
    try {
        const result = await apiSend('POST', '/__studio/api/widgets/apply', {
            path: state.path,
            content: sent,
            widget,
            data,
            dryRun: !writesArchive,
        }, controller.signal);
        if (ticket !== saveEpoch) return;
        adoptServer(result, { sent, path: state.path, keepDirty: !writesArchive });
        const widgetMeta = widgetHistoryMeta(widget, data);
        undoStack?.record(state.path, currentDocSnapshot(), {
            ...widgetMeta,
            coalesce: false,
            saved: writesArchive,
        });
        if (writesArchive) {
            previewStaged = false;
            await clearPreviewDraft(state.path, bumpStageRev());
            const navigated = presentPreview({ fresh: true });
            if (!navigated) reloadPreview();
            setSaveState('saved', 'Newsletter archive updated.');
            updateChrome();
            announce('Newsletter archive updated.');
            return;
        }
        state.codeDirty = codeApi.getValue() !== state.savedContent;
        if (state.codeDirty || state.galleryDirty) {
            await stagePreview(bumpStageRev(), { reload: true });
            setSaveState('unsaved', 'Widget settings are in the editor. Save to write this file.');
        } else releasePreviewDraft();
        updateChrome();
        announce(state.codeDirty ? 'Widget settings are ready. Save to write this file.' : 'Widget settings applied.');
    } catch (error) {
        if (error.name === 'AbortError' || ticket !== saveEpoch) return;
        setSaveState('error', error.message || 'Widget apply failed');
        updateChrome();
        announce(error.message || 'Widget apply failed');
    }
}

async function refreshDocument({ replaceContent, path, signal }) {
    try {
        const data = await apiSend('GET', `/__studio/api/page?path=${encodeURIComponent(path)}`, null, signal);
        adoptServer(data, { sent: replaceContent ? codeApi.getValue() : null, path, replaceContent });
    } catch {
        if (!state.section || !state.slug) return;
        try {
            const data = await apiSend('GET', `/__studio/api/gallery?section=${encodeURIComponent(state.section)}&slug=${encodeURIComponent(state.slug)}`, null, signal);
            adoptServer(data, { path, replaceContent: false });
        } catch { /* keep the editor buffer */ }
    }
}

function adoptServer(data, { sent = null, path, replaceContent = true, keepDirty = false } = {}) {
    const incoming = data && typeof data === 'object' ? data : {};
    if (incoming.engine && typeof incoming.content === 'string') {
        primeEngineBundle(incoming.content, incoming.engine);
    }
    if (incoming.config) applyStudioConfig(incoming.config);
    const target = normalizePath(incoming.path || path || state.path);
    if (target && normalizePath(state.path) && target !== normalizePath(state.path)) return;
    const merged = normalizePage(
        { ...(state.doc || {}), ...incoming, path: incoming.path || path || state.path },
        locate(path || state.path),
        {
            explicitWidgets: Boolean(incoming.widgets) || Boolean(state.doc?.explicitWidgets),
            explicitErrors: Array.isArray(incoming.errors) || Boolean(state.doc?.explicitErrors),
        },
    );
    const previous = state.doc?.images || [];
    const imagesChanged = imageSignature(previous) !== imageSignature(merged.images);
    if (!imagesChanged) merged.images = previous;
    const current = codeApi.getValue();
    const canReplace = replaceContent && typeof incoming.content === 'string' && (sent == null || current === sent || keepDirty);
    const holdEditor = canReplace && !keepDirty && inlineApi?.isSessionActive?.() && incoming.content !== current;
    if (keepDirty && canReplace) {
        codeApi.setValue(incoming.content);
        state.codeDirty = incoming.content !== state.savedContent;
        state.sourceLoaded = true;
        merged.sourceLoaded = true;
    } else if (holdEditor) {
        state.savedContent = incoming.content;
        state.codeDirty = true;
        state.sourceLoaded = true;
        merged.sourceLoaded = true;
    } else if (canReplace) {
        codeApi.setValue(incoming.content);
        state.savedContent = incoming.content;
        state.codeDirty = false;
        state.sourceLoaded = true;
        merged.sourceLoaded = true;
    } else if (typeof incoming.content === 'string') {
        state.savedContent = incoming.content;
        state.codeDirty = current !== incoming.content;
        state.sourceLoaded = true;
        merged.sourceLoaded = true;
    } else {
        merged.sourceLoaded = state.sourceLoaded;
        merged.content = state.sourceLoaded ? current : (merged.content || '');
    }
    merged.content = state.sourceLoaded ? codeApi.getValue() : (merged.content || '');
    state.doc = merged;
    state.section = merged.section || state.section;
    state.slug = merged.slug || state.slug;
    state.path = merged.path || state.path;
    const savedTab = findTab(state.path) || findTab(path);
    if (savedTab) savedTab.path = state.path;
    codeApi.setContext(editorFacts({
        path: state.path,
        section: state.section,
        slug: state.slug,
        editable: state.sourceLoaded,
        message: state.sourceLoaded ? '' : (merged.sourceMessage || ''),
    }));
    if (imagesChanged) galleryApi.setDoc(merged);
    else galleryApi.repaint();
    widgetsApi.setDoc(merged);
    patchFolderFromDoc(merged);
    updateGalleryTab();
    renderTree({ reveal: false });
    syncActiveTabCache();
    refreshEditors();
}

async function revertFile() {
    if (!state.doc) return;
    const local = state.codeDirty || state.galleryDirty;
    const git = Boolean(state.doc.dirty);
    if (!local && !git) return;
    const yes = await confirmDialog(
        'Revert?',
        git ? 'Restore this folder from git and discard unsaved edits.' : 'Discard unsaved edits and reload this file from disk.',
        'Revert',
    );
    if (!yes) return;
    window.clearTimeout(saveTimer);
    if (saveAbort) saveAbort.abort();
    saveEpoch += 1;
    previewStaged = false;
    void clearPreviewDraft(state.path, bumpStageRev());
    state.codeDirty = false;
    state.galleryDirty = false;
    if (git) {
        if (!state.slug) {
            setSaveState('error', 'Revert from git is available for folder pages.');
            updateChrome();
            return;
        }
        try {
            setSaveState('saving', 'Reverting…');
            await apiSend('POST', '/__studio/api/gallery/revert', { section: state.section, slug: state.slug });
        } catch (error) {
            setSaveState('error', error.message || 'Revert failed');
            updateChrome();
            announce(error.message || 'Revert failed');
            return;
        }
    }
    revertPendingPath = state.path;
    await openPath(state.path, { force: true });
    void gitPanelApi?.refresh?.();
    announce(git ? 'Restored from git.' : 'Reloaded from disk.');
}

async function onCreated(result) {
    await loadTree();
    void gitPanelApi?.refresh?.();
    if (result?.path) {
        await openPath(result.path, { force: true, mode: 'code', pin: true });
        codeApi.focus();
    }
}

async function syncAfterExternalChanges(paths = []) {
    const cleanPaths = (Array.isArray(paths) ? paths : []).map((p) => normalizePath(p)).filter(Boolean);
    for (const tab of state.tabs) {
        const tabPath = normalizePath(tab.path);
        if (!cleanPaths.length || cleanPaths.includes(tabPath)) {
            if (tabPath !== normalizePath(state.path)) {
                tab.loaded = false;
            }
        }
    }
    if (state.path && (!cleanPaths.length || cleanPaths.includes(normalizePath(state.path)))) {
        await openPath(state.path, { force: true });
    }
    await loadTree();
    void gitPanelApi?.refresh?.();
}

function setLeftShelfTab(tab, { silent = false, focus = false } = {}) {
    if (!['files', 'search', 'git'].includes(tab)) return;
    state.leftShelfTab = tab;
    leftShelfApi?.setTab?.(tab, { silent: true, focusTab: focus });
    if (!silent) {
        rememberPanelState();
        syncMenu();
        if (tab === 'search') projectSearchApi?.focus?.();
        else if (tab === 'git') void gitPanelApi?.refresh?.();
    }
}

function remapRenamedPath(path, oldPath, newPath, isFolder) {
    const clean = normalizePath(path);
    const oldClean = normalizePath(oldPath);
    const newClean = normalizePath(newPath);
    if (!clean || !oldClean || !newClean) return path;
    if (clean === oldClean) return newClean;
    if (isFolder && clean.startsWith(`${oldClean}/`)) {
        return `${newClean}${clean.slice(oldClean.length)}`;
    }
    return path;
}

function remapRenamedSlug(slug, oldSlug, newSlug) {
    const cur = String(slug || '').trim();
    const from = String(oldSlug || '').trim();
    const to = String(newSlug || '').trim();
    if (!cur || !from || !to) return cur;
    if (cur === from) return to;
    if (cur.startsWith(`${from}/`)) return `${to}${cur.slice(from.length)}`;
    return cur;
}

function remapDocSlides(list, oldPrefix, newPrefix) {
    if (!Array.isArray(list) || !oldPrefix || !newPrefix) return;
    for (const item of list) {
        if (!item || typeof item !== 'object') continue;
        if (typeof item.slideUrl === 'string') {
            item.slideUrl = remapRenamedPath(item.slideUrl, oldPrefix, newPrefix, true);
        }
        if (typeof item.thumbUrl === 'string') {
            item.thumbUrl = remapRenamedPath(item.thumbUrl, oldPrefix, newPrefix, true);
        }
    }
}

async function handleTreeRename(payload) {
    const kind = payload?.kind === 'section'
        ? 'section'
        : (payload?.kind === 'folder' ? 'folder' : 'file');
    const body = kind === 'section'
        ? {
            kind: 'section',
            section: payload.section,
            newName: payload.newName,
            path: payload.path || `/${payload.section}`,
        }
        : (kind === 'folder'
            ? {
                kind: 'folder',
                section: payload.section,
                oldSlug: payload.slug,
                newName: payload.newName,
                path: payload.path,
            }
            : {
                kind: 'file',
                path: payload.path,
                newName: payload.newName,
            });

    const result = await apiSend('POST', '/__studio/api/fs/rename', body);
    const isSection = result.kind === 'section' || kind === 'section';
    const isFolder = isSection || result.kind === 'folder' || kind === 'folder';
    const oldPath = normalizePath(result.oldPath || payload.path);
    const newPath = normalizePath(result.newPath);
    const oldSection = result.oldSection || payload.section || '';
    const section = result.section || result.newSection || (isSection ? payload.newName : payload.section) || '';
    const oldSlug = result.oldSlug ?? payload.slug ?? '';
    const newSlug = result.newSlug ?? oldSlug;

    if (oldPath && newPath && oldPath !== newPath) {
        remapOpenStateAfterMove(oldPath, newPath, isFolder, oldSlug, newSlug, oldSection, section);
        if (state.path) {
            codeApi?.setContext(editorFacts({
                path: state.path,
                section: state.section,
                slug: state.slug,
                editable: state.sourceLoaded,
                message: state.sourceLoaded ? '' : (state.doc?.sourceMessage || ''),
            }));
            if (state.codeDirty || state.galleryDirty) {
                void stagePreview(bumpStageRev(), { reload: true });
            }
        }

        try {
            const recent = readRecent();
            let recentChanged = false;
            for (const entry of recent) {
                const remapped = remapRenamedPath(entry.path, oldPath, newPath, isFolder);
                if (remapped !== entry.path) {
                    entry.path = remapped;
                    if (!isFolder) entry.label = fileName(remapped);
                    if (isSection && entry.section === oldSection) entry.section = section;
                    if (isFolder && oldSlug && newSlug) {
                        entry.slug = remapRenamedSlug(entry.slug, oldSlug, newSlug);
                    }
                    recentChanged = true;
                }
            }
            if (recentChanged) {
                localStorage.setItem(RECENT_KEY, JSON.stringify(recent));
            }
        } catch { /* private mode */ }
    }

    if (isSection && oldSection && section && oldSection !== section) {
        if (state.expandedSections.has(oldSection)) {
            state.expandedSections.delete(oldSection);
            state.expandedSections.add(section);
        }
        const nextExpanded = new Set();
        const oldPrefix = `${oldSection}/`;
        for (const key of state.expandedFolders) {
            if (key.startsWith(oldPrefix)) {
                nextExpanded.add(`${section}/${key.slice(oldPrefix.length)}`);
            } else {
                nextExpanded.add(key);
            }
        }
        state.expandedFolders = nextExpanded;
    } else if (section) {
        state.expandedSections.add(section);
    }

    if (!isSection && isFolder && section && oldSlug && newSlug) {
        const oldFolderKey = `${section}/${oldSlug}`;
        const newFolderKey = `${section}/${newSlug}`;
        const nextExpanded = new Set();
        let wasExpanded = false;
        for (const key of state.expandedFolders) {
            if (key === oldFolderKey) {
                nextExpanded.add(newFolderKey);
                wasExpanded = true;
            } else if (key.startsWith(`${oldFolderKey}/`)) {
                nextExpanded.add(`${newFolderKey}${key.slice(oldFolderKey.length)}`);
            } else {
                nextExpanded.add(key);
            }
        }
        if (wasExpanded) nextExpanded.add(newFolderKey);
        state.expandedFolders = nextExpanded;
    } else if (!isFolder && section && newSlug) {
        state.expandedFolders.add(`${section}/${newSlug}`);
    }

    const focusTreeKey = isSection
        ? `section:${section}`
        : (isFolder ? `folder:${section}/${newSlug}` : `file:${newPath}`);

    await loadTree({ focusTreeKey, reveal: true });
    syncTreeOpsHistory(result);
    updateChrome();
    rememberSession();
    void gitPanelApi?.refresh?.();
    announce(`Renamed ${payload.oldName} to ${payload.newName}.`);
    return result;
}

function syncTreeOpsHistory(result) {
    if (!result || typeof result !== 'object') return;
    if (typeof result.canUndo === 'boolean') {
        state.treeUndo = {
            canUndo: Boolean(result.canUndo),
            canRedo: Boolean(result.canRedo),
            undoLabel: String(result.undoLabel || ''),
            redoLabel: String(result.redoLabel || ''),
        };
        syncMenu();
    }
}

function clearTreeCut() {
    if (state.treeClipboard?.mode !== 'cut') return false;
    state.treeClipboard = null;
    renderTree({ reveal: false });
    announce('Cleared cut selection.');
    return true;
}

function handleTreeCut(info) {
    if (!info || (info.kind !== 'file' && info.kind !== 'folder' && info.kind !== 'section')) return;
    if (info.kind === 'section' && info.section === '/') return;
    state.treeClipboard = { ...info, mode: 'cut' };
    renderTree({ reveal: false });
    announce(`Cut ${info.name || info.slug || info.section || info.path}. Select a destination and press Ctrl+V to move.`);
}

function handleTreeCopy(info) {
    if (!info || (info.kind !== 'file' && info.kind !== 'folder' && info.kind !== 'section')) return;
    if (info.kind === 'section' && info.section === '/') return;
    state.treeClipboard = { ...info, mode: 'copy' };
    renderTree({ reveal: false });
    announce(`Copied ${info.name || info.slug || info.section || info.path}. Select a destination and press Ctrl+V to paste.`);
}

async function handleTreeDuplicate(info) {
    if (!info || (info.kind !== 'file' && info.kind !== 'folder' && info.kind !== 'section')) return;
    if (info.kind === 'section' && info.section === '/') return;
    try {
        const result = await apiSend('POST', '/__studio/api/fs/duplicate', {
            kind: info.kind,
            path: info.path,
            section: info.section,
            slug: info.slug,
        });
        syncTreeOpsHistory(result);
        const isSection = result.kind === 'section';
        const isFolder = isSection || result.kind === 'folder';
        const sec = result.section || info.section;
        if (sec) state.expandedSections.add(sec);
        if (sec && result.newSlug) state.expandedFolders.add(`${sec}/${result.newSlug}`);
        const focusTreeKey = isSection
            ? `section:${sec}`
            : (isFolder ? `folder:${sec}/${result.newSlug}` : `file:${normalizePath(result.newPath)}`);
        await loadTree({ focusTreeKey, reveal: true });
        const openTarget = isFolder ? result.primaryFile : result.newPath;
        if (openTarget) await openPath(openTarget, { pin: true });
        void gitPanelApi?.refresh?.();
        announce(`Duplicated ${info.name || info.slug || info.section} as ${result.newName || result.newSlug || result.section}.`);
    } catch (error) {
        announce(error.message || 'Could not duplicate item.');
    }
}

async function handleTreePaste(targetInfo) {
    const clip = state.treeClipboard;
    if (!clip || !clip.path) {
        announce('Nothing in file tree clipboard to paste.');
        return;
    }
    const dest = targetInfo || treeApi?.getSelectedRowInfo?.() || {
        section: state.section || '/',
        slug: state.slug,
    };
    if (!dest || !dest.section) {
        announce('Select a section or folder in the file tree before pasting.');
        return;
    }
    const isCut = clip.mode === 'cut';
    const endpoint = isCut ? '/__studio/api/fs/move' : '/__studio/api/fs/copy';
    try {
        const result = await apiSend('POST', endpoint, {
            kind: clip.kind,
            path: clip.path,
            section: clip.section,
            slug: clip.slug,
            targetSection: dest.section,
            targetSlug: (clip.kind === 'folder' || clip.kind === 'section') ? '' : (dest.slug || ''),
        });
        syncTreeOpsHistory(result);
        const isSection = result.kind === 'section';
        const isFolder = isSection || result.kind === 'folder';
        const oldPath = normalizePath(result.oldPath || clip.path);
        const newPath = normalizePath(result.newPath);
        const sec = result.section || dest.section;
        if (isCut) {
            state.treeClipboard = null;
            if (oldPath && newPath && oldPath !== newPath) {
                remapOpenStateAfterMove(
                    oldPath,
                    newPath,
                    isFolder,
                    result.oldSlug || clip.slug,
                    result.newSlug || '',
                    result.oldSection || clip.section || '',
                    sec,
                );
            }
        }
        if (sec) state.expandedSections.add(sec);
        if (sec && result.newSlug) state.expandedFolders.add(`${sec}/${result.newSlug}`);
        const focusTreeKey = isSection
            ? `section:${sec}`
            : (isFolder ? `folder:${sec}/${result.newSlug}` : `file:${newPath}`);
        await loadTree({ focusTreeKey, reveal: true });
        updateChrome();
        rememberSession();
        void gitPanelApi?.refresh?.();
        announce(`${isCut ? 'Moved' : 'Pasted'} ${clip.name || clip.slug || clip.section} to ${newPath}.`);
    } catch (error) {
        announce(error.message || (isCut ? 'Could not move item.' : 'Could not paste item.'));
    }
}

async function handleTreeDelete(info) {
    if (!info || (info.kind !== 'file' && info.kind !== 'folder' && info.kind !== 'section')) return;
    if (info.kind === 'section' && info.section === '/') return;
    const isSection = info.kind === 'section';
    const isFolder = isSection || info.kind === 'folder';
    const label = info.name || info.slug || info.section || info.path;
    const confirmed = await confirmDialog(
        isSection ? `Delete section "${label}"?` : (isFolder ? `Delete folder "${label}"?` : `Delete file "${label}"?`),
        `This moves "${info.path}" to Studio trash so you can undo it at any time with Ctrl+Z (or Edit → Undo).`,
        isSection ? 'Delete Section' : (isFolder ? 'Delete Folder' : 'Delete File'),
    );
    if (!confirmed) return;
    try {
        const result = await apiSend('POST', '/__studio/api/fs/delete', {
            kind: info.kind,
            path: info.path,
            section: info.section,
            slug: info.slug,
        });
        syncTreeOpsHistory(result);
        if (state.treeClipboard?.path === info.path) state.treeClipboard = null;
        if (isSection && info.section) state.expandedSections.delete(info.section);
        await closeDeletedPaths(normalizePath(result.deletedPath || info.path), isFolder);
        await loadTree({ reveal: false });
        updateChrome();
        rememberSession();
        void gitPanelApi?.refresh?.();
        announce(`Deleted ${label}. Press Ctrl+Z in the file tree to undo.`);
    } catch (error) {
        announce(error.message || 'Could not delete item.');
    }
}

function handleTreeNewPage(info) {
    const section = info?.section || state.section || '/';
    const slug = (info?.kind === 'folder' || info?.kind === 'file') ? (info?.slug || '') : '';
    newPageApi?.open?.({ section, slug, templateId: 'blank' });
}

async function handleTreeNewFolder(info) {
    const isRootTarget = !info || !info.section || info.section === '/';
    const parentSec = isRootTarget ? '/' : info.section;
    const parentSlug = (info?.kind === 'folder' || info?.kind === 'file') ? (info?.slug || '') : '';
    const parentPath = isRootTarget
        ? '/'
        : (parentSlug ? `/${parentSec}/${parentSlug}` : `/${parentSec}`);

    return new Promise((resolve) => {
        const dialog = el('dialog', { class: 'confirm-dialog' });
        const form = el('form', { method: 'dialog' });
        const titleText = isRootTarget ? 'New Top-Level Section / Folder' : `New Folder in ${parentPath}`;
        const hintText = isRootTarget
            ? 'Creates a new top-level section folder at the website root (/).'
            : `Creates a new folder inside ${parentPath}.`;
        const fieldWrap = el('label', { class: 'field', text: isRootTarget ? 'Section / folder name' : 'Folder name' });
        const nameInput = el('input', {
            type: 'text',
            autocomplete: 'off',
            spellcheck: 'false',
            placeholder: isRootTarget ? 'blog' : 'new-folder',
        });
        fieldWrap.append(nameInput);
        const errNode = el('p', { class: 'field-error', hidden: true, role: 'alert' });
        const actions = el('div', { class: 'confirm-actions' });
        const cancel = el('button', { class: 'btn', type: 'button', text: 'Cancel' });
        const createBtn = el('button', { class: 'btn btn-primary', type: 'submit', text: 'Create Folder' });
        actions.append(cancel, createBtn);
        form.append(
            el('h2', { text: titleText }),
            el('p', { class: 'field-hint', text: hintText }),
            fieldWrap,
            errNode,
            actions,
        );
        dialog.append(form);

        let busy = false;
        cancel.addEventListener('click', () => {
            dialog.close();
            resolve(null);
        });
        dialog.addEventListener('close', () => {
            dialog.remove();
            resolve(null);
        }, { once: true });
        nameInput.addEventListener('input', () => {
            errNode.hidden = true;
            errNode.textContent = '';
        });
        form.addEventListener('submit', async (event) => {
            event.preventDefault();
            if (busy) return;
            const rawName = nameInput.value.trim();
            if (!rawName || !/^[A-Za-z0-9][A-Za-z0-9_-]*$/.test(rawName)) {
                errNode.hidden = false;
                errNode.textContent = 'Enter a folder name using letters, numbers, hyphens, and underscores.';
                nameInput.focus();
                return;
            }
            busy = true;
            createBtn.disabled = true;
            createBtn.textContent = 'Creating…';
            errNode.hidden = true;
            try {
                const result = await apiSend('POST', '/__studio/api/fs/mkdir', {
                    section: parentSec,
                    slug: parentSlug,
                    name: rawName,
                });
                syncTreeOpsHistory(result);
                const isSection = result.kind === 'section';
                const sec = result.section || (isRootTarget ? rawName : parentSec);
                const newSlug = result.newSlug ?? result.slug ?? '';
                if (sec) state.expandedSections.add(sec);
                if (sec && newSlug) state.expandedFolders.add(`${sec}/${newSlug}`);
                const focusTreeKey = isSection ? `section:${sec}` : `folder:${sec}/${newSlug}`;
                dialog.close();
                await loadTree({ focusTreeKey, reveal: true });
                void gitPanelApi?.refresh?.();
                announce(`Created folder ${result.path || `/${sec}`}.`);
                resolve(result);
            } catch (error) {
                busy = false;
                createBtn.disabled = false;
                createBtn.textContent = 'Create Folder';
                errNode.hidden = false;
                errNode.textContent = error.message || 'Could not create folder.';
                nameInput.focus();
            }
        });

        document.body.append(dialog);
        dialog.showModal();
        nameInput.focus();
    });
}

async function handleTreeUndo() {
    try {
        const result = await apiSend('POST', '/__studio/api/fs/undo', {});
        syncTreeOpsHistory(result);
        const isSection = result.kind === 'section';
        const isFolder = isSection || result.kind === 'folder';
        const op = result.undoneOp || '';
        const oldPath = normalizePath(result.oldPath || '');
        const newPath = normalizePath(result.newPath || '');
        if ((op === 'rename' || op === 'move') && oldPath && newPath) {
            remapOpenStateAfterMove(
                oldPath,
                newPath,
                isFolder,
                result.oldSlug || '',
                result.newSlug || '',
                result.oldSection || '',
                result.section || '',
            );
        } else if ((op === 'duplicate' || op === 'copy' || op === 'mkdir') && oldPath) {
            await closeDeletedPaths(oldPath, isFolder);
        }
        if (result.section) state.expandedSections.add(result.section);
        if (result.section && result.newSlug) state.expandedFolders.add(`${result.section}/${result.newSlug}`);
        const focusTreeKey = newPath
            ? (isSection ? `section:${result.section}` : (isFolder ? `folder:${result.section}/${result.newSlug}` : `file:${newPath}`))
            : '';
        await loadTree({ focusTreeKey, reveal: Boolean(focusTreeKey) });
        if (op === 'delete' && (result.primaryFile || newPath)) {
            if (result.primaryFile) await openPath(result.primaryFile, { pin: true });
        }
        updateChrome();
        rememberSession();
        void gitPanelApi?.refresh?.();
        announce(`Undid: ${result.label || op}.`);
    } catch (error) {
        announce(error.message || 'Nothing to undo in file tree.');
    }
}

async function handleTreeRedo() {
    try {
        const result = await apiSend('POST', '/__studio/api/fs/redo', {});
        syncTreeOpsHistory(result);
        const isSection = result.kind === 'section';
        const isFolder = isSection || result.kind === 'folder';
        const op = result.redoneOp || '';
        const oldPath = normalizePath(result.oldPath || '');
        const newPath = normalizePath(result.newPath || '');
        if ((op === 'rename' || op === 'move') && oldPath && newPath) {
            remapOpenStateAfterMove(
                oldPath,
                newPath,
                isFolder,
                result.oldSlug || '',
                result.newSlug || '',
                result.oldSection || '',
                result.section || '',
            );
        } else if (op === 'delete' && oldPath) {
            await closeDeletedPaths(oldPath, isFolder);
        }
        if (result.section) state.expandedSections.add(result.section);
        if (result.section && result.newSlug) state.expandedFolders.add(`${result.section}/${result.newSlug}`);
        const focusTreeKey = newPath
            ? (isSection ? `section:${result.section}` : (isFolder ? `folder:${result.section}/${result.newSlug}` : `file:${newPath}`))
            : '';
        await loadTree({ focusTreeKey, reveal: Boolean(focusTreeKey) });
        updateChrome();
        rememberSession();
        void gitPanelApi?.refresh?.();
        announce(`Redid: ${result.label || op}.`);
    } catch (error) {
        announce(error.message || 'Nothing to redo in file tree.');
    }
}

function remapOpenStateAfterMove(oldPath, newPath, isFolder, oldSlug, newSlug, oldSection = '', newSection = '') {
    undoStack?.renamePath(oldPath, newPath, { isFolder });
    for (const tab of state.tabs) {
        const nextTabPath = remapRenamedPath(tab.path, oldPath, newPath, isFolder);
        if (nextTabPath !== tab.path) {
            tab.path = nextTabPath;
            if (tab.doc) {
                tab.doc.path = nextTabPath;
                if (tab.doc.previewUrl) {
                    tab.doc.previewUrl = remapRenamedPath(tab.doc.previewUrl, oldPath, newPath, isFolder);
                }
                if (isFolder && oldSection && newSection && tab.doc.section === oldSection) {
                    tab.doc.section = newSection;
                }
                if (isFolder && oldSlug && newSlug) {
                    tab.doc.slug = remapRenamedSlug(tab.doc.slug, oldSlug, newSlug);
                }
                if (isFolder) {
                    remapDocSlides(tab.doc.images, oldPath, newPath);
                    remapDocSlides(tab.doc.unlistedSlides, oldPath, newPath);
                }
            }
        }
    }
    if (state.path) {
        const nextActive = remapRenamedPath(state.path, oldPath, newPath, isFolder);
        if (nextActive !== state.path) {
            state.path = nextActive;
            if (isFolder && oldSection && newSection && state.section === oldSection) {
                state.section = newSection;
            }
            if (isFolder && oldSlug && newSlug) {
                state.slug = remapRenamedSlug(state.slug, oldSlug, newSlug);
            }
            if (state.doc) {
                state.doc.path = nextActive;
                if (isFolder && oldSection && newSection && state.doc.section === oldSection) {
                    state.doc.section = newSection;
                }
                if (isFolder && oldSlug && newSlug) {
                    state.doc.slug = state.slug;
                }
                if (state.doc.previewUrl) {
                    state.doc.previewUrl = remapRenamedPath(state.doc.previewUrl, oldPath, newPath, isFolder);
                }
                if (isFolder) {
                    remapDocSlides(state.doc.images, oldPath, newPath);
                    remapDocSlides(state.doc.unlistedSlides, oldPath, newPath);
                }
            }
            writeHash();
            presentPreview({ fresh: true });
        }
    }
}

async function closeDeletedPaths(deletedPath, isFolder) {
    if (!deletedPath) return;
    const prefix = `${deletedPath}/`;
    const matches = (p) => {
        const norm = normalizePath(p);
        return norm === deletedPath || (isFolder && norm.startsWith(prefix));
    };
    for (const tab of [...state.tabs]) {
        if (matches(tab.path)) {
            tab.codeDirty = false;
            tab.galleryDirty = false;
            const idx = state.tabs.indexOf(tab);
            if (idx >= 0) state.tabs.splice(idx, 1);
            await clearPreviewDraft(tab.path, bumpStageRev());
        }
    }
    if (state.path && matches(state.path)) {
        const next = state.tabs[0] || null;
        if (next) await showTab(next);
        else clearEditor();
    }
}

function renderTree({ reveal = false, focusTreeKey = '' } = {}) {
    treeApi?.render({
        sections: state.sections,
        expandedSections: state.expandedSections,
        expandedFolders: state.expandedFolders,
        activePath: state.path,
        query: state.treeQuery,
        filter: state.treeFilter,
        cutTreeKey: state.treeClipboard?.mode === 'cut' ? state.treeClipboard.treeKey : '',
        reveal,
        focusTreeKey,
    });
}

function updateChrome() {
    const crumb = state.path ? breadcrumb(state.path) : (projectFolderName() || 'No file');
    els.crumb.textContent = crumb;
    const fullPath = [state.repoRoot, state.path].filter(Boolean).join('/');
    els.crumb.title = state.path
        ? `${fullPath} — Click or hover to see the full repository path of the active file`
        : 'No file is open';
    const label = activeTitle();
    document.title = label ? `${label} — ${productTitle()}` : productTitle();
    if (els.version) els.version.title = versionTip();
    els.revert.hidden = !(state.doc && (state.doc.dirty || state.codeDirty || state.galleryDirty));
    els.codeTab?.classList.toggle('is-unsaved', Boolean(state.codeDirty));
    els.galleryTab?.classList.toggle('is-unsaved', Boolean(state.galleryDirty));
    els.saveNow.disabled = !state.doc;
    inlineApi?.setEnabled(Boolean(state.sourceLoaded && state.path));
    updateGalleryTab();
    refreshEditors();
    syncUndoUi();
    syncMenu();
}

function updateGalleryTab() {
    const count = state.doc ? (galleryApi?.imageCount?.() ?? state.doc?.images?.length ?? 0) : 0;
    const unlisted = state.doc ? (galleryApi?.unlistedCount?.() ?? state.doc?.unlistedSlides?.length ?? 0) : 0;
    const photos = `${count} ${count === 1 ? 'photo' : 'photos'}`;
    const label = unlisted > 0 ? `${photos} + Unlisted` : photos;
    if (els.galleryCount) {
        els.galleryCount.textContent = label;
        els.galleryCount.title = unlisted > 0
            ? `${photos}. ${unlisted} slide${unlisted === 1 ? '' : 's'} on disk are not listed in the gallery.`
            : `${photos} in the gallery`;
    }
    els.galleryTab.hidden = false;
}

function setMode(mode, { silent = false } = {}) {
    const previous = state.mode === 'gallery' ? 'gallery' : 'code';
    let next = mode === 'gallery' ? 'gallery' : 'code';
    if (next === 'gallery' && !state.doc && !state.path) next = 'code';
    if (next === 'gallery') refreshHtmlGalleryFromEditor();
    state.mode = next;
    if (els.center) els.center.dataset.pageView = next;
    for (const tab of els.modeTabs) {
        const on = tab.dataset.mode === next;
        tab.setAttribute('aria-selected', on ? 'true' : 'false');
        tab.tabIndex = on ? 0 : -1;
    }
    els.codeMount.hidden = next !== 'code';
    els.galleryMount.hidden = next !== 'gallery';
    if (els.gallerySheet) els.gallerySheet.hidden = next !== 'gallery';
    if (els.previewBar) els.previewBar.hidden = next === 'gallery';
    if (els.stage) els.stage.hidden = next === 'gallery';
    galleryApi?.setActive(next === 'gallery');
    els.preview?.setAttribute('aria-label', next === 'gallery' ? 'Gallery contact sheet' : 'Rendered result');
    els.hDivider?.setAttribute('aria-label', next === 'gallery' ? 'Resize caption editor and gallery' : 'Resize code editor and preview');
    if (els.hDivider) {
        els.hDivider.title = next === 'gallery'
            ? 'Drag to resize the caption editor and the gallery'
            : 'Drag to resize Top Code Editor vs Bottom Rendered Preview height';
    }
    if (next === 'gallery') {
        if (state.previewFull) setPreviewFull(false);
        parkPreview();
    } else if (previous === 'gallery') {
        if (state.codeDirty || state.galleryDirty) void stagePreview(bumpStageRev(), { reload: true });
        else presentPreview({ fresh: true });
    }
    paintSplit(shownRatio());
    const tab = findTab(state.path);
    if (tab) tab.mode = state.mode;
    if (!silent) writeHash();
    layoutPreview();
    syncMenu();
}

function setSaveState(kind, detail) {
    state.saveState = kind;
    if (detail != null) state.saveDetail = detail;
    const labels = {
        saved: 'Saved',
        saving: 'Saving…',
        unsaved: 'Unsaved Changes',
        git: 'Modified in Git',
        error: 'Error',
    };
    els.save.textContent = labels[kind] || 'Saved';
    els.save.dataset.tone = kind === 'error' ? 'danger' : kind === 'saved' ? 'ok' : 'warn';
    els.save.title = saveStatusTip(kind, state.saveDetail);
}

function saveStatusTip(kind, detail) {
    if (kind === 'error') return detail || 'The last save failed. The edits are still in the editor.';
    if (kind === 'saving') return detail || 'Writing the active code buffer and gallery changes to disk';
    if (kind === 'unsaved') return 'Unsaved Changes — edits are in the editor and have not been written to disk';
    if (kind === 'git') return 'Modified in Git — the folder on disk has uncommitted changes that differ from HEAD';
    return 'Saved — changes are written to disk and this folder matches the last Git commit';
}

function versionTip() {
    const version = state.version ? `v${state.version}` : '';
    return version
        ? `ClassicCars Studio ${version} — click for About & server details`
        : 'ClassicCars Studio version — click for About & server details';
}

function syncTreeClear() {
    if (!els.treeClear || !els.treeSearch) return;
    els.treeClear.hidden = !els.treeSearch.value;
}

function setEditorStatus(message, isError = false) {
    const show = Boolean(isError && message);
    els.editorStatus.hidden = !show;
    els.editorStatus.classList.toggle('is-error', show);
    els.editorStatusText.textContent = message || '';
    els.editorStatusText.title = show ? message : '';
    els.editorRetry.hidden = !show;
}

function activeTitle() {
    if (!state.path) return '';
    if (state.section === 'sales') return state.doc?.details?.vehicleName || fileName(state.path);
    return state.doc?.widgets?.pageValues?.title || state.doc?.details?.vehicleName || fileName(state.path);
}

function projectFolderName() {
    const root = String(state.repoRoot || '').replace(/[\\/]+$/, '');
    const parts = root.split(/[\\/]/).filter(Boolean);
    return parts[parts.length - 1] || '';
}

function productTitle() {
    const base = state.version ? `ClassicCars Studio v${state.version}` : 'ClassicCars Studio';
    const folder = projectFolderName();
    return folder ? `${base} — ${folder}` : base;
}

function breadcrumb(path) {
    return normalizePath(path).split('/').filter(Boolean).join(' / ') || 'No file';
}

function fileName(path) {
    const parts = normalizePath(path).split('/').filter(Boolean);
    return parts[parts.length - 1] || path;
}

function revealPath(path) {
    const loc = locate(path);
    if (loc.section) state.expandedSections.add(loc.section);
    if (loc.slug) state.expandedFolders.add(`${loc.section}/${loc.slug}`);
}

function folderExists(section, slug) {
    const found = state.sections.find((item) => item.id === section);
    const needle = String(slug || '').toLowerCase();
    return Boolean(found?.folders.some((folder) => String(folder.slug || '').toLowerCase() === needle));
}

function findFolder(section, slug) {
    return state.sections.find((item) => item.id === section)?.folders.find((folder) => folder.slug === slug) || null;
}

function patchFolderFromDoc(doc) {
    const path = normalizePath(doc.path || '');
    const sectionId = doc.section || locate(path).section;
    const slug = doc.slug || locate(path).slug;
    const folder = findFolder(sectionId, slug);
    if (folder) {
        const owns = !path || fileOwnsFolderIssues(folder, path);
        folder.dirty = Boolean(doc.dirty);
        if (owns && Array.isArray(doc.errors)) folder.issueCount = doc.errors.length;
        if (Array.isArray(doc.images)) folder.photoCount = doc.images.length;
        if (Array.isArray(doc.unlistedSlides)) folder.unlistedCount = doc.unlistedSlides.length;
        if (doc.section === 'sales' && doc.details?.vehicleName) folder.name = doc.details.vehicleName;
        if (path) applyFileIssueCount(folder.files, path, doc);
    }
    if (path && Array.isArray(doc.errors)) {
        for (const section of state.sections || []) {
            if (sectionId && section.id !== sectionId) continue;
            applyFileIssueCount(section.rootFiles, path, doc);
        }
    }
}

function fileOwnsFolderIssues(folder, path) {
    const clean = normalizePath(path);
    if (!folder || !clean) return false;
    if (normalizePath(folder.primaryFile) === clean) return true;
    const pages = (folder.files || []).filter((file) => /\.(php|html|shtml|htm)$/i.test(String(file.name || file.path || '')));
    if (pages.length === 1 && normalizePath(pages[0].path) === clean) return true;
    return /\/sales\/[^/]+\/index\.php$/i.test(clean);
}

function applyFileIssueCount(files, path, doc) {
    if (!path || !Array.isArray(files) || !Array.isArray(doc?.errors)) return;
    const file = files.find((item) => normalizePath(item.path) === path);
    if (!file) return;
    file.issueCount = doc.errors.length;
}

function collectMakes() {
    const names = [];
    for (const section of state.sections) {
        for (const folder of section.folders) if (folder.makeType) names.push(folder.makeType);
    }
    return names;
}

function setTreeOpen(open, { silent = false } = {}) {
    state.treeOpen = open;
    document.documentElement.dataset.tree = open ? 'open' : 'closed';
    const inner = els.tree?.querySelector('.tree-inner');
    if (inner) inner.toggleAttribute('inert', !open);
    else els.tree.toggleAttribute('inert', !open);
    els.treeToggle.setAttribute('aria-expanded', open ? 'true' : 'false');
    if (!silent) {
        persistLayout();
        layoutPreview();
        rememberPanelState();
    }
    syncMenu();
}

function setInspectorOpen(open, { silent = false } = {}) {
    state.inspectorOpen = open;
    document.documentElement.dataset.inspector = open ? 'open' : 'closed';
    const inner = els.inspector?.querySelector('.inspector-inner');
    if (inner) inner.toggleAttribute('inert', !open);
    else els.inspector.toggleAttribute('inert', !open);
    els.inspectorToggle.setAttribute('aria-expanded', open ? 'true' : 'false');
    if (!silent) {
        persistLayout();
        layoutPreview();
        rememberPanelState();
    }
    syncMenu();
}

function setTerminalOpen(open, { silent = false, focus = false, restoreFocus = false } = {}) {
    const next = Boolean(open);
    const hadFocus = !next && terminalApi?.containsFocus?.();
    state.terminalOpen = next;
    document.documentElement.dataset.terminal = next ? 'open' : 'closed';
    if (els.terminalPanel) {
        els.terminalPanel.hidden = !next;
        els.terminalPanel.toggleAttribute('inert', !next);
    }
    if (els.terminalResizer) {
        els.terminalResizer.hidden = !next;
    }
    els.terminalToggle?.setAttribute('aria-expanded', next ? 'true' : 'false');
    if (!silent) {
        try { localStorage.setItem(TERMINAL_OPEN_KEY, next ? '1' : '0'); } catch { /* private mode */ }
        paintTerminalHeight();
        persistLayout();
        layoutPreview();
        rememberPanelState();
    }
    syncMenu();
    if (next && focus) {
        terminalApi?.focus?.();
    } else if (!next && (restoreFocus || hadFocus)) {
        codeApi?.focus?.();
    }
}

function toggleTerminal() {
    const next = !state.terminalOpen;
    setTerminalOpen(next, { focus: next, restoreFocus: !next });
    announce(next ? 'Terminal opened.' : 'Terminal hidden.');
}

function terminalHeightLimit() {
    const centerH = els.center?.clientHeight || 0;
    if (centerH < 320) return TERMINAL_HEIGHT_MAX;
    return Math.max(TERMINAL_HEIGHT_MIN, Math.min(TERMINAL_HEIGHT_MAX, Math.round(centerH * 0.72)));
}

function setTerminalHeight(px, { persistLocal = true } = {}) {
    const height = clamp(Math.round(Number(px)), TERMINAL_HEIGHT_MIN, TERMINAL_HEIGHT_MAX);
    if (!Number.isFinite(height)) return;
    state.terminalHeight = height;
    paintTerminalHeight();
    if (persistLocal) {
        try { localStorage.setItem(TERMINAL_HEIGHT_KEY, String(height)); } catch { /* private mode */ }
    }
}

function paintTerminalHeight() {
    const limit = terminalHeightLimit();
    const painted = clamp(state.terminalHeight, TERMINAL_HEIGHT_MIN, limit);
    document.documentElement.style.setProperty('--terminal-height', `${painted}px`);
    if (els.terminalResizer) {
        els.terminalResizer.setAttribute('aria-valuenow', String(painted));
        els.terminalResizer.setAttribute('aria-valuemin', String(TERMINAL_HEIGHT_MIN));
        els.terminalResizer.setAttribute('aria-valuemax', String(limit));
    }
}

function openEditorsLimit() {
    const panel = els.tree?.clientHeight || 0;
    if (panel < 280) return OPEN_EDITORS_HEIGHT_MAX;
    return Math.max(OPEN_EDITORS_HEIGHT_MIN, Math.min(OPEN_EDITORS_HEIGHT_MAX, panel - 280));
}

function fileTabbarLimit() {
    const pane = els.editor?.clientHeight || 0;
    if (pane < FILE_TABBAR_HEIGHT_MIN + 80) return FILE_TABBAR_HEIGHT_MAX;
    return Math.max(FILE_TABBAR_HEIGHT_MIN, Math.min(FILE_TABBAR_HEIGHT_MAX, pane - 80));
}

function setOpenEditorsHeight(px, { persistLocal = true } = {}) {
    const height = clamp(Math.round(Number(px)), OPEN_EDITORS_HEIGHT_MIN, OPEN_EDITORS_HEIGHT_MAX);
    if (!Number.isFinite(height)) return;
    state.openEditorsHeight = height;
    paintOpenEditorsHeight();
    if (persistLocal) {
        try { localStorage.setItem(OPEN_EDITORS_HEIGHT_KEY, String(height)); } catch { /* private mode */ }
    }
}

function paintOpenEditorsHeight() {
    const limit = openEditorsLimit();
    const painted = clamp(state.openEditorsHeight, OPEN_EDITORS_HEIGHT_MIN, limit);
    document.documentElement.style.setProperty('--open-editors-height', `${painted}px`);
    if (els.openEditorsResizer) {
        els.openEditorsResizer.setAttribute('aria-valuenow', String(painted));
        els.openEditorsResizer.setAttribute('aria-valuemin', String(OPEN_EDITORS_HEIGHT_MIN));
        els.openEditorsResizer.setAttribute('aria-valuemax', String(limit));
    }
}

function setFileTabbarHeight(px, { persistLocal = true } = {}) {
    const height = clamp(Math.round(Number(px)), FILE_TABBAR_HEIGHT_MIN, FILE_TABBAR_HEIGHT_MAX);
    if (!Number.isFinite(height)) return;
    state.fileTabbarHeight = height;
    paintFileTabbarHeight();
    if (persistLocal) {
        try { localStorage.setItem(FILE_TABBAR_HEIGHT_KEY, String(height)); } catch { /* private mode */ }
    }
}

function paintFileTabbarHeight() {
    const limit = fileTabbarLimit();
    const painted = clamp(state.fileTabbarHeight, FILE_TABBAR_HEIGHT_MIN, limit);
    document.documentElement.style.setProperty('--file-tabbar-height', `${painted}px`);
    if (els.fileTabbarResizer) {
        els.fileTabbarResizer.setAttribute('aria-valuenow', String(painted));
        els.fileTabbarResizer.setAttribute('aria-valuemin', String(FILE_TABBAR_HEIGHT_MIN));
        els.fileTabbarResizer.setAttribute('aria-valuemax', String(limit));
    }
}

function setTreeWidth(px) {
    const width = clamp(Math.round(px), 120, 800);
    state.treeWidth = width;
    document.documentElement.style.setProperty('--tree-width', `${width}px`);
    els.treeResizer?.setAttribute('aria-valuenow', String(width));
}

function setInspectorWidth(px) {
    const width = clamp(Math.round(px), 160, 800);
    state.inspectorWidth = width;
    document.documentElement.style.setProperty('--inspector-width', `${width}px`);
    els.inspectorResizer?.setAttribute('aria-valuenow', String(width));
}

function shownRatio() {
    return state.mode === 'gallery' ? state.galleryRatio : state.centerRatio;
}

function setCenterRatio(ratio) {
    const next = clampCenterRatio(ratio);
    if (state.mode === 'gallery') state.galleryRatio = next;
    else state.centerRatio = next;
    paintSplit(next);
}

function clampCenterRatio(ratio) {
    let next = Number(ratio);
    if (!Number.isFinite(next)) next = state.mode === 'gallery' ? 0.4 : 0.58;
    const height = els.center?.clientHeight || 0;
    if (height > 240) {
        const min = Math.min(0.45, 120 / height);
        next = Math.max(min, Math.min(1 - min, next));
    } else next = Math.max(0.2, Math.min(0.8, next));
    return next;
}

function paintSplit(ratio) {
    const next = Number.isFinite(ratio) ? ratio : shownRatio();
    els.editor.style.flex = `${next} 1 0px`;
    els.preview.style.flex = `${Math.max(0.05, 1 - next)} 1 0px`;
    els.hDivider?.setAttribute('aria-valuenow', String(Math.round(next * 100)));
}

function setPreviewFull(on) {
    if (state.mode === 'gallery' && on) {
        announce('Switch back to Page to expand the 1:1 preview.');
        return;
    }
    state.previewFull = Boolean(on);
    document.body.classList.toggle('preview-full', state.previewFull);
    els.full.setAttribute('aria-pressed', state.previewFull ? 'true' : 'false');
    els.full.textContent = state.previewFull ? 'Exit Full Preview' : 'Full Window Preview';
    layoutPreview();
    requestAnimationFrame(() => layoutPreview());
    syncMenu();
}

function setViewportSize(value) {
    if (!Object.prototype.hasOwnProperty.call(VIEWPORTS, value)) return;
    state.viewport = value;
    if (els.viewport && els.viewport.value !== value) els.viewport.value = value;
    layoutPreview();
    persistLayout();
    syncMenu();
}

function syncMenu() {
    menuApi?.refresh();
}

function menuSnapshot() {
    const theme = getTheme();
    const hasDomSelection = Boolean(window.getSelection && !window.getSelection().isCollapsed && String(window.getSelection()));
    const auxEditable = isAuxiliaryInput(document.activeElement) && editableNode(document.activeElement);
    return {
        recent: readRecent(),
        canSave: Boolean(state.doc),
        canCloseTab: state.tabs.length > 0,
        canReopenClosedTab: hasClosedTabs(),
        canKeepOpen: Boolean(findTab(state.path)?.isPreview),
        canSync: Boolean(state.doc && state.sourceLoaded && state.path),
        canRevert: Boolean(state.doc && (state.doc.dirty || state.codeDirty || state.galleryDirty)),
        canUndo: Boolean(undoStack?.canUndo(state.path) || state.treeUndo.canUndo || auxEditable),
        canRedo: Boolean(undoStack?.canRedo(state.path) || state.treeUndo.canRedo || auxEditable),
        canUndoHistory: Boolean(state.doc && state.path),
        canEdit: editableNode(editTarget),
        canCopy: editableNode(editTarget) || hasDomSelection || Boolean(state.terminalOpen && terminalApi?.hasSelection?.()),
        canSelectAll: editableNode(editTarget) || Boolean(state.terminalOpen),
        canInsert: Boolean(codeApi?.canInsertBlocks?.()),
        canComment: Boolean(state.sourceLoaded && codeApi?.canToggleComment?.()),
        canFind: Boolean(state.sourceLoaded),
        canPreview: Boolean(state.doc?.previewUrl),
        canPreviewEdit: state.viewportMode === 'edit' || Boolean(state.sourceLoaded && state.path),
        hasGallery: Boolean(state.doc?.hasGallery),
        mode: state.mode,
        wordWrap: Boolean(codeApi?.isWordWrap?.()),
        viewport: state.viewport,
        theme: theme.preset,
        treeOpen: state.treeOpen,
        leftShelfTab: state.leftShelfTab || 'files',
        inspectorOpen: state.inspectorOpen,
        terminalOpen: state.terminalOpen,
        previewFull: state.previewFull,
        previewEdit: state.viewportMode === 'edit',
        unlisted: galleryApi?.unlistedCount?.() || 0,
        isJalbum: Boolean(state.sourceLoaded && isJalbumPage(codeApi?.getValue?.() || '')),
        canZoomIn: zoomApi ? zoomApi.canZoomIn(macroZoomTarget()) : true,
        canZoomOut: zoomApi ? zoomApi.canZoomOut(macroZoomTarget()) : true,
        canResetZoom: Boolean(zoomApi?.canResetZoom?.(macroZoomTarget())),
        version: state.version,
        phpPort: state.phpPort,
        libraries: state.libraries,
        apiOnline: state.apiOnline,
        status: state.treeMessage,
    };
}

function jalbumContext() {
    const images = [
        ...(Array.isArray(state.doc?.images) ? state.doc.images : []),
        ...(Array.isArray(state.doc?.unlistedSlides) ? state.doc.unlistedSlides : []),
    ];
    return {
        html: state.sourceLoaded ? codeApi.getValue() : '',
        pageData: jalbumPageData(),
        galleryData: { images },
        loadGallery: loadJalbumDeepData,
    };
}

async function loadJalbumDeepData() {
    const pagePath = normalizePath(state.path || '');
    const slash = pagePath.lastIndexOf('/');
    if (slash < 0) return null;
    const deepPath = `${pagePath.slice(0, slash + 1)}deep-data.json`;
    const data = await apiSend('GET', `/__studio/api/page?path=${encodeURIComponent(deepPath)}`);
    const text = typeof data?.content === 'string' ? data.content : '';
    if (!text.trim()) return null;
    return JSON.parse(text);
}

function jalbumPageData() {
    const path = normalizePath(state.path || '');
    const folder = folderForPath(path);
    const siblings = [];
    for (const file of folder?.files || []) {
        const filePath = normalizePath(file.path || '');
        const name = file.name || filePath.split('/').pop();
        if (!name || !/\.(html|shtml|htm)$/i.test(name)) continue;
        if ((filePath && filePath === path) || /^index\.html?$/i.test(name)) continue;
        siblings.push({ name, path: filePath || name });
    }
    return {
        path,
        section: state.section || '',
        slug: state.slug || '',
        siblings,
    };
}

function applyConvertedPage(html) {
    if (!state.doc || !state.sourceLoaded || typeof html !== 'string') {
        announce('Open a jAlbum page before converting.');
        return;
    }
    setMode('code');
    pinActiveTab();
    codeApi.setValue(html);
    codeApi.setCaret({ selectionStart: 0, selectionEnd: 0, scrollTop: 0, scrollLeft: 0 });
    state.doc.content = html;
    state.codeDirty = html !== state.savedContent;
    widgetsApi?.noteSource?.(html);
    refreshHtmlGalleryFromEditor();
    undoStack?.record(state.path, currentDocSnapshot(), {
        kind: 'convert',
        label: 'Converted jAlbum page',
        coalesce: false,
    });
    if (state.codeDirty || state.galleryDirty) scheduleSave();
    else releasePreviewDraft();
    announce('Converted in the editor. The preview is unsaved until you click Save.');
}

function toggleCodeWordWrap() {
    if (!codeApi?.setWordWrap || !codeApi.isWordWrap) return;
    const next = !codeApi.isWordWrap();
    codeApi.setWordWrap(next);
    syncMenu();
    announce(next ? 'Word wrap on.' : 'Word wrap off.');
}

function performMenu(action, arg) {
    switch (action) {
        case 'new-page':
            newPageApi?.open();
            break;
        case 'open-page':
            void openJump();
            break;
        case 'open-folder':
            openFolderApi?.open();
            break;
        case 'open-recent':
            if (arg) void openPath(arg);
            break;
        case 'clear-recent':
            clearRecent();
            break;
        case 'save':
            void saveCurrent();
            break;
        case 'close-tab':
            void closeTab(state.path);
            break;
        case 'reopen-closed-tab':
            void reopenClosedTab();
            break;
        case 'keep-open':
            pinActiveTab();
            break;
        case 'save-sync':
            void saveAndSync();
            break;
        case 'revert':
            void revertFile();
            break;
        case 'reload':
            void refreshPreview(true);
            break;
        case 'exit':
            void exitStudio();
            break;
        case 'undo':
            editCommand('undo');
            break;
        case 'redo':
            editCommand('redo');
            break;
        case 'undo-history':
            openUndoHistory();
            break;
        case 'cut':
            editCommand('cut');
            break;
        case 'copy':
            editCommand('copy');
            break;
        case 'paste':
            editCommand('paste');
            break;
        case 'select-all':
            editCommand('selectAll');
            break;
        case 'find':
            setMode('code');
            codeApi?.openFind();
            break;
        case 'find-in-project':
            openProjectSearch();
            break;
        case 'shelf-view':
            if (!state.treeOpen) setTreeOpen(true);
            setLeftShelfTab(arg || 'files');
            break;
        case 'toggle-comment':
            setMode('code');
            if (!codeApi?.toggleComment?.()) announce('Open a page with source to toggle comments.');
            break;
        case 'toggle-preview-edit':
            toggleViewportEdit();
            break;
        case 'insert-block':
            setMode('code');
            if (!codeApi?.insertBlock?.(arg)) announce('Open an HTML page to insert a block.');
            break;
        case 'toggle-tree':
            setTreeOpen(!state.treeOpen);
            break;
        case 'toggle-inspector':
            setInspectorOpen(!state.inspectorOpen);
            break;
        case 'toggle-terminal':
            toggleTerminal();
            break;
        case 'mode-code':
            setMode('code');
            break;
        case 'word-wrap':
            toggleCodeWordWrap();
            break;
        case 'mode-gallery':
            if (!state.doc?.hasGallery) announce('This page has no gallery.');
            else setMode('gallery');
            break;
        case 'viewport':
            setViewportSize(arg);
            break;
        case 'scroll-gallery':
            scrollPreviewToGallery();
            break;
        case 'full-preview':
            setPreviewFull(!state.previewFull);
            break;
        case 'refresh-preview':
            void refreshPreview(true);
            break;
        case 'zoom-in':
            zoomApi?.zoomIn(macroZoomTarget() ? { panel: 'macro' } : undefined);
            break;
        case 'zoom-out':
            zoomApi?.zoomOut(macroZoomTarget() ? { panel: 'macro' } : undefined);
            break;
        case 'zoom-reset':
            zoomApi?.resetZoom(macroZoomTarget() ? { panel: 'macro' } : undefined);
            break;
        case 'theme':
            setPreset(arg);
            break;
        case 'theme-custom':
            themeApi?.open();
            break;
        case 'error-check':
            void runErrorCheck();
            break;
        case 'project-macros':
            macroBuilderApi?.open?.({ createNew: false });
            break;
        case 'new-macro':
            macroBuilderApi?.open?.({ createNew: true });
            break;
        case 'sync-seo':
            void syncSeoWidget();
            break;
        case 'suggestion':
            revealWidgets(['relatedSales']);
            break;
        case 'tracking':
            revealWidgets(['googleTracking', 'extremeTracking', 'adminTracking']);
            break;
        case 'convert-jalbum':
            converterApi?.open();
            break;
        case 'append-unlisted': {
            const count = galleryApi?.appendAllUnlisted?.() || 0;
            if (!count) announce('There are no unlisted slides to append.');
            else {
                setMode('gallery');
                announce(`Appended ${count} unlisted slide${count === 1 ? '' : 's'}.`);
            }
            break;
        }
        case 'git-auto-fetch-settings':
            gitFooterApi?.openPeriodicCheckModal?.({ pendingAction: null });
            break;
        case 'open-config':
            void openConfigInEditor();
            break;
        case 'open-config-folder':
            void openConfigFolder();
            break;
        case 'backup-config':
            void backupConfigFile();
            break;
        case 'recover-config':
            void recoverConfigFile();
            break;
        case 'clear-config':
            void clearConfigFile();
            break;
        case 'check-updates':
            void updaterApi?.checkForUpdates({ manual: true });
            break;
        default:
            break;
    }
}

async function openConfigInEditor() {
    await openPath(CONFIG_PAGE_PATH, { force: true, mode: 'code', pin: true });
    setMode('code');
    codeApi?.focus?.();
    announce('Opened config.json in the code editor.');
}

async function openConfigFolder() {
    try {
        const data = await apiSend('POST', '/__studio/api/config/open-folder', {});
        announce(data?.folder ? `Opened config folder: ${data.folder}` : 'Opened config file folder.');
    } catch (error) {
        announce(error.message || 'Could not open the config file folder.');
    }
}

function syncConfigTabFromDoc(data) {
    if (data?.config) applyStudioConfig(data.config);
    const cfgTab = findTab(CONFIG_PAGE_PATH);
    if (normalizePath(state.path) === CONFIG_PAGE_PATH && typeof data?.content === 'string') {
        adoptServer(data, { path: CONFIG_PAGE_PATH, replaceContent: true });
        setSaveState('saved', 'Saved.');
        updateChrome();
    } else if (cfgTab && typeof data?.content === 'string') {
        const doc = finishPage(data, locate(CONFIG_PAGE_PATH), true);
        fillTabFromDoc(cfgTab, doc);
        refreshEditors();
    } else if (state.doc) {
        widgetsApi?.setDoc(state.doc);
    }
}

async function backupConfigFile() {
    try {
        if (normalizePath(state.path) === CONFIG_PAGE_PATH && state.codeDirty) {
            const saved = await saveCurrent();
            if (!saved) return;
        }
        const data = await apiSend('POST', '/__studio/api/config/backup', {});
        if (data?.config) applyStudioConfig(data.config);
        announce('Backed up config.json to state/config.backup.json.');
    } catch (error) {
        announce(error.message || 'Could not back up config.json.');
    }
}

async function recoverConfigFile() {
    try {
        const data = await apiSend('POST', '/__studio/api/config/recover', {});
        syncConfigTabFromDoc(data);
        announce('Recovered config.json from state/config.backup.json.');
    } catch (error) {
        announce(error.message || 'Could not recover config.json.');
    }
}

async function clearConfigFile() {
    const confirmed = await confirmDialog(
        'Clear config file?',
        'This clears the tracking keys in state/config.json back to an empty template. You can restore them with Tools → Config → Recover Config.',
        'Clear Config',
    );
    if (!confirmed) return;
    try {
        const data = await apiSend('POST', '/__studio/api/config/clear', {});
        syncConfigTabFromDoc(data);
        announce('Cleared config.json. Use Tools → Config → Recover Config to restore.');
    } catch (error) {
        announce(error.message || 'Could not clear config.json.');
    }
}

function macroZoomTarget() {
    return document.querySelector('dialog.macro-builder-dialog[open]') ? 'macro' : null;
}

function applyZoomShortcut(key, event) {
    const panel = macroZoomTarget();
    const opts = panel ? { panel } : {};
    if (key === '=' || key === '+') {
        menuApi?.close();
        zoomApi?.zoomIn(opts);
        return true;
    }
    if (key === '-' || key === '_') {
        menuApi?.close();
        zoomApi?.zoomOut(opts);
        return true;
    }
    if (key === '0' && !event?.shiftKey) {
        menuApi?.close();
        zoomApi?.resetZoom(opts);
        return true;
    }
    return false;
}

function runShortcut(key, event) {
    if (key === 'h' && event?.shiftKey) {
        menuApi?.close();
        undoHistoryApi?.toggle();
        return true;
    }
    const openDialog = document.querySelector('dialog[open]');
    if (openDialog?.classList.contains('macro-builder-dialog') && applyZoomShortcut(key, event)) return true;
    if (openDialog && !openDialog.classList.contains('undo-history-dialog')) return key === 's';
    if (key === 'z' && !event?.shiftKey) {
        if (isAuxiliaryInput(document.activeElement)) return false;
        menuApi?.close();
        void performUndo();
        return true;
    }
    if ((key === 'z' && event?.shiftKey) || (key === 'y' && !event?.shiftKey)) {
        if (isAuxiliaryInput(document.activeElement)) return false;
        menuApi?.close();
        void performRedo();
        return true;
    }
    if (openDialog) return key === 's';
    if (key === 'f5' || key === 'r') {
        menuApi?.close();
        void refreshPreview(true);
        return true;
    }
    if (key === 'b') {
        menuApi?.close();
        setTreeOpen(!state.treeOpen);
        return true;
    }
    if (key === 'j') {
        menuApi?.close();
        setInspectorOpen(!state.inspectorOpen);
        return true;
    }
    if (key === '`' || key === '~' || event?.code === 'Backquote') {
        menuApi?.close();
        toggleTerminal();
        return true;
    }
    if (key === 'o' && event?.shiftKey) {
        menuApi?.close();
        openFolderApi?.open();
        return true;
    }
    if (key === 'k' || key === 'o') {
        menuApi?.close();
        void openJump();
        return true;
    }
    if (key === 'n') {
        menuApi?.close();
        newPageApi?.open();
        return true;
    }
    if ((key === 'tab' || key === 'pagedown' || key === 'pageup') && !event?.altKey) {
        menuApi?.close();
        if (key === 'pageup' || (key === 'tab' && event?.shiftKey)) cycleTabs(-1);
        else cycleTabs(1);
        return true;
    }
    if (key === 'w' && !event?.shiftKey) {
        menuApi?.close();
        void closeTab(state.path);
        return true;
    }
    if (key === 't' && event?.shiftKey) {
        menuApi?.close();
        void reopenClosedTab();
        return true;
    }
    if (key === 's' && event?.shiftKey) {
        menuApi?.close();
        void saveAndSync();
        return true;
    }
    if (key === 's') {
        menuApi?.close();
        void saveCurrent();
        return true;
    }
    if (key === 'f' && event?.shiftKey) {
        menuApi?.close();
        openProjectSearch();
        return true;
    }
    if (key === 'e' && event?.shiftKey) {
        menuApi?.close();
        if (!state.treeOpen) setTreeOpen(true);
        setLeftShelfTab('files', { focus: true });
        return true;
    }
    if (key === 'g' && event?.shiftKey) {
        menuApi?.close();
        if (!state.treeOpen) setTreeOpen(true);
        setLeftShelfTab('git', { focus: true });
        return true;
    }
    if (key === 'f') {
        const active = document.activeElement;
        const field = active && active.closest?.('input, textarea, select');
        const inEditor = active && active.closest?.('#editor-pane');
        const inFrame = event?.view && event.view !== window;
        if (field && !inEditor && !inFrame) return false;
        menuApi?.close();
        setMode('code');
        codeApi?.openFind();
        return true;
    }
    if ((key === '/' || event?.code === 'Slash') && !event?.shiftKey) {
        if (!state.sourceLoaded || state.mode !== 'code' || state.previewFull) return false;
        const active = document.activeElement;
        const field = active && active.closest?.('input, textarea, select');
        const inCode = active && active.closest?.('#code-mount');
        if (field && !inCode) return false;
        menuApi?.close();
        return Boolean(codeApi?.toggleComment?.());
    }
    if (key === 'e') {
        menuApi?.close();
        toggleViewportEdit();
        return true;
    }
    if (applyZoomShortcut(key, event)) return true;
    if (key === 'q') {
        menuApi?.close();
        void exitStudio();
        return true;
    }
    return false;
}

function toggleViewportEdit() {
    const editing = inlineApi?.getMode?.() === 'edit';
    if (!editing && els.viewportEdit?.disabled) {
        announce('Open a page with source before editing text in the preview.');
        return;
    }
    inlineApi?.setMode(editing ? 'view' : 'edit');
}

function openProjectSearch() {
    if (!state.treeOpen) setTreeOpen(true);
    setLeftShelfTab('search');
    projectSearchApi?.focus?.(true);
}

const WIDGET_NAMES = {
    pageValues: 'Page Values',
    salesListing: 'Vehicle Listing',
    relatedSales: 'Related Sales',
    googleTracking: 'Google Analytics',
    extremeTracking: 'eXTReMe Tracker',
    adminTracking: 'Admin Traffic Logger',
    indexSalesLinks: 'Section Index Sales Links',
    projectInventory: 'Project Inventory',
    searchImage: 'Search Image',
    newsletterArchive: 'Newsletter Archive',
    newsletterSubscribe: 'Newsletter Subscribe',
};

function widgetHistoryMeta(widget, data) {
    const friendly = WIDGET_NAMES[widget] || widget || 'Widget';
    if (widget === 'salesListing') {
        return {
            kind: 'details',
            label: `Edited vehicle/page details (Applied widget: ${friendly})`,
        };
    }
    if (data?.enabled === false || data?.action === 'remove') {
        return {
            kind: 'widget',
            label: `Applied widget: Removed ${friendly}`,
        };
    }
    return {
        kind: 'widget',
        label: `Applied widget: ${friendly}`,
    };
}

function currentDocSnapshot(docOverride = null) {
    const doc = docOverride || state.doc;
    const sourceLoaded = docOverride ? Boolean(doc.sourceLoaded) : state.sourceLoaded;
    const content = sourceLoaded
        ? (docOverride ? String(doc.content ?? '') : (codeApi ? codeApi.getValue() : String(doc?.content ?? '')))
        : '';
    const caret = (!docOverride && codeApi?.getCaret)
        ? codeApi.getCaret()
        : { selectionStart: 0, selectionEnd: 0, scrollTop: 0, scrollLeft: 0 };
    return {
        content,
        sourceLoaded,
        caret,
        images: Array.isArray(doc?.images) ? doc.images : [],
        unlistedSlides: Array.isArray(doc?.unlistedSlides) ? doc.unlistedSlides : [],
        details: doc?.details || {},
        mode: state.mode,
    };
}

function isAuxiliaryInput(node) {
    if (!node || !node.closest) return false;
    if (node.closest('#code-mount, #gallery-mount, #gallery-sheet, #preview-pane')) return false;
    if (node.closest('#tree-panel, #terminal-panel, .find-bar')) return true;
    const dialog = node.closest('dialog');
    if (dialog) return !dialog.classList.contains('undo-history-dialog') || node.matches('input, select');
    return false;
}

function openUndoHistory() {
    undoHistoryApi?.open(state.path);
}

function syncUndoUi() {
    const timeline = undoStack?.getTimeline?.(state.path);
    const entries = timeline?.entries || [];
    const hasHistory = Boolean(state.doc && state.path && entries.length > 0);
    const canUndo = Boolean(timeline?.canUndo);
    const canRedo = Boolean(timeline?.canRedo);
    const count = entries.length;
    codeApi?.syncUndoUi?.({ canUndo, canRedo, count, hasHistory });
    galleryApi?.syncUndoUi?.({ canUndo, canRedo, count, hasHistory });
    if (els.undoHistoryBtn) {
        els.undoHistoryBtn.disabled = !hasHistory;
    }
    if (els.undoHistoryBadge) {
        els.undoHistoryBadge.hidden = !(hasHistory && count > 0);
        els.undoHistoryBadge.textContent = String(count);
    }
    menuApi?.refresh?.();
}

async function performUndo(targetPath) {
    if (!targetPath && treeApi?.containsFocus?.() && state.treeUndo.canUndo) {
        await handleTreeUndo();
        return true;
    }
    const path = normalizePath(targetPath || state.path);
    if (!path) {
        if (state.treeUndo.canUndo) {
            await handleTreeUndo();
            return true;
        }
        announce('Open a page before using Undo.');
        return false;
    }
    if (normalizePath(state.path) !== path) {
        await openPath(path);
    }
    const entry = undoStack?.undo(path);
    if (!entry) {
        if (!targetPath && state.treeUndo.canUndo) {
            await handleTreeUndo();
            return true;
        }
        announce('Nothing to undo.');
        return false;
    }
    applyHistoryEntry(entry, 'Undid to');
    return true;
}

async function performRedo(targetPath) {
    if (!targetPath && treeApi?.containsFocus?.() && state.treeUndo.canRedo) {
        await handleTreeRedo();
        return true;
    }
    const path = normalizePath(targetPath || state.path);
    if (!path) {
        if (state.treeUndo.canRedo) {
            await handleTreeRedo();
            return true;
        }
        announce('Open a page before using Redo.');
        return false;
    }
    if (normalizePath(state.path) !== path) {
        await openPath(path);
    }
    const entry = undoStack?.redo(path);
    if (!entry) {
        if (!targetPath && state.treeUndo.canRedo) {
            await handleTreeRedo();
            return true;
        }
        announce('Nothing to redo.');
        return false;
    }
    applyHistoryEntry(entry, 'Redid to');
    return true;
}

async function performJumpToHistory(targetPath, targetIndex) {
    const path = normalizePath(targetPath || state.path);
    if (!path) return false;
    if (normalizePath(state.path) !== path) {
        await openPath(path);
    }
    const entry = undoStack?.jumpTo(path, targetIndex);
    if (!entry) return false;
    applyHistoryEntry(entry, 'Restored');
    return true;
}

function applyHistoryEntry(entry, verb = 'Restored') {
    if (!entry || !state.doc) return;
    pinActiveTab();
    inlineApi?.cancelSession?.();
    codeApi?.clearHighlight?.();
    if (state.sourceLoaded && typeof entry.content === 'string') {
        codeApi.setValue(entry.content);
        if (entry.caret) codeApi.setCaret(entry.caret);
        state.doc.content = entry.content;
    }
    if (entry.details && typeof entry.details === 'object') {
        state.doc.details = { ...entry.details };
    }
    if (state.doc.gallerySource === 'html') {
        attachGallerySource(state.doc);
        if (Array.isArray(entry.images) && entry.images.length) {
            state.doc.images = entry.images.map((img) => ({ ...img }));
            state.doc.unlistedSlides = omitListedSlides(entry.unlistedSlides, state.doc.images, state.doc.content);
        }
    } else if (Array.isArray(entry.images)) {
        state.doc.images = entry.images.map((img) => ({ ...img }));
        state.doc.unlistedSlides = omitListedSlides(entry.unlistedSlides, state.doc.images, state.doc.content);
    }
    galleryApi.setDoc(state.doc);
    state.codeDirty = Boolean(state.sourceLoaded && codeApi.getValue() !== state.savedContent);
    state.galleryDirty = Boolean(
        state.doc?.gallerySource !== 'html'
        && imageSignature(state.doc?.images) !== state.savedImagesSig,
    );
    widgetsApi?.setDoc(state.doc, { force: true });
    patchFolderFromDoc(state.doc);
    updateGalleryTab();
    renderTree({ reveal: false });
    syncActiveTabCache();
    if (state.codeDirty || state.galleryDirty) {
        scheduleSave({ delay: 80, reload: true });
    } else {
        releasePreviewDraft();
    }
    updateChrome();
    announce(`${verb} #${(entry.index ?? 0) + 1}: ${entry.label}`);
}

function rememberEditTarget(node) {
    if (editableNode(node)) editTarget = node;
}

function editableNode(node) {
    if (!node || !node.isConnected || node.disabled || node.readOnly) return false;
    if (node.isContentEditable) return true;
    const tag = node.tagName;
    if (tag === 'TEXTAREA') return true;
    if (tag !== 'INPUT') return false;
    const type = String(node.getAttribute('type') || 'text').toLowerCase();
    return ['text', 'search', 'url', 'email', 'password', 'number'].includes(type);
}

function editCommand(command) {
    const node = editTarget;
    if (command === 'undo') {
        if (isAuxiliaryInput(document.activeElement) && editableNode(document.activeElement)) {
            try { document.execCommand('undo'); } catch { /* ignore */ }
            return;
        }
        void performUndo();
        return;
    }
    if (command === 'redo') {
        if (isAuxiliaryInput(document.activeElement) && editableNode(document.activeElement)) {
            try { document.execCommand('redo'); } catch { /* ignore */ }
            return;
        }
        void performRedo();
        return;
    }
    if (command === 'copy') {
        const inputHasSel = editableNode(node) && document.activeElement === node &&
            typeof node.selectionStart === 'number' && node.selectionStart !== node.selectionEnd;
        if (!inputHasSel && state.terminalOpen && terminalApi?.hasSelection?.()) {
            terminalApi.copySelection();
            return;
        }
        const domSel = window.getSelection?.();
        if (!inputHasSel && domSel && !domSel.isCollapsed) {
            const text = String(domSel.toString() || '');
            if (text) {
                try { document.execCommand('copy'); } catch { /* ignore */ }
                try { navigator.clipboard?.writeText?.(text)?.catch?.(() => {}); } catch { /* ignore */ }
                return;
            }
        }
    }
    if (command === 'selectAll' && state.terminalOpen && terminalApi?.containsFocus?.() &&
        document.activeElement?.tagName !== 'INPUT') {
        terminalApi.selectAll();
        return;
    }
    if (!editableNode(node)) {
        announce('Click a text field before using this edit command.');
        return;
    }
    node.focus();
    const doc = node.ownerDocument || document;
    if (command === 'selectAll') {
        if (typeof node.select === 'function') node.select();
        else doc.execCommand('selectAll');
        return;
    }
    let ran = false;
    try { ran = doc.execCommand(command); } catch { ran = false; }
    if (command === 'paste' && !ran) void pasteFromClipboard(node);
}

async function pasteFromClipboard(node) {
    let text = '';
    try {
        text = await navigator.clipboard.readText();
    } catch {
        announce('Clipboard paste was blocked. Click the field and use Ctrl+V.');
        return;
    }
    node.focus();
    if (typeof node.setRangeText === 'function') {
        const start = node.selectionStart ?? node.value.length;
        const end = node.selectionEnd ?? start;
        node.setRangeText(text, start, end, 'end');
        node.dispatchEvent(new Event('input', { bubbles: true }));
        return;
    }
    try { node.ownerDocument.execCommand('insertText', false, text); } catch { /* the field keeps its current text */ }
}

function readRecent() {
    try {
        const parsed = JSON.parse(localStorage.getItem(RECENT_KEY) || '[]');
        if (!Array.isArray(parsed)) return [];
        const items = [];
        for (const item of parsed) {
            if (!item || typeof item.path !== 'string' || !item.path) continue;
            const path = normalizePath(item.path);
            if (!path) continue;
            items.push({
                path,
                label: String(item.label || fileName(path)),
                section: String(item.section || ''),
                slug: String(item.slug || ''),
            });
            if (items.length === 10) break;
        }
        return items;
    } catch {
        return [];
    }
}

function rememberRecent(doc) {
    const path = normalizePath(doc?.path || '');
    if (!path) return;
    const entry = {
        path,
        label: activeTitle() || fileName(path),
        section: String(doc.section || ''),
        slug: String(doc.slug || ''),
    };
    const next = [entry, ...readRecent().filter((item) => item.path !== path)].slice(0, 10);
    try { localStorage.setItem(RECENT_KEY, JSON.stringify(next)); } catch { /* private mode */ }
    syncMenu();
}

function clearRecent() {
    try { localStorage.removeItem(RECENT_KEY); } catch { /* private mode */ }
    syncMenu();
    announce('Cleared recent files.');
}

async function reloadWorkspace() {
    await refreshPreview(true);
}

async function exitStudio() {
    captureActiveTab();
    for (const tab of [...state.tabs]) {
        if (!tabIsBufferDirty(tab)) continue;
        const ok = await saveTabBuffer(tab);
        if (!ok || (normalizePath(tab.path) === normalizePath(state.path) && state.saveState === 'error')) {
            announce('Fix the save error before exiting.');
            return;
        }
    }
    try {
        await fetch('/__studio/api/quit', {
            method: 'POST',
            keepalive: true,
            cache: 'no-store',
            headers: { Accept: 'application/json', 'Content-Type': 'application/json' },
            body: '{}',
        });
    } catch { /* the window close still runs */ }
    window.close();
}

async function runErrorCheck() {
    if (!state.inspectorOpen) setInspectorOpen(true);
    if (state.path && state.doc) {
        await refreshDocument({
            replaceContent: !state.codeDirty && codeApi.getValue() === state.savedContent,
            path: state.path,
        });
    }
    revealWidgets(['error-check']);
    announce('Common error check refreshed.');
}

async function syncSeoWidget() {
    if (!state.doc || !state.path) {
        announce('Open a page before syncing SEO.');
        return;
    }
    const widget = /^\/sales\/[^/]+\/index\.php$/i.test(state.path || '') ? 'salesListing' : 'pageValues';
    revealWidgets([widget]);
    await applyWidget(widget, widgetsApi.readData(widget));
}

function revealWidgets(names) {
    if (!state.inspectorOpen) setInspectorOpen(true);
    requestAnimationFrame(() => widgetsApi?.showWidgets?.(names));
}

function applyLayout(layout, { silent = false } = {}) {
    if (!layout || typeof layout !== 'object') return;
    const chrome = sessionChromeApplied ? sessionChrome : null;
    const lockTree = Boolean(chrome && typeof chrome.leftPanelOpen === 'boolean');
    const lockInspector = Boolean(chrome && typeof chrome.rightPanelOpen === 'boolean');
    const lockTreeWidth = Boolean(chrome && typeof chrome.leftPanelWidth === 'number');
    const lockWidth = Boolean(chrome && typeof chrome.rightPanelWidth === 'number');
    if (!lockTree && typeof layout.treeOpen === 'boolean') setTreeOpen(layout.treeOpen, { silent: true });
    else if (!lockTree && layout.treeOpen == null && typeof layout.drawerOpen === 'boolean') setTreeOpen(layout.drawerOpen, { silent: true });
    if (!lockInspector && typeof layout.inspectorOpen === 'boolean') setInspectorOpen(layout.inspectorOpen, { silent: true });
    if (!lockTreeWidth && typeof layout.treeWidth === 'number') setTreeWidth(layout.treeWidth);
    if (!lockWidth && typeof layout.inspectorWidth === 'number') setInspectorWidth(layout.inspectorWidth);
    const lockEditors = editorsSized || Boolean(chrome && typeof chrome.openEditorsHeight === 'number');
    const lockTabs = editorsSized || Boolean(chrome && typeof chrome.fileTabbarHeight === 'number');
    if (!lockEditors && typeof layout.openEditorsHeight === 'number') {
        setOpenEditorsHeight(layout.openEditorsHeight, { persistLocal: !silent });
    }
    if (!lockTabs && typeof layout.fileTabbarHeight === 'number') {
        setFileTabbarHeight(layout.fileTabbarHeight, { persistLocal: !silent });
    }
    if (typeof layout.centerRatio === 'number') setCenterRatio(layout.centerRatio);
    else setCenterRatio(state.centerRatio);
    if (Object.prototype.hasOwnProperty.call(VIEWPORTS, layout.viewport)) {
        state.viewport = layout.viewport;
        if (els.viewport) els.viewport.value = layout.viewport;
    }
    if (['compact', 'standard', 'large'].includes(layout.density)) {
        state.density = layout.density;
        galleryApi?.setDensity(layout.density);
        zoomApi?.refresh?.();
    }
    layoutPreview();
    if (!silent) persistLayout();
}

function currentLayout() {
    return {
        treeOpen: state.treeOpen,
        inspectorOpen: state.inspectorOpen,
        drawerOpen: state.treeOpen,
        treeWidth: state.treeWidth,
        inspectorWidth: state.inspectorWidth,
        openEditorsHeight: state.openEditorsHeight,
        fileTabbarHeight: state.fileTabbarHeight,
        centerRatio: state.centerRatio,
        viewport: state.viewport,
        density: state.density,
    };
}

function bindResizer(handle, { onMove, onReset, onCommit = null, row = false }) {
    if (!handle) return;
    const commit = () => { if (onCommit) onCommit(); };
    handle.addEventListener('pointerdown', (event) => {
        if (event.button !== 0) return;
        event.preventDefault();
        document.documentElement.classList.add('is-resizing');
        if (row) document.documentElement.classList.add('is-resizing-row');
        handle.setPointerCapture(event.pointerId);
        const move = (ev) => onMove(ev);
        const up = (ev) => {
            if (ev.pointerId !== event.pointerId) return;
            handle.removeEventListener('pointermove', move);
            handle.removeEventListener('pointerup', up);
            document.documentElement.classList.remove('is-resizing', 'is-resizing-row');
            persistLayout();
            commit();
            layoutPreview();
        };
        handle.addEventListener('pointermove', move);
        handle.addEventListener('pointerup', up);
    });
    handle.addEventListener('dblclick', () => {
        onReset();
        persistLayout();
        commit();
        layoutPreview();
    });
    handle.addEventListener('keydown', (event) => {
        if (!['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'].includes(event.key)) return;
        event.preventDefault();
        onMove(event);
        persistLayout();
        commit();
        layoutPreview();
    });
}

function layoutPreview() {
    if (state.mode === 'gallery' || els.stage?.hidden) return;
    const width = els.stage.clientWidth;
    const height = els.stage.clientHeight;
    const fixed = VIEWPORTS[state.viewport];
    const userZoom = zoomApi?.getZoom?.('preview') || 1;
    if (!fixed && Math.abs(userZoom - 1) < 0.001) {
        els.frame.style.width = '100%';
        els.frame.style.height = '100%';
        els.frame.style.left = '0px';
        els.frame.style.transform = 'none';
        els.scale.textContent = '100%';
        els.scale.title = '100% — Current proportional zoom scale of the preview viewport';
        return;
    }
    const baseScale = fixed ? (width > 0 ? Math.min(1, width / fixed) : 1) : 1;
    const scale = (baseScale > 0 ? baseScale : 1) * userZoom;
    const safe = scale > 0 ? scale : 1;
    const frameWidth = fixed ? Math.min(fixed, Math.max(1, width) / safe) : Math.max(1, width) / safe;
    els.frame.style.width = `${frameWidth}px`;
    els.frame.style.height = `${Math.max(height, 1) / safe}px`;
    els.frame.style.transform = `scale(${safe})`;
    els.frame.style.transformOrigin = 'top left';
    els.frame.style.left = `${Math.max(0, (width - frameWidth * safe) / 2)}px`;
    const percent = Math.round(safe * 100);
    els.scale.textContent = `${percent}%`;
    const resetHint = Math.abs(userZoom - 1) >= 0.001
        ? ` (${Math.round(userZoom * 100)}% zoom — click or press Ctrl+0 to reset)`
        : '';
    els.scale.title = `${percent}% — Current proportional zoom scale of the preview viewport${resetHint}`;
}

function showDarkPreview() {
    els.frame.dataset.src = '';
    els.frame.removeAttribute('src');
    els.frame.srcdoc = DARK_PREVIEW_DOC;
}

function stripStudioQuery(url) {
    const text = String(url || '');
    const hashAt = text.indexOf('#');
    const hash = hashAt >= 0 ? text.slice(hashAt) : '';
    const base = hashAt >= 0 ? text.slice(0, hashAt) : text;
    const queryAt = base.indexOf('?');
    if (queryAt < 0) return `${base}${hash}`;
    const path = base.slice(0, queryAt);
    const kept = base.slice(queryAt + 1).split('&').filter((part) => {
        const name = part.split('=')[0];
        return name !== '_studio' && name !== '_t';
    });
    return `${path}${kept.length ? `?${kept.join('&')}` : ''}${hash}`;
}

function withCacheQuery(url, name) {
    const text = String(url || '');
    const hashAt = text.indexOf('#');
    const hash = hashAt >= 0 ? text.slice(hashAt) : '';
    const base = hashAt >= 0 ? text.slice(0, hashAt) : text;
    return `${base}${base.includes('?') ? '&' : '?'}${name}=${Date.now()}${hash}`;
}

function replacePreviewLocation(target) {
    try {
        const win = els.frame.contentWindow;
        const href = els.frame.contentDocument?.URL || '';
        if (!win || !href || href === 'about:blank' || href.startsWith('about:')) return false;
        win.location.replace(target);
        return true;
    } catch {
        return false;
    }
}

function frameIsParked() {
    const src = els.frame?.getAttribute('src') || '';
    return !src || src === 'about:blank';
}

function parkPreview() {
    if (!els.frame) return;
    els.frame.dataset.src = '';
    els.frame.removeAttribute('srcdoc');
    els.frame.src = 'about:blank';
    els.scrollGallery.disabled = true;
    els.refresh.disabled = true;
}

function stripDraftQuery(url) {
    if (!url) return '';
    const hashAt = url.indexOf('#');
    const hash = hashAt >= 0 ? url.slice(hashAt) : '';
    const base = hashAt >= 0 ? url.slice(0, hashAt) : url;
    const queryAt = base.indexOf('?');
    if (queryAt < 0) return `${base}${hash}`;
    const path = base.slice(0, queryAt);
    const kept = base.slice(queryAt + 1).split('&').filter((part) => {
        const name = part.split('=')[0];
        return name !== '__draft' && name !== '__preview_draft';
    });
    return `${path}${kept.length ? `?${kept.join('&')}` : ''}${hash}`;
}

function withDraftQuery(url) {
    const stripped = stripDraftQuery(url);
    if (!stripped || /^(https?:|data:)/i.test(stripped)) return stripped;
    const hashAt = stripped.indexOf('#');
    const hash = hashAt >= 0 ? stripped.slice(hashAt) : '';
    const base = hashAt >= 0 ? stripped.slice(0, hashAt) : stripped;
    return `${base}${base.includes('?') ? '&' : '?'}__draft=1${hash}`;
}

function decoratePreview(url) {
    if (!url) return '';
    return (state.codeDirty || state.galleryDirty) ? withDraftQuery(url) : stripDraftQuery(url);
}

function presentPreview({ fresh = false } = {}) {
    if (state.mode === 'gallery') {
        parkPreview();
        return false;
    }
    if (state.doc?.isMedia) {
        const path = state.doc.path || state.doc.previewUrl || '';
        if (path && previewNav?.showDocument) return Boolean(previewNav.showDocument(path));
        if (path && els.frame) {
            els.frame.dataset.src = path;
            els.frame.removeAttribute('src');
            els.frame.srcdoc = mediaPreviewHtml(state.doc);
            return true;
        }
        return false;
    }
    return setPreviewUrl(state.doc?.previewUrl || '', { fresh });
}

function setPreviewUrl(url, { fresh = false } = {}) {
    if (state.mode === 'gallery') {
        parkPreview();
        return false;
    }
    const next = encodePreview(decoratePreview(url || ''));
    if (!next) {
        els.scrollGallery.disabled = true;
        els.refresh.disabled = true;
        showDarkPreview();
        return false;
    }
    const bust = fresh || frameIsParked();
    const showing = els.frame.getAttribute('src') || '';
    if (!bust && els.frame.dataset.src === next && stripStudioQuery(showing) === next && !els.frame.getAttribute('srcdoc')) {
        return false;
    }
    previewNav?.rememberScroll();
    const same = Boolean(previewNav?.isSameDocument?.(next));
    if (same) previewNav.expectSameDocument();
    else previewNav?.clearPending?.();
    els.frame.dataset.src = next;
    // srcdoc wins over src, so the dark placeholder has to come off before the page can render.
    els.frame.removeAttribute('srcdoc');
    const target = bust ? withCacheQuery(next, '_studio') : next;
    if (same && replacePreviewLocation(target)) return true;
    els.frame.src = target;
    return true;
}

function mediaPreviewHtml(doc) {
    const href = escapePreview(encodePreview(doc?.previewUrl || doc?.path || ''));
    const name = escapePreview(fileName(doc?.path || ''));
    const kind = String(doc?.mediaKind || doc?.fileType || '').toLowerCase();
    const dims = Number(doc?.width) > 0 && Number(doc?.height) > 0 ? `${doc.width} × ${doc.height}` : '';
    const size = formatMediaBytes(doc?.fileSize);
    const meta = escapePreview([name, dims, size].filter(Boolean).join(' · '));
    const stage = kind === 'image'
        ? `<div class="stage checker"><img src="${href}" alt="${name}"></div>`
        : kind === 'pdf'
            ? `<div class="stage"><embed src="${href}" type="application/pdf"></div>`
            : kind === 'video'
                ? `<div class="stage checker"><video src="${href}" controls></video></div>`
                : kind === 'audio'
                    ? `<div class="stage"><audio src="${href}" controls></audio></div>`
                    : `<div class="stage card"><p>${name}</p><p><a href="${href}" target="_blank">Open file</a></p></div>`;
    return `<!DOCTYPE html><html><head><meta charset="utf-8"><style>
html,body{margin:0;height:100%;background:#111;color:#e6e6e6;font:13px/1.4 sans-serif}
body{display:flex;flex-direction:column;min-height:100%}
.stage{flex:1;min-height:0;display:flex;align-items:center;justify-content:center}
.checker{background:
linear-gradient(45deg,#1b1b1b 25%,transparent 25%),linear-gradient(-45deg,#1b1b1b 25%,transparent 25%),
linear-gradient(45deg,transparent 75%,#1b1b1b 75%),linear-gradient(-45deg,transparent 75%,#1b1b1b 75%);
background-size:24px 24px;background-position:0 0,0 12px,12px -12px,-12px 0;background-color:#141414}
img,video{max-width:100%;max-height:100%;object-fit:contain;display:block}
embed{width:100%;height:100%;border:0;background:#111}
.card{flex-direction:column;gap:8px;text-align:center}
a{color:#f0d48a}
footer{flex:0 0 auto;padding:8px 12px;text-align:center;color:#c8c8c8}
</style></head><body>${stage}<footer>${meta}</footer></body></html>`;
}

function escapePreview(value) {
    return String(value || '')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}

function formatMediaBytes(value) {
    const bytes = Number(value) || 0;
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

function encodePreview(url) {
    if (!url) return '';
    if (/^(https?:|data:)/i.test(url)) return url;
    const hashAt = url.indexOf('#');
    const hash = hashAt >= 0 ? url.slice(hashAt) : '';
    const withoutHash = hashAt >= 0 ? url.slice(0, hashAt) : url;
    const queryAt = withoutHash.indexOf('?');
    const path = queryAt >= 0 ? withoutHash.slice(0, queryAt) : withoutHash;
    const query = queryAt >= 0 ? withoutHash.slice(queryAt) : '';
    const encoded = path.split('/').map((part, index) => {
        if (!part) return index === 0 ? '' : part;
        try { return encodeURIComponent(decodeURIComponent(part)); } catch { return encodeURIComponent(part); }
    }).join('/');
    return `${encoded}${query}${hash}`;
}

function capturePreview() {
    try {
        const win = els.frame.contentWindow;
        const doc = els.frame.contentDocument;
        if (!win || !doc || doc.URL === 'about:blank') return null;
        const more = doc.querySelector('details.photoMore');
        return { x: win.scrollX || 0, y: win.scrollY || 0, open: Boolean(more && more.open) };
    } catch {
        return null;
    }
}

function restorePreview(snap) {
    if (!snap) return;
    try {
        const doc = els.frame.contentDocument;
        const win = els.frame.contentWindow;
        if (!doc || !win) return;
        const more = doc.querySelector('details.photoMore');
        if (more) more.open = snap.open;
        win.scrollTo(snap.x, snap.y);
    } catch { /* preview is not same-origin yet */ }
}

function keepsNativeContextMenu(target) {
    let node = target?.nodeType === 1 ? target : target?.parentElement;
    while (node && node !== document.documentElement) {
        const tag = node.tagName;
        if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') return true;
        if (node.isContentEditable) return true;
        node = node.parentElement;
    }
    return false;
}

let previewReloadFlight = null;

function isPreviewReloadChord(event) {
    if (!event || event.altKey || event.isComposing) return false;
    if (event.key === 'F5') return true;
    return (event.ctrlKey || event.metaKey) && (event.key === 'r' || event.key === 'R');
}

// F5 and Ctrl/Cmd+R reload only #preview-frame. preventDefault stops the WebView from reloading /__studio/.
function onPreviewReloadKey(event) {
    if (!isPreviewReloadChord(event)) return;
    event.preventDefault();
    event.stopPropagation();
    if (event.repeat) return;
    menuApi?.close();
    void refreshPreview(true);
}

function bindPreviewFrameReloadKeys() {
    let win = null;
    let doc = null;
    try {
        win = els.frame?.contentWindow || null;
        doc = els.frame?.contentDocument || null;
    } catch {
        return;
    }
    const mark = '__studioPreviewReload';
    if (win && win[mark] !== onPreviewReloadKey) {
        win.addEventListener('keydown', onPreviewReloadKey, true);
        win[mark] = onPreviewReloadKey;
    }
    if (doc && doc[mark] !== onPreviewReloadKey) {
        doc.addEventListener('keydown', onPreviewReloadKey, true);
        doc[mark] = onPreviewReloadKey;
    }
}

async function refreshPreview(stageDraft = true) {
    if (previewReloadFlight) return previewReloadFlight;
    const flight = {};
    previewReloadFlight = flight;
    const run = (async () => {
        try {
            if (stageDraft !== false && state.path && (state.codeDirty || state.galleryDirty)) {
                window.clearTimeout(saveTimer);
                await stagePreview(bumpStageRev(), { reload: false });
            }
            reloadPreview();
        } finally {
            if (previewReloadFlight === flight) previewReloadFlight = null;
        }
    })();
    return run;
}

function reloadPreview() {
    if (state.mode === 'gallery') return;
    const snap = capturePreview();
    const onLoad = () => {
        els.frame.removeEventListener('load', onLoad);
        restorePreview(snap);
        requestAnimationFrame(() => restorePreview(snap));
        window.setTimeout(() => restorePreview(snap), 50);
    };
    els.frame.addEventListener('load', onLoad);
    // location.replace keeps a draft refresh from stacking cache-busted iframe history entries.
    if (previewNav?.reloadCurrent?.()) return;
    els.frame.removeEventListener('load', onLoad);
}

function scrollPreviewToGallery() {
    if (state.mode === 'gallery') return;
    try {
        const doc = els.frame.contentDocument;
        const win = els.frame.contentWindow;
        if (!doc || !win) return;
        const more = doc.querySelector('details.photoMore');
        if (more) more.open = true;
        const target = doc.querySelector('.imageAlbum, .articleGallerySection');
        if (target) target.scrollIntoView({ block: 'start', behavior: 'smooth' });
    } catch {
        announce('The preview is not ready to scroll yet.');
    }
}

async function confirmSwitchProject() {
    if (!anyDirty()) return true;
    return confirmDialog(
        'Open another project folder?',
        'Unsaved edits in this project will be left behind.',
        'Open Folder',
    );
}

function pickProjectLanding() {
    const preferred = ['/sales/index.php', '/index.php', '/index.html', '/index.shtml', '/index.htm'];
    const allPaths = new Set();
    let firstAny = '';
    for (const section of state.sections || []) {
        for (const file of section.rootFiles || []) {
            if (!file?.path) continue;
            const clean = normalizePath(file.path);
            allPaths.add(clean);
            if (!firstAny && /\.(php|html?|shtml)$/i.test(clean)) firstAny = clean;
        }
        for (const folder of section.folders || []) {
            const primary = normalizePath(folder?.primaryFile || '');
            if (primary) {
                allPaths.add(primary);
                if (!firstAny) firstAny = primary;
            }
            for (const file of folder?.files || []) {
                if (!file?.path) continue;
                const clean = normalizePath(file.path);
                allPaths.add(clean);
                if (!firstAny && /\.(php|html?|shtml)$/i.test(clean)) firstAny = clean;
            }
        }
    }
    for (const cand of preferred) {
        if (allPaths.has(cand)) return cand;
    }
    return firstAny;
}

async function openProjectLanding() {
    const picked = pickProjectLanding();
    const candidates = picked
        ? [picked, '/sales/index.php', '/index.html', '/index.php']
        : ['/sales/index.php', '/index.html', '/index.php'];
    const tried = new Set();
    for (const raw of candidates) {
        const path = normalizePath(raw);
        if (!path || tried.has(path)) continue;
        tried.add(path);
        installTabs([]);
        state.path = '';
        state.doc = null;
        await openPath(path, { force: true, pin: true });
        if (state.doc && normalizePath(state.path) === path) return true;
    }
    installTabs([]);
    state.path = '';
    state.doc = null;
    state.section = '';
    state.slug = '';
    codeApi?.setValue?.('');
    showDarkPreview();
    updateChrome();
    return false;
}

async function useProjectRoot(data) {
    state.repoRoot = data?.repoRoot || state.repoRoot;
    if (data?.phpPort != null && data.phpPort !== '') state.phpPort = String(data.phpPort);
    state.apiOnline = true;
    window.clearTimeout(saveTimer);
    if (saveAbort) saveAbort.abort();
    saveEpoch += 1;
    previewStaged = false;
    void clearPreviewDraft('', bumpStageRev());
    state.codeDirty = false;
    state.galleryDirty = false;
    state.path = '';
    state.doc = null;
    state.section = '';
    state.slug = '';
    state.sourceLoaded = false;
    state.savedContent = '';
    state.expandedSections = new Set();
    state.expandedFolders = new Set();
    installTabs([]);
    codeApi?.setValue?.('');
    state.treeMessage = 'File tree loaded';
    updateChrome();
    if (state.repoRoot) openFolderApi?.remember?.(state.repoRoot);
    void loadProjectMacros();
    await loadTree();
    void gitPanelApi?.refresh?.();
    terminalApi?.syncRepoRoot?.(state.repoRoot);
    void terminalApi?.refreshInfo?.();
    const opened = await openProjectLanding();
    announce(opened
        ? `Project folder is ${state.repoRoot}`
        : `Project folder is ${state.repoRoot}. No landing page was found.`);
}

async function loadHealth() {
    try {
        const data = await apiSend('GET', '/__studio/api/health');
        state.phpPort = data.phpPort == null || data.phpPort === '' ? '' : String(data.phpPort);
        state.libraries = Array.isArray(data.libraries) ? data.libraries : null;
        state.repoRoot = data.repoRoot || state.repoRoot || '';
        state.apiOnline = true;
        if (state.repoRoot) openFolderApi?.remember?.(state.repoRoot);
        terminalApi?.syncRepoRoot?.(state.repoRoot);
        if (data.config) {
            applyStudioConfig(data.config);
            if (state.doc) widgetsApi?.setDoc(state.doc);
        }
        if (data.appConfig) {
            gitFooterApi?.applyAppConfig?.(data.appConfig);
        }
        updaterApi?.scheduleStartupCheck?.(data.appConfig || null);
        if (data.version) applyVersion(data.version);
        else updateChrome();
    } catch {
        state.apiOnline = false;
        state.libraries = null;
    }
}

async function loadProjectMacros() {
    try {
        const data = await apiSend('GET', '/__studio/api/macros');
        applyMacrosResponse(data);
    } catch {
        widgetsApi?.setMacrosConfig?.({ macros: projectMacros, showBuiltin: showBuiltinWidgets });
    }
}

async function saveProjectMacros(payload) {
    try {
        const data = await apiSend('POST', '/__studio/api/macros', payload || {});
        applyMacrosResponse(data);
        announce('Saved project macros.');
    } catch (error) {
        announce(error.message || 'Could not save project macros.');
    }
}

function applyMacrosResponse(data) {
    if (!data || typeof data !== 'object') return;
    if (Array.isArray(data.macros)) projectMacros = data.macros;
    if (Array.isArray(data.presets) && data.presets.length) macroPresets = data.presets;
    if (typeof data.showBuiltinWidgets === 'boolean') showBuiltinWidgets = data.showBuiltinWidgets;
    widgetsApi?.setMacrosConfig?.({
        macros: projectMacros,
        showBuiltin: showBuiltinWidgets,
    });
}

function applyMacroSourceResult(result, macro, mode) {
    if (!state.doc || !state.sourceLoaded || !result || typeof result.content !== 'string') {
        announce('Open a page with source before applying a macro.');
        return;
    }
    const next = result.content;
    const current = codeApi.getValue();
    setMode('code');
    if (next !== current) {
        pinActiveTab();
        codeApi.setValue(next);
        state.doc.content = next;
        state.codeDirty = next !== state.savedContent;
        widgetsApi?.noteSource?.(next);
        widgetsApi?.setDoc?.(state.doc, { force: true });
        undoStack?.record(state.path, currentDocSnapshot(), {
            kind: 'macro',
            label: result.label || `${mode === 'remove' ? 'Removed' : 'Applied'} macro: ${macro?.title || 'Macro'}`,
            coalesce: false,
        });
        if (state.codeDirty || state.galleryDirty) {
            scheduleSave();
            setSaveState('unsaved', 'Macro applied in editor. Save to write this file.');
        } else {
            releasePreviewDraft();
        }
        updateChrome();
    }
    if (result.range && Number.isFinite(result.range.startOffset)) {
        codeApi.revealRange(result.range.startOffset, result.range.endOffset, macro?.title || 'Macro');
    }
    announce(result.label ? `${result.label}.` : 'Macro applied.');
}

function applyVersion(version) {
    const text = String(version || '').trim().replace(/^v/i, '');
    if (!text) return;
    state.version = text;
    els.version.hidden = false;
    els.version.textContent = `v${text}`;
    els.version.title = versionTip();
    updateChrome();
}

async function loadRemoteTheme() {
    try {
        const data = await apiSend('GET', '/__studio/api/theme');
        const theme = data.theme && typeof data.theme === 'object' ? data.theme : data;
        if (!theme || (!theme.preset && !theme.custom && !theme.layout)) return;
        if (!wasThemeTouched()) replaceTheme(theme, { persistLocal: false });
        if (!wasLayoutTouched() && theme.layout) applyLayout(theme.layout, { silent: true });
        if (sessionChrome && !panelTouched && !widgetsTouched) {
            sessionChromeApplied = false;
            applySessionChrome(sessionChrome);
        }
        rememberTheme();
    } catch { /* local theme remains */ }
}

function buildJumpDialog() {
    jumpDialog = el('dialog', { class: 'jump-dialog', 'aria-label': 'Jump to a file' });
    const label = el('label', { class: 'field', text: 'Jump to a file' });
    jumpInput = el('input', {
        type: 'search',
        placeholder: 'Name, slug, path, or #8096',
        autocomplete: 'off',
        spellcheck: 'false',
        title: 'Find a vehicle, article, award, or project by name, path, or inventory number (Ctrl+K)',
    });
    label.append(jumpInput);
    jumpResults = el('div', { class: 'jump-results', role: 'listbox' });
    const close = el('button', { class: 'btn', type: 'button', text: 'Close', title: 'Close quick jump' });
    close.addEventListener('click', () => jumpDialog.close());
    jumpDialog.append(label, jumpResults, close);
    document.body.append(jumpDialog);
    jumpInput.addEventListener('input', renderJump);
    jumpInput.addEventListener('keydown', (event) => {
        if (event.key === 'ArrowDown') { event.preventDefault(); moveJump(1); }
        else if (event.key === 'ArrowUp') { event.preventDefault(); moveJump(-1); }
        else if (event.key === 'Enter') {
            event.preventDefault();
            void chooseJump(jumpList[jumpActive]);
        }
    });
}

async function openJump() {
    if (jumpDialog.open) return;
    if (!state.sections.length) await loadTree();
    jumpInput.value = '';
    renderJump();
    jumpDialog.showModal();
    jumpInput.focus();
}

function renderJump() {
    jumpList = jumpMatches(state.sections, jumpInput.value);
    jumpActive = Math.max(0, Math.min(jumpActive, jumpList.length - 1));
    jumpResults.replaceChildren();
    if (!jumpList.length) {
        jumpResults.append(el('p', { class: 'empty', text: 'No files match.' }));
        return;
    }
    jumpList.forEach((match, index) => {
        const button = el('button', {
            class: `jump-item${index === jumpActive ? ' is-active' : ''}`,
            type: 'button',
            role: 'option',
            title: `Open ${match.path}`,
        });
        button.append(el('strong', { text: match.title || match.path }));
        const meta = [match.meta];
        if (match.folder?.inventoryNumber) meta.push(`#${match.folder.inventoryNumber}`);
        if (match.sectionId === 'sales' && match.folder?.price) meta.push(formatPriceLabel(match.folder.price, match.folder.sold).short);
        meta.push(match.path);
        button.append(el('span', { class: 'jump-meta', text: meta.filter(Boolean).join(' · ') }));
        button.addEventListener('click', () => { void chooseJump(match); });
        button.addEventListener('mousemove', () => {
            jumpActive = index;
            for (const item of jumpResults.querySelectorAll('.jump-item')) item.classList.remove('is-active');
            button.classList.add('is-active');
        });
        jumpResults.append(button);
    });
}

function moveJump(delta) {
    if (!jumpList.length) return;
    jumpActive = (jumpActive + delta + jumpList.length) % jumpList.length;
    renderJump();
    jumpResults.querySelector('.is-active')?.scrollIntoView({ block: 'nearest' });
}

async function chooseJump(match) {
    jumpDialog.close();
    if (match?.path) await openPath(match.path);
}

function confirmDialog(title, message, confirmLabel) {
    return new Promise((resolve) => {
        const dialog = el('dialog', { class: 'confirm-dialog' });
        const form = el('form', { method: 'dialog' });
        const actions = el('div', { class: 'confirm-actions' });
        const cancel = el('button', { class: 'btn', type: 'submit', value: 'cancel', text: 'Cancel', title: 'Cancel and leave things as they are' });
        const ok = el('button', { class: 'btn btn-primary', type: 'submit', value: 'confirm', text: confirmLabel, title: confirmLabel });
        actions.append(cancel, ok);
        form.append(el('h2', { text: title }), el('p', { text: message }), actions);
        dialog.append(form);
        dialog.addEventListener('close', () => {
            resolve(dialog.returnValue === 'confirm');
            dialog.remove();
        }, { once: true });
        document.body.append(dialog);
        dialog.showModal();
        cancel.focus();
    });
}

function parseHash() {
    let raw = window.location.hash.replace(/^#/, '');
    if (!raw) return { path: '', mode: 'code' };
    try { raw = decodeURIComponent(raw); } catch { /* keep the raw hash */ }
    let mode = 'code';
    if (raw.endsWith('::gallery')) {
        mode = 'gallery';
        raw = raw.slice(0, -'::gallery'.length);
    }
    if (raw.startsWith('/')) return { path: normalizePath(raw), mode };
    const parts = raw.split('/').filter(Boolean);
    const section = SECTIONS.some((item) => item.id === parts[0]) ? parts[0] : '';
    if (!section) return { path: '', mode: 'code' };
    let slug = '';
    if (parts.length >= 2) {
        const last = parts[parts.length - 1];
        if (['gallery', 'code', 'details', 'share'].includes(last)) {
            if (last === 'gallery') mode = 'gallery';
            slug = parts.slice(1, -1).join('/');
        } else slug = parts.slice(1).join('/');
    }
    return { legacy: true, section, slug, mode };
}

function writeHash() {
    if (!state.path) return;
    const wanted = `${state.path}${state.mode === 'gallery' ? '::gallery' : ''}`;
    let current = window.location.hash.replace(/^#/, '');
    try { current = decodeURIComponent(current); } catch { /* compare the raw hash */ }
    if (current === wanted) return;
    history.replaceState(null, '', `#${encodeURI(wanted)}`);
}

function finishPage(raw, loc, fromPage) {
    const doc = normalizePage(raw, loc, {
        explicitWidgets: Boolean(raw?.widgets),
        explicitErrors: Array.isArray(raw?.errors),
    });
    doc.isMedia = Boolean(raw?.isMedia);
    doc.mediaKind = raw?.mediaKind || '';
    doc.fileType = raw?.fileType || doc.fileType || '';
    doc.fileSize = Number(raw?.fileSize) || 0;
    doc.width = Number(raw?.width) || 0;
    doc.height = Number(raw?.height) || 0;
    doc.sourceLoaded = typeof raw?.content === 'string' && !doc.isMedia;
    if (!fromPage) doc.sourceLoaded = typeof raw?.content === 'string' && !doc.isMedia;
    if (!doc.previewUrl && doc.path !== CONFIG_PAGE_PATH) {
        if (doc.isMedia || /\.(php|html?|shtml|txt|json|css|js|xml|csv|pdf|jpe?g|png|gif|webp|svg|bmp|tiff?|mp4|m4v|webm|mov|mp3|wav|docx?|rtf)$/i.test(doc.path || '')) {
            doc.previewUrl = doc.path;
        } else if (loc.section && loc.slug) doc.previewUrl = defaultPrimary(loc.section, loc.slug);
    }
    return doc;
}

function normalizePage(raw, loc, flags = {}) {
    const source = raw && typeof raw === 'object' ? raw : {};
    const explicitWidgets = flags.explicitWidgets != null ? flags.explicitWidgets : Boolean(source.widgets);
    const explicitErrors = flags.explicitErrors != null ? flags.explicitErrors : Array.isArray(source.errors);
    const path = normalizePath(source.path || loc.path || '');
    const section = source.section || loc.section || '';
    const slug = source.slug || loc.slug || '';
    const widgets = normalizeWidgets(source, section, explicitWidgets);
    const images = Array.isArray(source.images) ? source.images : [];
    const unlistedSlides = Array.isArray(source.unlistedSlides) ? source.unlistedSlides : [];
    let hasGallery;
    if (typeof source.hasGallery === 'boolean') hasGallery = source.hasGallery;
    else hasGallery = Boolean(slug && (section === 'sales' || images.length || unlistedSlides.length));
    const content = typeof source.content === 'string' ? source.content : '';
    const doc = {
        ...source,
        path,
        section,
        slug,
        content,
        initialContent: typeof source.initialContent === 'string' ? source.initialContent : content,
        previewUrl: source.previewUrl || '',
        hasGallery,
        images,
        unlistedSlides,
        details: source.details || {},
        widgets,
        share: widgets.share,
        pageValues: widgets.pageValues,
        relatedSales: widgets.relatedSales,
        errors: explicitErrors ? normalizeErrors(source.errors) : warningsToErrors(source.warnings),
        dirty: Boolean(source.dirty),
        explicitWidgets,
        explicitErrors,
        gallerySource: source.gallerySource || '',
    };
    return attachGallerySource(doc);
}

function normalizeWidgets(raw, section, explicit) {
    const widgets = explicit && raw.widgets ? raw.widgets : {};
    const pageValues = {
        present: false,
        title: '',
        description: '',
        image: '',
        type: '',
        date: '',
        ...(raw.pageValues || {}),
        ...(widgets.pageValues || {}),
    };
    const relatedSales = {
        present: false,
        hasInclude: false,
        vehicleType: '',
        makeType: '',
        vehicleName: '',
        overrideIds: '',
        ...(raw.relatedSales || {}),
        ...(widgets.relatedSales || {}),
    };
    const cfg = getStudioConfig();
    const googleTracking = {
        present: false,
        inheritedFromInventoryPhp: false,
        measurementId: cfg.googleMeasurementId || '',
        ...(widgets.googleTracking || {}),
    };
    const extremeTracking = {
        present: false,
        inheritedFromInventoryPhp: false,
        login: cfg.extremeLogin || '',
        pid: section || 'articles',
        hasNoscript: false,
        ...(widgets.extremeTracking || {}),
    };
    const adminTracking = {
        present: false,
        inheritedFromInventoryPhp: false,
        hasHeaderInclude: false,
        recordsViaLogview: false,
        hasTrackJs: false,
        ...(widgets.adminTracking || {}),
    };
    const share = { ...(raw.share || {}), ...(widgets.share || {}) };
    if (!explicit) {
        pageValues.present = Boolean(pageValues.title || pageValues.description || pageValues.image || pageValues.date);
        relatedSales.present = Boolean(relatedSales.vehicleType || relatedSales.makeType || relatedSales.vehicleName || relatedSales.overrideIds || relatedSales.hasInclude);
    }
    if (!googleTracking.measurementId) googleTracking.measurementId = cfg.googleMeasurementId || '';
    if (!extremeTracking.login) extremeTracking.login = cfg.extremeLogin || '';
    if (!extremeTracking.pid) extremeTracking.pid = section || 'articles';
    return { pageValues, relatedSales, googleTracking, extremeTracking, adminTracking, share };
}

function normalizeErrors(list) {
    return (Array.isArray(list) ? list : []).map((item, index) => ({
        id: item?.id || `error-${index}`,
        severity: String(item?.severity || 'error').toLowerCase() === 'warning' ? 'warning' : 'error',
        title: item?.title || 'Check',
        message: item?.message || '',
        fixable: Boolean(item?.fixable),
    }));
}

function warningsToErrors(warnings) {
    return (Array.isArray(warnings) ? warnings : []).map((item, index) => {
        const message = typeof item === 'string' ? item : (item?.message || item?.text || '');
        return { id: `warning-${index}`, severity: 'warning', title: message || 'Warning', message, fixable: false };
    });
}

function normalizeTree(data) {
    const incoming = Array.isArray(data?.sections) ? data.sections : (Array.isArray(data?.tree) ? data.tree : []);
    const byId = new Map();
    for (const section of incoming) {
        const id = section?.id || section?.section;
        if (id) byId.set(id, normalizeSection(section));
    }
    const ordered = [...byId.values()];
    ordered.sort((a, b) => {
        if (a.id === '/') return -1;
        if (b.id === '/') return 1;
        return byFilename(a.id, b.id);
    });
    return ordered;
}

function normalizeSection(section) {
    const id = section.id || section.section;
    const folders = (Array.isArray(section.folders) ? section.folders : []).map((folder) => normalizeFolder(id, folder));
    const rootFiles = (Array.isArray(section.rootFiles) ? section.rootFiles : []).map((file) => normalizeFile(file)).filter(Boolean);
    folders.sort((a, b) => byFilename(folderSortKey(a), folderSortKey(b)));
    rootFiles.sort((a, b) => byFilename(a.name, b.name));
    return {
        id,
        label: id,
        rootFiles,
        folders,
        totalFolders: folders.length || rootFiles.length,
    };
}

function emptySection(meta) {
    return { id: meta.id, label: meta.label, rootFiles: [], folders: [], totalFolders: 0 };
}

function normalizeFolder(sectionId, folder) {
    const slug = String(folder.slug || '').trim();
    const files = (Array.isArray(folder.files) ? folder.files : []).map((file) => normalizeFile(file)).filter(Boolean);
    let primary = normalizePath(folder.primaryFile || folder.primary || '');
    if (primary && !files.some((file) => file.path === primary)) primary = '';
    if (!primary) primary = preferListedFile(files)?.path || '';
    files.sort((a, b) => byFilename(a.name, b.name));
    void sectionId;
    const warnings = Array.isArray(folder.warnings) ? folder.warnings : [];
    return {
        slug,
        name: folder.name || folder.vehicleName || slug || 'Untitled',
        primaryFile: primary,
        files,
        photoCount: Number(folder.photoCount) || 0,
        unlistedCount: Number(folder.unlistedCount) || 0,
        dirty: Boolean(folder.dirty),
        issueCount: folder.issueCount != null && folder.issueCount !== '' ? (Number(folder.issueCount) || 0) : warnings.length,
        inventoryNumber: folder.inventoryNumber || '',
        makeType: folder.makeType || '',
        price: folder.price || '',
        sold: Boolean(folder.sold) || String(folder.price || '').trim().toUpperCase() === 'SOLD',
        doNotRender: Boolean(folder.doNotRender),
        warnings,
    };
}

function normalizeFile(file) {
    if (!file) return null;
    if (typeof file === 'string') {
        const path = normalizePath(file);
        const name = path.split('/').pop();
        return name ? { name, path, type: fileType(name) } : null;
    }
    const path = normalizePath(file.path || '');
    if (!path) return null;
    const name = file.name || path.split('/').pop();
    const entry = { name, path, type: file.type || fileType(name) };
    if (file.issueCount != null && file.issueCount !== '') entry.issueCount = Number(file.issueCount) || 0;
    if (file.dirty != null && file.dirty !== '') entry.dirty = Boolean(file.dirty);
    return entry;
}

function preferListedFile(files) {
    return files.find((file) => /^index\.(php|html|shtml|htm)$/i.test(file.name))
        || files.find((file) => /\.(php|html|shtml|htm)$/i.test(file.name))
        || files.find((file) => /\.pdf$/i.test(file.name) || file.type === 'pdf')
        || files.find((file) => /\.(jpe?g|png|gif|webp|svg|mp4|m4v|webm|mov)$/i.test(file.name))
        || files[0]
        || null;
}

function fileType(name) {
    const base = String(name || '');
    const dot = base.lastIndexOf('.');
    if (dot <= 0 || dot === base.length - 1) return 'file';
    const ext = base.slice(dot + 1).toLowerCase();
    if (ext === 'php') return 'php';
    if (ext === 'shtml') return 'shtml';
    if (ext === 'html' || ext === 'htm') return 'html';
    if (ext === 'txt') return 'txt';
    if (ext === 'json') return 'json';
    if (ext === 'css') return 'css';
    if (ext === 'js') return 'js';
    if (ext === 'xml') return 'xml';
    if (ext === 'csv') return 'csv';
    if (ext === 'pdf') return 'pdf';
    if (ext === 'jpg' || ext === 'jpeg') return 'jpg';
    if (ext === 'png') return 'png';
    if (ext === 'gif') return 'gif';
    if (ext === 'webp') return 'webp';
    if (ext === 'svg') return 'svg';
    if (ext === 'mp4' || ext === 'm4v' || ext === 'webm' || ext === 'mov') return 'video';
    if (ext === 'mp3' || ext === 'wav') return 'audio';
    if (ext === 'swf' || ext === 'rm') return 'media';
    if (ext === 'doc' || ext === 'docx') return 'doc';
    return ext;
}

function defaultPrimary(section, slug) {
    if (section === 'sales') return `/sales/${slug}/index.php`;
    return `/${section}/${slug}/index.html`;
}

function locate(path) {
    const clean = normalizePath(path);
    const parts = clean.split('/').filter(Boolean);
    let section = parts.length >= 2 ? parts[0] : (parts.length === 1 ? '/' : '');
    let slug = parts.length >= 3 ? parts.slice(1, -1).join('/') : '';
    for (const item of state.sections || []) {
        if ((item.rootFiles || []).some((file) => file.path === clean)) {
            return { path: clean, section: item.id, slug: '' };
        }
        for (const folder of item.folders || []) {
            const paths = [folder.primaryFile, ...(folder.files || []).map((file) => file.path)];
            if (paths.includes(clean)) {
                section = item.id;
                slug = folder.slug;
            }
        }
    }
    return { path: clean, section, slug };
}

function byFilename(a, b) {
    return String(a || '').localeCompare(String(b || ''), undefined, { numeric: true, sensitivity: 'base' });
}

function folderSortKey(folder) {
    if (!folder) return '';
    if (folder.slug) return String(folder.slug);
    const primary = String(folder.primaryFile || folder.path || '');
    const base = primary.split('/').filter(Boolean).pop();
    return base || String(folder.name || '');
}

function normalizePath(path) {
    let text = String(path || '').trim();
    if (!text) return '';
    try { text = decodeURIComponent(text); } catch { /* keep the original text */ }
    text = text.replace(/\\/g, '/');
    if (!text.startsWith('/')) text = `/${text}`;
    text = text.replace(/\/{2,}/g, '/');
    if (text.length > 1 && text.endsWith('/')) text = text.slice(0, -1);
    return text;
}

function imageSignature(images) {
    if (!Array.isArray(images)) return '';
    return images.map((image) => `${image?.name || ''}\u0000${image?.caption || ''}`).join('\n');
}

async function apiSend(method, path, body, signal) {
    const response = await fetch(path, {
        method,
        signal,
        cache: 'no-store',
        headers: {
            Accept: 'application/json',
            ...(body ? { 'Content-Type': 'application/json' } : {}),
        },
        body: body ? JSON.stringify(body) : undefined,
    });
    const data = await response.json().catch(() => null);
    if (!response.ok || (data && data.ok === false)) {
        const error = new Error((data && (data.error || data.message)) || `Request failed (${response.status})`);
        error.status = response.status;
        throw error;
    }
    return data || { ok: true };
}

function announce(message) {
    if (!els.live) return;
    els.live.textContent = '';
    window.setTimeout(() => { els.live.textContent = message; }, 20);
}

function clamp(value, min, max) {
    return Math.max(min, Math.min(max, value));
}

if (typeof document !== 'undefined') {
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
    else init();
}
