// src/routes/usage.js
// GET /api/usage — 5h/weekly 扩展视图，给 K10 未来 P1-D 消费

export const tn_create_usage_route = ({ getAggregate }) => {
    return (req, res) => {
        const snap = getAggregate();
        const body = {
            fetchedAt: snap.fetchedAt,
            primaryPercent: snap.primaryPercent,
            secondaryPercent: snap.secondaryPercent,
            chatgpt: {
                ok: snap.chatgpt.ok,
                stale: snap.chatgpt.stale,
                ageSec: snap.chatgpt.ageSec,
                accountId: snap.chatgpt.accountId,
                planType: snap.chatgpt.planType,
                primary: snap.chatgpt.primary,
                secondary: snap.chatgpt.secondary,
                extras: snap.chatgpt.extras,
                error: snap.chatgpt.error,
            },
            minimax: {
                ok: snap.minimax.ok,
                stale: snap.minimax.stale,
                ageSec: snap.minimax.ageSec,
                models: snap.minimax.models,
                error: snap.minimax.error,
            },
        };
        res.set('Content-Type', 'application/json; charset=utf-8');
        res.set('Cache-Control', 'no-store');
        res.json(body);
    };
};
