const PHP_WORDS = new Set([
    'function', 'return', 'if', 'else', 'elseif', 'foreach', 'for', 'while',
    'require_once', 'include', 'include_once', 'require', 'echo', 'isset',
    'true', 'false', 'null', 'array', 'as', 'new',
]);

const JS_WORDS = new Set([
    'var', 'let', 'const', 'function', 'window', 'document', 'return',
    'if', 'else', 'for', 'while', 'new', 'this', 'true', 'false', 'null',
    'undefined', 'typeof', 'in', 'of', 'switch', 'case', 'break', 'continue',
    'try', 'catch', 'throw', 'class', 'async', 'await', 'do',
]);

const GROUPING_AT = new Set(['media', 'supports', 'container', 'layer', 'keyframes', 'document', 'scope']);

const MULTI_OPS = ['===', '!==', '>>=', '<<=', '=>', '->', '::', '&&', '||', '++', '--', '+=', '-=', '*=', '/=', '.=', '==', '!=', '<=', '>=', '??', '?.', '<<', '>>'];

const META_HEAD = /^<!--\s*\/?\s*(page-values|related-sales|generated-seo)(?![A-Za-z0-9_-])/i;
const META_LINE = /^([ \t]*)(Override IDs|[A-Za-z_][\w-]*)([ \t]*:)(.*)$/i;

export function languageFromPath(path) {
    const name = String(path || '');
    if (/\.php$/i.test(name)) return 'php';
    if (/\.(html?|shtml)$/i.test(name)) return 'html';
    if (/\.css$/i.test(name)) return 'css';
    if (/\.m?js$/i.test(name)) return 'js';
    if (/\.json$/i.test(name)) return 'json';
    return 'text';
}

export function highlightSyntax(source, language) {
    const text = String(source ?? '');
    const lang = String(language || 'text').toLowerCase();
    if (!text || lang === 'text' || lang === 'plain') return esc(text);
    const ctx = { text, i: 0, out: [] };
    const end = text.length;
    if (lang === 'css') scanCss(ctx, end);
    else if (lang === 'js' || lang === 'javascript') scanJs(ctx, end);
    else if (lang === 'json') scanJson(ctx, end);
    else if (lang === 'php' && !/^\s*</.test(text)) {
        scanPhp(ctx, end);
        if (ctx.i < end) scanMarkup(ctx, end);
    } else scanMarkup(ctx, end);
    if (ctx.i < end) emit(ctx, '', ctx.i, end);
    return ctx.out.join('');
}

export function highlightDocument(source, language) {
    const text = String(source ?? '');
    let html = highlightSyntax(text, language);
    // The source already ends in a newline. A space after it forces that
    // last empty line to paint, the same way a textarea does.
    if (text.endsWith('\n')) html += ' ';
    return html;
}

function scanMarkup(ctx, end) {
    const text = ctx.text;
    while (ctx.i < end) {
        const i = ctx.i;
        const phpLen = phpOpenLength(text, i, end);
        if (phpLen) {
            emit(ctx, 'tok-php-tag', i, i + phpLen);
            ctx.i = i + phpLen;
            scanPhp(ctx, end);
            continue;
        }
        if (text.startsWith('<!--', i)) {
            if (isSsi(text, i)) scanSsi(ctx, end);
            else if (META_HEAD.test(text.slice(i, Math.min(end, i + 64)))) scanMeta(ctx, end);
            else scanHtmlComment(ctx, end);
            continue;
        }
        if (text[i] === '<' && text[i + 1] === '!' && scanDoctype(ctx, end)) continue;
        if (text[i] === '<' && looksLikeTag(text, i, end)) {
            const tag = scanTag(ctx, end);
            if (!tag) {
                emit(ctx, '', i, i + 1);
                ctx.i = i + 1;
                continue;
            }
            if (!tag.closing && !tag.selfClosing) enterEmbedded(ctx, end, tag);
            continue;
        }
        if (text[i] === '&') {
            const ent = entityEnd(text, i, end);
            if (ent > i) {
                emit(ctx, 'tok-entity', i, ent);
                ctx.i = ent;
                continue;
            }
        }
        scanPlainUntil(ctx, end, (ch) => ch !== '<' && ch !== '&');
        if (ctx.i === i) {
            emit(ctx, '', i, i + 1);
            ctx.i = i + 1;
        }
    }
}

function enterEmbedded(ctx, end, tag) {
    if (tag.name === 'style') {
        const stop = findEndTag(ctx.text, ctx.i, end, 'style');
        scanCss(ctx, stop);
        return;
    }
    if (tag.name === 'script') {
        const stop = findEndTag(ctx.text, ctx.i, end, 'script');
        const type = tag.typeValue.trim().toLowerCase();
        if (type.startsWith('application/ld+json') || type.startsWith('application/json')) scanJson(ctx, stop);
        else scanJs(ctx, stop);
    }
}

function scanHtmlComment(ctx, end) {
    const close = indexBefore(ctx.text, '-->', ctx.i + 4, end);
    const stop = close < 0 ? end : close + 3;
    emit(ctx, 'tok-comment', ctx.i, stop);
    ctx.i = stop;
}

function scanDoctype(ctx, end) {
    const slice = ctx.text.slice(ctx.i, Math.min(end, ctx.i + 16));
    if (!/^<!doctype(?![A-Za-z0-9_-])/i.test(slice)) return false;
    let stop = ctx.text.indexOf('>', ctx.i);
    if (stop < 0 || stop >= end) stop = end;
    else stop += 1;
    emit(ctx, 'tok-doctype', ctx.i, stop);
    ctx.i = stop;
    return true;
}

function scanSsi(ctx, end) {
    const text = ctx.text;
    const start = ctx.i;
    const close = indexBefore(text, '-->', start + 4, end);
    const limit = close < 0 ? end : close;
    const head = /^<!--\s*#\s*[A-Za-z]+/.exec(text.slice(start, limit));
    if (!head) {
        scanHtmlComment(ctx, end);
        return;
    }
    emit(ctx, 'tok-ssi', start, start + head[0].length);
    ctx.i = start + head[0].length;
    while (ctx.i < limit) {
        const i = ctx.i;
        const c = text[i];
        if (isSpace(c)) {
            emitSpace(ctx, limit);
            continue;
        }
        if (c === '"' || c === "'") {
            scanHtmlQuoted(ctx, limit, c);
            continue;
        }
        if (isAttrStart(c)) {
            const name = i;
            ctx.i = i + 1;
            while (ctx.i < limit && isAttrChar(text[ctx.i])) ctx.i += 1;
            emit(ctx, 'tok-attr', name, ctx.i);
            emitGap(ctx, limit);
            if (text[ctx.i] === '=') {
                emit(ctx, 'tok-punctuation', ctx.i, ctx.i + 1);
                ctx.i += 1;
                emitGap(ctx, limit);
                const q = text[ctx.i];
                if (q === '"' || q === "'") scanHtmlQuoted(ctx, limit, q);
                else if (ctx.i < limit && text[ctx.i] !== '>') scanUnquoted(ctx, limit);
            }
            continue;
        }
        emit(ctx, 'tok-ssi', i, i + 1);
        ctx.i = i + 1;
    }
    if (close >= 0) {
        emit(ctx, 'tok-ssi', close, close + 3);
        ctx.i = close + 3;
    }
}

function scanMeta(ctx, end) {
    const text = ctx.text;
    const start = ctx.i;
    const close = indexBefore(text, '-->', start + 4, end);
    const limit = close < 0 ? end : close;
    const head = META_HEAD.exec(text.slice(start, Math.min(limit, start + 64)));
    const headLen = head ? head[0].length : 4;
    emit(ctx, 'tok-meta-comment', start, Math.min(limit, start + headLen));
    let i = Math.min(limit, start + headLen);
    while (i < limit) {
        let lineEnd = text.indexOf('\n', i);
        if (lineEnd < 0 || lineEnd >= limit) lineEnd = limit;
        else lineEnd += 1;
        emitMetaLine(ctx, i, lineEnd);
        i = lineEnd;
    }
    ctx.i = limit;
    if (close >= 0) {
        emit(ctx, 'tok-meta-comment', close, close + 3);
        ctx.i = close + 3;
    }
}

function emitMetaLine(ctx, start, end) {
    const text = ctx.text;
    const nl = end > start && text[end - 1] === '\n' ? 1 : 0;
    const bodyEnd = end - nl;
    const body = text.slice(start, bodyEnd);
    const match = META_LINE.exec(body);
    if (!match) {
        emit(ctx, 'tok-meta-comment', start, end);
        return;
    }
    const indent = match[1].length;
    const keyLen = match[2].length + match[3].length;
    if (indent) emit(ctx, 'tok-meta-comment', start, start + indent);
    emit(ctx, 'tok-meta-key', start + indent, start + indent + keyLen);
    emitClassified(ctx, start + indent + keyLen, bodyEnd, 'tok-meta-val');
    if (nl) emit(ctx, '', end - 1, end);
}

function scanTag(ctx, end) {
    const text = ctx.text;
    const open = ctx.i;
    if (text[open] !== '<') return null;
    let i = open + 1;
    if (text[i] === '/') i += 1;
    if (!isIdentStart(text[i])) return null;
    const nameStart = i;
    i += 1;
    while (i < end && isNameChar(text[i])) i += 1;
    const name = text.slice(nameStart, i);
    emit(ctx, 'tok-punctuation', open, nameStart);
    emit(ctx, 'tok-tag', nameStart, i);
    ctx.i = i;
    const attrs = scanAttributes(ctx, end);
    return {
        name: name.toLowerCase(),
        closing: text[open + 1] === '/',
        selfClosing: attrs.selfClosing,
        typeValue: attrs.typeValue,
    };
}

function scanAttributes(ctx, end) {
    const text = ctx.text;
    let typeValue = '';
    let selfClosing = false;
    while (ctx.i < end) {
        const c = text[ctx.i];
        if (c === '>') {
            emit(ctx, 'tok-punctuation', ctx.i, ctx.i + 1);
            ctx.i += 1;
            break;
        }
        if (c === '/' && text[ctx.i + 1] === '>') {
            emit(ctx, 'tok-punctuation', ctx.i, ctx.i + 2);
            ctx.i += 2;
            selfClosing = true;
            break;
        }
        if (isSpace(c)) {
            emitSpace(ctx, end);
            continue;
        }
        if (!isAttrStart(c)) {
            emit(ctx, 'tok-punctuation', ctx.i, ctx.i + 1);
            ctx.i += 1;
            continue;
        }
        const nameAt = ctx.i;
        ctx.i += 1;
        while (ctx.i < end && isAttrChar(text[ctx.i])) ctx.i += 1;
        const attr = text.slice(nameAt, ctx.i);
        emit(ctx, 'tok-attr', nameAt, ctx.i);
        emitGap(ctx, end);
        if (text[ctx.i] !== '=') continue;
        emit(ctx, 'tok-punctuation', ctx.i, ctx.i + 1);
        ctx.i += 1;
        emitGap(ctx, end);
        const q = text[ctx.i];
        let value = '';
        if (q === '"' || q === "'") value = scanHtmlQuoted(ctx, end, q);
        else if (ctx.i < end && text[ctx.i] !== '>' && text[ctx.i] !== '/') value = scanUnquoted(ctx, end);
        if (attr.toLowerCase() === 'type') typeValue = value;
    }
    return { typeValue, selfClosing };
}

function scanHtmlQuoted(ctx, end, quote) {
    const text = ctx.text;
    const open = ctx.i;
    if (text[open] !== quote) return '';
    ctx.i = open + 1;
    let seg = open;
    let raw = '';
    while (ctx.i < end) {
        if (text[ctx.i] === quote) {
            ctx.i += 1;
            emit(ctx, 'tok-string', seg, ctx.i);
            return raw;
        }
        if (text[ctx.i] === '&') {
            const ent = entityEnd(text, ctx.i, end);
            if (ent > ctx.i) {
                emit(ctx, 'tok-string', seg, ctx.i);
                raw += text.slice(ctx.i, ent);
                emit(ctx, 'tok-entity', ctx.i, ent);
                ctx.i = ent;
                seg = ctx.i;
                continue;
            }
        }
        raw += text[ctx.i];
        ctx.i += 1;
    }
    emit(ctx, 'tok-string', seg, ctx.i);
    return raw;
}

function scanUnquoted(ctx, end) {
    const text = ctx.text;
    const start = ctx.i;
    let raw = '';
    while (ctx.i < end) {
        const c = text[ctx.i];
        if (isSpace(c) || c === '>' || c === '"' || c === "'") break;
        if (c === '/' && text[ctx.i + 1] === '>') break;
        raw += c;
        ctx.i += 1;
    }
    emit(ctx, 'tok-string', start, ctx.i);
    return raw;
}

function scanCss(ctx, end) {
    const stack = ['rules'];
    let pendingAt = '';
    while (ctx.i < end) {
        if (skipGap(ctx, end)) continue;
        if (ctx.i >= end) break;
        const mode = stack[stack.length - 1];
        const c = ctx.text[ctx.i];
        if (c === '{') {
            emit(ctx, 'tok-punctuation', ctx.i, ctx.i + 1);
            ctx.i += 1;
            const grouping = GROUPING_AT.has(pendingAt);
            stack.push(mode === 'rules' && pendingAt && grouping ? 'rules' : 'decls');
            pendingAt = '';
            continue;
        }
        if (c === '}') {
            emit(ctx, 'tok-punctuation', ctx.i, ctx.i + 1);
            ctx.i += 1;
            if (stack.length > 1) stack.pop();
            pendingAt = '';
            continue;
        }
        if (c === ';') {
            emit(ctx, 'tok-punctuation', ctx.i, ctx.i + 1);
            ctx.i += 1;
            pendingAt = '';
            continue;
        }
        if (mode === 'rules' && c === '@') {
            pendingAt = scanAtKeyword(ctx, end);
            scanCssExpr(ctx, end);
            continue;
        }
        if (mode === 'rules') {
            scanSelector(ctx, end);
            continue;
        }
        if (!scanCssProperty(ctx, end)) {
            emit(ctx, 'tok-punctuation', ctx.i, ctx.i + 1);
            ctx.i += 1;
        }
    }
}

function scanAtKeyword(ctx, end) {
    const start = ctx.i;
    let i = start + 1;
    while (i < end && isCssIdentChar(ctx.text[i])) i += 1;
    const name = ctx.text.slice(start + 1, i).toLowerCase();
    emit(ctx, 'tok-keyword', start, i);
    ctx.i = i;
    return name;
}

function scanSelector(ctx, end) {
    const text = ctx.text;
    const start = ctx.i;
    let i = start;
    while (i < end) {
        const c = text[i];
        if (c === '{' || c === '}' || c === '@' || c === ';') break;
        if (c === '/' && text[i + 1] === '*') break;
        i += 1;
    }
    if (i === start) {
        emit(ctx, 'tok-selector', start, start + 1);
        ctx.i = start + 1;
        return;
    }
    emit(ctx, 'tok-selector', start, i);
    ctx.i = i;
}

function scanCssProperty(ctx, end) {
    const text = ctx.text;
    const start = ctx.i;
    let i = start;
    if (text.startsWith('--', i)) i += 2;
    else if (text[i] === '-') i += 1;
    if (!isCssIdentStart(text[i])) return false;
    while (i < end && isCssIdentChar(text[i])) i += 1;
    emit(ctx, 'tok-prop', start, i);
    ctx.i = i;
    emitGap(ctx, end);
    if (text[ctx.i] === ':') {
        emit(ctx, 'tok-punctuation', ctx.i, ctx.i + 1);
        ctx.i += 1;
        scanCssExpr(ctx, end);
    }
    return true;
}

function scanCssExpr(ctx, end) {
    const text = ctx.text;
    let depth = 0;
    while (ctx.i < end) {
        const i = ctx.i;
        const c = text[i];
        if (c === '/' && text[i + 1] === '*') {
            scanBlockComment(ctx, end);
            continue;
        }
        if (c === '"' || c === "'") {
            scanSimpleString(ctx, end, c);
            continue;
        }
        if (isSpace(c)) {
            emitSpace(ctx, end);
            continue;
        }
        if ((c === '{' || c === ';' || c === '}') && depth === 0) return;
        if (c === '(') {
            depth += 1;
            emit(ctx, 'tok-punctuation', i, i + 1);
            ctx.i = i + 1;
            continue;
        }
        if (c === ')') {
            depth = Math.max(0, depth - 1);
            emit(ctx, 'tok-punctuation', i, i + 1);
            ctx.i = i + 1;
            continue;
        }
        if (c === '#' && scanHex(ctx, end)) continue;
        if (scanCssNumber(ctx, end)) continue;
        if ((isCssIdentStart(c) || c === '-') && scanCssIdent(ctx, end)) continue;
        emit(ctx, 'tok-punctuation', i, i + 1);
        ctx.i = i + 1;
    }
}

function scanCssIdent(ctx, end) {
    const text = ctx.text;
    const start = ctx.i;
    let i = start;
    if (text[i] === '-' && text[i + 1] === '-') i += 2;
    else if (text[i] === '-') i += 1;
    if (!isCssIdentStart(text[i])) return false;
    while (i < end && isCssIdentChar(text[i])) i += 1;
    const word = text.slice(start, i).toLowerCase();
    emit(ctx, word === 'important' ? 'tok-keyword' : 'tok-val', start, i);
    ctx.i = i;
    return true;
}

function scanHex(ctx, end) {
    const text = ctx.text;
    if (text[ctx.i] !== '#') return false;
    let i = ctx.i + 1;
    const digits = i;
    while (i < end && isHex(text[i])) i += 1;
    const len = i - digits;
    if (len !== 3 && len !== 4 && len !== 6 && len !== 8) return false;
    if (isCssIdentChar(text[i])) return false;
    emit(ctx, 'tok-number', ctx.i, i);
    ctx.i = i;
    return true;
}

function scanCssNumber(ctx, end) {
    const text = ctx.text;
    let i = ctx.i;
    const sign = text[i] === '-' || text[i] === '+';
    if (sign) {
        const next = text[i + 1];
        if (!(isDigit(next) || (next === '.' && isDigit(text[i + 2])))) return false;
        i += 1;
    }
    const before = i;
    if (text[i] === '.' && isDigit(text[i + 1])) {
        i += 2;
        while (isDigit(text[i])) i += 1;
    } else if (isDigit(text[i])) {
        while (isDigit(text[i])) i += 1;
        if (text[i] === '.' && isDigit(text[i + 1])) {
            i += 2;
            while (isDigit(text[i])) i += 1;
        }
    }
    if (i === before) return false;
    if (text[i] === '%') i += 1;
    else if (isIdentStart(text[i])) {
        while (isIdentChar(text[i])) i += 1;
    }
    emit(ctx, 'tok-number', ctx.i, i);
    ctx.i = i;
    return true;
}

function scanJs(ctx, end) {
    scanCode(ctx, end, JS_WORDS, false);
}

function scanJson(ctx, end) {
    const text = ctx.text;
    while (ctx.i < end) {
        const i = ctx.i;
        const c = text[i];
        if (isSpace(c)) {
            emitSpace(ctx, end);
            continue;
        }
        if (c === '"') {
            scanJsonString(ctx, end);
            continue;
        }
        if (c === '-' || isDigit(c)) {
            if (scanPlainNumber(ctx, end, true)) continue;
        }
        if (isIdentStart(c)) {
            const wordEnd = readIdent(text, i, end);
            const word = text.slice(i, wordEnd);
            const cls = word === 'true' || word === 'false' || word === 'null' ? 'tok-keyword' : '';
            emit(ctx, cls, i, wordEnd);
            ctx.i = wordEnd;
            continue;
        }
        emit(ctx, 'tok-punctuation', i, i + 1);
        ctx.i = i + 1;
    }
}

function scanJsonString(ctx, end) {
    const text = ctx.text;
    const start = ctx.i;
    let i = start + 1;
    while (i < end) {
        if (text[i] === '\\') {
            i = Math.min(end, i + 2);
            continue;
        }
        if (text[i] === '"') {
            i += 1;
            break;
        }
        i += 1;
    }
    let k = i;
    while (k < end && isSpace(text[k])) k += 1;
    emit(ctx, text[k] === ':' ? 'tok-attr' : 'tok-string', start, i);
    ctx.i = i;
}

function scanPhp(ctx, end) {
    const text = ctx.text;
    while (ctx.i < end) {
        const i = ctx.i;
        const c = text[i];
        if (c === '/' && text[i + 1] === '/') {
            scanLineComment(ctx, end);
            continue;
        }
        if (c === '/' && text[i + 1] === '*') {
            scanBlockComment(ctx, end);
            continue;
        }
        if (c === '#' && text[i + 1] !== '[') {
            scanLineComment(ctx, end);
            continue;
        }
        if (c === "'") {
            scanPhpSingle(ctx, end);
            continue;
        }
        if (c === '"') {
            scanPhpDouble(ctx, end);
            continue;
        }
        if (c === '<' && text[i + 1] === '<' && text[i + 2] === '<' && scanHeredoc(ctx, end)) continue;
        if (c === '?' && text[i + 1] === '>') {
            emit(ctx, 'tok-php-tag', i, i + 2);
            ctx.i = i + 2;
            return;
        }
        if (c === '$' && scanVariable(ctx, end)) continue;
        if (isIdentStart(c)) {
            scanWord(ctx, end, PHP_WORDS, true);
            continue;
        }
        if (isDigit(c) || (c === '.' && isDigit(text[i + 1]))) {
            if (scanPlainNumber(ctx, end, false)) continue;
        }
        if (isSpace(c)) {
            emitSpace(ctx, end);
            continue;
        }
        if (scanMultiOp(ctx, end)) continue;
        emit(ctx, 'tok-punctuation', i, i + 1);
        ctx.i = i + 1;
    }
}

function scanCode(ctx, end, words, fold) {
    const text = ctx.text;
    while (ctx.i < end) {
        const i = ctx.i;
        const c = text[i];
        if (c === '/' && text[i + 1] === '/') {
            scanLineComment(ctx, end);
            continue;
        }
        if (c === '/' && text[i + 1] === '*') {
            scanBlockComment(ctx, end);
            continue;
        }
        if (c === '"' || c === "'" || c === '`') {
            scanSimpleString(ctx, end, c);
            continue;
        }
        if (isSpace(c)) {
            emitSpace(ctx, end);
            continue;
        }
        if (isIdentStart(c)) {
            scanWord(ctx, end, words, fold);
            continue;
        }
        if (isDigit(c) || (c === '.' && isDigit(text[i + 1]))) {
            if (scanPlainNumber(ctx, end, false)) continue;
        }
        if (scanMultiOp(ctx, end)) continue;
        emit(ctx, 'tok-punctuation', i, i + 1);
        ctx.i = i + 1;
    }
}

function scanHeredoc(ctx, end) {
    const text = ctx.text;
    const start = ctx.i;
    const slice = text.slice(start, Math.min(end, start + 80));
    const match = /^<<<[ \t]*(['"]?)([A-Za-z_][A-Za-z0-9_]*)\1/.exec(slice);
    if (!match) return false;
    let nl = start + match[0].length;
    if (text[nl] === '\r') nl += 1;
    if (text[nl] !== '\n') return false;
    const ident = match[2];
    emit(ctx, 'tok-keyword', start, start + match[0].length);
    emit(ctx, '', start + match[0].length, nl + 1);
    ctx.i = nl + 1;
    const closer = findHeredocClose(text, ctx.i, end, ident);
    scanMarkup(ctx, closer ? closer.ident : end);
    if (!closer) return true;
    emit(ctx, 'tok-keyword', closer.ident, closer.after);
    ctx.i = closer.after;
    return true;
}

function findHeredocClose(text, from, end, ident) {
    let i = from;
    while (i < end) {
        let j = i;
        while (j < end && (text[j] === ' ' || text[j] === '\t')) j += 1;
        if (text.startsWith(ident, j)) {
            let k = j + ident.length;
            if (!(k < end && isIdentChar(text[k]))) {
                if (text[k] === ';') k += 1;
                if (lineTail(text, k, end)) return { ident: j, after: k };
            }
        }
        const nl = text.indexOf('\n', i);
        if (nl < 0 || nl >= end) return null;
        i = nl + 1;
    }
    return null;
}

function lineTail(text, index, end) {
    let k = index;
    while (k < end && (text[k] === ' ' || text[k] === '\t')) k += 1;
    return k >= end || text[k] === '\n' || text[k] === '\r';
}

function scanPhpSingle(ctx, end) {
    const text = ctx.text;
    const start = ctx.i;
    ctx.i += 1;
    while (ctx.i < end) {
        if (text[ctx.i] === '\\') {
            ctx.i = Math.min(end, ctx.i + 2);
            continue;
        }
        if (text[ctx.i] === "'") {
            ctx.i += 1;
            break;
        }
        ctx.i += 1;
    }
    emit(ctx, 'tok-string', start, ctx.i);
}

function scanPhpDouble(ctx, end) {
    const text = ctx.text;
    let seg = ctx.i;
    ctx.i += 1;
    while (ctx.i < end) {
        const c = text[ctx.i];
        if (c === '\\') {
            ctx.i = Math.min(end, ctx.i + 2);
            continue;
        }
        if (c === '"') {
            ctx.i += 1;
            break;
        }
        if (c === '$' && (isIdentStart(text[ctx.i + 1]) || text[ctx.i + 1] === '{')) {
            emit(ctx, 'tok-string', seg, ctx.i);
            if (text[ctx.i + 1] === '{') {
                const brace = ctx.i;
                ctx.i += 2;
                while (ctx.i < end && text[ctx.i] !== '}') ctx.i += 1;
                if (text[ctx.i] === '}') ctx.i += 1;
                emit(ctx, 'tok-variable', brace, ctx.i);
            } else scanVariable(ctx, end);
            seg = ctx.i;
            continue;
        }
        ctx.i += 1;
    }
    emit(ctx, 'tok-string', seg, ctx.i);
}

function scanVariable(ctx, end) {
    const text = ctx.text;
    if (text[ctx.i] !== '$') return false;
    let i = ctx.i + 1;
    if (text[i] === '$') i += 1;
    if (!isIdentStart(text[i])) return false;
    i += 1;
    while (i < end && isIdentChar(text[i])) i += 1;
    emit(ctx, 'tok-variable', ctx.i, i);
    ctx.i = i;
    return true;
}

function scanWord(ctx, end, words, fold) {
    const wordEnd = readIdent(ctx.text, ctx.i, end);
    const word = ctx.text.slice(ctx.i, wordEnd);
    const key = fold ? word.toLowerCase() : word;
    emit(ctx, words.has(key) ? 'tok-keyword' : '', ctx.i, wordEnd);
    ctx.i = wordEnd;
}

function scanPlainNumber(ctx, end, allowSign) {
    const text = ctx.text;
    let i = ctx.i;
    if (allowSign && (text[i] === '-' || text[i] === '+')) {
        if (!isDigit(text[i + 1])) return false;
        i += 1;
    }
    if (text[i] === '0' && (text[i + 1] === 'x' || text[i + 1] === 'X')) {
        const hex = i + 2;
        i += 2;
        while (isHex(text[i])) i += 1;
        if (i === hex) return false;
        emit(ctx, 'tok-number', ctx.i, i);
        ctx.i = i;
        return true;
    }
    const before = i;
    if (!isDigit(text[i])) return false;
    while (isDigit(text[i])) i += 1;
    if (text[i] === '.' && isDigit(text[i + 1])) {
        i += 2;
        while (isDigit(text[i])) i += 1;
    }
    if (i === before) return false;
    emit(ctx, 'tok-number', ctx.i, i);
    ctx.i = i;
    return true;
}

function scanSimpleString(ctx, end, quote) {
    const text = ctx.text;
    const start = ctx.i;
    ctx.i += 1;
    while (ctx.i < end) {
        if (text[ctx.i] === '\\') {
            ctx.i = Math.min(end, ctx.i + 2);
            continue;
        }
        if (text[ctx.i] === quote) {
            ctx.i += 1;
            break;
        }
        if (quote !== '`' && text[ctx.i] === '\n') break;
        ctx.i += 1;
    }
    emit(ctx, 'tok-string', start, ctx.i);
}

function scanLineComment(ctx, end) {
    const text = ctx.text;
    const start = ctx.i;
    let i = start;
    while (i < end && text[i] !== '\n') i += 1;
    if (i < end && text[i] === '\n') i += 1;
    emit(ctx, 'tok-comment', start, i);
    ctx.i = i;
}

function scanBlockComment(ctx, end) {
    const at = ctx.text.indexOf('*/', ctx.i + 2);
    const stop = at < 0 || at >= end ? end : at + 2;
    emit(ctx, 'tok-comment', ctx.i, stop);
    ctx.i = stop;
}

function scanMultiOp(ctx, end) {
    const text = ctx.text;
    for (let index = 0; index < MULTI_OPS.length; index += 1) {
        const op = MULTI_OPS[index];
        if (ctx.i + op.length <= end && text.startsWith(op, ctx.i)) {
            emit(ctx, 'tok-punctuation', ctx.i, ctx.i + op.length);
            ctx.i += op.length;
            return true;
        }
    }
    return false;
}

function emitClassified(ctx, start, end, cls) {
    const text = ctx.text;
    let i = start;
    let seg = start;
    while (i < end) {
        if (text[i] === '&') {
            const ent = entityEnd(text, i, end);
            if (ent > i) {
                emit(ctx, cls, seg, i);
                emit(ctx, 'tok-entity', i, ent);
                i = ent;
                seg = i;
                continue;
            }
        }
        i += 1;
    }
    emit(ctx, cls, seg, end);
}

function scanPlainUntil(ctx, end, keep) {
    const text = ctx.text;
    const start = ctx.i;
    let i = start;
    while (i < end && keep(text[i])) i += 1;
    emit(ctx, '', start, i);
    ctx.i = i;
}

function skipGap(ctx, end) {
    const c = ctx.text[ctx.i];
    if (c === '/' && ctx.text[ctx.i + 1] === '*') {
        scanBlockComment(ctx, end);
        return true;
    }
    if (isSpace(c)) {
        emitSpace(ctx, end);
        return true;
    }
    return false;
}

function emitGap(ctx, end) {
    if (isSpace(ctx.text[ctx.i])) emitSpace(ctx, end);
}

function emitSpace(ctx, end) {
    const start = ctx.i;
    let i = start;
    while (i < end && isSpace(ctx.text[i])) i += 1;
    emit(ctx, '', start, i);
    ctx.i = i;
}

function entityEnd(text, i, end) {
    if (text[i] !== '&' || i + 2 >= end) return -1;
    let j = i + 1;
    if (text[j] === '#') {
        j += 1;
        if (text[j] === 'x' || text[j] === 'X') {
            j += 1;
            const hex = j;
            while (j < end && isHex(text[j])) j += 1;
            if (j === hex || text[j] !== ';') return -1;
            return j + 1;
        }
        const dec = j;
        while (j < end && isDigit(text[j])) j += 1;
        if (j === dec || text[j] !== ';') return -1;
        return j + 1;
    }
    const name = j;
    if (!isIdentStart(text[j])) return -1;
    j += 1;
    while (j < end && isIdentChar(text[j])) j += 1;
    if (j === name || text[j] !== ';') return -1;
    return j + 1;
}

function findEndTag(text, from, end, name) {
    const lower = name.toLowerCase();
    for (let i = from; i < end; i += 1) {
        if (text[i] !== '<' || text[i + 1] !== '/') continue;
        let j = i + 2;
        let k = 0;
        while (k < lower.length && j < end) {
            if (fold(text[j]) !== lower[k]) break;
            j += 1;
            k += 1;
        }
        if (k !== lower.length) continue;
        if (j < end && isNameChar(text[j])) continue;
        return i;
    }
    return end;
}

function phpOpenLength(text, i, end) {
    if (i + 2 >= end || text[i] !== '<' || text[i + 1] !== '?') return 0;
    if (text[i + 2] === '=') return 3;
    if (i + 5 <= end && text.slice(i, i + 5).toLowerCase() === '<?php') {
        if (!isIdentChar(text[i + 5])) return 5;
    }
    return 0;
}

function looksLikeTag(text, i, end) {
    const c = text[i + 1];
    if (i + 1 >= end || !c) return false;
    return c === '/' || c === '!' || isIdentStart(c);
}

function isSsi(text, i) {
    return /^<!--\s*#/.test(text.slice(i, i + 12));
}

function indexBefore(text, needle, from, end) {
    const at = text.indexOf(needle, from);
    if (at < 0 || at >= end) return -1;
    return at;
}

function readIdent(text, i, end) {
    let j = i + 1;
    while (j < end && isIdentChar(text[j])) j += 1;
    return j;
}

function emit(ctx, cls, start, end) {
    if (end <= start) return;
    const raw = esc(ctx.text.slice(start, end));
    if (!cls) ctx.out.push(raw);
    else ctx.out.push('<span class="', cls, '">', raw, '</span>');
}

function esc(value) {
    let out = '';
    for (let i = 0; i < value.length; i += 1) {
        const ch = value[i];
        if (ch === '&') out += '&amp;';
        else if (ch === '<') out += '&lt;';
        else if (ch === '>') out += '&gt;';
        else out += ch;
    }
    return out;
}

function isSpace(ch) {
    return ch === ' ' || ch === '\n' || ch === '\t' || ch === '\r' || ch === '\f';
}

function isDigit(ch) {
    return ch >= '0' && ch <= '9';
}

function isHex(ch) {
    return isDigit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

function isIdentStart(ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch === '_';
}

function isIdentChar(ch) {
    return isIdentStart(ch) || isDigit(ch);
}

function isNameChar(ch) {
    return isIdentChar(ch) || ch === '-' || ch === ':';
}

function isAttrStart(ch) {
    return isIdentStart(ch) || ch === ':' || ch === '_';
}

function isAttrChar(ch) {
    return isNameChar(ch);
}

function isCssIdentStart(ch) {
    return isIdentStart(ch);
}

function isCssIdentChar(ch) {
    return isIdentChar(ch) || ch === '-';
}

function fold(ch) {
    return ch >= 'A' && ch <= 'Z' ? String.fromCharCode(ch.charCodeAt(0) + 32) : ch;
}
