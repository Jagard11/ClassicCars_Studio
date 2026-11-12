/* Standalone C WebAssembly Engine for ClassicCars Studio (compiled to wasm32).
 * Provides zero-syscall, linear-memory C acceleration for LCS line-diff DP,
 * greedy anchor matching, FNV-1a line hashing, and HTML escaping. */

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;

extern u8 __heap_base;
static u32 g_heap_ptr = 0;

__attribute__((export_name("wasm_reset_heap")))
void wasm_reset_heap(void) {
    g_heap_ptr = (u32)(unsigned long)&__heap_base;
    if (g_heap_ptr < 4096u) g_heap_ptr = 4096u;
}

__attribute__((export_name("wasm_alloc")))
u32 wasm_alloc(u32 bytes) {
    u32 aligned, out;
    if (g_heap_ptr == 0) wasm_reset_heap();
    aligned = (bytes + 7u) & ~7u;
    out = g_heap_ptr;
    g_heap_ptr += aligned;
    return out;
}

__attribute__((export_name("wasm_line_hash")))
u32 wasm_line_hash(const u8 *ptr, u32 len) {
    u32 h = 2166136261u;
    u32 i;
    for (i = 0; i < len; i++) {
        h ^= (u32)ptr[i];
        h *= 16777619u;
    }
    return h;
}

__attribute__((export_name("wasm_lcs_dp")))
u32 wasm_lcs_dp(const u32 *base_h, u32 m, const u32 *curr_h, u32 n,
                u16 *dp, u32 *out_pairs) {
    u32 stride = n + 1u;
    u32 i, j, count = 0;
    for (i = 0; i <= m * stride + n; i++) dp[i] = 0;
    for (i = m; i > 0; i--) {
        u32 r = i - 1u;
        u32 bh = base_h[r];
        u32 row = r * stride;
        u32 next_row = i * stride;
        for (j = n; j > 0; j--) {
            u32 c = j - 1u;
            if (bh == curr_h[c]) {
                dp[row + c] = (u16)(dp[next_row + c + 1u] + 1u);
            } else {
                u16 down = dp[next_row + c];
                u16 right = dp[row + c + 1u];
                dp[row + c] = down >= right ? down : right;
            }
        }
    }
    i = 0;
    j = 0;
    while (i < m && j < n) {
        if (base_h[i] == curr_h[j]) {
            out_pairs[count * 2u] = i;
            out_pairs[count * 2u + 1u] = j;
            count++;
            i++;
            j++;
        } else if (dp[(i + 1u) * stride + j] >= dp[i * stride + j + 1u]) {
            i++;
        } else {
            j++;
        }
    }
    return count;
}

__attribute__((export_name("wasm_greedy_anchors")))
u32 wasm_greedy_anchors(const u32 *base_h, u32 m, const u32 *curr_h, u32 n,
                        u32 *out_pairs) {
    u32 i = 0, j = 0, count = 0;
    while (i < m && j < n) {
        if (base_h[i] == curr_h[j]) {
            out_pairs[count * 2u] = i;
            out_pairs[count * 2u + 1u] = j;
            count++;
            i++;
            j++;
            continue;
        }
        {
            u32 matched = 0;
            u32 rem_m = m - i;
            u32 rem_n = n - j;
            u32 limit = rem_m > rem_n ? rem_m : rem_n;
            u32 step;
            if (limit > 12u) limit = 12u;
            for (step = 1u; step <= limit; step++) {
                if (j + step < n && base_h[i] == curr_h[j + step]) {
                    j += step;
                    matched = 1u;
                    break;
                }
                if (i + step < m && base_h[i + step] == curr_h[j]) {
                    i += step;
                    matched = 1u;
                    break;
                }
            }
            if (!matched) {
                i++;
                j++;
            }
        }
    }
    return count;
}
