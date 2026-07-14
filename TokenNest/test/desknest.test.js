import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import {
    tn_create_desknest_routes,
    tn_desknest_default,
    tn_desknest_load,
    tn_desknest_normalize,
    tn_desknest_save,
} from '../src/routes/desknest.js';

const withTemp = async (fn) => {
    const dir = mkdtempSync(join(tmpdir(), 'tn-desknest-'));
    try { await fn(dir); } finally { rmSync(dir, { recursive: true, force: true }); }
};

const makeRes = () => {
    let statusCode = 200;
    let body = null;
    return {
        status(code) { statusCode = code; return this; },
        json(value) { body = value; return this; },
        get statusCode() { return statusCode; },
        get body() { return body; },
    };
};

test('desknest default retains settings without legacy menu fields', () => {
    const config = tn_desknest_default();
    assert.equal(config.settings.homeMode, 'auto');
    assert.equal(config.settings.gestureConfirm, true);
    assert.equal('menu' in config, false);
});

test('desknest normalize keeps only supported settings', () => {
    const result = tn_desknest_normalize({
        menu: { today: 'must be ignored' },
        settings: { homeMode: 'life', gestureConfirm: false, theme: 'soft', aiWarningPercent: 91, ignored: 'x' },
    });
    assert.deepEqual(result, {
        settings: { homeMode: 'life', gestureConfirm: false, theme: 'soft', aiWarningPercent: 91 },
    });
    assert.equal('menu' in result, false);
});

test('desknest settings persist and a corrupt store fails closed', async () => withTemp((dir) => {
    const configPath = join(dir, 'desknest.json');
    tn_desknest_save(configPath, { settings: { homeMode: 'quiet', aiWarningPercent: 75 } });
    assert.equal(tn_desknest_load(configPath).settings.homeMode, 'quiet');
    writeFileSync(configPath, '{broken', 'utf8');
    assert.throws(() => tn_desknest_save(configPath, tn_desknest_default()), /store is unreadable/);
}));

test('/api/desknest returns settings plus usage and never what2eat/menu content', async () => withTemp((dir) => {
    const routes = tn_create_desknest_routes({
        configPath: join(dir, 'desknest.json'),
        getAggregate: () => ({ primaryPercent: 42, secondaryPercent: 18 }),
    });
    const getRes = makeRes();
    routes.get({}, getRes);
    assert.equal(getRes.statusCode, 200);
    assert.equal(getRes.body.usage.primaryPercent, 42);
    assert.equal('menu' in getRes.body, false);
    assert.equal('what2eat' in getRes.body, false);

    const putRes = makeRes();
    routes.put({ body: { settings: { theme: 'soft' }, menu: { active: true } } }, putRes);
    assert.equal(putRes.body.settings.theme, 'soft');
    assert.equal('menu' in putRes.body, false);
}));
