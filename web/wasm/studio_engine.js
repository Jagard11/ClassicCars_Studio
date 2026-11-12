// Native C -> WebAssembly bridge for ClassicCars Studio (web/wasm/studio_engine.wasm)

const encoder = new TextEncoder();
const decoder = new TextDecoder();

let wasmExports = null;

async function loadWasmExports() {
    const wasmUrl = new URL('./studio_engine.wasm', import.meta.url);
    let bytes;
    if (typeof process !== 'undefined' && process.versions?.node) {
        const { readFileSync } = await import('node:fs');
        bytes = readFileSync(wasmUrl);
    } else {
        const res = await fetch(wasmUrl);
        bytes = await res.arrayBuffer();
    }
    const mod = new WebAssembly.Module(bytes);
    const inst = new WebAssembly.Instance(mod, {});
    return inst.exports;
}

wasmExports = await loadWasmExports();

function writeUtf8(str) {
    const text = String(str ?? '');
    if (!text.length) return { ptr: wasmExports.wasm_alloc(1), len: 0 };
    const maxBytes = text.length * 3 + 8;
    const ptr = wasmExports.wasm_alloc(maxBytes);
    const view = new Uint8Array(wasmExports.memory.buffer, ptr, maxBytes);
    const { written } = encoder.encodeInto(text, view);
    view[written] = 0;
    return { ptr, len: written };
}

function readResultUtf8() {
    const ptr = wasmExports.wasm_result_ptr();
    const len = wasmExports.wasm_result_len();
    if (!ptr || !len) return '';
    return decoder.decode(new Uint8Array(wasmExports.memory.buffer, ptr, len));
}

let cachedSource = null;
let cachedLang = '';
let cachedHighlightHtml = null;
let cachedMinimapLines = null;

export function primeEngineBundle(source, engine) {
    if (typeof source !== 'string' || !engine || typeof engine !== 'object') return;
    cachedSource = source;
    cachedLang = String(engine.language || '').toLowerCase();
    cachedHighlightHtml = typeof engine.highlightedHtml === 'string' ? engine.highlightedHtml : null;
    cachedMinimapLines = Array.isArray(engine.minimap) ? engine.minimap : null;
}

export function getCachedHighlightHtml(source, language) {
    if (source === cachedSource && String(language || '').toLowerCase() === cachedLang && cachedHighlightHtml !== null) {
        return cachedHighlightHtml;
    }
    return null;
}

export function getCachedMinimapLines(source, language) {
    if (source === cachedSource && String(language || '').toLowerCase() === cachedLang && cachedMinimapLines !== null) {
        return cachedMinimapLines;
    }
    return null;
}

export function wasmLanguageFromPath(path) {
    wasmExports.wasm_reset_heap();
    const p = writeUtf8(path);
    wasmExports.wasm_detect_language(p.ptr, p.len);
    return readResultUtf8() || 'text';
}

export function wasmHighlightSyntax(source, language) {
    const text = String(source ?? '');
    const lang = String(language || 'text').toLowerCase();
    wasmExports.wasm_reset_heap();
    const s = writeUtf8(text);
    const l = writeUtf8(lang);
    wasmExports.wasm_highlight_document(s.ptr, s.len, l.ptr, l.len, 0);
    return readResultUtf8();
}

export function wasmHighlightDocument(source, language) {
    const text = String(source ?? '');
    const lang = String(language || 'text').toLowerCase();
    if (text === cachedSource && lang === cachedLang && cachedHighlightHtml !== null) {
        return cachedHighlightHtml;
    }
    wasmExports.wasm_reset_heap();
    const s = writeUtf8(text);
    const l = writeUtf8(lang);
    wasmExports.wasm_highlight_document(s.ptr, s.len, l.ptr, l.len, 1);
    return readResultUtf8();
}

export function wasmComputeLineDiff(baseText, currentText) {
    wasmExports.wasm_reset_heap();
    const hasBase = typeof baseText === 'string' ? 1 : 0;
    const b = hasBase ? writeUtf8(baseText) : { ptr: 0, len: 0 };
    const c = writeUtf8(currentText);
    wasmExports.wasm_compute_line_diff(b.ptr, b.len, hasBase, c.ptr, c.len);

    const lineCount = wasmExports.wasm_diff_line_count();
    const addedCount = wasmExports.wasm_diff_added_count();
    const modifiedCount = wasmExports.wasm_diff_modified_count();
    const deletedCount = wasmExports.wasm_diff_deleted_count();
    const kindsPtr = wasmExports.wasm_diff_line_kinds_ptr();
    const delAfterPtr = wasmExports.wasm_diff_deleted_after_ptr();

    const lineKinds = new Uint8Array(lineCount);
    const deletedAfter = new Uint8Array(lineCount);
    if (lineCount > 0) {
        lineKinds.set(new Uint8Array(wasmExports.memory.buffer, kindsPtr, lineCount));
        deletedAfter.set(new Uint8Array(wasmExports.memory.buffer, delAfterPtr, lineCount));
    }
    const meta = JSON.parse(readResultUtf8() || '{"deletions":[],"hunks":[]}');
    return {
        hasChanges: addedCount > 0 || modifiedCount > 0 || deletedCount > 0,
        addedCount,
        modifiedCount,
        deletedCount,
        lineKinds,
        deletedAfter,
        deletions: meta.deletions || [],
        hunks: meta.hunks || [],
    };
}

export function wasmScanMinimapLines(source, language) {
    const text = String(source ?? '');
    const lang = String(language || 'text').toLowerCase();
    if (text === cachedSource && lang === cachedLang && cachedMinimapLines !== null) {
        return cachedMinimapLines;
    }
    wasmExports.wasm_reset_heap();
    const s = writeUtf8(text);
    const l = writeUtf8(lang);
    wasmExports.wasm_scan_minimap_lines(s.ptr, s.len, l.ptr, l.len);
    return JSON.parse(readResultUtf8() || '[]');
}

export function wasmDetectCommentContext(source, offset, filePath = '') {
    const text = String(source ?? '');
    const clamped = Math.max(0, Math.min(text.length, Math.trunc(Number(offset) || 0)));
    wasmExports.wasm_reset_heap();
    const s = writeUtf8(text);
    const p = writeUtf8(filePath);
    wasmExports.wasm_detect_comment_context(s.ptr, s.len, clamped, p.ptr, p.len);
    return readResultUtf8() || 'html';
}

export function wasmComputeCommentToggle(source, selectionStart, selectionEnd, filePath = '') {
    const text = String(source ?? '');
    const clamp = (v) => Math.max(0, Math.min(text.length, Math.trunc(Number(v) || 0)));
    const sStart = clamp(selectionStart);
    const sEnd = clamp(selectionEnd);
    wasmExports.wasm_reset_heap();
    const s = writeUtf8(text);
    const p = writeUtf8(filePath);
    wasmExports.wasm_toggle_comment(s.ptr, s.len, sStart, sEnd, p.ptr, p.len);
    return JSON.parse(readResultUtf8());
}

export function wasmSanitizeMacroSlug(text, fallback = 'macro') {
    wasmExports.wasm_reset_heap();
    const s = writeUtf8(text);
    const f = writeUtf8(fallback);
    wasmExports.wasm_sanitize_macro_slug(s.ptr, s.len, f.ptr, f.len);
    return readResultUtf8();
}

export function wasmFormatPriceLabel(price, soldFlag = false) {
    wasmExports.wasm_reset_heap();
    const p = writeUtf8(price);
    wasmExports.wasm_format_price_label(p.ptr, p.len, soldFlag ? 1 : 0);
    return JSON.parse(readResultUtf8());
}

export function wasmNormalizePrice(price) {
    wasmExports.wasm_reset_heap();
    const p = writeUtf8(price);
    wasmExports.wasm_normalize_price(p.ptr, p.len);
    return readResultUtf8();
}

export function wasmInsertAtTarget(source, snippet, target, cursorOffset = -1) {
    const text = String(source ?? '');
    const cur = Number.isFinite(cursorOffset) && cursorOffset >= 0
        ? Math.min(text.length, Math.max(0, Math.floor(cursorOffset)))
        : -1;
    wasmExports.wasm_reset_heap();
    const s = writeUtf8(text);
    const sn = writeUtf8(snippet);
    const tg = writeUtf8(target);
    wasmExports.wasm_insert_at_target(s.ptr, s.len, sn.ptr, sn.len, tg.ptr, tg.len, cur);
    return JSON.parse(readResultUtf8());
}
