const STORAGE_KEY = 'studio.zoomFactor';
const MIN_ZOOM = 0.5;
const MAX_ZOOM = 2.5;
const DEFAULT_ZOOM = 1;
const ZOOM_STEP = 0.1;
const BASE_CODE_FONT_PX = 13;

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

export function mountZoom(options = {}) {
    const frame = options.frame || null;
    const scaleBadge = options.scaleBadge || null;
    const seenEvents = new WeakSet();
    let zoom = readStoredZoom();
    let detachFrame = () => {};

    function readStoredZoom() {
        try {
            const raw = localStorage.getItem(STORAGE_KEY);
            if (raw == null || raw === '') return DEFAULT_ZOOM;
            return clampZoom(raw);
        } catch {
            return DEFAULT_ZOOM;
        }
    }

    function writeStoredZoom(value) {
        try {
            if (Math.abs(value - DEFAULT_ZOOM) < 0.001) localStorage.removeItem(STORAGE_KEY);
            else localStorage.setItem(STORAGE_KEY, String(value));
        } catch { /* private mode */ }
    }

    function applyZoom(announceChange = false) {
        const root = document.documentElement;
        const codeSize = `${Math.round(BASE_CODE_FONT_PX * zoom * 100) / 100}px`;
        root.style.setProperty('--studio-zoom', String(zoom));
        root.style.setProperty('--studio-code-size', codeSize);
        for (const shell of document.querySelectorAll('.code-shell')) {
            shell.style.setProperty('--code-size', codeSize);
        }
        applyGalleryZoom();
        options.onZoomChange?.(zoom, { announce: announceChange });
    }

    function applyGalleryZoom() {
        for (const sheet of document.querySelectorAll('.sheet')) {
            if (Math.abs(zoom - DEFAULT_ZOOM) < 0.001) {
                sheet.style.removeProperty('--cols');
                continue;
            }
            const density = sheet.getAttribute('data-density') || 'standard';
            const baseCols = density === 'compact' ? 6 : density === 'large' ? 3 : 4;
            const cols = Math.max(1, Math.min(10, Math.round(baseCols / zoom)));
            sheet.style.setProperty('--cols', String(cols));
        }
    }

    function setZoom(next, { announce = true } = {}) {
        const clamped = clampZoom(next);
        if (Math.abs(clamped - zoom) < 0.001) return false;
        zoom = clamped;
        writeStoredZoom(zoom);
        applyZoom(announce);
        return true;
    }

    function stepZoom(direction, { announce = true } = {}) {
        if (!direction) return false;
        const next = clampZoom(zoom + (direction > 0 ? ZOOM_STEP : -ZOOM_STEP));
        return setZoom(next, { announce });
    }

    function zoomIn(opts) {
        return stepZoom(1, opts);
    }

    function zoomOut(opts) {
        return stepZoom(-1, opts);
    }

    function resetZoom(opts) {
        return setZoom(DEFAULT_ZOOM, opts);
    }

    function onWheel(event) {
        if (!event || !event.ctrlKey || event.altKey || event.metaKey) return;
        if (seenEvents.has(event)) return;
        seenEvents.add(event);
        event.preventDefault();
        event.stopPropagation();
        const dir = wheelZoomDirection(event);
        if (dir > 0) zoomIn();
        else if (dir < 0) zoomOut();
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
        } catch {
            return;
        }
        detachFrame = () => {
            try {
                win?.removeEventListener('wheel', onWheel, { capture: true });
                doc?.removeEventListener('wheel', onWheel, { capture: true });
            } catch { /* previous preview document was discarded */ }
        };
    }

    function onScaleBadgeClick() {
        if (Math.abs(zoom - DEFAULT_ZOOM) >= 0.001) resetZoom();
    }

    window.addEventListener('wheel', onWheel, { capture: true, passive: false });
    document.addEventListener('wheel', onWheel, { capture: true, passive: false });
    frame?.addEventListener('load', attachFrame);
    scaleBadge?.addEventListener('click', onScaleBadgeClick);
    attachFrame();
    applyZoom(false);

    return {
        getZoom: () => zoom,
        setZoom,
        zoomIn,
        zoomOut,
        resetZoom,
        refresh: () => applyZoom(false),
        canZoomIn: () => zoom < MAX_ZOOM - 0.001,
        canZoomOut: () => zoom > MIN_ZOOM + 0.001,
        canResetZoom: () => Math.abs(zoom - DEFAULT_ZOOM) >= 0.001,
        destroy() {
            detachFrame();
            window.removeEventListener('wheel', onWheel, { capture: true });
            document.removeEventListener('wheel', onWheel, { capture: true });
            frame?.removeEventListener('load', attachFrame);
            scaleBadge?.removeEventListener('click', onScaleBadgeClick);
        },
    };
}
