import test from 'node:test';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import {
    What2EatError,
    tn_create_admin_guard,
    tn_create_what2eat_routes,
    tn_create_what2eat_store,
    tn_what2eat_normalize,
} from '../src/routes/what2eat.js';

const ROOT = dirname(dirname(fileURLToPath(import.meta.url)));

const makeStore = (dir, now = () => new Date('2026-07-14T03:00:00.000Z')) => tn_create_what2eat_store({
    draftPath: join(dir, 'what2eat.draft.json'),
    publishedPath: join(dir, 'what2eat.published.json'),
    ackPath: join(dir, 'what2eat.ack.json'),
    now,
});

const item = (overrides = {}) => ({
    id: 'rice-1',
    name: '番茄牛腩饭',
    count: 3,
    price: '28.50',
    score: 85,
    ...overrides,
});

const withTemp = async (fn) => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-what2eat-'));
    try { await fn(dir); } finally { rmSync(dir, { recursive: true, force: true }); }
};

const makeRes = () => {
    let statusCode = 200;
    let body = null;
    let ended = false;
    const headers = {};
    return {
        status(code) { statusCode = code; return this; },
        set(key, value) { headers[key] = value; return this; },
        json(value) { body = value; return this; },
        end() { ended = true; return this; },
        get statusCode() { return statusCode; },
        get body() { return body; },
        get ended() { return ended; },
        get headers() { return headers; },
    };
};

test('what2eat starts with an empty draft and no built-in dishes', async () => withTemp((dir) => {
    const state = makeStore(dir).getAdminState();
    assert.equal(state.draftVersion, 0);
    assert.deepEqual(state.what2eat.items, []);
    assert.equal(state.published, null);
    assert.equal(state.lastAck, null);
}));

test('what2eat enforces the firmware wire limits and Count A fields', () => {
    const valid = tn_what2eat_normalize({ items: [item({ id: 'x'.repeat(23), name: '菜'.repeat(10), price: '123456.78', count: 65535, score: 100 })] });
    assert.equal(valid.items[0].count, 65535);
    assert.equal(valid.items[0].name, '菜'.repeat(10));
    const halfStep = tn_what2eat_normalize({ items: [item({ score: 65 })] });
    assert.equal(halfStep.items[0].score, 65);
    assert.throws(() => tn_what2eat_normalize({ items: [item({ id: 'x'.repeat(24) })] }), /23 UTF-8 bytes/);
    assert.throws(() => tn_what2eat_normalize({ items: [item({ name: '菜'.repeat(11) })] }), /31 UTF-8 bytes/);
    assert.throws(() => tn_what2eat_normalize({ items: [item({ price: '1234567.89' })] }), /9 UTF-8 bytes/);
    assert.throws(() => tn_what2eat_normalize({ items: [item({ count: 65536 })] }), /0 to 65535/);
    assert.throws(() => tn_what2eat_normalize({ items: [item({ score: 101 })] }), /10 to 100/);
    assert.throws(() => tn_what2eat_normalize({ items: [item({ score: 88 })] }), /0.5 increments/);
    assert.throws(() => tn_what2eat_normalize({ items: [item({ score: 0 })] }), /10 to 100/);
    assert.equal(tn_what2eat_normalize({ items: Array.from({ length: 15 }, (_, i) => item({ id: `i-${i}` })) }).items.length, 15);
    assert.throws(() => tn_what2eat_normalize({ items: Array.from({ length: 16 }, (_, i) => item({ id: `i-${i}` })) }), /at most 15/);
    assert.throws(() => tn_what2eat_normalize({ items: [item({ name: '饭"店' })] }), /invalid format/);
    assert.throws(() => tn_what2eat_normalize({ items: [item({ name: '饭\\店' })] }), /invalid format/);
    assert.throws(() => tn_what2eat_normalize({ items: [item({ name: '饭\n店' })] }), /control characters/);
    assert.throws(() => tn_what2eat_normalize({ items: [item({ name: '饭🍚' })] }), /invalid format/);
});

test('what2eat accepted name ranges are backed by the dynamic firmware font profile', () => {
    const config = JSON.parse(readFileSync(join(ROOT, '..', 'tools', 'cnfontnest.json'), 'utf8'));
    const profile = config.profiles.find((entry) => entry.id === 'lv_font_16_dynamic');
    assert.ok(profile, 'lv_font_16_dynamic profile must exist');
    const command = profile.converter.command.join(' ');
    for (const range of ['0x20-0x7F', '0x2000-0x206F', '0x3000-0x303F', '0x3040-0x30FF', '0x4E00-0x9FFF', '0xFF00-0xFFEF']) {
        assert.match(command, new RegExp(range));
    }
});

test('saving a draft never creates a sync payload', async () => withTemp((dir) => {
    const store = makeStore(dir);
    const saved = store.saveDraft({ items: [item()] }, 0);
    assert.equal(saved.draftVersion, 1);
    assert.equal(store.loadPublished(), null);
    assert.equal(store.syncAfter(0), null);
}));

test('an empty draft stays editable but cannot create an unusable release', async () => withTemp((dir) => {
    const store = makeStore(dir);
    const draft = store.saveDraft({ items: [] }, 0);
    assert.equal(draft.draftVersion, 1);
    assert.throws(
        () => store.publish(draft.draftVersion),
        (error) => error instanceof What2EatError && error.code === 'EMPTY_DRAFT' && error.status === 400,
    );
    assert.equal(store.loadPublished(), null);
}));

test('publish increments revision and sync-after is idempotent', async () => withTemp((dir) => {
    let tick = 0;
    const store = makeStore(dir, () => new Date(1_720_923_600_000 + tick++ * 1000));
    const draft1 = store.saveDraft({ items: [item()] }, 0);
    const release1 = store.publish(draft1.draftVersion);
    assert.equal(release1.revision, 1);
    assert.match(release1.contentHash, /^[a-f0-9]{64}$/);
    assert.deepEqual(store.syncAfter(0), release1);
    assert.equal(store.syncAfter(1), null);

    const draft2 = store.saveDraft({ items: [item({ count: 9 })] }, draft1.draftVersion);
    assert.equal(store.syncAfter(1), null, 'saving a newer draft does not publish it');
    const release2 = store.publish(draft2.draftVersion);
    assert.equal(release2.revision, 2);
    assert.equal(store.syncAfter(1).what2eat.items[0].count, 9);
}));

test('revision survives a service restart and stale draft writes conflict', async () => withTemp((dir) => {
    const first = makeStore(dir);
    const draft1 = first.saveDraft({ items: [item()] }, 0);
    first.publish(draft1.draftVersion);

    const restarted = makeStore(dir);
    assert.equal(restarted.loadPublished().revision, 1);
    assert.throws(() => restarted.saveDraft({ items: [] }, 0), (error) => error instanceof What2EatError && error.code === 'DRAFT_CONFLICT' && error.status === 409);
    const draft2 = restarted.saveDraft({ items: [item({ count: 2 })] }, 1);
    assert.equal(restarted.publish(draft2.draftVersion).revision, 2);
}));

test('a corrupt draft fails closed and is not overwritten', async () => withTemp((dir) => {
    const draftPath = join(dir, 'what2eat.draft.json');
    writeFileSync(draftPath, '{broken', 'utf8');
    const store = makeStore(dir);
    assert.throws(() => store.saveDraft({ items: [item()] }, 0), (error) => error.code === 'CORRUPT_STORE' && error.status === 500);
    assert.equal(readFileSync(draftPath, 'utf8'), '{broken');
}));

test('a semantically corrupt published revision fails closed', async () => withTemp((dir) => {
    const publishedPath = join(dir, 'what2eat.published.json');
    writeFileSync(publishedPath, JSON.stringify({
        schemaVersion: 1,
        revision: '1',
        contentHash: 'invalid',
        what2eat: { items: [] },
    }), 'utf8');
    const store = makeStore(dir);
    assert.throws(() => store.loadPublished(), (error) => error.code === 'CORRUPT_STORE' && error.status === 500);
}));

test('legacy zero ratings migrate to the new 1.0 floor', async () => withTemp((dir) => {
    const store = makeStore(dir);
    const draft = store.saveDraft({ items: [item({ score: 85 })] }, 0);
    store.publish(draft.draftVersion);

    const publishedPath = join(dir, 'what2eat.published.json');
    const legacy = JSON.parse(readFileSync(publishedPath, 'utf8'));
    legacy.what2eat.items[0].score = 0;
    legacy.contentHash = crypto.createHash('sha256')
        .update(JSON.stringify(legacy.what2eat), 'utf8')
        .digest('hex');
    writeFileSync(publishedPath, `${JSON.stringify(legacy)}\n`, 'utf8');

    const migrated = makeStore(dir).loadPublished();
    assert.equal(migrated.what2eat.items[0].score, 10);
    assert.equal(migrated.contentHash, crypto.createHash('sha256')
        .update(JSON.stringify(migrated.what2eat), 'utf8')
        .digest('hex'));
}));

test('ack accepts the frozen applied/rejected wire and never regresses revision', async () => withTemp((dir) => {
    let tick = 0;
    const store = makeStore(dir, () => new Date(1_720_923_600_000 + tick++ * 1000));
    const draft1 = store.saveDraft({ items: [item()] }, 0);
    store.publish(draft1.draftVersion);
    const applied = store.acknowledge({ revision: 1, status: 'applied' });
    assert.equal(applied.status, 'applied');

    const draft2 = store.saveDraft({ items: [item({ count: 4 })] }, 1);
    store.publish(draft2.draftVersion);
    const rejected = store.acknowledge({ revision: 2, status: 'rejected', error: 'hash mismatch' });
    assert.equal(rejected.error, 'hash mismatch');
    assert.deepEqual(store.acknowledge({ revision: 1, status: 'applied' }), rejected);
    assert.throws(() => store.acknowledge({ revision: 3, status: 'applied' }), (error) => error.code === 'UNKNOWN_REVISION');
}));

test('routes return 204 without pending content and the frozen 200 envelope after publish', async () => withTemp((dir) => {
    const store = makeStore(dir);
    const routes = tn_create_what2eat_routes({ store });
    const emptyRes = makeRes();
    routes.sync({ query: { after: '0' } }, emptyRes);
    assert.equal(emptyRes.statusCode, 204);
    assert.equal(emptyRes.ended, true);

    const draft = store.saveDraft({ items: [item()] }, 0);
    store.publish(draft.draftVersion);
    const syncRes = makeRes();
    routes.sync({ query: { after: '0' } }, syncRes);
    assert.equal(syncRes.statusCode, 200);
    assert.deepEqual(Object.keys(syncRes.body), ['schemaVersion', 'revision', 'contentHash', 'what2eat']);
    assert.deepEqual(Object.keys(syncRes.body.what2eat.items[0]), ['id', 'name', 'count', 'price', 'score']);
    assert.equal(syncRes.headers['Cache-Control'], 'no-store');
}));

test('admin writes are loopback-only without a token and token-protected when configured', () => {
    const openGuard = tn_create_admin_guard({ token: '' });
    let nextCalled = false;
    openGuard({ ip: '::1', get: () => undefined }, makeRes(), () => { nextCalled = true; });
    assert.equal(nextCalled, true);

    const denied = makeRes();
    openGuard({ ip: '192.168.1.20', get: () => undefined }, denied, () => {});
    assert.equal(denied.statusCode, 401);

    const guarded = tn_create_admin_guard({ token: 'admin-secret' });
    const allowed = makeRes();
    guarded({ ip: '192.168.1.20', get: () => 'admin-secret' }, allowed, () => { nextCalled = 'token'; });
    assert.equal(nextCalled, 'token');
});

test('what2eat sync and ack routes accept board requests without device auth', async () => withTemp((dir) => {
    const store = makeStore(dir);
    const routes = tn_create_what2eat_routes({ store });
    const draft = store.saveDraft({ items: [item()] }, 0);
    store.publish(draft.draftVersion);

    const syncRes = makeRes();
    routes.sync({ query: { after: '0' } }, syncRes);
    assert.equal(syncRes.statusCode, 200);

    const ackRes = makeRes();
    routes.ack({ body: { revision: 1, status: 'applied' } }, ackRes);
    assert.equal(ackRes.statusCode, 200);
    assert.equal(ackRes.body.status, 'applied');
}));

test('management page does not interpolate persisted values through innerHTML', () => {
    const html = readFileSync(join(ROOT, 'web', 'index.html'), 'utf8');
    assert.equal(html.includes('innerHTML'), false);
    assert.match(html, /replaceChildren/);
    assert.match(html, /textContent/);
    assert.match(html, /\/api\/desknest/);
    assert.match(html, /id="homeMode"/);
    assert.match(html, /id="usage"/);
});
