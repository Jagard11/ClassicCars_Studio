import { el } from './dom.js';
import { DIFF_ADDED, DIFF_MODIFIED } from './line-diff.js';

const MAX_COLS = 92;
const MAX_LINE_PITCH = 5.5;
const MIN_SLIDER_HEIGHT = 22;

const JS_KEYWORDS = new Set([
    'function', 'return', 'if', 'else', 'elseif', 'foreach', 'for', 'while',
    'require_once', 'include', 'include_once', 'require', 'echo', 'isset',
    'var', 'let', 'const', 'class', 'async', 'await', 'switch', 'case',
    'break', 'continue', 'try', 'catch', 'throw', 'new', 'true', 'false', 'null',
]);

const BLOCK_OPEN_RE = /^<(section|article|header|footer|main|nav|aside|figure|div|form|table|ul|ol|style|script)\b([^>]*)/i;
const BLOCK_CLOSE_RE = /^<\/(section|article|header|footer|main|nav|aside|figure|div|form|table|ul|ol|style|script)\b/i;
const META_START_RE = /^<!--\s*(page-values|related-sales|generated-seo)\b/i;

export function mountCodeMinimap({ textarea, onScrollTo }) {
    const root = el('div', {
        class: 'code-overview',
        role: 'scrollbar',
        'aria-label': 'Code overview scrollbar and minimap',
        'aria-orientation': 'vertical',
        'aria-valuemin': '0',
        'aria-valuemax': '100',
        'aria-valuenow': '0',
        title: 'Code overview — click or drag to scroll',
    });
    const canvas = el('canvas', {
        class: 'code-overview-canvas',
        'aria-hidden': 'true',
    });
    const slider = el('div', {
        class: 'code-overview-slider',
        'aria-hidden': 'true',
    });
    root.append(canvas, slider);

    let parsedLines = [];
    let lineStarts = [0];
    let language = 'text';
    let diffState = null;
    let findMatches = [];
    let activeFindIndex = -1;
    let selectionRange = { start: 0, end: 0, cursorLine: 1, startLine: 1, endLine: 1 };
    let targetRange = null;
    let palette = null;
    let dragging = false;
    let dragGrabOffset = 0;
    let dragMode = 'track';
    let lastHoverLine = -1;

    function updateDocument({ source = '', lang = 'text', diff = null } = {}) {
        const text = String(source ?? '');
        language = String(lang || 'text').toLowerCase();
        diffState = diff;
        const rawLines = text.split('\n');
        lineStarts = new Array(rawLines.length);
        let offset = 0;
        for (let i = 0; i < rawLines.length; i += 1) {
            lineStarts[i] = offset;
            offset += rawLines[i].length + 1;
        }
        parsedLines = scanMinimapLines(rawLines, language);
        root.dataset.lines = String(rawLines.length);
        root.dataset.diffAdded = String(diff?.addedCount || 0);
        root.dataset.diffModified = String(diff?.modifiedCount || 0);
        root.dataset.diffDeleted = String(diff?.deletedCount || 0);
        render();
    }

    function updateFind({ hits = [], needleLength = 0, activeIndex = -1, open = false } = {}) {
        if (!open || !hits.length || needleLength <= 0) {
            findMatches = [];
            activeFindIndex = -1;
            root.dataset.findMatches = '0';
            render();
            return;
        }
        activeFindIndex = activeIndex;
        findMatches = hits.map((pos, index) => {
            const startLoc = offsetToLineCol(pos);
            const endLoc = offsetToLineCol(pos + Math.max(1, needleLength));
            return {
                index,
                startLine: startLoc.line,
                endLine: endLoc.line,
                startCol: startLoc.col,
                endCol: startLoc.line === endLoc.line ? Math.max(startLoc.col + 1, endLoc.col) : MAX_COLS,
                active: index === activeIndex,
            };
        });
        root.dataset.findMatches = String(findMatches.length);
        render();
    }

    function updateSelection({ selectionStart = 0, selectionEnd = 0 } = {}) {
        const start = Math.max(0, Number(selectionStart) || 0);
        const end = Math.max(start, Number(selectionEnd) || 0);
        const startLoc = offsetToLineCol(start);
        const endLoc = end > start ? offsetToLineCol(Math.max(start, end - 1)) : startLoc;
        selectionRange = {
            start,
            end,
            cursorLine: startLoc.line,
            startLine: startLoc.line,
            endLine: endLoc.line,
            startCol: startLoc.col,
            endCol: end > start ? Math.max(startLoc.col + 1, endLoc.col + 1) : startLoc.col,
            hasSelection: end > start,
        };
        root.dataset.cursorLine = String(selectionRange.cursorLine);
        if (selectionRange.hasSelection) {
            root.dataset.selectionLines = `${selectionRange.startLine}-${selectionRange.endLine}`;
        } else {
            delete root.dataset.selectionLines;
        }
        render();
    }

    function updateTarget(range = null) {
        if (range && Number.isFinite(range.startLine) && Number.isFinite(range.endLine)) {
            targetRange = {
                startLine: Math.max(1, range.startLine),
                endLine: Math.max(range.startLine, range.endLine),
                kind: range.kind || 'inline',
            };
            root.dataset.highlightLines = `${targetRange.startLine}-${targetRange.endLine}`;
        } else {
            targetRange = null;
            delete root.dataset.highlightLines;
        }
        render();
    }

    function syncScroll() {
        updateSliderPosition();
    }

    function metrics() {
        const rect = root.getBoundingClientRect();
        const width = Math.max(48, Math.round(rect.width || root.clientWidth || 70));
        const height = Math.max(40, Math.round(rect.height || root.clientHeight || textarea.clientHeight || 240));
        const lineCount = Math.max(1, parsedLines.length);
        const availHeight = Math.max(24, height - 4);
        const pitch = Math.min(MAX_LINE_PITCH, availHeight / lineCount);
        const mapSpan = Math.max(MIN_SLIDER_HEIGHT, Math.min(height, Math.round(lineCount * pitch + 4)));
        return { width, height, lineCount, pitch, mapSpan };
    }

    function updateSliderPosition(m = metrics()) {
        const scrollHeight = Math.max(1, textarea.scrollHeight || 1);
        const clientHeight = Math.max(1, textarea.clientHeight || m.height);
        const scrollTop = Math.max(0, textarea.scrollTop || 0);
        const maxScroll = Math.max(0, scrollHeight - clientHeight);
        const trackSpan = m.mapSpan;

        let sliderHeight;
        let sliderTop;
        if (maxScroll <= 1) {
            sliderHeight = trackSpan;
            sliderTop = 0;
            root.setAttribute('aria-valuenow', '0');
        } else {
            const visibleRatio = Math.min(1, clientHeight / scrollHeight);
            sliderHeight = Math.max(MIN_SLIDER_HEIGHT, Math.min(trackSpan, Math.round(visibleRatio * trackSpan)));
            const maxSliderTop = Math.max(0, trackSpan - sliderHeight);
            const progress = Math.max(0, Math.min(1, scrollTop / maxScroll));
            sliderTop = Math.round(progress * maxSliderTop);
            root.setAttribute('aria-valuenow', String(Math.round(progress * 100)));
        }
        slider.style.top = `${sliderTop}px`;
        slider.style.height = `${sliderHeight}px`;
        return { sliderTop, sliderHeight, maxScroll, trackSpan };
    }

    function render() {
        const m = metrics();
        const dpr = Math.max(1, Math.min(3, window.devicePixelRatio || 1));
        const targetW = Math.round(m.width * dpr);
        const targetH = Math.round(m.height * dpr);
        if (canvas.width !== targetW || canvas.height !== targetH) {
            canvas.width = targetW;
            canvas.height = targetH;
        }
        const ctx = canvas.getContext('2d');
        if (!ctx) {
            updateSliderPosition(m);
            return;
        }

        const colors = palette || readPalette();
        palette = colors;
        ctx.save();
        ctx.scale(dpr, dpr);
        ctx.clearRect(0, 0, m.width, m.height);

        const rulerWidth = 13;
        const rulerX = m.width - rulerWidth;
        const diffLaneW = 4;
        const codeX = diffLaneW + 3;
        const codeW = Math.max(16, rulerX - codeX - 3);
        const colScale = codeW / MAX_COLS;
        const barH = Math.max(1.15, Math.min(3.2, m.pitch * 0.66));
        const padTop = 2;

        // Subtle separator line for the right-hand overview ruler lane
        ctx.fillStyle = colors.rulerBg;
        ctx.fillRect(rulerX, 0, rulerWidth, m.height);
        ctx.fillStyle = colors.rulerBorder;
        ctx.fillRect(rulerX - 0.5, 0, 1, m.height);

        const lineToY = (line1) => padTop + (Math.max(1, line1) - 1) * m.pitch;
        const rulerLineToY = (line1) => {
            if (m.mapSpan >= m.height - 8) return lineToY(line1);
            return padTop + ((Math.max(1, line1) - 1) / Math.max(1, m.lineCount)) * (m.mapSpan - padTop);
        };

        // 1. Structural block background shading & bracket guides
        for (let i = 0; i < parsedLines.length; i += 1) {
            const info = parsedLines[i];
            const y = lineToY(i + 1);
            if (info.isMetaBlock) {
                ctx.fillStyle = colors.metaBand;
                ctx.fillRect(codeX, y, codeW, Math.max(1.5, m.pitch));
            } else if (info.blockDepth > 0 && info.segments.length > 0) {
                const guideX = codeX + Math.min(20, info.blockDepth * 3) * colScale;
                ctx.fillStyle = colors.blockGuide;
                ctx.fillRect(guideX, y, 1, Math.max(1.2, m.pitch));
            }
            if (info.isBlockHeader) {
                ctx.fillStyle = colors.blockHeaderBand;
                ctx.fillRect(codeX, y, codeW, Math.max(1.8, barH + 0.6));
            }
        }

        // 2. Selection range highlight
        if (selectionRange.hasSelection) {
            const y0 = lineToY(selectionRange.startLine);
            const y1 = lineToY(selectionRange.endLine) + Math.max(2, m.pitch);
            ctx.fillStyle = colors.selectionBand;
            ctx.fillRect(codeX - 1, y0, codeW + 2, Math.max(2.5, y1 - y0));

            const ry0 = rulerLineToY(selectionRange.startLine);
            const ry1 = rulerLineToY(selectionRange.endLine) + Math.max(3, m.pitch);
            ctx.fillStyle = colors.selectionTick;
            ctx.fillRect(rulerX + 4, ry0, 4, Math.max(3, ry1 - ry0));
        }

        // 3. Jump / inline edit target highlight
        if (targetRange) {
            const y0 = lineToY(targetRange.startLine);
            const y1 = lineToY(targetRange.endLine) + Math.max(2.5, m.pitch);
            ctx.fillStyle = colors.targetBand;
            ctx.fillRect(0, y0, rulerX, Math.max(3, y1 - y0));
            ctx.fillStyle = colors.targetTick;
            ctx.fillRect(rulerX + 8, rulerLineToY(targetRange.startLine), 4, Math.max(4, y1 - y0));
        }

        // 4. Active cursor line highlight
        if (selectionRange.cursorLine >= 1 && selectionRange.cursorLine <= m.lineCount) {
            const cy = lineToY(selectionRange.cursorLine);
            ctx.fillStyle = colors.cursorBand;
            ctx.fillRect(codeX - 1, cy, codeW + 2, Math.max(2, m.pitch));
            ctx.fillStyle = colors.cursorTick;
            ctx.fillRect(rulerX + 8, rulerLineToY(selectionRange.cursorLine), 4, 2);
        }

        // 5. Code block / syntax token micro-bars
        const step = m.pitch < 0.6 ? Math.ceil(0.6 / m.pitch) : 1;
        for (let i = 0; i < parsedLines.length; i += step) {
            const info = parsedLines[i];
            if (!info.segments.length) continue;
            const y = lineToY(i + 1);
            for (let s = 0; s < info.segments.length; s += 1) {
                const seg = info.segments[s];
                const x = codeX + seg.col * colScale;
                const w = Math.max(1.4, seg.len * colScale);
                ctx.fillStyle = colors.tokens[seg.token] || colors.tokens.text;
                ctx.fillRect(x, y, Math.min(codeW - (x - codeX), w), barH);
            }
        }

        // 6. Git / modification comparisons (left diff lane + right overview ruler left lane)
        if (diffState && diffState.hasChanges) {
            const kinds = diffState.lineKinds;
            const itemH = Math.max(2, m.pitch);
            for (let i = 0; i < kinds.length; i += 1) {
                const kind = kinds[i];
                if (!kind) continue;
                const y = lineToY(i + 1);
                const ry = rulerLineToY(i + 1);
                const color = kind === DIFF_ADDED ? colors.diffAdd : colors.diffMod;
                ctx.fillStyle = color;
                ctx.fillRect(1, y, 2.5, itemH);
                ctx.fillRect(rulerX + 1, ry, 3, Math.max(2.5, itemH));
            }
            if (Array.isArray(diffState.deletions)) {
                ctx.fillStyle = colors.diffDel;
                for (let d = 0; d < diffState.deletions.length; d += 1) {
                    const del = diffState.deletions[d];
                    const y = lineToY(del.line);
                    const ry = rulerLineToY(del.line);
                    ctx.beginPath();
                    ctx.moveTo(0.5, y - 2.2);
                    ctx.lineTo(5, y);
                    ctx.lineTo(0.5, y + 2.2);
                    ctx.closePath();
                    ctx.fill();
                    ctx.fillRect(rulerX + 1, Math.max(0, ry - 1), 3.5, 2.5);
                }
            }
        }

        // 7. Find match highlights (both inline on minimap code blocks and in overview ruler center lane)
        if (findMatches.length > 0) {
            for (let f = 0; f < findMatches.length; f += 1) {
                const hit = findMatches[f];
                const y = lineToY(hit.startLine);
                const ry = rulerLineToY(hit.startLine);
                const x = codeX + Math.min(MAX_COLS - 4, hit.startCol) * colScale;
                const w = Math.max(4, Math.min(codeW - (x - codeX), (hit.endCol - hit.startCol) * colScale));
                const h = Math.max(2.6, barH + 1);

                ctx.fillStyle = hit.active ? colors.findActiveLine : colors.findLineTint;
                ctx.fillRect(codeX, y - 0.5, codeW, h + 1);

                ctx.fillStyle = hit.active ? colors.findActive : colors.findMatch;
                ctx.fillRect(x, y - 0.5, w, h);

                ctx.fillRect(
                    rulerX + 4,
                    Math.max(0, ry - (hit.active ? 1.5 : 0.5)),
                    hit.active ? 8 : 5,
                    hit.active ? Math.max(4, h + 1.5) : Math.max(3, h),
                );
            }
        }

        ctx.restore();
        updateSliderPosition(m);
    }

    function offsetToLineCol(offset) {
        let low = 0;
        let high = lineStarts.length - 1;
        while (low <= high) {
            const mid = (low + high) >> 1;
            if (lineStarts[mid] <= offset) low = mid + 1;
            else high = mid - 1;
        }
        const lineIdx = Math.max(0, Math.min(lineStarts.length - 1, high));
        const col = Math.min(MAX_COLS, Math.max(0, offset - lineStarts[lineIdx]));
        return { line: lineIdx + 1, col };
    }

    function clientYToLine(clientY) {
        const rect = root.getBoundingClientRect();
        const m = metrics();
        const relY = Math.max(0, Math.min(m.mapSpan, clientY - rect.top));
        const ratio = m.mapSpan > 0 ? relY / m.mapSpan : 0;
        return Math.max(1, Math.min(m.lineCount, Math.floor(ratio * m.lineCount) + 1));
    }

    function scrollFromPointer(clientY) {
        const rect = root.getBoundingClientRect();
        const m = metrics();
        const { sliderHeight, maxScroll, trackSpan } = updateSliderPosition(m);
        if (maxScroll <= 0) return;
        const y = clientY - rect.top;
        const maxSliderTop = Math.max(1, trackSpan - sliderHeight);
        let desiredSliderTop;
        if (dragMode === 'thumb') {
            desiredSliderTop = y - dragGrabOffset;
        } else {
            desiredSliderTop = y - sliderHeight / 2;
        }
        const ratio = Math.max(0, Math.min(1, desiredSliderTop / maxSliderTop));
        const nextScrollTop = Math.round(ratio * maxScroll);
        textarea.scrollTop = nextScrollTop;
        onScrollTo?.(nextScrollTop);
        updateSliderPosition(m);
    }

    function updateHoverTooltip(clientY) {
        const line = clientYToLine(clientY);
        if (line === lastHoverLine) return;
        lastHoverLine = line;
        const info = parsedLines[line - 1];
        const parts = [`Ln ${line}`];
        if (info?.blockLabel) parts.push(info.blockLabel);
        if (findMatches.some((hit) => hit.startLine <= line && hit.endLine >= line)) {
            parts.push('Find match');
        }
        if (targetRange && line >= targetRange.startLine && line <= targetRange.endLine) {
            parts.push('Active target');
        }
        if (diffState?.hasChanges) {
            const kind = diffState.lineKinds[line - 1];
            if (kind === DIFF_ADDED) parts.push('Added in editor / Git');
            else if (kind === DIFF_MODIFIED) parts.push('Modified in editor / Git');
            else if (diffState.deletedAfter[line - 1]) parts.push('Lines deleted here');
        }
        const tipText = parts.join(' · ');
        if (root.hasAttribute('data-tooltip')) root.setAttribute('data-tooltip', tipText);
        else root.setAttribute('title', tipText);
    }

    root.addEventListener('pointerdown', (event) => {
        if (event.button !== 0) return;
        event.preventDefault();
        const sliderRect = slider.getBoundingClientRect();
        const insideThumb = event.clientY >= sliderRect.top && event.clientY <= sliderRect.bottom;
        dragging = true;
        dragMode = insideThumb ? 'thumb' : 'track';
        dragGrabOffset = insideThumb ? event.clientY - sliderRect.top : sliderRect.height / 2;
        root.classList.add('is-dragging');
        try { root.setPointerCapture(event.pointerId); } catch { /* synthetic event */ }
        if (!insideThumb) scrollFromPointer(event.clientY);
    });

    root.addEventListener('pointermove', (event) => {
        if (dragging) {
            event.preventDefault();
            scrollFromPointer(event.clientY);
            return;
        }
        updateHoverTooltip(event.clientY);
    });

    const endDrag = (event) => {
        if (!dragging) return;
        dragging = false;
        root.classList.remove('is-dragging');
        try {
            if (event?.pointerId != null && root.hasPointerCapture?.(event.pointerId)) {
                root.releasePointerCapture(event.pointerId);
            }
        } catch { /* ignore */ }
    };

    root.addEventListener('pointerup', endDrag);
    root.addEventListener('pointercancel', endDrag);
    root.addEventListener('wheel', (event) => {
        if (event.ctrlKey || event.metaKey || event.altKey) return;
        event.preventDefault();
        textarea.scrollTop += event.deltaY;
        if (event.deltaX) textarea.scrollLeft += event.deltaX;
        onScrollTo?.(textarea.scrollTop);
        updateSliderPosition();
    }, { passive: false });

    if (typeof ResizeObserver === 'function') {
        const ro = new ResizeObserver(() => render());
        ro.observe(root);
    }

    if (typeof MutationObserver === 'function') {
        const mo = new MutationObserver(() => {
            palette = null;
            render();
        });
        mo.observe(document.documentElement, {
            attributes: true,
            attributeFilter: ['data-theme', 'style', 'class'],
        });
    }

    return {
        element: root,
        updateDocument,
        updateFind,
        updateSelection,
        updateTarget,
        syncScroll,
        refresh: () => {
            palette = null;
            render();
        },
    };
}

function scanMinimapLines(lines, language) {
    const out = new Array(lines.length);
    let inComment = false;
    let inMeta = false;
    let inPhp = language === 'php';
    let inStyle = language === 'css';
    let inScript = language === 'js';
    const blockStack = [];

    for (let idx = 0; idx < lines.length; idx += 1) {
        const raw = lines[idx];
        const expanded = raw.replace(/\t/g, '    ');
        const trimmed = expanded.trim();
        let indent = 0;
        while (indent < expanded.length && expanded[indent] === ' ') indent += 1;

        let isBlockHeader = false;
        let activeBlockLabel = blockStack.length ? blockStack[blockStack.length - 1] : '';

        if (!inComment && trimmed) {
            const metaMatch = META_START_RE.exec(trimmed);
            if (metaMatch) {
                inMeta = true;
                isBlockHeader = true;
                activeBlockLabel = `<!-- ${metaMatch[1].toLowerCase()} -->`;
                blockStack.push(activeBlockLabel);
            } else if (language === 'html' || language === 'php') {
                const openMatch = BLOCK_OPEN_RE.exec(trimmed);
                if (openMatch) {
                    const tag = openMatch[1].toLowerCase();
                    const attrs = openMatch[2] || '';
                    const clsMatch = /\bclass\s*=\s*["']([^"']+)/i.exec(attrs);
                    const idMatch = /\bid\s*=\s*["']([^"']+)/i.exec(attrs);
                    const suffix = clsMatch
                        ? `.${clsMatch[1].trim().split(/\s+/)[0]}`
                        : (idMatch ? `#${idMatch[1].trim()}` : '');
                    const label = `<${tag}${suffix}>`;
                    isBlockHeader = true;
                    activeBlockLabel = label;
                    if (!/\/>$/.test(trimmed) && !new RegExp(`</${tag}\\b`, 'i').test(trimmed)) {
                        blockStack.push(label);
                    }
                    if (tag === 'style' && !/<\/style\b/i.test(trimmed)) inStyle = true;
                    if (tag === 'script' && !/<\/script\b/i.test(trimmed)) inScript = true;
                } else if (BLOCK_CLOSE_RE.test(trimmed)) {
                    if (blockStack.length) blockStack.pop();
                    if (/^<\/style\b/i.test(trimmed)) inStyle = false;
                    if (/^<\/script\b/i.test(trimmed)) inScript = false;
                }
            } else if ((language === 'js' || language === 'css') && /\{[\s]*$/.test(trimmed)) {
                isBlockHeader = true;
                const head = trimmed.replace(/\{[\s]*$/, '').trim().slice(0, 32);
                if (head) {
                    activeBlockLabel = head;
                    blockStack.push(head);
                }
            } else if ((language === 'js' || language === 'css') && /^\}/.test(trimmed)) {
                if (blockStack.length) blockStack.pop();
            }
        }

        const segments = [];
        const limit = Math.min(expanded.length, MAX_COLS);
        let col = Math.min(indent, limit);

        while (col < limit) {
            if (expanded[col] === ' ') {
                col += 1;
                continue;
            }
            const rest = expanded.slice(col, limit);

            if (inComment || inMeta) {
                const closeAt = rest.indexOf('-->');
                if (closeAt >= 0) {
                    const span = Math.max(1, closeAt + 3);
                    segments.push({ col, len: span, token: inMeta ? 'metaKey' : 'comment' });
                    col += span;
                    inComment = false;
                    if (inMeta && blockStack.length) blockStack.pop();
                    inMeta = false;
                } else {
                    if (inMeta && rest.includes(':')) {
                        const colon = rest.indexOf(':');
                        segments.push({ col, len: Math.max(1, colon + 1), token: 'metaKey' });
                        if (colon + 1 < rest.length) {
                            segments.push({ col: col + colon + 1, len: rest.length - colon - 1, token: 'string' });
                        }
                    } else {
                        segments.push({ col, len: rest.length, token: inMeta ? 'metaKey' : 'comment' });
                    }
                    break;
                }
                continue;
            }

            if (rest.startsWith('<!--#')) {
                const closeAt = rest.indexOf('-->');
                const len = closeAt >= 0 ? closeAt + 3 : rest.length;
                segments.push({ col, len, token: 'ssi' });
                col += len;
                continue;
            }

            if (rest.startsWith('<!--')) {
                const closeAt = rest.indexOf('-->');
                if (closeAt >= 0) {
                    segments.push({ col, len: closeAt + 3, token: 'comment' });
                    col += closeAt + 3;
                } else {
                    inComment = true;
                    segments.push({ col, len: rest.length, token: 'comment' });
                    break;
                }
                continue;
            }

            if (rest.startsWith('<?')) {
                inPhp = true;
                const len = rest.toLowerCase().startsWith('<?php') ? 5 : 2;
                segments.push({ col, len, token: 'ssi' });
                col += len;
                continue;
            }

            if (inPhp && rest.startsWith('?>')) {
                inPhp = language === 'php' && !expanded.includes('<');
                segments.push({ col, len: 2, token: 'ssi' });
                col += 2;
                continue;
            }

            if ((inPhp || inScript || inStyle) && (rest.startsWith('//') || rest.startsWith('/*'))) {
                segments.push({ col, len: rest.length, token: 'comment' });
                break;
            }

            const ch = expanded[col];
            if (ch === '"' || ch === "'") {
                let end = col + 1;
                while (end < limit && expanded[end] !== ch) {
                    if (expanded[end] === '\\') end += 1;
                    end += 1;
                }
                if (end < limit) end += 1;
                segments.push({ col, len: Math.max(1, end - col), token: 'string' });
                col = end;
                continue;
            }

            if (ch === '<' && (language === 'html' || language === 'php')) {
                let end = col + 1;
                if (expanded[end] === '/' || expanded[end] === '!') end += 1;
                while (end < limit && /[A-Za-z0-9_:-]/.test(expanded[end])) end += 1;
                const len = Math.max(1, end - col);
                segments.push({ col, len, token: 'tag' });
                col = end;
                continue;
            }

            if (ch === '$' && inPhp) {
                let end = col + 1;
                while (end < limit && /[A-Za-z0-9_]/.test(expanded[end])) end += 1;
                segments.push({ col, len: Math.max(1, end - col), token: 'variable' });
                col = end;
                continue;
            }

            if (/[A-Za-z_@-]/.test(ch)) {
                let end = col + 1;
                while (end < limit && /[A-Za-z0-9_:-]/.test(expanded[end])) end += 1;
                const word = expanded.slice(col, end);
                const lower = word.toLowerCase();
                let token = 'text';
                if (expanded[end] === '=' || (inStyle && expanded[end] === ':')) token = 'attr';
                else if (JS_KEYWORDS.has(lower) || word.startsWith('@')) token = 'keyword';
                else if (inStyle) token = 'tag';
                segments.push({ col, len: Math.max(1, end - col), token });
                col = end;
                continue;
            }

            if (/[0-9#]/.test(ch)) {
                let end = col + 1;
                while (end < limit && /[0-9A-Fa-f.%pxremvhw-]/.test(expanded[end])) end += 1;
                segments.push({ col, len: Math.max(1, end - col), token: 'number' });
                col = end;
                continue;
            }

            let end = col + 1;
            while (end < limit && !/[ \t"'<$A-Za-z0-9_@#]/.test(expanded[end])) end += 1;
            segments.push({ col, len: Math.max(1, end - col), token: 'punct' });
            col = end;
        }

        out[idx] = {
            indent: Math.min(indent, MAX_COLS),
            blockDepth: blockStack.length,
            isBlockHeader,
            isMetaBlock: inMeta || META_START_RE.test(trimmed),
            blockLabel: activeBlockLabel,
            segments,
        };
    }

    return out;
}

function readPalette() {
    const style = getComputedStyle(document.documentElement);
    const theme = document.documentElement.getAttribute('data-theme') || 'gold';
    const isLight = theme === 'daylight';
    const isContrast = theme === 'contrast';
    const pick = (name, fallback) => style.getPropertyValue(name).trim() || fallback;

    return {
        rulerBg: isLight ? 'rgba(15, 23, 42, 0.04)' : 'rgba(255, 255, 255, 0.03)',
        rulerBorder: pick('--border-subtle', '#333333'),
        blockGuide: isLight ? 'rgba(15, 23, 42, 0.14)' : 'rgba(255, 255, 255, 0.12)',
        blockHeaderBand: isLight ? 'rgba(2, 132, 199, 0.08)' : 'rgba(255, 204, 0, 0.07)',
        metaBand: isLight ? 'rgba(154, 52, 18, 0.09)' : 'rgba(255, 225, 74, 0.08)',
        cursorBand: isLight ? 'rgba(2, 132, 199, 0.14)' : 'rgba(255, 255, 255, 0.11)',
        cursorTick: pick('--text-primary', '#ffffff'),
        selectionBand: isLight ? 'rgba(2, 132, 199, 0.24)' : 'rgba(31, 232, 222, 0.24)',
        selectionTick: pick('--accent-focus', '#1fe8de'),
        targetBand: isLight ? 'rgba(217, 119, 6, 0.26)' : 'rgba(245, 158, 11, 0.28)',
        targetTick: '#f59e0b',
        findLineTint: isLight ? 'rgba(217, 119, 6, 0.22)' : 'rgba(245, 158, 11, 0.24)',
        findActiveLine: isLight ? 'rgba(2, 132, 199, 0.30)' : 'rgba(31, 232, 222, 0.32)',
        findMatch: isContrast ? '#ffff00' : '#f59e0b',
        findActive: pick('--accent-focus', '#1fe8de'),
        diffAdd: isContrast ? '#00ffff' : (isLight ? '#16a34a' : '#34d399'),
        diffMod: isContrast ? '#ffff00' : (isLight ? '#0284c7' : '#38bdf8'),
        diffDel: pick('--status-danger', '#ff8f8f'),
        tokens: {
            tag: pick('--syn-tag', '#ff8f70'),
            attr: pick('--syn-attr', '#d2a8ff'),
            string: pick('--syn-string', '#9ddea0'),
            comment: pick('--syn-comment', '#a8a8a8'),
            ssi: pick('--syn-ssi', '#ee82ff'),
            metaKey: pick('--syn-meta-key', '#ffe14a'),
            keyword: pick('--syn-keyword', '#79b8ff'),
            variable: pick('--syn-variable', '#5cefff'),
            number: pick('--syn-number', '#ffb86c'),
            punct: pick('--syn-punct', '#dcdcdc'),
            text: pick('--text-muted', '#b3b3b3'),
        },
    };
}
