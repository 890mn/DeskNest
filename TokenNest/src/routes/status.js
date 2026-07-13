// src/routes/status.js
// GET /status.json — 简化结构化输出，包含绝对重置时间（北京时间）和重置卡列表

import { tn_logger } from '../logger.js';

// UTC ISO 字符串 → 北京时间 "YYYY-MM-DDTHH:MM:SS+08:00"
const tn_utc_to_iso_cst = (utc) => {
    if (!utc) return null;
    const d = new Date(utc);
    const s = d.toLocaleString('zh-CN', {
        timeZone: 'Asia/Shanghai',
        year: 'numeric', month: '2-digit', day: '2-digit',
        hour: '2-digit', minute: '2-digit', second: '2-digit',
        hour12: false,
    });
    return s.replace(/\//g, '-').replace(' ', 'T') + '+08:00';
};

// 当前北京时间 "2026-07-09 14:32"
const tn_now_cst = () =>
    new Date().toLocaleString('zh-CN', {
        timeZone: 'Asia/Shanghai',
        year: 'numeric', month: '2-digit', day: '2-digit',
        hour: '2-digit', minute: '2-digit',
        hour12: false,
    });

// 距今秒数 → 北京时间 ISO "+08:00"
const tn_secs_to_iso = (sec) => {
    if (!Number.isFinite(sec) || sec <= 0) return null;
    const d = new Date(Date.now() + sec * 1000);
    return d.toISOString().replace(/(\.\d{3})/, '').replace('Z', '+08:00');
};

// Unix 秒时间戳 → 北京时间 ISO "+08:00"
const tn_unix_to_iso = (unixSec) => {
    if (!Number.isFinite(unixSec) || unixSec <= 0) return null;
    const d = new Date(unixSec * 1000);
    return d.toISOString().replace(/(\.\d{3})/, '').replace('Z', '+08:00');
};

const tn_clamp = (n) => Math.max(0, Math.min(100, Math.round(n ?? 0)));

// chatgpt 输出
const tn_chatgpt_out = (snap) => {
    const sourceOk = Boolean(snap?.ok) && !Boolean(snap?.stale);
    const fiveHourAvailable = sourceOk && Boolean(snap?.primary);
    const weeklyAvailable = sourceOk && Boolean(snap?.secondary);
    // reset_at 是未来 Unix 秒时间戳（> 1B），转北京时间
    const fiveHourAt = (snap?.primary?.resetsAt && snap.primary.resetsAt > 1_000_000_000)
        ? tn_unix_to_iso(snap.primary.resetsAt)
        : (snap?.primary?.resetsInSeconds ? tn_secs_to_iso(snap.primary.resetsInSeconds) : null);
    return {
        percent: tn_clamp(snap?.primary?.usedPercent ?? 0),
        weeklyPercent: tn_clamp(snap?.secondary?.usedPercent ?? 0),
        fiveHourAvailable,
        weeklyAvailable,
        fiveHourExpireAt: fiveHourAt,
        weekExpireAt: snap?.secondary?.resetsInSeconds
            ? tn_secs_to_iso(snap.secondary.resetsInSeconds)
            : null,
    };
};

// minimax 输出
const tn_minimax_out = (snap) => {
    if (!snap || !snap.ok || snap.stale || !snap.models?.length) {
        return {
            percent: 0,
            weeklyPercent: 0,
            fiveHourAvailable: false,
            weeklyAvailable: false,
            fiveHourExpireAt: null,
            weekExpireAt: null,
        };
    }
    const eligible = snap.models.filter((m) => m.primary && Number.isFinite(m.primary.usedPercent));
    if (!eligible.length) {
        return {
            percent: 0,
            weeklyPercent: 0,
            fiveHourAvailable: false,
            weeklyAvailable: false,
            fiveHourExpireAt: null,
            weekExpireAt: null,
        };
    }
    const m = eligible.reduce((a, b) => (a.primary.usedPercent >= b.primary.usedPercent ? a : b));
    return {
        percent: tn_clamp(m.primary.usedPercent ?? 0),
        weeklyPercent: tn_clamp(m.secondary?.usedPercent ?? 0),
        fiveHourAvailable: true,
        weeklyAvailable: Boolean(m.secondary),
        fiveHourExpireAt: m.primary.resetsInSeconds ? tn_secs_to_iso(m.primary.resetsInSeconds) : null,
        weekExpireAt: m.secondary?.resetsInSeconds ? tn_secs_to_iso(m.secondary.resetsInSeconds) : null,
    };
};

// 重置卡列表：只保留 available 的
const tn_codex_resets = (snap) => {
    if (!snap?.resetCredits) return [];
    return snap.resetCredits
        .filter((c) => c.status === 'available')
        .map((c, i) => ({
            name: `Codex RE${i + 1}`,
            expireAt: tn_utc_to_iso_cst(c.expiresAt),
        }));
};

const tn_warning_text = (snap, staleThresholdSec) => {
    const parts = [];
    for (const s of [snap.chatgpt, snap.minimax]) {
        if (s.error) parts.push(`${s.ok ? 'warn' : 'err'}:${s.error.slice(0, 32)}`);
        else if (s.stale) parts.push(`stale ${s.ageSec}s`);
    }
    return parts.join('; ').slice(0, 48);
};

export const tn_create_status_route = ({ getAggregate, staleThresholdSec, sources }) => {
    return (req, res) => {
        const snap = getAggregate();

        const body = {
            updatedAtText: tn_now_cst(),
            warningText: tn_warning_text(snap, staleThresholdSec),
            serverNow: new Date().toISOString().replace(/(\.\d{3})/, '').replace('Z', '+08:00'),

            chatgpt: tn_chatgpt_out(snap.chatgpt),
            minimax: tn_minimax_out(snap.minimax),
            codexResets: tn_codex_resets(snap.chatgpt),
        };

        res.set('Content-Type', 'application/json; charset=utf-8');
        res.set('Cache-Control', 'no-store');
        res.json(body);
    };
};
