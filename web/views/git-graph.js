import { el } from './dom.js';

const LANE_COLORS = [
    '#3b82f6', /* Primary trunk: blue */
    '#f59e0b', /* Secondary merge/branch: orange */
    '#a855f7', /* Tertiary branch: purple */
    '#10b981', /* Quaternary branch: green */
];

const SVG_NS = 'http://www.w3.org/2000/svg';

function svgEl(tag, attrs = {}) {
    const node = document.createElementNS(SVG_NS, tag);
    for (const [k, v] of Object.entries(attrs)) {
        if (v != null) node.setAttribute(k, String(v));
    }
    return node;
}

function computeGraphRows(commits) {
    const lanes = [];
    const rows = [];

    for (let i = 0; i < commits.length; i += 1) {
        const c = commits[i];
        const hash = c.hash || c.shortHash || '';
        const parents = Array.isArray(c.parents) && c.parents.length
            ? c.parents
            : String(c.parentHashes || '').split(/\s+/).filter(Boolean);

        let lane = lanes.indexOf(hash);
        if (lane < 0) {
            lane = lanes.indexOf(null);
            if (lane < 0) {
                lane = lanes.length;
                lanes.push(hash);
            } else {
                lanes[lane] = hash;
            }
        }

        const beforeLanes = lanes.slice();
        const passThrough = [];
        for (let l = 0; l < beforeLanes.length; l += 1) {
            if (beforeLanes[l] !== null) passThrough.push(l);
        }

        /* Collapse any duplicate lanes pointing at the same commit hash */
        for (let l = 0; l < lanes.length; l += 1) {
            if (l !== lane && lanes[l] === hash) {
                lanes[l] = null;
            }
        }

        const connections = [];
        if (parents.length === 0) {
            lanes[lane] = null;
        } else {
            const firstParent = parents[0];
            const existingFirst = lanes.indexOf(firstParent);
            if (existingFirst >= 0 && existingFirst !== lane) {
                connections.push({ fromLane: lane, toLane: existingFirst, colorLane: Math.max(lane, existingFirst) });
                lanes[lane] = null;
            } else {
                lanes[lane] = firstParent;
                connections.push({ fromLane: lane, toLane: lane, colorLane: lane });
            }

            for (let p = 1; p < parents.length; p += 1) {
                const parentHash = parents[p];
                let pLane = lanes.indexOf(parentHash);
                if (pLane < 0) {
                    pLane = lanes.indexOf(null);
                    if (pLane < 0) {
                        pLane = lanes.length;
                        lanes.push(parentHash);
                    } else {
                        lanes[pLane] = parentHash;
                    }
                }
                connections.push({ fromLane: lane, toLane: pLane, colorLane: pLane });
            }
        }

        while (lanes.length > 0 && lanes[lanes.length - 1] === null) {
            lanes.pop();
        }

        const afterLanes = lanes.slice();
        const maxLane = Math.max(
            lane,
            beforeLanes.length - 1,
            afterLanes.length - 1,
            ...connections.map((cn) => Math.max(cn.fromLane, cn.toLane)),
            0,
        );

        rows.push({
            commit: c,
            isHead: i === 0,
            lane,
            beforeLanes,
            afterLanes,
            passThrough,
            connections,
            maxLane: Math.min(maxLane, 3),
        });
    }

    return rows;
}

function buildRowSvg(row) {
    const laneSpacing = 12;
    const padX = 10;
    const height = 26;
    const midY = height / 2;
    const cols = Math.max(2, row.maxLane + 1);
    const width = padX * 2 + (cols - 1) * laneSpacing;
    const xFor = (l) => padX + Math.min(l, 3) * laneSpacing;
    const colorFor = (l) => LANE_COLORS[Math.min(l, LANE_COLORS.length - 1)];

    const svg = svgEl('svg', {
        class: 'git-graph-svg',
        width,
        height,
        viewBox: `0 0 ${width} ${height}`,
        'aria-hidden': 'true',
    });

    /* Top half incoming lines for active lanes before this commit */
    for (let l = 0; l < row.beforeLanes.length && l <= 3; l += 1) {
        if (row.beforeLanes[l] === null) continue;
        const x = xFor(l);
        const targetX = row.beforeLanes[l] === (row.commit.hash || row.commit.shortHash) ? xFor(row.lane) : x;
        if (targetX === x) {
            svg.append(svgEl('line', {
                x1: x,
                y1: 0,
                x2: x,
                y2: midY,
                stroke: colorFor(l),
                'stroke-width': '2',
            }));
        } else {
            svg.append(svgEl('path', {
                d: `M ${x} 0 C ${x} ${midY * 0.65}, ${targetX} ${midY * 0.35}, ${targetX} ${midY}`,
                fill: 'none',
                stroke: colorFor(Math.max(l, row.lane)),
                'stroke-width': '2',
            }));
        }
    }

    /* Bottom half continuing lines for lanes that pass through untouched */
    for (let l = 0; l < row.afterLanes.length && l <= 3; l += 1) {
        if (l === row.lane || row.afterLanes[l] === null || row.beforeLanes[l] !== row.afterLanes[l]) continue;
        const x = xFor(l);
        svg.append(svgEl('line', {
            x1: x,
            y1: midY,
            x2: x,
            y2: height,
            stroke: colorFor(l),
            'stroke-width': '2',
        }));
    }

    /* Outgoing parent connections from this commit node */
    for (const cn of row.connections) {
        const x1 = xFor(cn.fromLane);
        const x2 = xFor(cn.toLane);
        const stroke = colorFor(cn.colorLane);
        if (x1 === x2) {
            svg.append(svgEl('line', {
                x1,
                y1: midY,
                x2,
                y2: height,
                stroke,
                'stroke-width': '2',
            }));
        } else {
            svg.append(svgEl('path', {
                d: `M ${x1} ${midY} C ${x1} ${midY + (height - midY) * 0.55}, ${x2} ${midY + (height - midY) * 0.45}, ${x2} ${height}`,
                fill: 'none',
                stroke,
                'stroke-width': '2',
            }));
        }
    }

    /* Commit node circle */
    const cx = xFor(row.lane);
    const nodeColor = colorFor(row.lane);
    if (row.isHead) {
        svg.append(svgEl('circle', {
            cx,
            cy: midY,
            r: '5.5',
            fill: 'var(--surface-1, #121317)',
            stroke: nodeColor,
            'stroke-width': '1.8',
        }));
        svg.append(svgEl('circle', {
            cx,
            cy: midY,
            r: '2.6',
            fill: nodeColor,
        }));
    } else {
        svg.append(svgEl('circle', {
            cx,
            cy: midY,
            r: '3.6',
            fill: nodeColor,
        }));
    }

    return svg;
}

function parseRefs(refsText, defaultBranch) {
    const raw = String(refsText || '').trim();
    if (!raw) return { branch: '', hasRemote: false };
    const parts = raw.split(',').map((s) => s.trim()).filter(Boolean);
    let branch = '';
    let hasRemote = false;

    for (const part of parts) {
        if (part.startsWith('HEAD -> ')) {
            branch = part.slice('HEAD -> '.length).trim();
        } else if (part.startsWith('origin/')) {
            if (part !== 'origin/HEAD') hasRemote = true;
        } else if (!part.startsWith('tag:') && !branch) {
            branch = part;
        }
    }
    if (!branch && defaultBranch) branch = defaultBranch;
    return { branch, hasRemote };
}

export function renderCommitGraphList(container, commits, { defaultBranch = '', onSelectCommit } = {}) {
    if (!container) return;
    container.replaceChildren();
    if (!Array.isArray(commits) || !commits.length) {
        container.append(el('p', { class: 'git-empty-note', text: 'No commits yet.' }));
        return;
    }

    const rows = computeGraphRows(commits);
    for (const row of rows) {
        const c = row.commit;
        const isMerge = (Array.isArray(c.parents) && c.parents.length > 1) ||
            String(c.parentHashes || '').trim().split(/\s+/).filter(Boolean).length > 1;
        const btn = el('button', {
            type: 'button',
            class: `git-commit-row${row.isHead ? ' is-head' : ''}${isMerge ? ' is-merge' : ''}`,
            title: `${c.shortHash} — ${c.subject}\n${c.author} (${c.relativeDate})\nClick to view commit diff`,
        });

        btn.append(buildRowSvg(row));
        btn.append(el('span', { class: 'git-commit-subj', text: c.subject || c.shortHash }));

        const { branch, hasRemote } = parseRefs(c.refs, row.isHead ? defaultBranch : '');
        if (branch) {
            const badge = el('span', {
                class: 'git-commit-branch-pill',
                title: `Branch ${branch}`,
            });
            badge.append(
                el('span', { class: 'git-commit-branch-glyph', 'aria-hidden': 'true', text: '◎' }),
                el('span', { text: branch }),
            );
            btn.append(badge);
        }
        if (hasRemote || (row.isHead && branch)) {
            btn.append(el('span', {
                class: 'git-commit-cloud-pill',
                'aria-hidden': 'true',
                title: 'Synced with remote repository',
                text: '☁',
            }));
        } else if (c.author) {
            btn.append(el('span', { class: 'git-commit-author', text: c.author }));
        }

        btn.addEventListener('click', () => {
            onSelectCommit?.(c);
        });
        container.append(btn);
    }
}
