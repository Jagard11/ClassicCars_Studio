export const DIFF_NONE = 0;
export const DIFF_ADDED = 1;
export const DIFF_MODIFIED = 2;

const MAX_DP_CELLS = 360000;

export function computeLineDiff(baseText, currentText) {
    const current = normalizeNewlines(currentText);
    const currLines = current.split('\n');
    const lineCount = currLines.length;
    const lineKinds = new Uint8Array(lineCount);
    const deletedAfter = new Uint8Array(lineCount);

    if (typeof baseText !== 'string') {
        return {
            hasChanges: false,
            addedCount: 0,
            modifiedCount: 0,
            deletedCount: 0,
            lineKinds,
            deletedAfter,
            deletions: [],
            hunks: [],
        };
    }

    const base = normalizeNewlines(baseText);
    if (base === current) {
        return {
            hasChanges: false,
            addedCount: 0,
            modifiedCount: 0,
            deletedCount: 0,
            lineKinds,
            deletedAfter,
            deletions: [],
            hunks: [],
        };
    }

    const baseLines = base.split('\n');
    const baseLen = baseLines.length;
    let prefix = 0;
    const minLen = Math.min(baseLen, lineCount);
    while (prefix < minLen && baseLines[prefix] === currLines[prefix]) {
        prefix += 1;
    }

    let baseEnd = baseLen - 1;
    let currEnd = lineCount - 1;
    while (baseEnd >= prefix && currEnd >= prefix && baseLines[baseEnd] === currLines[currEnd]) {
        baseEnd -= 1;
        currEnd -= 1;
    }

    const midBaseLen = Math.max(0, baseEnd - prefix + 1);
    const midCurrLen = Math.max(0, currEnd - prefix + 1);
    const pairs = matchMiddleLines(baseLines, currLines, prefix, midBaseLen, midCurrLen);

    let addedCount = 0;
    let modifiedCount = 0;
    let deletedCount = 0;
    const deletions = [];
    const hunks = [];

    let prevBase = prefix - 1;
    let prevCurr = prefix - 1;
    const anchors = [...pairs, [baseEnd + 1, currEnd + 1]];

    for (let i = 0; i < anchors.length; i += 1) {
        const [nextBase, nextCurr] = anchors[i];
        const delSpan = nextBase - prevBase - 1;
        const insSpan = nextCurr - prevCurr - 1;

        if (delSpan > 0 && insSpan === 0) {
            const targetIdx = Math.max(0, Math.min(lineCount - 1, prevCurr >= 0 ? prevCurr : 0));
            deletedAfter[targetIdx] = 1;
            deletedCount += delSpan;
            deletions.push({ line: targetIdx + 1, count: delSpan });
            hunks.push({ startLine: targetIdx + 1, endLine: targetIdx + 1, kind: 'del', count: delSpan });
        } else if (delSpan === 0 && insSpan > 0) {
            const startIdx = prevCurr + 1;
            const endIdx = nextCurr - 1;
            for (let c = startIdx; c <= endIdx; c += 1) {
                lineKinds[c] = DIFF_ADDED;
                addedCount += 1;
            }
            hunks.push({ startLine: startIdx + 1, endLine: endIdx + 1, kind: 'add', count: insSpan });
        } else if (delSpan > 0 && insSpan > 0) {
            const startIdx = prevCurr + 1;
            const endIdx = nextCurr - 1;
            const modSpan = Math.min(delSpan, insSpan);
            for (let k = 0; k < insSpan; k += 1) {
                const c = startIdx + k;
                if (k < modSpan) {
                    lineKinds[c] = DIFF_MODIFIED;
                    modifiedCount += 1;
                } else {
                    lineKinds[c] = DIFF_ADDED;
                    addedCount += 1;
                }
            }
            hunks.push({ startLine: startIdx + 1, endLine: endIdx + 1, kind: 'mod', count: insSpan });
            if (delSpan > insSpan) {
                const extraDel = delSpan - insSpan;
                const targetIdx = Math.max(0, Math.min(lineCount - 1, endIdx));
                deletedAfter[targetIdx] = 1;
                deletedCount += extraDel;
                deletions.push({ line: targetIdx + 1, count: extraDel });
            }
        }

        prevBase = nextBase;
        prevCurr = nextCurr;
    }

    return {
        hasChanges: addedCount > 0 || modifiedCount > 0 || deletedCount > 0,
        addedCount,
        modifiedCount,
        deletedCount,
        lineKinds,
        deletedAfter,
        deletions,
        hunks,
    };
}

function normalizeNewlines(value) {
    return String(value ?? '').replace(/\r\n?/g, '\n');
}

function matchMiddleLines(baseLines, currLines, offset, m, n) {
    if (m === 0 || n === 0) return [];
    if (m * n <= MAX_DP_CELLS) {
        return lcsPairs(baseLines, currLines, offset, m, n);
    }
    return greedyAnchorPairs(baseLines, currLines, offset, m, n);
}

function lcsPairs(baseLines, currLines, offset, m, n) {
    const stride = n + 1;
    const dp = new Uint16Array((m + 1) * stride);
    for (let i = m - 1; i >= 0; i -= 1) {
        const bLine = baseLines[offset + i];
        const row = i * stride;
        const nextRow = (i + 1) * stride;
        for (let j = n - 1; j >= 0; j -= 1) {
            if (bLine === currLines[offset + j]) {
                dp[row + j] = dp[nextRow + j + 1] + 1;
            } else {
                const down = dp[nextRow + j];
                const right = dp[row + j + 1];
                dp[row + j] = down >= right ? down : right;
            }
        }
    }
    const pairs = [];
    let i = 0;
    let j = 0;
    while (i < m && j < n) {
        if (baseLines[offset + i] === currLines[offset + j]) {
            pairs.push([offset + i, offset + j]);
            i += 1;
            j += 1;
        } else if (dp[(i + 1) * stride + j] >= dp[i * stride + j + 1]) {
            i += 1;
        } else {
            j += 1;
        }
    }
    return pairs;
}

function greedyAnchorPairs(baseLines, currLines, offset, m, n) {
    const pairs = [];
    let i = 0;
    let j = 0;
    const windowSize = 12;
    while (i < m && j < n) {
        if (baseLines[offset + i] === currLines[offset + j]) {
            pairs.push([offset + i, offset + j]);
            i += 1;
            j += 1;
            continue;
        }
        let matched = false;
        const limit = Math.min(windowSize, Math.max(m - i, n - j));
        for (let step = 1; step <= limit; step += 1) {
            if (j + step < n && baseLines[offset + i] === currLines[offset + j + step]) {
                j += step;
                matched = true;
                break;
            }
            if (i + step < m && baseLines[offset + i + step] === currLines[offset + j]) {
                i += step;
                matched = true;
                break;
            }
        }
        if (!matched) {
            i += 1;
            j += 1;
        }
    }
    return pairs;
}
