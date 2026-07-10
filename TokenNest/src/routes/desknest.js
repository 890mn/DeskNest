import fs from 'node:fs';
import path from 'node:path';

const HOME_MODES = new Set(['auto', 'ai', 'life', 'quiet']);
const THEMES = new Set(['dark', 'soft']);

const cleanText = (value, fallback, max = 48) => {
    if (typeof value !== 'string') return fallback;
    const text = value.trim().replace(/[\r\n]/g, ' ');
    return text ? text.slice(0, max) : fallback;
};

const cleanItem = (item = {}) => ({
    name: cleanText(item.name, '未命名', 24),
    price: cleanText(String(item.price ?? ''), '0', 8).replace(/[^0-9.]/g, '').slice(0, 8) || '0',
    score: Math.max(0, Math.min(100, Number.parseInt(item.score, 10) || 0)),
    active: Boolean(item.active),
});

export const tn_desknest_default = () => ({
    menu: {
        today: '今天 番茄牛腩面',
        yesterday: '昨天 日式咖喱饭',
        items: [
            { name: '番茄牛腩面', price: '28', score: 84, active: false },
            { name: '葱油拌面', price: '12', score: 72, active: false },
            { name: '砂锅豆腐汤', price: '22', score: 81, active: true },
            { name: '韩式泡菜锅', price: '35', score: 78, active: false },
            { name: '麻辣香锅', price: '42', score: 87, active: false },
        ],
    },
    settings: {
        homeMode: 'auto',
        gestureConfirm: true,
        theme: 'dark',
        aiWarningPercent: 80,
    },
});

export const tn_desknest_normalize = (raw = {}) => {
    const fallback = tn_desknest_default();
    const inputItems = Array.isArray(raw.menu?.items) ? raw.menu.items.slice(0, 5) : fallback.menu.items;
    const items = inputItems.map(cleanItem);
    while (items.length < 5) items.push(cleanItem({ name: '待添加', price: '0', score: 0 }));
    const activeIndex = items.findIndex((item) => item.active);
    items.forEach((item, index) => { item.active = activeIndex >= 0 && index === activeIndex; });

    const homeMode = HOME_MODES.has(raw.settings?.homeMode) ? raw.settings.homeMode : fallback.settings.homeMode;
    const theme = THEMES.has(raw.settings?.theme) ? raw.settings.theme : fallback.settings.theme;
    return {
        menu: {
            today: cleanText(raw.menu?.today, fallback.menu.today),
            yesterday: cleanText(raw.menu?.yesterday, fallback.menu.yesterday),
            items,
        },
        settings: {
            homeMode,
            gestureConfirm: raw.settings?.gestureConfirm !== false,
            theme,
            aiWarningPercent: Math.max(50, Math.min(100, Number.parseInt(raw.settings?.aiWarningPercent, 10) || 80)),
        },
    };
};

export const tn_desknest_load = (configPath) => {
    try {
        if (!fs.existsSync(configPath)) return tn_desknest_default();
        return tn_desknest_normalize(JSON.parse(fs.readFileSync(configPath, 'utf8')));
    } catch {
        return tn_desknest_default();
    }
};

export const tn_desknest_save = (configPath, raw) => {
    const value = tn_desknest_normalize(raw);
    fs.mkdirSync(path.dirname(configPath), { recursive: true });
    const tempPath = `${configPath}.tmp`;
    fs.writeFileSync(tempPath, `${JSON.stringify(value, null, 2)}\n`, 'utf8');
    fs.renameSync(tempPath, configPath);
    return value;
};

export const tn_create_desknest_routes = ({ configPath, getAggregate }) => ({
    get: (_req, res) => res.json({ ...tn_desknest_load(configPath), usage: getAggregate?.() ?? null }),
    put: (req, res) => res.json(tn_desknest_save(configPath, req.body)),
});
