// src/sources/minimax.js
// MiniMax Token Plan 用量源（用户写"minimax" 实指 MiniMax / MiniMax）
// - 端点：GET https://www.minimaxi.com/v1/api/openplatform/coding_plan/remains
// - 认证：Authorization: Bearer <apiKey>（long-lived key）
// - 字段命名反直觉：current_interval_usage_count / current_weekly_usage_count
//   实际是 **剩余** 数量（参考 Eyozy/minimax-usage）
//   已用 = total - usage_count

import fs from 'node:fs';
import { tn_http_fetch } from '../http.js';
import { tn_cache_write, tn_cache_read, tn_cache_age_sec } from '../cache/store.js';
import { tn_logger } from '../logger.js';

const log = tn_logger('minimax');

const URL_REMAINS = 'https://www.minimaxi.com/v1/api/openplatform/coding_plan/remains';

const tn_load_key = (configPath) => {
    if (!fs.existsSync(configPath)) return null;
    try {
        const obj = JSON.parse(fs.readFileSync(configPath, 'utf8'));
        if (typeof obj.apiKey === 'string' && obj.apiKey.length >= 10) return obj.apiKey;
        return null;
    } catch (e) {
        log.warn(`minimax config parse failed: ${e.message}`);
        return null;
    }
};

// 优先级：直接读 *_remaining_percent（API 自己给的剩余%）
// fallback：total/usage_count 反推（usage_count 是剩余，反直觉）
const tn_pct_from_pair = (totalRaw, remainRaw, remainingPctRaw) => {
    if (Number.isFinite(remainingPctRaw)) {
        const rem = Math.max(0, Math.min(100, Number(remainingPctRaw)));
        return Math.max(0, Math.min(100, 100 - rem));
    }
    const total = Number(totalRaw);
    const remain = Number(remainRaw);
    if (!Number.isFinite(total) || total <= 0) return null;
    if (!Number.isFinite(remain)) return null;
    const used = Math.max(0, total - remain);
    return Math.max(0, Math.min(100, Math.round((used / total) * 100)));
};

// MiniMax API 的 remains_time / weekly_remains_time 是毫秒，不是秒！
// 换算：ms ÷ 1000 = 秒。
// 注意：end_time 字段也是毫秒时间戳，但这里不需要（remains_time 已含剩余量）
const tn_ms_to_reset_sec = (msRaw) => {
    const ms = Number(msRaw);
    if (!Number.isFinite(ms) || ms <= 0) return 0;
    return Math.max(0, Math.floor(ms / 1000));
};

const tn_window = (model) => {
    const primary = tn_pct_from_pair(
        model.current_interval_total_count,
        model.current_interval_usage_count,
        model.current_interval_remaining_percent,
    );
    const secondary = tn_pct_from_pair(
        model.current_weekly_total_count,
        model.current_weekly_usage_count,
        model.current_weekly_remaining_percent,
    );
    // remains_time 是毫秒时间戳差值，转秒
    const primaryReset = tn_ms_to_reset_sec(model.remains_time);
    const weeklyReset = tn_ms_to_reset_sec(model.weekly_remains_time);
    return {
        primary: primary === null ? null : {
            usedPercent: primary,
            resetsInSeconds: primaryReset,
            remaining: Number(model.current_interval_usage_count) || 0,
            total: Number(model.current_interval_total_count) || 0,
        },
        secondary: secondary === null ? null : {
            usedPercent: secondary,
            resetsInSeconds: weeklyReset,
            remaining: Number(model.current_weekly_usage_count) || 0,
            total: Number(model.current_weekly_total_count) || 0,
        },
    };
};

const tn_parse_remains = (json) => {
    const baseResp = json.base_resp ?? json.baseResp ?? null;
    const statusCode = baseResp?.status_code ?? baseResp?.statusCode ?? null;
    const statusMsg = baseResp?.status_msg ?? baseResp?.statusMsg ?? null;
    if (statusCode !== null && statusCode !== 0) {
        return { ok: false, error: `minimax status_code=${statusCode} ${statusMsg ?? ''}`.trim() };
    }
    const arr = Array.isArray(json.model_remains) ? json.model_remains
        : Array.isArray(json.modelRemains) ? json.modelRemains
        : [];
    if (!arr.length) {
        return { ok: false, error: 'no model_remains in response' };
    }
    const models = arr.map((m) => {
        if (!m || typeof m !== 'object') return null;
        const w = tn_window(m);
        return {
            name: m.model_name ?? m.modelName ?? 'unknown',
            primary: w.primary,
            secondary: w.secondary,
        };
    }).filter(Boolean);
    return { ok: true, models };
};

export const tn_minimax_poll_once = async ({ configPath, fetchImpl }) => {
    const apiKey = tn_load_key(configPath);
    if (!apiKey) {
        return { ok: false, error: 'minimax apiKey missing or too short', configPath };
    }
    const doFetch = fetchImpl ?? ((url, opts) => tn_http_fetch(url, { ...opts, timeoutMs: 15000 }));
    let res;
    try {
        res = await doFetch(URL_REMAINS, {
            headers: { Authorization: `Bearer ${apiKey}` },
        });
    } catch (e) {
        return { ok: false, error: `network: ${e.message}` };
    }
    if (res.status === 401 || res.status === 403) {
        return { ok: false, error: `auth failed HTTP ${res.status}` };
    }
    if (!res.ok) {
        const t = await res.text().catch(() => '');
        return { ok: false, error: `HTTP ${res.status}: ${t.slice(0, 200)}` };
    }
    let json;
    try {
        json = await res.json();
    } catch (e) {
        return { ok: false, error: `json parse: ${e.message}` };
    }
    return tn_parse_remains(json);
};

export const tn_create_minimax_source = (config) => {
    const { paths: { cacheDir, minimaxConfig } } = config;
    let stopped = false;
    let timer = null;
    let lastTickAt = 0;
    let lastTickOk = false;

    const tick = async () => {
        if (stopped) return;
        lastTickAt = Date.now();
        try {
            const r = await tn_minimax_poll_once({ configPath: minimaxConfig });
            lastTickOk = r.ok;
            if (r.ok) {
                tn_cache_write(cacheDir, 'minimax', r);
                const p = r.models[0]?.primary?.usedPercent ?? 0;
                const s = r.models[0]?.secondary?.usedPercent ?? 0;
                log.info(`poll ok: models=${r.models.length} first primary=${p}% secondary=${s}%`);
            } else {
                const prev = tn_cache_read(cacheDir, 'minimax');
                tn_cache_write(cacheDir, 'minimax', { ...(prev?.data ?? {}), ok: false, error: r.error });
                log.warn(`poll failed: ${r.error}`);
            }
        } catch (e) {
            lastTickOk = false;
            log.error(`tick exception: ${e.message}`);
        } finally {
            if (!stopped) timer = setTimeout(tick, config.poll.minimaxIntervalSec * 1000);
        }
    };

    return {
        name: 'minimax',
        intervalSec: config.poll.minimaxIntervalSec,
        start: () => {
            stopped = false;
            tick();
        },
        stop: () => {
            stopped = true;
            if (timer) clearTimeout(timer);
        },
        getCached: () => {
            const entry = tn_cache_read(cacheDir, 'minimax');
            if (!entry) return { ok: false, ageSec: Infinity, stale: true, error: null };
            const ageSec = tn_cache_age_sec(entry);
            return {
                ...entry.data,
                ageSec,
                stale: ageSec > config.staleThresholdSec || entry.data?.ok === false,
                error: entry.data?.error ?? null,
            };
        },
        getStatus: () => {
            const intervalMs = config.poll.minimaxIntervalSec * 1000;
            const elapsedMs = lastTickAt > 0 ? Date.now() - lastTickAt : intervalMs;
            const nextSec = Math.max(0, Math.ceil((intervalMs - elapsedMs) / 1000));
            return {
                ageSec: lastTickAt > 0 ? Math.floor(elapsedMs / 1000) : Infinity,
                nextRefreshInSec: nextSec,
                intervalSec: config.poll.minimaxIntervalSec,
                ok: lastTickOk,
            };
        },
    };
};
