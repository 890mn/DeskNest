// src/sources/chatgpt.js
// ChatGPT Plus/Pro (Codex 订阅) 用量源
// - 认证：复用 Codex CLI 的 OAuth 结果 ~/.codex/auth.json
//   （避免读浏览器 cookie —— LevelDB 加密、DPAPI 受保护）
// - 端点：GET https://chatgpt.com/backend-api/wham/usage
//   Authorization: Bearer <access_token>
// - 401 时用 refresh_token 换新 access_token，写回 auth.json
// - 字段映射：
//   rate_limit.primary_window.used_percent   → primary  (5h)
//   rate_limit.secondary_window.used_percent → secondary (weekly)
// - 重置卡：GET https://chatgpt.com/backend-api/wham/rate-limit-reset-credits
//   返回 credits[]，每张有 granted_at / expires_at (UTC)

import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { tn_http_fetch } from '../http.js';
import { tn_cache_write, tn_cache_read, tn_cache_age_sec } from '../cache/store.js';
import { tn_logger } from '../logger.js';

const log = tn_logger('chatgpt');

const WHAM_URL = 'https://chatgpt.com/backend-api/wham/usage';
const RLC_URL = 'https://chatgpt.com/backend-api/wham/rate-limit-reset-credits';
const OAUTH_TOKEN_URL = 'https://auth.openai.com/oauth/token';
const OAUTH_CLIENT_ID = 'app_EMoamEEZ73f0CkXaXp7hrann';

// 8 天寿命。Codex CLI 自身不主动 refresh access_token（只 refresh id_token），
// 我们按 last_refresh 距今 7 天主动 refresh 一次。
const REFRESH_AFTER_MS = 7 * 24 * 60 * 60 * 1000;

const tn_read_auth_file = (authFile) => {
    if (!fs.existsSync(authFile)) return null;
    try {
        const text = fs.readFileSync(authFile, 'utf8');
        const obj = JSON.parse(text);
        if (!obj.tokens) return null;
        return obj;
    } catch (e) {
        log.warn(`auth.json parse failed: ${e.message}`);
        return null;
    }
};

const tn_write_auth_file = (authFile, obj) => {
    try {
        const tmp = `${authFile}.tmp`;
        fs.writeFileSync(tmp, JSON.stringify(obj, null, 2), 'utf8');
        fs.renameSync(tmp, authFile);
    } catch (e) {
        log.error(`auth.json write failed: ${e.message}`);
    }
};

const tn_oauth_refresh = async (refreshToken, fetchImpl) => {
    const body = new URLSearchParams({
        grant_type: 'refresh_token',
        refresh_token: refreshToken,
        client_id: OAUTH_CLIENT_ID,
    });
    const doFetch = fetchImpl ?? ((url, opts) => tn_http_fetch(url, { ...opts, timeoutMs: 15000 }));
    const res = await doFetch(OAUTH_TOKEN_URL, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body,
        timeoutMs: 15000,
    });
    if (!res.ok) {
        const text = await res.text().catch(() => '');
        throw new Error(`oauth refresh HTTP ${res.status}: ${text.slice(0, 200)}`);
    }
    const json = await res.json();
    if (!json.access_token) throw new Error('oauth refresh: no access_token in response');
    return json;
};

// wham/usage 的 used_percent = 用户已用配额百分比（不是剩余）
const tn_window_to_pct = (win) => {
    if (!win || typeof win !== 'object') return null;
    const raw = win.used_percent ?? win.usedPercent ?? win.usage_percent ?? null;
    if (typeof raw !== 'number') return null;
    return Math.max(0, Math.min(100, Math.round(raw)));
};

// 剩余多少秒后重置（倒计时用）
// reset_at 是未来绝对 Unix 时间戳，不是距今秒数
const tn_window_to_reset_sec = (win) => {
    if (!win) return 0;
    if (typeof win.reset_at === 'number' && win.reset_at > 1_000_000_000) {
        // reset_at 是未来 Unix 时间戳（> 1B），转距今秒数
        return Math.max(0, Math.floor(win.reset_at - Date.now() / 1000));
    }
    if (typeof win.reset_after_seconds === 'number') return Math.max(0, Math.floor(win.reset_after_seconds));
    if (typeof win.resetsInSeconds === 'number') return Math.max(0, Math.floor(win.resetsInSeconds));
    return 0;
};

// reset_at Unix 时间戳 → 本地"HH:MM"字符串（如 "14:30"）
const tn_window_to_reset_local = (win) => {
    if (!win) return null;
    const unixTs = win.reset_at ?? null;
    if (typeof unixTs !== 'number' || unixTs <= 0) return null;
    const d = new Date(unixTs * 1000);
    return d.toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit', hour12: false });
};

const tn_window_to_reached = (win) => {
    if (!win) return false;
    return Boolean(win.limit_reached ?? win.limitReached);
};

const tn_parse_wham = (json) => {
    const rl = json.rate_limit ?? json.rateLimit ?? {};
    const primary = rl.primary_window ?? rl.primaryWindow ?? null;
    const secondary = rl.secondary_window ?? rl.secondaryWindow ?? null;

    return {
        accountId: json.account_id ?? json.accountId ?? null,
        planType: rl.plan_type ?? rl.planType ?? null,
        primary: primary ? {
            usedPercent: tn_window_to_pct(primary) ?? 0,
            resetsInSeconds: tn_window_to_reset_sec(primary),
            resetsAt: tn_window_to_reset_local(primary),
            limitReached: tn_window_to_reached(primary),
        } : null,
        secondary: secondary ? {
            usedPercent: tn_window_to_pct(secondary) ?? 0,
            resetsInSeconds: tn_window_to_reset_sec(secondary),
            resetsAt: tn_window_to_reset_local(secondary),
            limitReached: tn_window_to_reached(secondary),
        } : null,
    };
};

// UTC ISO 字符串 → 北京时间 "YYYY-MM-DDTHH:MM:SS+08:00"
const tn_utc_to_iso_cst = (utc) => {
    if (!utc) return null;
    const d = new Date(utc);
    // toLocaleString 带 timeZone: 'Asia/Shanghai' 已经是北京时间
    const s = d.toLocaleString('zh-CN', {
        timeZone: 'Asia/Shanghai',
        year: 'numeric', month: '2-digit', day: '2-digit',
        hour: '2-digit', minute: '2-digit', second: '2-digit',
        hour12: false,
    });
    // "2026/07/18 08:10:00" → "2026-07-18T08:10:00+08:00"
    return s.replace(/\//g, '-').replace(' ', 'T') + '+08:00';
};

// 每张重置卡的标准化结构
const tn_credit_to_obj = (c) => ({
    id: c.id ?? null,
    status: c.status ?? 'unknown',
    title: c.title ?? null,
    // 存 UTC ISO 字符串，格式化时再转北京时间
    grantedAt: c.granted_at ?? null,
    expiresAt: c.expires_at ?? null,
    grantedAtUtc: c.granted_at ?? null,
    expiresAtUtc: c.expires_at ?? null,
});

// 查询重置卡余额 endpoint
const tn_fetch_reset_credits = async ({ accessToken, fetchImpl }) => {
    const doFetch = fetchImpl ?? ((url, opts) => tn_http_fetch(url, { ...opts, timeoutMs: 10000 }));
    let res;
    try {
        res = await doFetch(RLC_URL, { headers: { Authorization: `Bearer ${accessToken}` } });
    } catch (e) {
        log.warn(`reset credits fetch failed: ${e.message}`);
        return { ok: false, error: `network: ${e.message}` };
    }
    if (res.status === 401) {
        log.warn('reset credits: 401 auth failed');
        return { ok: false, error: 'auth failed (401)' };
    }
    if (!res.ok) {
        const t = await res.text().catch(() => '');
        log.warn(`reset credits: HTTP ${res.status}: ${t.slice(0, 200)}`);
        return { ok: false, error: `HTTP ${res.status}: ${t.slice(0, 200)}` };
    }
    let json;
    try {
        json = await res.json();
    } catch (e) {
        log.warn(`reset credits: json parse failed: ${e.message}`);
        return { ok: false, error: `json: ${e.message}` };
    }
    const credits = Array.isArray(json?.credits) ? json.credits.map(tn_credit_to_obj) : [];
    log.info(`reset credits: ${credits.length} total, ${json?.available_count ?? 0} available`);
    return { ok: true, credits, availableCount: json?.available_count ?? credits.length };
};

export const tn_chatgpt_poll_once = async ({ authFile, fetchImpl }) => {
    if (!fs.existsSync(authFile)) {
        return { ok: false, error: 'auth file not found', authFile };
    }
    const auth = tn_read_auth_file(authFile);
    if (!auth || !auth.tokens?.access_token) {
        return { ok: false, error: 'auth file missing access_token', authFile };
    }

    const { access_token: accessToken, refresh_token: refreshToken, last_refresh: lastRefresh } = auth.tokens;
    const needsRefresh = !lastRefresh || (Date.now() - Date.parse(lastRefresh)) > REFRESH_AFTER_MS;
    let curAccess = accessToken;

    if (needsRefresh && refreshToken) {
        try {
            log.info('proactive OAuth refresh (last_refresh > 7d)');
            const tok = await tn_oauth_refresh(refreshToken, fetchImpl);
            curAccess = tok.access_token;
            auth.tokens.access_token = tok.access_token;
            if (tok.refresh_token) auth.tokens.refresh_token = tok.refresh_token;
            auth.tokens.last_refresh = new Date().toISOString();
            tn_write_auth_file(authFile, auth);
        } catch (e) {
            log.warn(`proactive refresh failed, will try existing token: ${e.message}`);
        }
    }

    const doFetch = fetchImpl ?? ((url, opts) => tn_http_fetch(url, { ...opts, timeoutMs: 15000 }));
    let whamRes = await doFetch(WHAM_URL, {
        headers: { Authorization: `Bearer ${curAccess}` },
    });

    if (whamRes.status === 401 && refreshToken) {
        log.info('got 401, refreshing token and retrying');
        try {
            const tok = await tn_oauth_refresh(refreshToken, fetchImpl);
            curAccess = tok.access_token;
            auth.tokens.access_token = tok.access_token;
            if (tok.refresh_token) auth.tokens.refresh_token = tok.refresh_token;
            auth.tokens.last_refresh = new Date().toISOString();
            tn_write_auth_file(authFile, auth);
            whamRes = await doFetch(WHAM_URL, {
                headers: { Authorization: `Bearer ${curAccess}` },
            });
        } catch (e) {
            return { ok: false, error: `401 refresh failed: ${e.message}`, authFile };
        }
    }

    if (!whamRes.ok) {
        const text = await whamRes.text().catch(() => '');
        return { ok: false, error: `HTTP ${whamRes.status}: ${text.slice(0, 200)}`, authFile };
    }

    let json;
    try {
        json = await whamRes.json();
    } catch (e) {
        return { ok: false, error: `json parse failed: ${e.message}` };
    }

    const parsed = tn_parse_wham(json);

    // 并行查重置卡（失败不影响用量结果）
    const rlResult = await tn_fetch_reset_credits({ accessToken: curAccess, fetchImpl });

    return {
        ok: true,
        ...parsed,
        resetCredits: rlResult.credits ?? [],
        resetCreditsAvailable: rlResult.availableCount ?? 0,
    };
};

export const tn_create_chatgpt_source = (config) => {
    const { paths: { cacheDir, codexAuthFile } } = config;
    const intervalMs = config.poll.chatgptIntervalSec * 1000;
    let stopped = false;
    let timer = null;
    let lastTickAt = 0;
    let lastTickOk = false;

    const tick = async () => {
        if (stopped) return;
        lastTickAt = Date.now();
        try {
            const r = await tn_chatgpt_poll_once({ authFile: codexAuthFile });
            lastTickOk = r.ok;
            if (r.ok) {
                tn_cache_write(cacheDir, 'chatgpt', r);
                log.info(`poll ok: primary=${r.primary?.usedPercent}% at ${r.primary?.resetsAt} secondary=${r.secondary?.usedPercent}% at ${r.secondary?.resetsAt}`);
            } else {
                const prev = tn_cache_read(cacheDir, 'chatgpt');
                tn_cache_write(cacheDir, 'chatgpt', { ...(prev?.data ?? {}), ok: false, error: r.error });
                log.warn(`poll failed: ${r.error}`);
            }
        } catch (e) {
            lastTickOk = false;
            log.error(`tick exception: ${e.message}`);
        } finally {
            if (!stopped) timer = setTimeout(tick, intervalMs);
        }
    };

    return {
        name: 'chatgpt',
        intervalSec: config.poll.chatgptIntervalSec,
        start: () => {
            stopped = false;
            tick();
        },
        stop: () => {
            stopped = true;
            if (timer) clearTimeout(timer);
        },
        getCached: () => {
            const entry = tn_cache_read(cacheDir, 'chatgpt');
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
            const elapsedMs = lastTickAt > 0 ? Date.now() - lastTickAt : intervalMs;
            const nextSec = Math.max(0, Math.ceil((intervalMs - elapsedMs) / 1000));
            return {
                ageSec: lastTickAt > 0 ? Math.floor(elapsedMs / 1000) : Infinity,
                nextRefreshInSec: nextSec,
                intervalSec: config.poll.chatgptIntervalSec,
                ok: lastTickOk,
            };
        },
    };
};
