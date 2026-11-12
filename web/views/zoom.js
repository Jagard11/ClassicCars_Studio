const STORAGE_KEY = 'studio.panelZoom';
const LEGACY_STORAGE_KEY = 'studio.zoomFactor';
const MIN_ZOOM = 0.5;
const MAX_ZOOM = 2.5;
const DEFAULT_ZOOM = 1;
const ZOOM_STEP = 0.1;
const BASE_CODE_FONT_PX = 13;

export const PANEL_IDS = ['tree', 'editor', 'preview', 'terminal', 'widgets', 'macro'];

const PANEL_LABELS = {
    tree: 'Left Shelf',
    editor: 'Editor',
    preview: 'Preview',
    terminal: 'Terminal',
    widgets: 'Widgets',
    macro: 'Project Macros',
};

const PANEL_ROOT_SELECTORS = {
    tree: '#tree-panel',
    editor: '#editor-pane, #gallery-sheet',
    preview: '#preview-pane',
    terminal: '#terminal-panel',
    widgets: '#inspector-panel, #widgets-panel',
    macro: '.macro-builder-dialog',
};

export function clampZoom(value) {
    const num = Number(value);
    if (!Number.isFinite(num)) return DEFAULT_ZOOM;
    const clamped = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, num));
    return Math.round(clamped * 100) / 100;
}

export function wheelZoomDirection(event) {
    if (!event || !event.ctrlKey || event.altKey || event.metaKey) return 0;
    const raw = event.deltaY !== 0
        ? -event.deltaY
        : event.deltaX !== 0
            ? -event.deltaX
            : (Number(event.wheelDelta) || 0);
    if (raw > 0) return 1;
    if (raw < 0) return -1;
    return 0;
}

export function panelFromNode(node, frame = null) {
    if (!node) return null;
    if (frame) {
        try {
            if (node === frame || node.ownerDocument === frame.contentDocument) return 'preview';
        } catch { /* cross-origin or detached */ }
    }
    const el = node.nodeType === 1 ? node : node.parentElement;
    if (!el || typeof el.closest !== 'function') return null;
    if (el.closest('.macro-builder-dialog')) return 'macro';
    if (el.closest('#gallery-sheet')) return 'editor';
    if (el.closest('#editor-pane, #code-mount, #gallery-mount, #view-gallery, #view-details, #view-share')) return 'editor';
    if (el.closest('#preview-pane, #preview-stage, #preview-frame')) return 'preview';
    if (el.closest('#terminal-panel')) return 'terminal';
    if (el.closest('#tree-panel')) return 'tree';
    if (el.closest('#inspector-panel, #widgets-panel, #widget-mount')) return 'widgets';
    return null;
}

export function mountZoom(options = {}) {
    const frame = options.frame || null;
    const scaleBadge = options.scaleBadge || null;
    const seenEvents = new WeakSet();
    const zooms = readStoredZooms();
    let hoveredPanel = null;
    let focusedPanel = null;
    let detachFrame = () => {};

    function defaultZoomMap() {
        return {
            tree: DEFAULT_ZOOM,
            editor: DEFAULT_ZOOM,
            preview: DEFAULT_ZOOM,
            terminal: DEFAULT_ZOOM,
            widgets: DEFAULT_ZOOM,
            macro: DEFAULT_ZOOM,
        };
    }

    function validPanel(panel) {
        return PANEL_IDS.includes(panel) ? panel : null;
    }

    function activePanel(preferred = null) {
        return validPanel(preferred) || validPanel(hoveredPanel) || validPanel(focusedPanel) || 'preview';
    }

    function readStoredZooms() {
        const map = defaultZoomMap();
        try {
            const raw = localStorage?.getItem?.(STORAGE_KEY);
            if (raw) {
                const parsed = JSON.parse(raw);
                if (parsed && typeof parsed === 'object') {
                    for (const id of PANEL_IDS) {
                        if (parsed[id] != null) map[id] = clampZoom(parsed[id]);
                    }
                    return map;
                }
            }
            const legacy = localStorage?.getItem?.(LEGACY_STORAGE_KEY);
            if (legacy != null && legacy !== '') {
                const z = clampZoom(legacy);
                for (const id of PANEL_IDS) map[id] = z;
            }
        } catch { /* private mode */ }
        return map;
    }

    function writeStoredZooms() {
        try {
            const anyCustom = PANEL_IDS.some((id) => Math.abs(zooms[id] - DEFAULT_ZOOM) >= 0.001);
            if (!anyCustom) {
                localStorage?.removeItem?.(STORAGE_KEY);
                localStorage?.removeItem?.(LEGACY_STORAGE_KEY);
            } else {
                localStorage?.setItem?.(STORAGE_KEY, JSON.stringify(zooms));
            }
        } catch { /* private mode */ }
    }

    function applyPanelStyle(panel) {
        const z = zooms[panel] ?? DEFAULT_ZOOM;
        const root = document.documentElement;
        root?.style?.setProperty?.(`--zoom-${panel}`, String(z));
        const selector = PANEL_ROOT_SELECTORS[panel];
        if (selector && typeof document.querySelectorAll === 'function') {
            for (const el of document.querySelectorAll(selector)) {
                el.style?.setProperty?.('--panel-zoom', String(z));
            }
        }
        if (panel === 'editor') {
            const codeSize = `${Math.round(BASE_CODE_FONT_PX * z * 100) / 100}px`;
            root?.style?.setProperty?.('--studio-code-size', codeSize);
            if (typeof document.querySelectorAll === 'function') {
                for (const shell of document.querySelectorAll('.code-shell')) {
                    shell.style?.setProperty?.('--code-size', codeSize);
                }
            }
            applyGalleryZoom(z);
        }
    }

    function applyAllPanels(announcePanel = null) {
        for (const id of PANEL_IDS) applyPanelStyle(id);
        const target = validPanel(announcePanel) || activePanel();
        options.onZoomChange?.(zooms[target], {
            panel: target,
            label: PANEL_LABELS[target] || target,
            zooms: { ...zooms },
            announce: Boolean(announcePanel),
        });
    }

    function applyGalleryZoom(editorZoom = zooms.editor) {
        if (typeof document.querySelectorAll !== 'function') return;
        for (const sheet of document.querySelectorAll('.sheet')) {
            if (Math.abs(editorZoom - DEFAULT_ZOOM) < 0.001) {
                sheet.style?.removeProperty?.('--cols');
                continue;
            }
            const density = sheet.getAttribute?.('data-density') || 'standard';
            const baseCols = density === 'compact' ? 6 : density === 'large' ? 3 : 4;
            const cols = Math.max(1, Math.min(10, Math.round(baseCols / editorZoom)));
            sheet.style?.setProperty?.('--cols', String(cols));
        }
    }

    function resolveEventPanel(event) {
        if (!event) return null;
        if (frame) {
            try {
                if (event.view && event.view === frame.contentWindow) return 'preview';
            } catch { /* ignore */ }
        }
        const direct = panelFromNode(event.target, frame);
        if (direct) return direct;
        if (typeof document.elementFromPoint === 'function' && Number.isFinite(event.clientX) && Number.isFinite(event.clientY)) {
            const hit = document.elementFromPoint(event.clientX, event.clientY);
            const fromPoint = panelFromNode(hit, frame);
            if (fromPoint) return fromPoint;
        }
        return null;
    }

    function setZoom(next, opts = {}) {
        const panel = activePanel(opts.panel);
        const announce = opts.announce !== false;
        const clamped = clampZoom(next);
        if (Math.abs(clamped - zooms[panel]) < 0.001) return false;
        zooms[panel] = clamped;
        writeStoredZooms();
        applyPanelStyle(panel);
        options.onZoomChange?.(zooms[panel], {
            panel,
            label: PANEL_LABELS[panel] || panel,
            zooms: { ...zooms },
            announce,
        });
        return true;
    }

    function stepZoom(direction, opts = {}) {
        if (!direction) return false;
        const panel = activePanel(opts.panel);
        const next = clampZoom(zooms[panel] + (direction > 0 ? ZOOM_STEP : -ZOOM_STEP));
        return setZoom(next, { ...opts, panel });
    }

    function zoomIn(opts = {}) {
        return stepZoom(1, opts);
    }

    function zoomOut(opts = {}) {
        return stepZoom(-1, opts);
    }

    function resetZoom(opts = {}) {
        return setZoom(DEFAULT_ZOOM, opts);
    }

    function onPointerTrack(event) {
        const panel = resolveEventPanel(event);
        if (panel) {
            const changed = panel !== hoveredPanel;
            hoveredPanel = panel;
            if (changed) options.onActivePanelChange?.(panel);
        }
    }

    function onFocusIn(event) {
        const panel = resolveEventPanel(event);
        if (panel) focusedPanel = panel;
    }

    function onWheel(event) {
        if (!event || !event.ctrlKey || event.altKey || event.metaKey) return;
        if (seenEvents.has(event)) return;
        seenEvents.add(event);
        const panel = resolveEventPanel(event) || activePanel();
        hoveredPanel = panel;
        event.preventDefault();
        event.stopPropagation();
        const dir = wheelZoomDirection(event);
        if (dir > 0) zoomIn({ panel });
        else if (dir < 0) zoomOut({ panel });
    }

    function onFrameHover() {
        const changed = hoveredPanel !== 'preview';
        hoveredPanel = 'preview';
        if (changed) options.onActivePanelChange?.('preview');
    }

    function onFrameFocus() {
        focusedPanel = 'preview';
    }

    function attachFrame() {
        detachFrame();
        if (!frame) return;
        let win = null;
        let doc = null;
        try {
            win = frame.contentWindow;
            doc = frame.contentDocument;
        } catch {
            return;
        }
        if (!win && !doc) return;
        try {
            win?.addEventListener('wheel', onWheel, { capture: true, passive: false });
            doc?.addEventListener('wheel', onWheel, { capture: true, passive: false });
            doc?.addEventListener('pointermove', onFrameHover, { capture: true, passive: true });
            doc?.addEventListener('mouseenter', onFrameHover, { capture: true, passive: true });
            win?.addEventListener('focus', onFrameFocus, true);
            doc?.addEventListener('focusin', onFrameFocus, true);
        } catch {
            return;
        }
        detachFrame = () => {
            try {
                win?.removeEventListener('wheel', onWheel, { capture: true });
                doc?.removeEventListener('wheel', onWheel, { capture: true });
                doc?.removeEventListener('pointermove', onFrameHover, { capture: true });
                doc?.removeEventListener('mouseenter', onFrameHover, { capture: true });
                win?.removeEventListener('focus', onFrameFocus, true);
                doc?.removeEventListener('focusin', onFrameFocus, true);
            } catch { /* previous preview document was discarded */ }
        };
    }

    function onScaleBadgeClick() {
        if (Math.abs(zooms.preview - DEFAULT_ZOOM) >= 0.001) {
            resetZoom({ panel: 'preview' });
        }
    }

    window.addEventListener('wheel', onWheel, { capture: true, passive: false });
    document.addEventListener('wheel', onWheel, { capture: true, passive: false });
    document.addEventListener('pointermove', onPointerTrack, { capture: true, passive: true });
    document.addEventListener('mouseover', onPointerTrack, { capture: true, passive: true });
    document.addEventListener('focusin', onFocusIn, true);
    frame?.addEventListener('pointerenter', onFrameHover);
    frame?.addEventListener('mouseenter', onFrameHover);
    frame?.addEventListener('load', attachFrame);
    scaleBadge?.addEventListener('click', onScaleBadgeClick);
    attachFrame();
    applyAllPanels(null);

    return {
        getZoom: (panel = null) => zooms[activePanel(panel)],
        getPanelZooms: () => ({ ...zooms }),
        getActivePanel: () => activePanel(),
        setZoom,
        zoomIn,
        zoomOut,
        resetZoom,
        refresh: () => applyAllPanels(null),
        canZoomIn: (panel = null) => zooms[activePanel(panel)] < MAX_ZOOM - 0.001,
        canZoomOut: (panel = null) => zooms[activePanel(panel)] > MIN_ZOOM + 0.001,
        canResetZoom: (panel = null) => Math.abs(zooms[activePanel(panel)] - DEFAULT_ZOOM) >= 0.001,
        destroy() {
            detachFrame();
            window.removeEventListener('wheel', onWheel, { capture: true });
            document.removeEventListener('wheel', onWheel, { capture: true });
            document.removeEventListener('pointermove', onPointerTrack, { capture: true });
            document.removeEventListener('mouseover', onPointerTrack, { capture: true });
            document.removeEventListener('focusin', onFocusIn, true);
            frame?.removeEventListener('mouseenter', onFrameHover);
            frame?.removeEventListener('load', attachFrame);
            scaleBadge?.removeEventListener('click', onScaleBadgeClick);
        },
    };
}
