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
import { tn_create_desknest_routes } from './routes/desknest.js';
import {
    tn_create_admin_guard,
    tn_create_what2eat_routes,
    tn_create_what2eat_store,
} from './routes/what2eat.js';

const log = tn_logger('main');
const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '..');

const main = async () => {
    const config = tn_load_config(path.join(ROOT, 'config', 'tokennest.yaml'), {
        envPath: path.join(ROOT, '.env'),
    });
    log.info(`config: host=${config.server.host} port=${config.server.port} cacheDir=${config.paths.cacheDir}`);

    const chatgptSrc = tn_create_chatgpt_source(config);
    const minimaxSrc = tn_create_minimax_source(config);
    const sources = [chatgptSrc, minimaxSrc];

    // 启动轮询（不 await —— 让它后台跑）
    for (const s of sources) s.start();
    log.info('sources started (background)');

    const app = express();
    app.use(express.json({ limit: '32kb' }));
    app.use((req, _res, next) => {
        log.debug(`${req.method} ${req.path}`);
        next();
    });

    const getAggregate = () => tn_aggregate({
        cacheDir: config.paths.cacheDir,
        staleThresholdSec: config.staleThresholdSec,
    });
    const what2eatStore = tn_create_what2eat_store({
        draftPath: path.join(ROOT, 'config', 'what2eat.draft.json'),
        publishedPath: path.join(ROOT, 'config', 'what2eat.published.json'),
        ackPath: path.join(ROOT, 'config', 'what2eat.ack.json'),
    });
    const what2eatRoutes = tn_create_what2eat_routes({ store: what2eatStore });
    const deskRoutes = tn_create_desknest_routes({
        configPath: path.join(ROOT, 'config', 'desknest.json'),
        getAggregate,
    });
    const requireAdmin = tn_create_admin_guard();

    app.get('/status.json', tn_create_status_route({ getAggregate, staleThresholdSec: config.staleThresholdSec, sources }));
    app.get('/api/usage', tn_create_usage_route({ getAggregate }));
    app.get('/healthz', tn_create_health_route({ sources, staleThresholdSec: config.staleThresholdSec }));
    app.get('/api/desknest', requireAdmin, deskRoutes.get);
    app.put('/api/desknest', requireAdmin, deskRoutes.put);
    app.get('/api/what2eat/draft', requireAdmin, what2eatRoutes.getDraft);
    app.put('/api/what2eat/draft', requireAdmin, what2eatRoutes.putDraft);
    app.post('/api/what2eat/publish', requireAdmin, what2eatRoutes.publish);
    // what2eat sync/ack intentionally use no device token. The service is a
    // local/LAN control plane; admin writes remain protected above.
    app.get('/api/what2eat/sync', what2eatRoutes.sync);
    app.post('/api/what2eat/ack', what2eatRoutes.ack);
    app.use('/desk', express.static(path.join(ROOT, 'web')));
    app.get('/', (_req, res) => res.json({
        service: 'TokenNest',
        version: '0.1.0',
        endpoints: [
            '/status.json',
            '/api/usage',
            '/healthz',
            '/api/desknest',
            '/api/what2eat/draft',
            '/api/what2eat/publish',
            '/api/what2eat/sync',
            '/api/what2eat/ack',
            '/desk',
        ],
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
