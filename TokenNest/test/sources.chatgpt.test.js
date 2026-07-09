// test/sources.chatgpt.test.js
// 用 node:test 验证 ChatGPT source 解析、401 refresh、字段映射

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { tn_chatgpt_poll_once } from '../src/sources/chatgpt.js';
import { mkdtempSync, writeFileSync, rmSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const makeAuthFile = (dir, content) => {
    const p = join(dir, 'auth.json');
    writeFileSync(p, JSON.stringify(content));
    return p;
};

const fakeFetch = (handler) => async (url, opts = {}) => {
    const res = await handler(url, opts);
    if (typeof res === 'function') return res();
    return res;
};

test('chatgpt: happy path parses primary/secondary + reset credits', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-cg-'));
    try {
        const authFile = makeAuthFile(dir, {
            tokens: {
                access_token: 'A',
                refresh_token: 'R',
                last_refresh: new Date().toISOString(),
            },
        });
        const fetchImpl = fakeFetch((url) => {
            if (url.includes('wham/usage')) {
                return {
                    ok: true,
                    status: 200,
                    async json() {
                        return {
                            account_id: 'user_xyz',
                            rate_limit: {
                                plan_type: 'pro',
                                primary_window: { used_percent: 42, reset_at: Math.floor(Date.now() / 1000) + 3600, limit_reached: false },
                                secondary_window: { used_percent: 11, reset_at: Math.floor(Date.now() / 1000) + 86400, limit_reached: false },
                            },
                        };
                    },
                };
            }
            if (url.includes('rate-limit-reset-credits')) {
                return {
                    ok: true,
                    status: 200,
                    async json() {
                        return {
                            credits: [{
                                id: 'rlc_1',
                                status: 'available',
                                title: 'Full reset (Weekly + 5 hr)',
                                granted_at: '2026-07-01T00:00:00.000Z',
                                expires_at: '2026-07-31T00:00:00.000Z',
                            }],
                            available_count: 1,
                        };
                    },
                };
            }
            throw new Error('unexpected url ' + url);
        });
        const r = await tn_chatgpt_poll_once({ authFile, fetchImpl });
        assert.equal(r.ok, true);
        assert.equal(r.primary.usedPercent, 42);   // used_percent 直接透传
        assert.equal(r.secondary.usedPercent, 11);    // used_percent 直接透传
        assert.equal(r.planType, 'pro');
        // resetCredits 结构
        assert.equal(r.resetCreditsAvailable, 1);
        assert.equal(r.resetCredits.length, 1);
        assert.equal(r.resetCredits[0].status, 'available');
        assert.equal(r.resetCredits[0].title, 'Full reset (Weekly + 5 hr)');
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
});

test('chatgpt: 401 triggers refresh and retry', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-cg-'));
    try {
        const authFile = makeAuthFile(dir, {
            tokens: { access_token: 'OLD', refresh_token: 'REFRESH_ME', last_refresh: new Date().toISOString() },
        });
        let whamCalls = 0;
        const fetchImpl = fakeFetch(async (url) => {
            if (url.includes('oauth/token')) {
                return {
                    ok: true,
                    status: 200,
                    async json() { return { access_token: 'NEW', refresh_token: 'R2' }; },
                };
            }
            if (url.includes('wham/usage')) {
                whamCalls++;
                if (whamCalls === 1) {
                    return { ok: false, status: 401, async text() { return 'expired'; } };
                }
                return {
                    ok: true,
                    status: 200,
                    async json() {
                        return {
                            rate_limit: {
                                primary_window: { used_percent: 30, reset_at: 0, limit_reached: false },
                                secondary_window: { used_percent: 7, reset_at: 0, limit_reached: false },
                            },
                        };
                    },
                };
            }
            throw new Error('unexpected ' + url);
        });
        const r = await tn_chatgpt_poll_once({ authFile, fetchImpl });
        assert.equal(r.ok, true);
        assert.equal(r.primary.usedPercent, 30);  // used_percent 直接透传
        assert.equal(whamCalls, 2);
        // auth.json 应被写回
        const updated = JSON.parse(await import('node:fs/promises').then((m) => m.readFile(authFile, 'utf8')));
        assert.equal(updated.tokens.access_token, 'NEW');
        assert.equal(updated.tokens.refresh_token, 'R2');
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
});

test('chatgpt: missing auth file returns ok:false', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-cg-'));
    try {
        const r = await tn_chatgpt_poll_once({ authFile: join(dir, 'missing.json'), fetchImpl: fakeFetch(() => { throw new Error('should not call'); }) });
        assert.equal(r.ok, false);
        assert.match(r.error, /auth file not found/);
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
});

test('chatgpt: 5xx on wham is propagated', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-cg-'));
    try {
        const authFile = makeAuthFile(dir, { tokens: { access_token: 'A' } });
        const fetchImpl = fakeFetch(() => ({
            ok: false,
            status: 500,
            async text() { return 'boom'; },
        }));
        const r = await tn_chatgpt_poll_once({ authFile, fetchImpl });
        assert.equal(r.ok, false);
        assert.match(r.error, /HTTP 500/);
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
});
