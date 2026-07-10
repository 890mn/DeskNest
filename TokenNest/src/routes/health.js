// src/routes/health.js
// GET /healthz — 上游 + 缓存健康度

export const tn_create_health_route = ({ sources, staleThresholdSec = 300 }) => {
    return (req, res) => {
        const out = {};
        for (const src of sources) {
            const c = src.getCached();
            const ageSec = Number.isFinite(c.ageSec) ? c.ageSec : Infinity;
            const stale = c.stale === true || ageSec > staleThresholdSec;
            out[src.name] = {
                ok: c.ok !== false,
                ageSec,
                stale,
                error: c.error ?? null,
            };
        }
        const allOk = Object.values(out).every((v) => v.ok && !v.stale);
        res.status(allOk ? 200 : 503).json(out);
    };
};
