// src/sources/aggregator.js
// 归一化两个 source 的缓存条目，输出 UnifiedUsage
// 双源可独立 stale（一边挂掉不影响另一边）

import { tn_cache_read } from '../cache/store.js';

const tn_source_snapshot = (entry, staleThresholdSec, name) => {
    if (!entry) {
        return { ok: false, ageSec: Infinity, primary: null, secondary: null, stale: true, fetchedAt: null };
    }
    const { data, fetchedAt } = entry;
    const ageSec = Number.isFinite(Date.parse(fetchedAt))
        ? Math.max(0, Math.floor((Date.now() - Date.parse(fetchedAt)) / 1000))
        : Infinity;
    const stale = ageSec > staleThresholdSec || data?.ok === false;

    if (name === 'chatgpt') {
        return {
            ok: data?.ok !== false,
            ageSec,
            stale,
            fetchedAt,
            accountId: data?.accountId ?? null,
            planType: data?.planType ?? null,
            primary: data?.primary ?? null,
            secondary: data?.secondary ?? null,
            resetCreditsAvailable: data?.resetCreditsAvailable ?? 0,
            resetCredits: Array.isArray(data?.resetCredits) ? data.resetCredits : [],
            error: data?.error ?? null,
        };
    }
    if (name === 'minimax') {
        return {
            ok: data?.ok !== false,
            ageSec,
            stale,
            fetchedAt,
            models: Array.isArray(data?.models) ? data.models : [],
            error: data?.error ?? null,
        };
    }
    return { ok: false, ageSec, stale: true, fetchedAt: null };
};

const tn_max_pct = (...vals) => {
    const xs = vals.filter((v) => v !== null && v !== undefined && Number.isFinite(v));
    if (!xs.length) return 0;
    return Math.max(...xs);
};

export const tn_aggregate = ({ cacheDir, staleThresholdSec, now = Date.now() }) => {
    const chatgptEntry = tn_cache_read(cacheDir, 'chatgpt');
    const minimaxEntry = tn_cache_read(cacheDir, 'minimax');

    const chatgpt = tn_source_snapshot(chatgptEntry, staleThresholdSec, 'chatgpt');
    const minimax = tn_source_snapshot(minimaxEntry, staleThresholdSec, 'minimax');

    // 5h 哨兵：两个 source 的 primary 窗口中 max
    // （MiniMax 有多个 model 时取 max —— 上限保护）
    const minimaxPrimaryMax = Math.max(0, ...(minimax.models ?? [])
        .map((m) => m.primary?.usedPercent ?? -1)
        .filter((v) => v >= 0));
    const minimaxSecondaryMax = Math.max(0, ...(minimax.models ?? [])
        .map((m) => m.secondary?.usedPercent ?? -1)
        .filter((v) => v >= 0));

    const primaryPercent = tn_max_pct(
        chatgpt.primary?.usedPercent,
        Number.isFinite(minimaxPrimaryMax) ? minimaxPrimaryMax : null,
    );
    const secondaryPercent = tn_max_pct(
        chatgpt.secondary?.usedPercent,
        Number.isFinite(minimaxSecondaryMax) ? minimaxSecondaryMax : null,
    );

    return {
        fetchedAt: new Date(now).toISOString(),
        primaryPercent,    // 5h 哨兵（max）
        secondaryPercent,  // weekly 哨兵（max）
        chatgpt,
        minimax,
    };
};
