// src/logger.js
// TokenNest 轻量日志
// 命名 tn_logger；统一 [D][TOK] 前缀，匹配主项目 desknest 的 [D] 风格

const tn_ts = () => new Date().toISOString().replace('T', ' ').replace('Z', '');

const tn_fmt = (level, tag, msg, extra) => {
    const base = `[D][TOK] ${tn_ts()} ${level} [${tag}] ${msg}`;
    if (extra === undefined) return base;
    if (extra instanceof Error) return `${base} ${extra.message}`;
    if (typeof extra === 'object') {
        try { return `${base} ${JSON.stringify(extra)}`; } catch { return base; }
    }
    return `${base} ${extra}`;
};

export const tn_logger = (tag) => ({
    info: (msg, extra) => console.log(tn_fmt('INFO', tag, msg, extra)),
    warn: (msg, extra) => console.warn(tn_fmt('WARN', tag, msg, extra)),
    error: (msg, extra) => console.error(tn_fmt('ERR ', tag, msg, extra)),
    debug: (msg, extra) => {
        if (process.env.TN_DEBUG === '1') console.log(tn_fmt('DBG ', tag, msg, extra));
    },
});
