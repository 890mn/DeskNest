import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

const HOME_MODES = new Set(['auto', 'ai', 'life', 'quiet']);
const THEMES = new Set(['dark', 'soft']);

export const tn_desknest_default = () => ({
    settings: {
        homeMode: 'auto',
        gestureConfirm: true,
        theme: 'dark',
        aiWarningPercent: 80,
    },
});

export const tn_desknest_normalize = (raw = {}) => {
    const fallback = tn_desknest_default().settings;
    const input = raw.settings ?? raw;
    const homeMode = HOME_MODES.has(input?.homeMode) ? input.homeMode : fallback.homeMode;
    const theme = THEMES.has(input?.theme) ? input.theme : fallback.theme;
    const warning = Number.parseInt(input?.aiWarningPercent, 10);
    return {
        settings: {
            homeMode,
            gestureConfirm: input?.gestureConfirm !== false,
            theme,
            aiWarningPercent: Number.isInteger(warning) ? Math.max(50, Math.min(100, warning)) : fallback.aiWarningPercent,
        },
    };
};

export const tn_desknest_load = (configPath) => {
    if (!fs.existsSync(configPath)) return tn_desknest_default();
    try {
        return tn_desknest_normalize(JSON.parse(fs.readFileSync(configPath, 'utf8')));
    } catch (error) {
        const failure = new Error(`DeskNest settings store is unreadable: ${error.message}`);
        failure.code = 'DESKNEST_STORE_CORRUPT';
        throw failure;
    }
};

export const tn_desknest_save = (configPath, raw) => {
    // Refuse to overwrite a corrupt existing file.
    if (fs.existsSync(configPath)) tn_desknest_load(configPath);
    const value = tn_desknest_normalize(raw);
    fs.mkdirSync(path.dirname(configPath), { recursive: true });
    const tempPath = `${configPath}.${process.pid}.${crypto.randomBytes(6).toString('hex')}.tmp`;
    try {
        fs.writeFileSync(tempPath, `${JSON.stringify(value, null, 2)}\n`, { encoding: 'utf8', flag: 'wx' });
        fs.renameSync(tempPath, configPath);
    } finally {
        if (fs.existsSync(tempPath)) fs.rmSync(tempPath, { force: true });
    }
    return value;
};

const sendError = (res, error) => res.status(500).json({
    error: error.code ?? 'DESKNEST_STORE_ERROR',
    message: error.message,
});

export const tn_create_desknest_routes = ({ configPath, getAggregate }) => ({
    get: (_req, res) => {
        try { return res.json({ ...tn_desknest_load(configPath), usage: getAggregate?.() ?? null }); }
        catch (error) { return sendError(res, error); }
    },
    put: (req, res) => {
        try { return res.json(tn_desknest_save(configPath, req.body)); }
        catch (error) { return sendError(res, error); }
    },
});
