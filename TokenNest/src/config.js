// src/config.js
// TokenNest 配置加载
// 优先级：已存在的环境变量 (TN_*) > .env > config/tokennest.yaml > 默认值
// Node 18 是受支持基线，因此在这里加载 .env，不依赖 Node 20 的 --env-file。

import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { tn_logger } from './logger.js';

const log = tn_logger('cfg');

export const tn_load_env_file = (envPath, target = process.env) => {
    if (!envPath || !fs.existsSync(envPath)) return { loaded: false, keys: [] };

    const loadedKeys = [];
    const text = fs.readFileSync(envPath, 'utf8').replace(/^\uFEFF/, '');
    for (const raw of text.split(/\r?\n/)) {
        const line = raw.trim();
        if (!line || line.startsWith('#')) continue;
        const match = line.match(/^(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$/);
        if (!match) continue;

        const key = match[1];
        let value = match[2].trim();
        if ((value.startsWith('"') && value.endsWith('"')) ||
            (value.startsWith("'") && value.endsWith("'"))) {
            value = value.slice(1, -1);
        } else {
            value = value.replace(/\s+#.*$/, '').trim();
        }

        // Shell/NSSM 显式注入的变量优先，不让 .env 静默覆盖运行环境。
        if (target[key] === undefined) {
            target[key] = value;
            loadedKeys.push(key);
        }
    }
    return { loaded: true, keys: loadedKeys };
};

const DEFAULTS = {
    server: { host: '0.0.0.0', port: 8787 },
    poll: {
        chatgptIntervalSec: 60,
        minimaxIntervalSec: 60,
        requestTimeoutSec: 15,
    },
    paths: {
        cacheDir: './cache',
        codexAuthFile: '~/.codex/auth.json',
        minimaxConfig: './config/minimax.json',
    },
    staleThresholdSec: 300,
};

// 极简 YAML 解析 —— 我们的 example 形状固定（2 层 key:value + 注释），
// 不引外部依赖。如果未来字段变多再换 yaml 包。
const tn_parse_simple_yaml = (text) => {
    const root = {};
    const parentStack = [{ node: root, indent: -1 }];

    const push = (node, indent) => parentStack.push({ node, indent });
    const popUntil = (indent) => {
        while (parentStack.length > 1 && parentStack[parentStack.length - 1].indent >= indent) {
            parentStack.pop();
        }
    };

    const lines = text.split(/\r?\n/);
    for (const raw of lines) {
        const line = raw.replace(/#.*$/, '').replace(/\s+$/, '');
        if (!line.trim()) continue;
        const indent = line.match(/^\s*/)[0].length;
        const stripped = line.trim();
        if (stripped.includes(':')) {
            const [k, ...rest] = stripped.split(':');
            const v = rest.join(':').trim();
            // Pop back to the node whose indent is less than current indent
            popUntil(indent);
            const parent = parentStack[parentStack.length - 1].node;
            if (v === '') {
                // Section heading — create empty object, push onto stack
                const section = {};
                parent[k.trim()] = section;
                push(section, indent);
            } else {
                parent[k.trim()] = tn_coerce(v);
            }
        }
    }
    return root;
};

const tn_coerce = (v) => {
    if (v === 'true') return true;
    if (v === 'false') return false;
    if (v === 'null' || v === '') return null;
    if (/^-?\d+$/.test(v)) return parseInt(v, 10);
    if (/^-?\d+\.\d+$/.test(v)) return parseFloat(v);
    return v;
};

const tn_deep_merge = (base, over) => {
    if (over === undefined || over === null) return base;
    if (typeof base !== 'object' || base === null) return over;
    if (typeof over !== 'object') return over;
    const out = Array.isArray(base) ? [...base] : { ...base };
    for (const k of Object.keys(over)) {
        out[k] = tn_deep_merge(base[k], over[k]);
    }
    return out;
};

const tn_expand_home = (p) => p.startsWith('~/') ? path.resolve(os.homedir(), p.slice(2)) : p;

export const tn_load_config = (yamlPath, { envPath } = {}) => {
    const envResult = tn_load_env_file(envPath);
    if (envResult.loaded) log.info(`loaded env file: ${envPath} (${envResult.keys.length} values)`);
    let yamlCfg = {};
    if (yamlPath && fs.existsSync(yamlPath)) {
        try {
            const text = fs.readFileSync(yamlPath, 'utf8');
            yamlCfg = tn_parse_simple_yaml(text);
            log.info(`loaded yaml: ${yamlPath}`);
        } catch (e) {
            log.warn(`failed to read yaml, falling back to defaults: ${e.message}`);
        }
    } else if (yamlPath) {
        log.info(`yaml not found, using defaults: ${yamlPath}`);
    }

    const env = {
        server: {
            host: process.env.TN_HOST,
            port: process.env.TN_PORT ? parseInt(process.env.TN_PORT, 10) : undefined,
        },
        poll: {
            chatgptIntervalSec: process.env.TN_POLL_CHATGPT_SEC ? parseInt(process.env.TN_POLL_CHATGPT_SEC, 10) : undefined,
            minimaxIntervalSec: process.env.TN_POLL_MINIMAX_SEC ? parseInt(process.env.TN_POLL_MINIMAX_SEC, 10) : undefined,
            requestTimeoutSec: process.env.TN_REQUEST_TIMEOUT_SEC ? parseInt(process.env.TN_REQUEST_TIMEOUT_SEC, 10) : undefined,
        },
        paths: {
            cacheDir: process.env.TN_CACHE_DIR,
            codexAuthFile: process.env.TN_CODEX_AUTH_FILE,
            minimaxConfig: process.env.TN_MINIMAX_CONFIG,
        },
        staleThresholdSec: process.env.TN_STALE_THRESHOLD_SEC ? parseInt(process.env.TN_STALE_THRESHOLD_SEC, 10) : undefined,
    };

    const merged = tn_deep_merge(DEFAULTS, tn_deep_merge(yamlCfg, env));

    // 解析 ~ 路径
    merged.paths.cacheDir = path.resolve(merged.paths.cacheDir);
    merged.paths.codexAuthFile = tn_expand_home(merged.paths.codexAuthFile);
    merged.paths.minimaxConfig = path.resolve(merged.paths.minimaxConfig);

    // 确保 cacheDir 存在
    if (!fs.existsSync(merged.paths.cacheDir)) {
        fs.mkdirSync(merged.paths.cacheDir, { recursive: true });
        log.info(`created cacheDir: ${merged.paths.cacheDir}`);
    }

    return merged;
};
