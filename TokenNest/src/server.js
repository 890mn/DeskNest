// src/server.js
// TokenNest 入口
// 启动顺序：load config → start sources（先各拉一次让 cache 暖）→ mount routes → listen
// 优雅关闭：SIGINT 停轮询

import express from 'express';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { tn_load_config } from './config.js';
import { tn_logger } from './logger.js';
import { tn_create_chatgpt_source } from './sources/chatgpt.js';
import { tn_create_minimax_source } from './sources/minimax.js';
import { tn_aggregate } from './sources/aggregator.js';
import { tn_create_status_route } from './routes/status.js';
import { tn_create_usage_route } from './routes/usage.js';
import { tn_create_health_route } from './routes/health.js';

const log = tn_logger('main');
const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '..');

const main = async () => {
    const config = tn_load_config(path.join(ROOT, 'config', 'tokennest.yaml'));
    log.info(`config: host=${config.server.host} port=${config.server.port} cacheDir=${config.paths.cacheDir}`);

    const chatgptSrc = tn_create_chatgpt_source(config);
    const minimaxSrc = tn_create_minimax_source(config);
    const sources = [chatgptSrc, minimaxSrc];

    // 启动轮询（不 await —— 让它后台跑）
    for (const s of sources) s.start();
    log.info('sources started (background)');

    const app = express();
    app.use((req, _res, next) => {
        log.debug(`${req.method} ${req.path}`);
        next();
    });
    app.use((_req, res, next) => {
        res.set('Access-Control-Allow-Origin', '*');
        next();
    });

    const getAggregate = () => tn_aggregate({
        cacheDir: config.paths.cacheDir,
        staleThresholdSec: config.staleThresholdSec,
    });

    app.get('/status.json', tn_create_status_route({ getAggregate, staleThresholdSec: config.staleThresholdSec, sources }));
    app.get('/api/usage', tn_create_usage_route({ getAggregate }));
    app.get('/healthz', tn_create_health_route({ sources }));
    app.get('/', (_req, res) => res.json({
        service: 'TokenNest',
        version: '0.1.0',
        endpoints: ['/status.json', '/api/usage', '/healthz'],
    }));

    const server = app.listen(config.server.port, config.server.host, () => {
        log.info(`listening on http://${config.server.host}:${config.server.port}`);
    });

    const shutdown = (sig) => {
        log.info(`received ${sig}, shutting down`);
        for (const s of sources) s.stop();
        server.close(() => process.exit(0));
        setTimeout(() => process.exit(1), 5000).unref();
    };
    process.on('SIGINT', () => shutdown('SIGINT'));
    process.on('SIGTERM', () => shutdown('SIGTERM'));
};

main().catch((e) => {
    log.error(`fatal: ${e.message}`);
    log.error(e.stack);
    process.exit(1);
});
