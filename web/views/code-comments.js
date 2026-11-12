import {
    wasmComputeCommentToggle,
    wasmDetectCommentContext,
} from '../wasm/studio_engine.js';

export function detectCommentContext(source, offset, filePath = '') {
    return wasmDetectCommentContext(source, offset, filePath);
}

export function computeCommentToggle(source, selectionStart, selectionEnd, filePath = '') {
    return wasmComputeCommentToggle(source, selectionStart, selectionEnd, filePath);
}
