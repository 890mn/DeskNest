// src/http.js
// 带超时 + 指数退避的 fetch 封装
// 5xx / 网络错误重试 1/2/4/8/60s；4xx（除 408/429）不重试

import { tn_logger } from './logger.js';

const log = tn_logger('http');

const RETRY_DELAYS_MS = [1000, 2000, 4000, 8000, 60000];
const RETRYABLE_STATUS = new Set([408, 429, 500, 502, 503, 504]);

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

export const tn_http_fetch = async (url, opts = {}) => {
    const { timeoutMs = 15000, fetchImpl = globalThis.fetch, ...rest } = opts;
    let lastErr;
    for (let attempt = 0; attempt <= RETRY_DELAYS_MS.length; attempt++) {
        const controller = new AbortController();
        const timer = setTimeout(() => controller.abort(), timeoutMs);
        try {
            const res = await fetchImpl(url, { ...rest, signal: controller.signal });
            clearTimeout(timer);
            if (res.ok) return res;
            // 非 2xx
            if (RETRYABLE_STATUS.has(res.status) && attempt < RETRY_DELAYS_MS.length) {
                const delay = RETRY_DELAYS_MS[attempt];
                log.warn(`HTTP ${res.status} ${url} — retry in ${delay}ms (attempt ${attempt + 1})`);
                await sleep(delay);
                continue;
            }
            return res; // 不重试，调用方按 status 处理
        } catch (e) {
            clearTimeout(timer);
            lastErr = e;
            if (attempt < RETRY_DELAYS_MS.length) {
                const delay = RETRY_DELAYS_MS[attempt];
                log.warn(`network error ${url}: ${e.message} — retry in ${delay}ms (attempt ${attempt + 1})`);
                await sleep(delay);
                continue;
            }
            throw e;
        }
    }
    throw lastErr || new Error('tn_http_fetch: exhausted retries');
};
