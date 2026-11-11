import { languageFromPath } from './syntax-highlight.js';

export function detectCommentContext(source, offset, filePath = '') {
    const text = String(source ?? '');
    const target = clamp(offset, text.length);
    const baseLang = languageFromPath(filePath);
    if (baseLang === 'css') return 'css';
    if (baseLang === 'js') return 'js';
    if (baseLang === 'json') return 'json';

    const startsInPhp = baseLang === 'php' && !/^\s*</.test(text);
    return scanContextAt(text, target, startsInPhp);
}

export function computeCommentToggle(source, selectionStart, selectionEnd, filePath = '') {
    const text = String(source ?? '');
    const start = clamp(Math.min(selectionStart ?? 0, selectionEnd ?? 0), text.length);
    const end = clamp(Math.max(selectionStart ?? 0, selectionEnd ?? 0), text.length);

    if (start === end) {
        return toggleAtCursor(text, start, filePath);
    }
    return toggleSelection(text, start, end, filePath);
}

function toggleAtCursor(text, caret, filePath) {
    const lineStart = text.lastIndexOf('\n', Math.max(0, caret - 1)) + 1;
    let lineEnd = text.indexOf('\n', caret);
    if (lineEnd < 0) lineEnd = text.length;
    const hasCr = lineEnd > lineStart && text[lineEnd - 1] === '\r';
    const contentEnd = hasCr ? lineEnd - 1 : lineEnd;
    const line = text.slice(lineStart, contentEnd);
    const indent = (line.match(/^[ \t]*/) || [''])[0];
    const trailingWs = (line.match(/[ \t]*$/) || [''])[0];
    const firstNonWs = lineStart + indent.length;
    const lastNonWs = contentEnd - trailingWs.length;
    const trimmed = text.slice(firstNonWs, lastNonWs);

    const context = resolveLineContext(text, lineStart, contentEnd, firstNonWs, caret, trimmed, filePath);

    if (!trimmed) {
        const empty = emptyCommentFor(context);
        return {
            from: lineStart,
            to: contentEnd,
            insert: `${indent}${empty.insert}`,
            selectionStart: lineStart + indent.length + empty.cursorOffset,
            selectionEnd: lineStart + indent.length + empty.cursorOffset,
            context,
        };
    }

    const lineUncomment = tryUncommentSingleLine(trimmed, context);
    if (lineUncomment !== null) {
        if (!lineUncomment.text.trim()) {
            const pos = lineStart + indent.length;
            return {
                from: lineStart,
                to: contentEnd,
                insert: indent,
                selectionStart: pos,
                selectionEnd: pos,
                context,
            };
        }
        const nextCaret = clamp(
            caret - lineUncomment.prefixRemoved,
            firstNonWs,
            firstNonWs + lineUncomment.text.length,
        );
        return {
            from: firstNonWs,
            to: lastNonWs,
            insert: lineUncomment.text,
            selectionStart: nextCaret,
            selectionEnd: nextCaret,
            context,
        };
    }

    const enclosingUncomment = tryUncommentEnclosingBlockAtCaret(text, lineStart, contentEnd, firstNonWs, lastNonWs, caret, context);
    if (enclosingUncomment) return enclosingUncomment;

    const inlineUncomment = tryUncommentInlineAtCaret(text, firstNonWs, lastNonWs, caret, context);
    if (inlineUncomment) return inlineUncomment;

    if (isLineCommentContext(context)) {
        const singleLinePhp = matchSingleLinePhpTag(line);
        if (singleLinePhp) {
            const innerUncomment = tryUncommentSingleLine(singleLinePhp.body, 'php');
            const nextBody = innerUncomment ? innerUncomment.text : `/* ${singleLinePhp.body} */`;
            const delta = innerUncomment ? -innerUncomment.prefixRemoved : 3;
            const bodyStart = lineStart + singleLinePhp.open.length;
            const nextCaret = clamp(caret + delta, bodyStart, bodyStart + nextBody.length);
            return {
                from: bodyStart,
                to: bodyStart + singleLinePhp.body.length,
                insert: nextBody,
                selectionStart: nextCaret,
                selectionEnd: nextCaret,
                context: 'php',
            };
        }
        const prefix = '// ';
        const nextCaret = caret < firstNonWs ? firstNonWs + prefix.length : caret + prefix.length;
        return {
            from: firstNonWs,
            to: firstNonWs,
            insert: prefix,
            selectionStart: nextCaret,
            selectionEnd: nextCaret,
            context,
        };
    }

    const { open, close } = blockDelimitersFor(context);
    const wrapped = `${open} ${trimmed} ${close}`;
    const openShift = open.length + 1;
    const baseCaret = Math.max(firstNonWs, Math.min(lastNonWs, caret));
    const nextCaret = baseCaret + openShift;
    return {
        from: firstNonWs,
        to: lastNonWs,
        insert: wrapped,
        selectionStart: nextCaret,
        selectionEnd: nextCaret,
        context,
    };
}

function toggleSelection(text, start, end, filePath) {
    let effectiveEnd = end;
    const hadTrailingNewline = end > start && text[end - 1] === '\n';
    if (hadTrailingNewline) {
        effectiveEnd = end - 1;
        if (effectiveEnd > start && text[effectiveEnd - 1] === '\r') effectiveEnd -= 1;
    }
    if (effectiveEnd <= start) {
        return toggleAtCursor(text, start, filePath);
    }

    const firstLineStart = text.lastIndexOf('\n', Math.max(0, start - 1)) + 1;
    let firstLineEnd = text.indexOf('\n', start);
    if (firstLineEnd < 0) firstLineEnd = text.length;
    const firstLineContentEnd = firstLineEnd > firstLineStart && text[firstLineEnd - 1] === '\r'
        ? firstLineEnd - 1
        : firstLineEnd;

    const singleLine = effectiveEnd <= firstLineContentEnd && !hadTrailingNewline;
    if (singleLine) {
        const line = text.slice(firstLineStart, firstLineContentEnd);
        const indentLen = (line.match(/^[ \t]*/) || [''])[0].length;
        const trailLen = (line.match(/[ \t]*$/) || [''])[0].length;
        const firstNonWs = firstLineStart + indentLen;
        const lastNonWs = firstLineContentEnd - trailLen;

        const surrounding = tryUnwrapSurroundingDelimiters(text, start, effectiveEnd, firstNonWs, lastNonWs);
        if (surrounding) return surrounding;

        const isPartial = start > firstNonWs || effectiveEnd < lastNonWs;
        if (isPartial) {
            return togglePartialLineSelection(text, start, effectiveEnd, filePath);
        }
    }

    return toggleBlockOrLineSelection(text, start, end, effectiveEnd, hadTrailingNewline, filePath);
}

function togglePartialLineSelection(text, start, end, filePath) {
    const selected = text.slice(start, end);
    const leadWs = (selected.match(/^[ \t]*/) || [''])[0];
    const trailWs = (selected.match(/[ \t]*$/) || [''])[0];
    const innerFrom = start + leadWs.length;
    const innerTo = end - trailWs.length;
    if (innerFrom >= innerTo) {
        return toggleAtCursor(text, start, filePath);
    }
    const inner = text.slice(innerFrom, innerTo);
    const context = detectCommentContext(text, innerFrom, filePath);

    const unwrapped = unwrapBlockCommentText(inner, context)
        || (isLineCommentContext(context) && /^\/\/ ?/.test(inner) ? inner.replace(/^\/\/ ?/, '') : null);
    if (unwrapped !== null) {
        return {
            from: innerFrom,
            to: innerTo,
            insert: unwrapped,
            selectionStart: innerFrom,
            selectionEnd: innerFrom + unwrapped.length,
            context,
        };
    }

    const { open, close } = blockDelimitersFor(context);
    const wrapped = `${open} ${inner} ${close}`;
    return {
        from: innerFrom,
        to: innerTo,
        insert: wrapped,
        selectionStart: innerFrom,
        selectionEnd: innerFrom + wrapped.length,
        context,
    };
}

function toggleBlockOrLineSelection(text, start, origEnd, effectiveEnd, hadTrailingNewline, filePath) {
    const blockStart = text.lastIndexOf('\n', Math.max(0, start - 1)) + 1;
    let blockEnd = text.indexOf('\n', effectiveEnd);
    if (blockEnd < 0) blockEnd = text.length;
    if (blockEnd > blockStart && text[blockEnd - 1] === '\r') blockEnd -= 1;

    const selectedSlice = text.slice(start, effectiveEnd);
    const firstNonWsRel = selectedSlice.search(/\S/);
    const probeOffset = firstNonWsRel >= 0 ? start + firstNonWsRel : start;
    const trimmedSelection = selectedSlice.trim();

    let context = detectCommentContext(text, probeOffset, filePath);
    if (/^<\/?(?:style|script)\b/i.test(trimmedSelection)) {
        context = 'html';
    }

    const unwrappedDirect = unwrapSelectionIfBlockCommented(text, start, effectiveEnd, origEnd, hadTrailingNewline, context);
    if (unwrappedDirect) return unwrappedDirect;

    const unwrappedBlock = unwrapSelectionIfBlockCommented(text, blockStart, blockEnd, origEnd, hadTrailingNewline, context);
    if (unwrappedBlock) return unwrappedBlock;

    if (isLineCommentContext(context)) {
        return toggleMultiLineSlashComments(text, blockStart, blockEnd, start, origEnd, hadTrailingNewline, context);
    }

    return toggleMultiLineBlockComments(text, blockStart, blockEnd, start, origEnd, effectiveEnd, hadTrailingNewline, context);
}

function toggleMultiLineSlashComments(text, blockStart, blockEnd, origStart, origEnd, hadTrailingNewline, context) {
    const block = text.slice(blockStart, blockEnd);
    const rawLines = block.split('\n');
    const nonBlank = rawLines.filter((line) => line.replace(/\r$/, '').trim().length > 0);

    if (nonBlank.length === 0) {
        const commented = rawLines.map((line) => {
            const hasCr = line.endsWith('\r');
            const body = hasCr ? line.slice(0, -1) : line;
            return `${body}// ${hasCr ? '\r' : ''}`;
        }).join('\n');
        return buildBlockSelectionResult(blockStart, blockEnd, commented, origStart, origEnd, hadTrailingNewline, context);
    }

    const commentPattern = context === 'php' ? /^[ \t]*(?:\/\/|#(?![\[]))/ : /^[ \t]*\/\//;
    const allCommented = nonBlank.every((line) => commentPattern.test(line));

    if (allCommented) {
        const stripPattern = context === 'php'
            ? /^([ \t]*)(?:\/\/ ?|# ?)/
            : /^([ \t]*)\/\/ ?/;
        const uncommented = rawLines.map((line) => line.replace(stripPattern, '$1')).join('\n');
        return buildBlockSelectionResult(blockStart, blockEnd, uncommented, origStart, origEnd, hadTrailingNewline, context);
    }

    let minIndent = Infinity;
    for (const line of nonBlank) {
        const indentLen = (line.match(/^[ \t]*/) || [''])[0].length;
        if (indentLen < minIndent) minIndent = indentLen;
    }
    if (!Number.isFinite(minIndent)) minIndent = 0;

    const commented = rawLines.map((line) => {
        const clean = line.replace(/\r$/, '');
        if (!clean.trim()) return line;
        return `${line.slice(0, minIndent)}// ${line.slice(minIndent)}`;
    }).join('\n');

    return buildBlockSelectionResult(blockStart, blockEnd, commented, origStart, origEnd, hadTrailingNewline, context);
}

function toggleMultiLineBlockComments(text, blockStart, blockEnd, origStart, origEnd, effectiveEnd, hadTrailingNewline, context) {
    const { open, close } = blockDelimitersFor(context);
    const block = text.slice(blockStart, blockEnd);
    const rawLines = block.split('\n');
    const nonBlank = rawLines.filter((line) => line.replace(/\r$/, '').trim().length > 0);

    if (nonBlank.length > 0 && nonBlank.every((line) => unwrapBlockCommentText(line.replace(/\r$/, '').trim(), context) !== null)) {
        const uncommented = rawLines.map((line) => {
            const hasCr = line.endsWith('\r');
            const body = hasCr ? line.slice(0, -1) : line;
            const indent = (body.match(/^[ \t]*/) || [''])[0];
            const trail = (body.match(/[ \t]*$/) || [''])[0];
            const mid = body.slice(indent.length, body.length - trail.length);
            if (!mid) return line;
            const inner = unwrapBlockCommentText(mid, context);
            return inner === null ? line : `${indent}${inner}${trail}${hasCr ? '\r' : ''}`;
        }).join('\n');
        return buildBlockSelectionResult(blockStart, blockEnd, uncommented, origStart, origEnd, hadTrailingNewline, context);
    }

    const leadWs = (block.match(/^[ \t\r\n]*/) || [''])[0];
    const trailWs = (block.match(/[ \t\r\n]*$/) || [''])[0];
    const innerFrom = blockStart + leadWs.length;
    const innerTo = blockEnd - trailWs.length;
    if (innerFrom >= innerTo) {
        return toggleAtCursor(text, origStart, '');
    }

    const inner = text.slice(innerFrom, innerTo);
    if (inner.includes(close)) {
        const commentedLines = rawLines.map((line) => {
            const hasCr = line.endsWith('\r');
            const body = hasCr ? line.slice(0, -1) : line;
            const indent = (body.match(/^[ \t]*/) || [''])[0];
            const trail = (body.match(/[ \t]*$/) || [''])[0];
            const mid = body.slice(indent.length, body.length - trail.length);
            if (!mid) return line;
            if (unwrapBlockCommentText(mid, context) !== null) return line;
            return `${indent}${open} ${mid} ${close}${trail}${hasCr ? '\r' : ''}`;
        }).join('\n');
        return buildBlockSelectionResult(blockStart, blockEnd, commentedLines, origStart, origEnd, hadTrailingNewline, context);
    }

    const wrapped = `${open} ${inner} ${close}`;
    const fullLineSelected = origStart <= blockStart && (hadTrailingNewline || effectiveEnd >= innerTo);
    const selStart = fullLineSelected ? origStart : innerFrom;
    const selEnd = innerFrom + wrapped.length + (hadTrailingNewline ? (origEnd - innerTo) : 0);
    return {
        from: innerFrom,
        to: innerTo,
        insert: wrapped,
        selectionStart: selStart,
        selectionEnd: selEnd,
        context,
    };
}

function buildBlockSelectionResult(blockStart, blockEnd, replacement, origStart, origEnd, hadTrailingNewline, context) {
    const singleLine = !replacement.includes('\n');
    if (singleLine && origStart > blockStart && !hadTrailingNewline) {
        const indentLen = (replacement.match(/^[ \t]*/) || [''])[0].length;
        const trailLen = (replacement.match(/[ \t]*$/) || [''])[0].length;
        return {
            from: blockStart,
            to: blockEnd,
            insert: replacement,
            selectionStart: blockStart + indentLen,
            selectionEnd: blockStart + replacement.length - trailLen,
            context,
        };
    }
    const selStart = origStart <= blockStart ? blockStart : Math.min(origStart, blockStart + replacement.length);
    const trailingExtra = hadTrailingNewline ? Math.max(0, origEnd - blockEnd) : 0;
    return {
        from: blockStart,
        to: blockEnd,
        insert: replacement,
        selectionStart: selStart,
        selectionEnd: blockStart + replacement.length + trailingExtra,
        context,
    };
}

function unwrapSelectionIfBlockCommented(text, rangeStart, rangeEnd, origEnd, hadTrailingNewline, context) {
    const slice = text.slice(rangeStart, rangeEnd);
    const leadWs = (slice.match(/^[ \t\r\n]*/) || [''])[0];
    const trailWs = (slice.match(/[ \t\r\n]*$/) || [''])[0];
    const innerFrom = rangeStart + leadWs.length;
    const innerTo = rangeEnd - trailWs.length;
    if (innerFrom >= innerTo) return null;

    const inner = text.slice(innerFrom, innerTo);
    const unwrapped = unwrapBlockCommentText(inner, context);
    if (unwrapped === null) return null;

    const trailingExtra = hadTrailingNewline ? Math.max(0, origEnd - rangeEnd) : 0;
    return {
        from: innerFrom,
        to: innerTo,
        insert: unwrapped,
        selectionStart: innerFrom,
        selectionEnd: innerFrom + unwrapped.length + trailingExtra,
        context,
    };
}

function tryUnwrapSurroundingDelimiters(text, start, end, firstNonWs, lastNonWs) {
    const before = text.slice(firstNonWs, start);
    const after = text.slice(end, lastNonWs);
    const selected = text.slice(start, end);

    if (/^<!-- ?$/.test(before) && /^ ?-->$/.test(after) && !selected.includes('-->')) {
        return {
            from: firstNonWs,
            to: lastNonWs,
            insert: selected,
            selectionStart: firstNonWs,
            selectionEnd: firstNonWs + selected.length,
            context: 'html',
        };
    }
    if (/^\/\* ?$/.test(before) && /^ ?\*\/$/.test(after) && !selected.includes('*/')) {
        return {
            from: firstNonWs,
            to: lastNonWs,
            insert: selected,
            selectionStart: firstNonWs,
            selectionEnd: firstNonWs + selected.length,
            context: 'css',
        };
    }
    if (/^\/\/ ?$/.test(before) && after === '') {
        return {
            from: firstNonWs,
            to: lastNonWs,
            insert: selected,
            selectionStart: firstNonWs,
            selectionEnd: firstNonWs + selected.length,
            context: 'js',
        };
    }
    return null;
}

function tryUncommentSingleLine(trimmed, context) {
    if (trimmed.startsWith('<!--') && trimmed.endsWith('-->') && trimmed.indexOf('-->', 4) === trimmed.length - 3) {
        const prefixRemoved = trimmed.startsWith('<!-- ') ? 5 : 4;
        const suffixRemoved = trimmed.endsWith(' -->') && trimmed.length >= prefixRemoved + 4 ? 4 : 3;
        return {
            text: trimmed.slice(prefixRemoved, trimmed.length - suffixRemoved),
            prefixRemoved,
        };
    }
    if (trimmed.startsWith('/*') && trimmed.endsWith('*/') && trimmed.indexOf('*/', 2) === trimmed.length - 2) {
        const prefixRemoved = trimmed.startsWith('/* ') ? 3 : 2;
        const suffixRemoved = trimmed.endsWith(' */') && trimmed.length >= prefixRemoved + 3 ? 3 : 2;
        return {
            text: trimmed.slice(prefixRemoved, trimmed.length - suffixRemoved),
            prefixRemoved,
        };
    }
    if (isLineCommentContext(context)) {
        if (trimmed.startsWith('//')) {
            const prefixRemoved = trimmed.startsWith('// ') ? 3 : 2;
            return {
                text: trimmed.slice(prefixRemoved),
                prefixRemoved,
            };
        }
        if (context === 'php' && trimmed.startsWith('#') && !trimmed.startsWith('#[')) {
            const prefixRemoved = trimmed.startsWith('# ') ? 2 : 1;
            return {
                text: trimmed.slice(prefixRemoved),
                prefixRemoved,
            };
        }
    }
    return null;
}

function tryUncommentEnclosingBlockAtCaret(text, lineStart, contentEnd, firstNonWs, lastNonWs, caret, context) {
    const trimmed = text.slice(firstNonWs, lastNonWs);
    if (tryUncommentSingleLine(trimmed, context) !== null) return null;

    const pairs = context === 'html'
        ? [['<!--', '-->']]
        : [['/*', '*/'], ['<!--', '-->']];

    for (const [open, close] of pairs) {
        const openAt = text.lastIndexOf(open, caret);
        if (openAt < 0) continue;
        const prevClose = text.indexOf(close, openAt + open.length);
        if (prevClose < 0 || prevClose + close.length <= lineStart) continue;
        const closeEnd = prevClose + close.length;
        if (caret < openAt || caret > closeEnd) continue;
        if (openAt >= lineStart && closeEnd <= contentEnd) continue;
        const head = text.slice(openAt, Math.min(closeEnd, openAt + 64));
        if (open === '<!--' && /^<!--\s*(?:#|\/?\s*(?:page-values|related-sales|generated-seo)\b)/i.test(head)) {
            continue;
        }
        const raw = text.slice(openAt, closeEnd);
        const unwrapped = stripOuterDelimiters(raw, open, close);
        const prefixRemoved = raw.length - unwrapped.length > close.length
            ? (raw.startsWith(`${open} `) ? open.length + 1 : open.length)
            : open.length;
        const nextCaret = clamp(caret - prefixRemoved, openAt, openAt + unwrapped.length);
        return {
            from: openAt,
            to: closeEnd,
            insert: unwrapped,
            selectionStart: nextCaret,
            selectionEnd: nextCaret,
            context,
        };
    }
    return null;
}

function tryUncommentInlineAtCaret(text, firstNonWs, lastNonWs, caret, context) {
    const pairs = context === 'html'
        ? [['<!--', '-->']]
        : [['/*', '*/'], ['<!--', '-->']];

    for (const [open, close] of pairs) {
        let searchFrom = firstNonWs;
        while (searchFrom < lastNonWs) {
            const cStart = text.indexOf(open, searchFrom);
            if (cStart < 0 || cStart >= lastNonWs) break;
            const cEndAt = text.indexOf(close, cStart + open.length);
            if (cEndAt < 0 || cEndAt + close.length > lastNonWs) break;
            const cEnd = cEndAt + close.length;
            if (caret >= cStart && caret <= cEnd) {
                const raw = text.slice(cStart, cEnd);
                const prefixLen = raw.startsWith(`${open} `) ? open.length + 1 : open.length;
                const suffixLen = raw.endsWith(` ${close}`) && raw.length >= prefixLen + close.length + 1
                    ? close.length + 1
                    : close.length;
                const inner = raw.slice(prefixLen, raw.length - suffixLen);
                const nextCaret = clamp(caret - prefixLen, cStart, cStart + inner.length);
                return {
                    from: cStart,
                    to: cEnd,
                    insert: inner,
                    selectionStart: nextCaret,
                    selectionEnd: nextCaret,
                    context,
                };
            }
            searchFrom = cEnd;
        }
    }
    return null;
}

function unwrapBlockCommentText(inner, context) {
    if (inner.startsWith('<!--') && inner.endsWith('-->') && inner.indexOf('-->', 4) === inner.length - 3) {
        return stripOuterDelimiters(inner, '<!--', '-->');
    }
    if (inner.startsWith('/*') && inner.endsWith('*/') && inner.indexOf('*/', 2) === inner.length - 2) {
        return stripOuterDelimiters(inner, '/*', '*/');
    }
    if (context === 'html') return null;
    return null;
}

function stripOuterDelimiters(text, open, close) {
    let body = text.slice(open.length, text.length - close.length);
    if (/^[ \t]*\r?\n/.test(body) && /\r?\n[ \t]*$/.test(body)) {
        body = body.replace(/^[ \t]*\r?\n/, '').replace(/\r?\n[ \t]*$/, '');
        return body;
    }
    if (body.startsWith(' ')) body = body.slice(1);
    if (body.endsWith(' ')) body = body.slice(0, -1);
    return body;
}

function resolveLineContext(text, lineStart, lineEnd, firstNonWs, caret, trimmed, filePath) {
    if (!trimmed) {
        return detectCommentContext(text, caret, filePath);
    }
    if (/^<\/?(?:style|script)\b/i.test(trimmed)) {
        return 'html';
    }
    if (trimmed.startsWith('<!--') && trimmed.endsWith('-->')) {
        return 'html';
    }
    if (matchSingleLinePhpTag(trimmed)) {
        return 'php';
    }
    return detectCommentContext(text, firstNonWs, filePath);
}

function matchSingleLinePhpTag(line) {
    const match = /^([ \t]*<\?(?:php|=)[ \t]+)(.+?)([ \t]*\?>[ \t]*)$/i.exec(line);
    if (!match) return null;
    return {
        open: match[1],
        body: match[2],
        close: match[3],
    };
}

function emptyCommentFor(context) {
    if (context === 'html') return { insert: '<!--  -->', cursorOffset: 5 };
    if (context === 'css') return { insert: '/*  */', cursorOffset: 3 };
    return { insert: '// ', cursorOffset: 3 };
}

function blockDelimitersFor(context) {
    if (context === 'html') return { open: '<!--', close: '-->' };
    return { open: '/*', close: '*/' };
}

function isLineCommentContext(context) {
    return context === 'php' || context === 'js' || context === 'json';
}

function scanContextAt(text, target, startsInPhp) {
    let inPhp = Boolean(startsInPhp);
    let enclosing = '';
    let i = 0;

    while (i < target) {
        if (inPhp) {
            const c = text[i];
            if (c === "'" || c === '"') {
                i = skipQuotedString(text, i, c, true);
                if (i > target) return 'php';
                continue;
            }
            if ((c === '/' && text[i + 1] === '/') || (c === '#' && text[i + 1] !== '[')) {
                const nl = text.indexOf('\n', i + 1);
                const lineLimit = nl < 0 ? text.length : nl;
                const closeInLine = text.indexOf('?>', i + 1);
                if (closeInLine >= 0 && closeInLine < lineLimit) {
                    if (target <= closeInLine + 1) return 'php';
                    inPhp = false;
                    i = closeInLine + 2;
                    continue;
                }
                if (nl < 0 || nl >= target) return 'php';
                i = nl + 1;
                continue;
            }
            if (c === '/' && text[i + 1] === '*') {
                const close = text.indexOf('*/', i + 2);
                if (close < 0 || close + 2 > target) return 'php';
                i = close + 2;
                continue;
            }
            if (c === '<' && text[i + 1] === '<' && text[i + 2] === '<') {
                const heredoc = matchPhpHeredoc(text, i);
                if (heredoc) {
                    if (target < heredoc.bodyStart) return 'php';
                    if (target < heredoc.bodyEnd) {
                        return scanContextAt(text.slice(heredoc.bodyStart, heredoc.bodyEnd), target - heredoc.bodyStart, false);
                    }
                    i = heredoc.afterClose;
                    continue;
                }
            }
            if (c === '?' && text[i + 1] === '>') {
                if (target <= i + 1) return 'php';
                inPhp = false;
                i += 2;
                continue;
            }
            i += 1;
            continue;
        }

        const phpLen = phpOpenLength(text, i);
        if (phpLen > 0) {
            if (target < i + phpLen) return 'php';
            inPhp = true;
            i += phpLen;
            continue;
        }

        if (enclosing === 'css') {
            if (text[i] === '/' && text[i + 1] === '*') {
                const close = text.indexOf('*/', i + 2);
                if (close < 0 || close + 2 > target) return 'css';
                i = close + 2;
                continue;
            }
            if (text[i] === '"' || text[i] === "'") {
                i = skipQuotedString(text, i, text[i], false);
                if (i > target) return 'css';
                continue;
            }
            if (text[i] === '<' && text[i + 1] === '/' && matchesEndTag(text, i, 'style')) {
                const closeGt = indexOfTagClose(text, i);
                if (closeGt < 0 || target <= closeGt) return 'html';
                enclosing = '';
                i = closeGt + 1;
                continue;
            }
            i += 1;
            continue;
        }

        if (enclosing === 'js' || enclosing === 'json') {
            if (text[i] === '/' && text[i + 1] === '/') {
                const nl = text.indexOf('\n', i + 2);
                if (nl < 0 || nl >= target) return enclosing;
                i = nl + 1;
                continue;
            }
            if (text[i] === '/' && text[i + 1] === '*') {
                const close = text.indexOf('*/', i + 2);
                if (close < 0 || close + 2 > target) return enclosing;
                i = close + 2;
                continue;
            }
            if (text[i] === '"' || text[i] === "'" || text[i] === '`') {
                i = skipQuotedString(text, i, text[i], text[i] === '`');
                if (i > target) return enclosing;
                continue;
            }
            if (text[i] === '<' && text[i + 1] === '/' && matchesEndTag(text, i, 'script')) {
                const closeGt = indexOfTagClose(text, i);
                if (closeGt < 0 || target <= closeGt) return 'html';
                enclosing = '';
                i = closeGt + 1;
                continue;
            }
            i += 1;
            continue;
        }

        if (text.startsWith('<!--', i)) {
            const close = text.indexOf('-->', i + 4);
            if (close < 0 || target <= close + 3) return 'html';
            i = close + 3;
            continue;
        }

        if (text[i] === '<') {
            const special = readEmbeddedOpenTag(text, i);
            if (special) {
                if (target <= special.gt) return 'html';
                if (!special.selfClosing) {
                    if (special.name === 'style') enclosing = 'css';
                    else if (special.isJson) enclosing = 'json';
                    else enclosing = 'js';
                }
                i = special.gt + 1;
                continue;
            }
        }

        i += 1;
    }

    if (inPhp) return 'php';
    if (!inPhp && phpOpenLength(text, target) > 0) return 'php';
    if (enclosing === 'css') {
        if (text[target] === '<' && text[target + 1] === '/' && matchesEndTag(text, target, 'style')) {
            return 'html';
        }
        return 'css';
    }
    if (enclosing === 'js' || enclosing === 'json') {
        if (text[target] === '<' && text[target + 1] === '/' && matchesEndTag(text, target, 'script')) {
            return 'html';
        }
        return enclosing;
    }
    return 'html';
}

function readEmbeddedOpenTag(text, start) {
    const head = /^<(style|script)(?![A-Za-z0-9_:-])/i.exec(text.slice(start, start + 16));
    if (!head) return null;
    const name = head[1].toLowerCase();
    let i = start + head[0].length;
    let quote = '';
    let typeValue = '';

    while (i < text.length) {
        const ch = text[i];
        if (quote) {
            if (ch === quote) quote = '';
            i += 1;
            continue;
        }
        if (ch === '"' || ch === "'") {
            quote = ch;
            i += 1;
            continue;
        }
        if (ch === '>') {
            const rawTag = text.slice(start, i + 1);
            const selfClosing = /\/\s*>$/.test(rawTag);
            const typeMatch = /\btype\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>"']+))/i.exec(rawTag);
            if (typeMatch) {
                typeValue = (typeMatch[1] || typeMatch[2] || typeMatch[3] || '').trim().toLowerCase();
            }
            const isJson = typeValue.startsWith('application/ld+json') || typeValue.startsWith('application/json');
            return { name, gt: i, selfClosing, isJson };
        }
        i += 1;
    }
    return { name, gt: text.length, selfClosing: false, isJson: false };
}

function matchPhpHeredoc(text, start) {
    const slice = text.slice(start, Math.min(text.length, start + 80));
    const match = /^<<<[ \t]*(['"]?)([A-Za-z_][A-Za-z0-9_]*)\1/.exec(slice);
    if (!match) return null;
    let nl = start + match[0].length;
    if (text[nl] === '\r') nl += 1;
    if (text[nl] !== '\n') return null;
    const ident = match[2];
    const bodyStart = nl + 1;
    let linePos = bodyStart;
    while (linePos < text.length) {
        let j = linePos;
        while (j < text.length && (text[j] === ' ' || text[j] === '\t')) j += 1;
        if (text.startsWith(ident, j)) {
            let k = j + ident.length;
            if (!(k < text.length && isIdentChar(text[k]))) {
                if (text[k] === ';') k += 1;
                let tail = k;
                while (tail < text.length && (text[tail] === ' ' || text[tail] === '\t')) tail += 1;
                if (tail >= text.length || text[tail] === '\n' || text[tail] === '\r') {
                    return { bodyStart, bodyEnd: linePos, afterClose: k };
                }
            }
        }
        const nextNl = text.indexOf('\n', linePos);
        if (nextNl < 0) break;
        linePos = nextNl + 1;
    }
    return { bodyStart, bodyEnd: text.length, afterClose: text.length };
}

function skipQuotedString(text, start, quote, multiline) {
    let i = start + 1;
    while (i < text.length) {
        if (text[i] === '\\') {
            i = Math.min(text.length, i + 2);
            continue;
        }
        if (text[i] === quote) return i + 1;
        if (!multiline && text[i] === '\n') return i;
        i += 1;
    }
    return text.length;
}

function phpOpenLength(text, i) {
    if (i + 2 >= text.length || text[i] !== '<' || text[i + 1] !== '?') return 0;
    if (text[i + 2] === '=') return 3;
    if (i + 5 <= text.length && text.slice(i, i + 5).toLowerCase() === '<?php') {
        if (!isIdentChar(text[i + 5])) return 5;
    }
    return 0;
}

function matchesEndTag(text, i, tagName) {
    const slice = text.slice(i, i + tagName.length + 3).toLowerCase();
    if (!slice.startsWith(`</${tagName}`)) return false;
    const next = slice[tagName.length + 2];
    return !next || !/[a-z0-9_:-]/.test(next);
}

function indexOfTagClose(text, from) {
    return text.indexOf('>', from);
}

function isIdentChar(ch) {
    if (!ch) return false;
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch === '_';
}

function clamp(value, minOrMax, maybeMax) {
    const n = Number(value);
    if (!Number.isFinite(n)) return maybeMax === undefined ? 0 : minOrMax;
    const min = maybeMax === undefined ? 0 : minOrMax;
    const max = maybeMax === undefined ? minOrMax : maybeMax;
    return Math.max(min, Math.min(max, Math.trunc(n)));
}
