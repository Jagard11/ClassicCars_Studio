import { wasmComputeLineDiff } from '../wasm/studio_engine.js';

export const DIFF_NONE = 0;
export const DIFF_ADDED = 1;
export const DIFF_MODIFIED = 2;

export function computeLineDiff(baseText, currentText) {
    return wasmComputeLineDiff(baseText, currentText);
}
