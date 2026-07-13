// test/aggregator.test.js
// 验证双源可独立 stale、5h/weekly max 取哨兵

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { tn_aggregate } from '../src/sources/aggregator.js';
import { tn_cache_write } from '../src/cache/store.js';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const makeCache = (dir, chatgpt, minimax, fetchedAtOffsetSec = 0) => {
    if (chatgpt !== null) tn_cache_write(dir, 'chatgpt', chatgpt);
    if (minimax !== null) tn_cache_write(dir, 'minimax', minimax);
    return dir;
};

test('aggregator: both ok → max of primary/secondary', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-agg-'));
    try {
        makeCache(dir,
            { ok: true, primary: { usedPercent: 40 }, secondary: { usedPercent: 10 } },
            { ok: true, models: [{ primary: { usedPercent: 30 }, secondary: { usedPercent: 80 } }] },
        );
        const r = tn_aggregate({ cacheDir: dir, staleThresholdSec: 300 });
        assert.equal(r.primaryPercent, 40);
        assert.equal(r.secondaryPercent, 80);
    } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('aggregator: chatgpt stale but ok=false → warning captured, minimax still used', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-agg-'));
    try {
        makeCache(dir,
            { ok: false, error: 'auth failed', primary: { usedPercent: 0 }, secondary: { usedPercent: 0 } },
            { ok: true, models: [{ primary: { usedPercent: 25 }, secondary: { usedPercent: 15 } }] },
        );
        const r = tn_aggregate({ cacheDir: dir, staleThresholdSec: 300 });
        assert.equal(r.chatgpt.ok, false);
        assert.match(r.chatgpt.error, /auth failed/);
        assert.equal(r.minimax.ok, true);
        assert.equal(r.primaryPercent, 25);
    } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('aggregator: no cache files → everything ok:false but no crash', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-agg-'));
    try {
        const r = tn_aggregate({ cacheDir: dir, staleThresholdSec: 300 });
        assert.equal(r.chatgpt.ok, false);
        assert.equal(r.minimax.ok, false);
        assert.equal(r.primaryPercent, 0);
        assert.equal(r.secondaryPercent, 0);
    } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('aggregator: multi-model minimax → primaryPercent = max of models', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-agg-'));
    try {
        makeCache(dir,
            null,
            { ok: true, models: [
                { primary: { usedPercent: 10 } },
                { primary: { usedPercent: 70 } },
                { primary: { usedPercent: 50 } },
            ] },
        );
        const r = tn_aggregate({ cacheDir: dir, staleThresholdSec: 300 });
        assert.equal(r.primaryPercent, 70);
    } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('aggregator: ChatGPT weekly-only contributes to secondary, not primary', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-agg-'));
    try {
        makeCache(dir,
            { ok: true, primary: null, secondary: { usedPercent: 0 } },
            { ok: true, models: [] },
        );
        const r = tn_aggregate({ cacheDir: dir, staleThresholdSec: 300 });
        assert.equal(r.primaryPercent, 0);
        assert.equal(r.secondaryPercent, 0);
        assert.equal(r.chatgpt.primary, null);
        assert.equal(r.chatgpt.secondary.usedPercent, 0);
    } finally { rmSync(dir, { recursive: true, force: true }); }
});
