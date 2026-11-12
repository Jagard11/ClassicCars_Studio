import {
    wasmHighlightDocument,
    wasmHighlightSyntax,
    wasmLanguageFromPath,
} from '../wasm/studio_engine.js';

export function languageFromPath(path) {
    return wasmLanguageFromPath(path);
}

export function highlightSyntax(source, language) {
    return wasmHighlightSyntax(source, language);
}

export function highlightDocument(source, language) {
    return wasmHighlightDocument(source, language);
}
