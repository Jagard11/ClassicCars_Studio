// Compiled from src/wasm/wasm_engine.c (clang --target=wasm32 -Os -fno-builtin -nostdlib)
const WASM_BASE64 =
    'AGFzbQEAAAABIgVgAABgAX8Bf2ACf38Bf2AGf39/f39/AX9gBX9/f39/AX8DBgUAAQIDBAQFAXAB' +
    'AQEFAwEAIAYIAX8BQZCIBAsHXgYGbWVtb3J5AgAPd2FzbV9yZXNldF9oZWFwAAAKd2FzbV9hbGxv' +
    'YwABDndhc21fbGluZV9oYXNoAAILd2FzbV9sY3NfZHAAAxN3YXNtX2dyZWVkeV9hbmNob3JzAAQK' +
    '4wcFHwBBAEGAIEGQiISAAEGQiISAAEGAIEkbNgKAiICAAAs8AQF/QQBBACgCgIiAgAAiAUGAIEGQ' +
    'iISAAEGQiISAAEGAIEkbIAEbIgEgAEEHakF4cWo2AoCIgIAAIAELOQEBf0HFu/KIeCECAkAgAUUN' +
    'AANAIAIgAC0AAHNBk4OACGwhAiAAQQFqIQAgAUF/aiIBDQALCyACC90DAQ5/IANBAWoiBiABbCIH' +
    'IANqIQhBfyEJIAQhCgNAIApBADsBACAKQQJqIQogCUEBaiIJIAhJDQALQQAhCwJAIAFFDQAgB0EB' +
    'dCADQQF0IgpqIARqQX5qIQwgAUF/aiAGbEEBdCAKaiAEakF+aiENIApBfnMhDiADQQJ0IAJqQXxq' +
    'IQ8gASEQA0AgEEF/aiEQAkAgA0UNACAAIBBBAnRqKAIAIREgDyEJIAwhCCANIQogAyEHA0ACQAJA' +
    'IBEgCSgCAEcNACAIQQJqLwEAQQFqIRIMAQsgCC8BACISIApBAmovAQAiEyASIBNLGyESCyAKIBI7' +
    'AQAgCUF8aiEJIAhBfmohCCAKQX5qIQogB0F/aiIHDQALCyAMIA5qIQwgDSAOaiENIBANAAsgA0UN' +
    'AEEAIQpBACEJQQAhCwNAAkACQCAAIApBAnRqKAIAIAIgCUECdGooAgBHDQAgBSALQQN0aiIIIAo2' +
    'AgAgCEEEaiAJNgIAIAlBAWohCSAKQQFqIQogC0EBaiELDAELIAogCkEBaiIIIAQgCCAGbEEBdGog' +
    'CUEBdCIIai8BACAEIAogBmxBAXRqIAhqQQJqLwEASSIIGyEKIAkgCGohCQsgCiABTw0BIAkgA0kN' +
    'AAsLIAsL6gIBDn9BACEFAkAgAUUNACADRQ0AIAJBBGohBiAAQQRqIQdBACEIQQAhCUEAIQUDQAJA' +
    'IAAgCEECdCIKaigCACILIAIgCUECdCIMaigCACINRw0AIAQgBUEDdGoiDiAINgIAIA5BBGogCTYC' +
    'ACAFQQFqIQUgCEEBaiIIIAFPDQIgCUEBaiIJIANJDQEMAgsCQAJAIAEgCGsiDiADIAlrIg8gDiAP' +
    'SxsiEA0AQQEhDAwBCyAGIAxqIQ8gByAKaiEKQQEhDkEAIQxBACAQQQwgEEEMSRsiEEEBIBBBAUsb' +
    'ayERA0ACQCAOIAlqIhIgA08NACALIA8oAgBHDQAgEiEJDAILAkACQCAIIA5qIhIgAU8NACAKKAIA' +
    'IA1GDQELIA9BBGohDyAKQQRqIQogDiAQTyEMIBEgDkEBaiIOakEBRw0BDAILCyASIQgLIAggDEEB' +
    'cSIOaiIIIAFPDQEgCSAOaiIJIANJDQALCyAFCw==';

let wasmExports = null;

function initWasmSync() {
    if (wasmExports) return wasmExports;
    if (typeof WebAssembly === 'undefined') return null;
    try {
        const binStr = atob(WASM_BASE64);
        const bytes = new Uint8Array(binStr.length);
        for (let i = 0; i < binStr.length; i += 1) {
            bytes[i] = binStr.charCodeAt(i);
        }
        const mod = new WebAssembly.Module(bytes);
        const inst = new WebAssembly.Instance(mod, {});
        wasmExports = inst.exports;
        return wasmExports;
    } catch {
        return null;
    }
}

function internLineIds(baseLines, currLines, offset, m, n) {
    const map = new Map();
    let nextId = 1;
    const baseIds = new Uint32Array(m);
    const currIds = new Uint32Array(n);
    for (let i = 0; i < m; i += 1) {
        const s = baseLines[offset + i];
        let id = map.get(s);
        if (!id) {
            id = nextId++;
            map.set(s, id);
        }
        baseIds[i] = id;
    }
    for (let j = 0; j < n; j += 1) {
        const s = currLines[offset + j];
        let id = map.get(s);
        if (!id) {
            id = nextId++;
            map.set(s, id);
        }
        currIds[j] = id;
    }
    return { baseIds, currIds };
}

export function wasmLcsPairs(baseLines, currLines, offset, m, n) {
    const exp = initWasmSync();
    if (!exp) return null;
    const dpBytes = (m + 1) * (n + 1) * 2;
    const pairsCap = Math.min(m, n);
    const totalBytes = (m + n + pairsCap * 2) * 4 + dpBytes + 64;
    if (totalBytes > exp.memory.buffer.byteLength - 8192) return null;

    exp.wasm_reset_heap();
    const basePtr = exp.wasm_alloc(m * 4);
    const currPtr = exp.wasm_alloc(n * 4);
    const dpPtr = exp.wasm_alloc(dpBytes);
    const outPtr = exp.wasm_alloc(pairsCap * 8);

    const { baseIds, currIds } = internLineIds(baseLines, currLines, offset, m, n);
    const u32 = new Uint32Array(exp.memory.buffer);
    u32.set(baseIds, basePtr >> 2);
    u32.set(currIds, currPtr >> 2);

    const count = exp.wasm_lcs_dp(basePtr, m, currPtr, n, dpPtr, outPtr);
    const pairs = new Array(count);
    const outBase = outPtr >> 2;
    for (let k = 0; k < count; k += 1) {
        pairs[k] = [offset + u32[outBase + k * 2], offset + u32[outBase + k * 2 + 1]];
    }
    return pairs;
}

export function wasmGreedyAnchorPairs(baseLines, currLines, offset, m, n) {
    const exp = initWasmSync();
    if (!exp) return null;
    const pairsCap = Math.min(m, n);
    const totalBytes = (m + n + pairsCap * 2) * 4 + 64;
    if (totalBytes > exp.memory.buffer.byteLength - 8192) return null;

    exp.wasm_reset_heap();
    const basePtr = exp.wasm_alloc(m * 4);
    const currPtr = exp.wasm_alloc(n * 4);
    const outPtr = exp.wasm_alloc(pairsCap * 8);

    const { baseIds, currIds } = internLineIds(baseLines, currLines, offset, m, n);
    const u32 = new Uint32Array(exp.memory.buffer);
    u32.set(baseIds, basePtr >> 2);
    u32.set(currIds, currPtr >> 2);

    const count = exp.wasm_greedy_anchors(basePtr, m, currPtr, n, outPtr);
    const pairs = new Array(count);
    const outBase = outPtr >> 2;
    for (let k = 0; k < count; k += 1) {
        pairs[k] = [offset + u32[outBase + k * 2], offset + u32[outBase + k * 2 + 1]];
    }
    return pairs;
}

export async function callNativeCodeEngine(endpoint, payload) {
    const res = await fetch(`/__studio/api/engine/${endpoint}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload || {}),
    });
    return res.json();
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
