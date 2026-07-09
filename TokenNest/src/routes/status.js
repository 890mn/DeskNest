// src/routes/status.js
// GET /status.json — 简化结构化输出，K10 端不再解析此格式
// 输出给 DeskNest UI 使用，包含绝对重置时间（北京时间）和重置卡列表

import { tn_logger } from '../logger.js';

const log = tn_logger('status');

// 格式化：当前北京时间（格式 "2026-07-09 14:32"）
const tn_now_cst = () =>
    new Date().toLocaleString('zh-CN', {
        timeZone: 'Asia/Shanghai',
        year: 'numeric', month: '2-digit', day: '2-digit',
        hour: '2-digit', minute: '2-digit',
        hour12: false,
    });

// 秒数 → 北京时间 ISO "+08:00"（resetsInSeconds 为距今秒数）
const tn_secs_to_iso = (sec) => {
    if (!Number.isFinite(sec) || sec <= 0) return null;
    const d = new Date(Date.now() + sec * 1000);
    return d.toISOString().replace(/(\.\d{3})/, '').replace('Z', '+08:00');
};

// Unix 秒时间戳 → 北京时间 ISO "+08:00"（reset_at 是绝对时间戳）
const tn_unix_to_iso = (unixSec) => {
    if (!Number.isFinite(unixSec) || unixSec <= 0) return null;
    const d = new Date(unixSec * 1000);
    return d.toISOString().replace(/(\.\d{3})/, '').replace('Z', '+08:00');
};

const tn_clamp = (n) => Math.max(0, Math.min(100, Math.round(n ?? 0)));

// 通用：从 source snap 提取 percent + expire 时间
const tn_chatgpt_out = (snap) => {
    if (!snap || !snap.ok || !snap.primary) {
        return { percent: 0, weeklyPercent: 0, fiveHourExpireAt: null, weekExpireAt: null };
    }
    return {
        percent: tn_clamp(snap.primary?.usedPercent ?? 0),
        weeklyPercent: tn_clamp(snap.secondary?.usedPercent ?? 0),
        // 5h 窗口：优先用 resetsAt（HH:MM），fallback 用 resetsInSeconds 推算
        fiveHourExpireAt: snap.primary?.resetsAt
            ? tn_unix_to_iso(snap.primary.resetsAt)
            : (snap.primary?.resetsInSeconds ? tn_secs_to_iso(snap.primary.resetsInSeconds) : null),
        // weekly 窗口：用 resetsInSeconds 推算
        weekExpireAt: snap.secondary?.resetsInSeconds
            ? tn_secs_to_iso(snap.secondary.resetsInSeconds)
            : null,
    };
};

const tn_minimax_out = (snap) => {
    if (!snap || !snap.ok || !snap.models?.length) {
        return { percent: 0, weeklyPercent: 0, fiveHourExpireAt: null, weekExpireAt: null };
    }
    // 选 primary% 最大的 model
    const eligible = snap.models.filter((m) => m.primary && Number.isFinite(m.primary.usedPercent));
    if (!eligible.length) {
        return { percent: 0, weeklyPercent: 0, fiveHourExpireAt: null, weekExpireAt: null };
    }
    const m = eligible.reduce((a, b) => (a.primary.usedPercent >= b.primary.usedPercent ? a : b));
    return {
        percent: tn_clamp(m.primary?.usedPercent ?? 0),
        weeklyPercent: tn_clamp(m.secondary?.usedPercent ?? 0),
        fiveHourExpireAt: m.primary?.resetsInSeconds ? tn_secs_to_iso(m.primary.resetsInSeconds) : null,
        weekExpireAt: m.secondary?.resetsInSeconds ? tn_secs_to_iso(m.secondary.resetsInSeconds) : null,
    };
};

// 重置卡列表：只保留 available 的，输出 { name: "Codex RE1", expireAt }
const tn_codex_resets = (snap) => {
    if (!snap?.resetCredits) return [];
    return snap.resetCredits
        .filter((c) => c.status === 'available')
        .map((c, i) => ({
            name: `Codex RE${i + 1}`,
            expireAt: c.expiresAt ?? null,
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
            // 更新时间（北京时间）
            updatedAtText: tn_now_cst(),
            warningText: tn_warning_text(snap, staleThresholdSec),
            // 服务器当前时间（ISO +08:00）
            serverNow: new Date().toISOString().replace(/(\.\d{3})/, '').replace('Z', '+08:00'),

            chatgpt: tn_chatgpt_out(snap.chatgpt),

            minimax: tn_minimax_out(snap.minimax),

            // 重置卡列表
            codexResets: tn_codex_resets(snap.chatgpt),
        };

        res.set('Content-Type', 'application/json; charset=utf-8');
        res.set('Cache-Control', 'no-store');
        res.json(body);
    };
};
