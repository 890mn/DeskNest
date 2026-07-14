import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { tn_load_env_file } from '../src/config.js';

test('env loader supports Node 18 and preserves explicitly injected values', () => {
    const dir = mkdtempSync(join(tmpdir(), 'tokennest-env-'));
    const envPath = join(dir, '.env');
    writeFileSync(envPath, [
        '# local secrets',
        'TN_WHAT2EAT_ADMIN_TOKEN=admin-from-file # comment',
        'export TN_PORT=9000',
        '',
    ].join('\n'), 'utf8');

    const target = { TN_PORT: '8787' };
    const result = tn_load_env_file(envPath, target);

    assert.equal(result.loaded, true);
    assert.deepEqual(result.keys.sort(), ['TN_WHAT2EAT_ADMIN_TOKEN']);
    assert.equal(target.TN_WHAT2EAT_ADMIN_TOKEN, 'admin-from-file');
    assert.equal(target.TN_PORT, '8787');
});
