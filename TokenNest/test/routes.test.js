// test/routes.test.js
// /status.json 新结构验证（简化格式）

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { tn_aggregate } from '../src/sources/aggregator.js';
import { tn_create_status_route } from '../src/routes/status.js';
import { tn_create_usage_route } from '../src/routes/usage.js';
import { tn_create_health_route } from '../src/routes/health.js';
import { tn_cache_write } from '../src/cache/store.js';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const makeRes = () => {
    const headers = {};
    let statusCode = 200;
    let body = null;
    return {
        set(k, v) { headers[k] = v; return this; },
        status(c) { statusCode = c; return this; },
        json(b) { body = b; return this; },
        get statusCode() { return statusCode; },
        get headers() { return headers; },
        get body() { return body; },
    };
};

const setupCache = (dir, chatgpt, minimax) => {
    if (chatgpt !== null) tn_cache_write(dir, 'chatgpt', chatgpt);
    if (minimax !== null) tn_cache_write(dir, 'minimax', minimax);
};

test('status.json: new format — top-level fields present', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-rt-'));
    try {
        setupCache(dir,
            { ok: true, planType: 'plus', primary: { usedPercent: 72 }, secondary: { usedPercent: 43 } },
            { ok: true, models: [{ primary: { usedPercent: 86 }, secondary: { usedPercent: 18 } }] },
        );
        const getAggregate = () => tn_aggregate({ cacheDir: dir, staleThresholdSec: 300 });
        const fakeSources = [
            { name: 'chatgpt', getStatus: () => ({ nextRefreshInSec: 47, intervalSec: 60, ok: true, ageSec: 13 }) },
            { name: 'minimax', getStatus: () => ({ nextRefreshInSec: 52, intervalSec: 60, ok: true, ageSec: 8 }) },
        ];
        const handler = tn_create_status_route({ getAggregate, staleThresholdSec: 300, sources: fakeSources });
        const res = makeRes();
        handler({}, res);
        assert.equal(res.statusCode, 200);
        assert.equal(typeof res.body.updatedAtText, 'string');
        assert.ok(res.body.updatedAtText.includes('20'), 'updatedAtText is CST date string');
        assert.equal(typeof res.body.serverNow, 'string');
        assert.ok(res.body.serverNow.includes('+08:00'), 'serverNow includes +08:00 timezone');
        assert.equal(typeof res.body.warningText, 'string');
        // chatgpt 字段
        assert.equal(res.body.chatgpt.percent, 72);
        assert.equal(res.body.chatgpt.weeklyPercent, 43);
        // minimax 字段
        assert.equal(res.body.minimax.percent, 86);
        assert.equal(res.body.minimax.weeklyPercent, 18);
        // codexResets 数组
        assert.ok(Array.isArray(res.body.codexResets));
    } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('status.json: chatgpt reset credits reflected in codexResets', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-rt-'));
    try {
        setupCache(dir, {
            ok: true, planType: 'plus',
            primary: { usedPercent: 72, resetsAt: null, resetsInSeconds: 3600 },
            secondary: { usedPercent: 43, resetsInSeconds: 86400 },
            resetCreditsAvailable: 2,
            resetCredits: [
                { id: 'r1', status: 'available', title: 'Full reset', grantedAt: '06-18 08:10', expiresAt: '07-18 08:10', grantedAtUtc: '2026-06-18T00:10:56Z', expiresAtUtc: '2026-07-18T00:10:56Z' },
                { id: 'r2', status: 'available', title: 'Full reset', grantedAt: '07-01 08:10', expiresAt: '07-31 08:10', grantedAtUtc: '2026-07-01T00:10:56Z', expiresAtUtc: '2026-07-31T00:10:56Z' },
            ],
        }, { ok: true, models: [] });
        const getAggregate = () => tn_aggregate({ cacheDir: dir, staleThresholdSec: 300 });
        const fakeSources = [
            { name: 'chatgpt', getStatus: () => ({ nextRefreshInSec: 10, intervalSec: 60, ok: true, ageSec: 5 }) },
            { name: 'minimax', getStatus: () => ({ nextRefreshInSec: 20, intervalSec: 60, ok: true, ageSec: 15 }) },
        ];
        const handler = tn_create_status_route({ getAggregate, staleThresholdSec: 300, sources: fakeSources });
        const res = makeRes();
        handler({}, res);
        assert.equal(res.body.codexResets.length, 2);
        assert.equal(res.body.codexResets[0].name, 'Codex RE1');
        assert.equal(res.body.codexResets[0].expireAt, '07-18 08:10');
        assert.equal(res.body.codexResets[1].name, 'Codex RE2');
        assert.equal(res.body.codexResets[1].expireAt, '07-31 08:10');
        // expired/redeemed 卡应被过滤
    } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('status.json: ok:false source → warningText non-empty', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-rt-'));
    try {
        setupCache(dir,
            { ok: false, error: '401 refresh failed: invalid_grant', primary: { usedPercent: 0 }, secondary: { usedPercent: 0 } },
            { ok: true, models: [{ primary: { usedPercent: 30 }, secondary: { usedPercent: 18 } }] },
        );
        const getAggregate = () => tn_aggregate({ cacheDir: dir, staleThresholdSec: 300 });
        const fakeSources = [
            { name: 'chatgpt', getStatus: () => ({ nextRefreshInSec: 47, intervalSec: 60, ok: true, ageSec: 13 }) },
            { name: 'minimax', getStatus: () => ({ nextRefreshInSec: 52, intervalSec: 60, ok: true, ageSec: 8 }) },
        ];
        const handler = tn_create_status_route({ getAggregate, staleThresholdSec: 300, sources: fakeSources });
        const res = makeRes();
        handler({}, res);
        assert.ok(res.body.warningText.includes('401'), 'warningText includes error');
    } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('status.json: both sources ok:false → warningText mentions both', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-rt-'));
    try {
        setupCache(dir,
            { ok: false, error: 'a', primary: { usedPercent: 0 }, secondary: { usedPercent: 0 } },
            { ok: false, error: 'b', models: [] },
        );
        const getAggregate = () => tn_aggregate({ cacheDir: dir, staleThresholdSec: 300 });
        const fakeSources = [
            { name: 'chatgpt', getStatus: () => ({ nextRefreshInSec: 30, intervalSec: 60, ok: true, ageSec: 30 }) },
            { name: 'minimax', getStatus: () => ({ nextRefreshInSec: 30, intervalSec: 60, ok: true, ageSec: 30 }) },
        ];
        const handler = tn_create_status_route({ getAggregate, staleThresholdSec: 300, sources: fakeSources });
        const res = makeRes();
        handler({}, res);
        assert.ok(res.body.warningText.includes('a'));
        assert.ok(res.body.warningText.includes('b'));
    } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('/api/usage: emits 5h/weekly primary/secondary per source', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-rt-'));
    try {
        setupCache(dir,
            { ok: true, primary: { usedPercent: 42 }, secondary: { usedPercent: 11 } },
            { ok: true, models: [{ primary: { usedPercent: 30 }, secondary: { usedPercent: 18 } }] },
        );
        const getAggregate = () => tn_aggregate({ cacheDir: dir, staleThresholdSec: 300 });
        const handler = tn_create_usage_route({ getAggregate });
        const res = makeRes();
        handler({}, res);
        assert.equal(res.statusCode, 200);
        assert.equal(res.body.primaryPercent, 42);
        assert.equal(res.body.secondaryPercent, 18);
        assert.equal(res.body.chatgpt.primary.usedPercent, 42);
        assert.equal(res.body.minimax.models[0].secondary.usedPercent, 18);
    } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('/healthz: 200 when all sources ok and fresh', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-rt-'));
    try {
        setupCache(dir,
            { ok: true, primary: { usedPercent: 1 }, secondary: { usedPercent: 1 } },
            { ok: true, models: [{ primary: { usedPercent: 1 }, secondary: { usedPercent: 1 } }] },
        );
        const sources = [
            { name: 'chatgpt', getCached: () => ({ ok: true, ageSec: 5, stale: false }) },
            { name: 'minimax', getCached: () => ({ ok: true, ageSec: 8, stale: false }) },
        ];
        const handler = tn_create_health_route({ sources });
        const res = makeRes();
        handler({}, res);
        assert.equal(res.statusCode, 200);
        assert.equal(res.body.chatgpt.ok, true);
    } finally { rmSync(dir, { recursive: true, force: true }); }
});
