// scripts/probe-chatgpt.js
// 一次性抓 ChatGPT wham/usage 真实响应，落档到 tmp/real-chatgpt.json
// 用法：
//   1. 先用 codex CLI 登录一次：npx -y @openai/codex auth login
//   2. node scripts/probe-chatgpt.js
//   3. 看 tmp/real-chatgpt.json，对照 src/sources/chatgpt.js 的 tn_parse_wham

import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '..');
const AUTH_FILE = process.env.TN_CODEX_AUTH_FILE || path.join(os.homedir(), '.codex', 'auth.json');
const OUT = path.join(ROOT, 'tmp', 'real-chatgpt.json');

const main = async () => {
    fs.mkdirSync(path.dirname(OUT), { recursive: true });
    if (!fs.existsSync(AUTH_FILE)) {
        console.error(`[probe-chatgpt] auth file not found: ${AUTH_FILE}`);
        console.error('  -> run: npx -y @openai/codex auth login');
        process.exit(1);
    }
    const auth = JSON.parse(fs.readFileSync(AUTH_FILE, 'utf8'));
    const accessToken = auth?.tokens?.access_token;
    if (!accessToken) {
        console.error(`[probe-chatgpt] no access_token in ${AUTH_FILE}`);
        process.exit(1);
    }

    const res = await fetch('https://chatgpt.com/backend-api/wham/usage', {
        headers: { Authorization: `Bearer ${accessToken}` },
    });
    const text = await res.text();
    fs.writeFileSync(OUT, text, 'utf8');
    console.log(`[probe-chatgpt] HTTP ${res.status} -> ${OUT} (${text.length} bytes)`);
    if (!res.ok) {
        console.error(text.slice(0, 500));
        process.exit(1);
    }
    try {
        const obj = JSON.parse(text);
        const rl = obj.rate_limit ?? obj.rateLimit ?? {};
        console.log('[probe-chatgpt] top-level keys:', Object.keys(obj).join(', '));
        console.log('[probe-chatgpt] rate_limit keys:', Object.keys(rl).join(', '));
        if (rl.primary_window) {
            console.log('[probe-chatgpt] primary_window:', JSON.stringify(rl.primary_window, null, 2));
        }
        if (rl.secondary_window) {
            console.log('[probe-chatgpt] secondary_window:', JSON.stringify(rl.secondary_window, null, 2));
        }
        if (Array.isArray(rl.additional_rate_limits)) {
            console.log(`[probe-chatgpt] additional_rate_limits: ${rl.additional_rate_limits.length} entries`);
            console.log(JSON.stringify(rl.additional_rate_limits[0], null, 2));
        }
    } catch (e) {
        console.error('[probe-chatgpt] response not JSON:', e.message);
    }
};

main().catch((e) => { console.error(e); process.exit(1); });
