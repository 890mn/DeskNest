// test/sources.minimax.test.js
// 用 node:test 验证 MiniMax source 解析、remaining→used 翻转、weekly 字段

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { tn_minimax_poll_once } from '../src/sources/minimax.js';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const makeCfg = (dir, content) => {
    const p = join(dir, 'minimax.json');
    writeFileSync(p, JSON.stringify(content));
    return p;
};

const fakeFetch = (handler) => async (url, opts = {}) => {
    const r = await handler(url, opts);
    if (typeof r === 'function') return r();
    return r;
};

test('minimax: parses model_remains, flips remaining→used, percent', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-mm-'));
    try {
        const cfg = makeCfg(dir, { apiKey: 'test-key-1234567890' });
        const fetchImpl = fakeFetch((url) => {
            if (url.includes('coding_plan/remains')) {
                return {
                    ok: true,
                    status: 200,
                    async json() {
                        return {
                            base_resp: { status_code: 0, status_msg: 'ok' },
                            model_remains: [
                                {
                                    model_name: 'MiniMax-M2',
                                    current_interval_total_count: 1000,
                                    current_interval_usage_count: 700,  // 剩余 → 已用 300 → 30%
                                    current_weekly_total_count: 5000,
                                    current_weekly_usage_count: 4100,    // 剩余 → 已用 900 → 18%
                                    remains_time: 1500,
                                    weekly_remains_time: 432000,
                                },
                            ],
                        };
                    },
                };
            }
            throw new Error('unexpected ' + url);
        });
        const r = await tn_minimax_poll_once({ configPath: cfg, fetchImpl });
        assert.equal(r.ok, true);
        assert.equal(r.models.length, 1);
        assert.equal(r.models[0].name, 'MiniMax-M2');
        assert.equal(r.models[0].primary.usedPercent, 30);
        assert.equal(r.models[0].primary.remaining, 700);
        assert.equal(r.models[0].primary.total, 1000);
        assert.equal(r.models[0].secondary.usedPercent, 18);
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
});

test('minimax: base_resp.status_code non-zero → ok:false', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-mm-'));
    try {
        const cfg = makeCfg(dir, { apiKey: 'test-key-1234567890' });
        const fetchImpl = fakeFetch(() => ({
            ok: true,
            status: 200,
            async json() {
                return { base_resp: { status_code: 1004, status_msg: 'invalid api key' } };
            },
        }));
        const r = await tn_minimax_poll_once({ configPath: cfg, fetchImpl });
        assert.equal(r.ok, false);
        assert.match(r.error, /status_code=1004/);
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
});

test('minimax: 401 auth failed', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-mm-'));
    try {
        const cfg = makeCfg(dir, { apiKey: 'test-key-1234567890' });
        const fetchImpl = fakeFetch(() => ({
            ok: false,
            status: 401,
            async text() { return 'unauth'; },
        }));
        const r = await tn_minimax_poll_once({ configPath: cfg, fetchImpl });
        assert.equal(r.ok, false);
        assert.match(r.error, /auth failed/);
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
});

test('minimax: missing/short apiKey → ok:false', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-mm-'));
    try {
        const cfg = makeCfg(dir, { apiKey: 'short' });
        const r = await tn_minimax_poll_once({ configPath: cfg, fetchImpl: fakeFetch(() => { throw new Error('no'); }) });
        assert.equal(r.ok, false);
        assert.match(r.error, /missing or too short/);
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
});

test('minimax: multi-model aggregator input', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-mm-'));
    try {
        const cfg = makeCfg(dir, { apiKey: 'test-key-1234567890' });
        const fetchImpl = fakeFetch(() => ({
            ok: true,
            status: 200,
            async json() {
                return {
                    base_resp: { status_code: 0 },
                    model_remains: [
                        { model_name: 'A', current_interval_total_count: 100, current_interval_usage_count: 80, current_weekly_total_count: 1000, current_weekly_usage_count: 900 },
                        { model_name: 'B', current_interval_total_count: 200, current_interval_usage_count: 50, current_weekly_total_count: 2000, current_weekly_usage_count: 100 },
                    ],
                };
            },
        }));
        const r = await tn_minimax_poll_once({ configPath: cfg, fetchImpl });
        assert.equal(r.ok, true);
        assert.equal(r.models.length, 2);
        assert.equal(r.models[0].primary.usedPercent, 20);
        assert.equal(r.models[1].primary.usedPercent, 75);
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
});

test('minimax: uses *_remaining_percent when total=0 (unlimited model)', async () => {
    // 复现真实场景：general model 5h total=0, 但 current_interval_remaining_percent=76 → 5h 已用 24%
    const dir = mkdtempSync(join(tmpdir(), 'tn-mm-'));
    try {
        const cfg = makeCfg(dir, { apiKey: 'test-key-1234567890' });
        const fetchImpl = fakeFetch(() => ({
            ok: true,
            status: 200,
            async json() {
                return {
                    base_resp: { status_code: 0 },
                    model_remains: [
                        {
                            model_name: 'general',
                            current_interval_total_count: 0,
                            current_interval_usage_count: 0,
                            current_interval_remaining_percent: 76,
                            current_weekly_total_count: 0,
                            current_weekly_usage_count: 0,
                            current_weekly_remaining_percent: 75,
                            remains_time: 9453662,
                            weekly_remains_time: 369453662,
                        },
                        {
                            model_name: 'video',
                            current_interval_total_count: 3,
                            current_interval_usage_count: 3,
                            current_interval_remaining_percent: 100,
                            current_weekly_total_count: 21,
                            current_weekly_usage_count: 18,
                            current_weekly_remaining_percent: 85,
                            remains_time: 23853662,
                            weekly_remains_time: 369453662,
                        },
                    ],
                };
            },
        }));
        const r = await tn_minimax_poll_once({ configPath: cfg, fetchImpl });
        assert.equal(r.ok, true);
        // general: 100 - 76 = 24
        assert.equal(r.models[0].primary.usedPercent, 24);
        assert.equal(r.models[0].secondary.usedPercent, 25);  // 100 - 75
        // video: 100 - 100 = 0
        assert.equal(r.models[1].primary.usedPercent, 0);
        assert.equal(r.models[1].secondary.usedPercent, 15);  // 100 - 85
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
});
