import test from 'node:test';
import assert from 'node:assert/strict';
import { tn_desknest_default, tn_desknest_normalize } from '../src/routes/desknest.js';

test('desknest default contains an editable daily menu and board settings', () => {
    const config = tn_desknest_default();
    assert.equal(config.menu.items.length, 5);
    assert.equal(config.settings.homeMode, 'auto');
    assert.equal(config.settings.gestureConfirm, true);
});

test('desknest normalize rejects extra menu rows and keeps supported settings', () => {
    const result = tn_desknest_normalize({
        menu: { today: '今天 测试', yesterday: '昨天 测试', items: Array(8).fill({ name: '饭', price: '20', score: 80 }) },
        settings: { homeMode: 'life', gestureConfirm: false, theme: 'soft', ignored: 'x' },
    });
    assert.equal(result.menu.items.length, 5);
    assert.equal(result.settings.homeMode, 'life');
    assert.equal(result.settings.gestureConfirm, false);
    assert.equal(result.settings.theme, 'soft');
    assert.equal('ignored' in result.settings, false);
});
