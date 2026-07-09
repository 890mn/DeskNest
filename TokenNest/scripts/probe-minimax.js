// scripts/probe-minimax.js
// 一次性抓 MiniMax coding_plan/remains 真实响应，落档到 tmp/real-minimax.json
// 用法：
//   1. 在 https://platform.minimaxi.com 申请 API key
//   2. cp config/minimax.example.json config/minimax.json, 填入 apiKey
//   3. node scripts/probe-minimax.js
//   4. 看 tmp/real-minimax.json，对照 src/sources/minimax.js 的 tn_parse_remains

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '..');
const CFG = path.join(ROOT, 'config', 'minimax.json');
const OUT = path.join(ROOT, 'tmp', 'real-minimax.json');

const main = async () => {
    fs.mkdirSync(path.dirname(OUT), { recursive: true });
    if (!fs.existsSync(CFG)) {
        console.error(`[probe-minimax] config not found: ${CFG}`);
        process.exit(1);
    }
    const cfg = JSON.parse(fs.readFileSync(CFG, 'utf8'));
    const apiKey = cfg.apiKey;
    if (!apiKey || apiKey.length < 10) {
        console.error(`[probe-minimax] apiKey missing or too short in ${CFG}`);
        process.exit(1);
    }
    const res = await fetch('https://www.minimaxi.com/v1/api/openplatform/coding_plan/remains', {
        headers: { Authorization: `Bearer ${apiKey}` },
    });
    const text = await res.text();
    fs.writeFileSync(OUT, text, 'utf8');
    console.log(`[probe-minimax] HTTP ${res.status} -> ${OUT} (${text.length} bytes)`);
    if (!res.ok) {
        console.error(text.slice(0, 500));
        process.exit(1);
    }
    try {
        const obj = JSON.parse(text);
        console.log('[probe-minimax] top-level keys:', Object.keys(obj).join(', '));
        if (obj.base_resp) {
            console.log('[probe-minimax] base_resp:', JSON.stringify(obj.base_resp));
        }
        const arr = obj.model_remains ?? obj.modelRemains ?? [];
        console.log(`[probe-minimax] model_remains: ${arr.length} entries`);
        arr.forEach((m, i) => {
            console.log(`[probe-minimax] --- model[${i}] keys:`, Object.keys(m).join(', '));
            console.log(JSON.stringify(m, null, 2));
            const total = m.current_interval_total_count;
            const remain = m.current_interval_usage_count;
            const wkTotal = m.current_weekly_total_count;
            const wkRemain = m.current_weekly_usage_count;
            console.log(`[probe-minimax] sanity: 5h total=${total} remain=${remain}  weekly total=${wkTotal} remain=${wkRemain}`);
        });
    } catch (e) {
        console.error('[probe-minimax] response not JSON:', e.message);
    }
};

main().catch((e) => { console.error(e); process.exit(1); });
