// src/routes/health.js
// GET /healthz — 上游 + 缓存健康度

export const tn_create_health_route = ({ sources }) => {
    return (req, res) => {
        const out = {};
        for (const src of sources) {
            const c = src.getCached();
            out[src.name] = {
                ok: c.ok !== false,
                ageSec: c.ageSec,
                stale: c.stale,
                error: c.error ?? null,
            };
        }
        const allOk = Object.values(out).every((v) => v.ok && !v.stale);
        res.status(allOk ? 200 : 503).json(out);
    };
};
