// src/cache/store.js
// 文件 JSON 缓存：每个 source 一个文件，原子写入。
// 注意：TokenNest/.gitignore 只忽略根目录 /cache/；本运行时模块必须跟踪。
// 文件结构：{ fetchedAt: ISO, data: <source-specific> }
// 读时校验 fetchedAt；解析失败视为未缓存

import fs from 'node:fs';
import path from 'node:path';
import { tn_logger } from '../logger.js';

const log = tn_logger('cache');

const tmp_path = (final) => `${final}.tmp`;

export const tn_cache_write = (cacheDir, name, data) => {
    const final = path.join(cacheDir, `${name}.json`);
    const tmp = tmp_path(final);
    const payload = JSON.stringify({
        fetchedAt: new Date().toISOString(),
        data,
    }, null, 2);
    try {
        fs.writeFileSync(tmp, payload, 'utf8');
        fs.renameSync(tmp, final); // 原子 rename
    } catch (e) {
        log.error(`cache write failed for ${name}: ${e.message}`);
    }
};

export const tn_cache_read = (cacheDir, name) => {
    const p = path.join(cacheDir, `${name}.json`);
    if (!fs.existsSync(p)) return null;
    try {
        const text = fs.readFileSync(p, 'utf8');
        const obj = JSON.parse(text);
        if (!obj || typeof obj.fetchedAt !== 'string') return null;
        return obj;
    } catch (e) {
        log.warn(`cache read failed for ${name}: ${e.message}`);
        return null;
    }
};

export const tn_cache_age_sec = (cacheEntry) => {
    if (!cacheEntry || !cacheEntry.fetchedAt) return Infinity;
    const t = Date.parse(cacheEntry.fetchedAt);
    if (Number.isNaN(t)) return Infinity;
    return Math.max(0, Math.floor((Date.now() - t) / 1000));
};
