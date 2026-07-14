import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

export const WHAT2EAT_SCHEMA_VERSION = 1;
export const WHAT2EAT_MAX_ITEMS = 15;
// Wire format keeps one decimal digit as an integer: 85 means 8.5.
export const WHAT2EAT_SCORE_MIN_TENTHS = 10;
export const WHAT2EAT_SCORE_MAX_TENTHS = 100;
export const WHAT2EAT_SCORE_STEP_TENTHS = 5;

const LIMITS = Object.freeze({
    idBytes: 23,
    nameBytes: 31,
    priceBytes: 9,
    ackErrorBytes: 160,
});

export class What2EatError extends Error {
    constructor(code, message, status = 400) {
        super(message);
        this.name = 'What2EatError';
        this.code = code;
        this.status = status;
    }
}

const byteLength = (value) => Buffer.byteLength(value, 'utf8');

const boundedText = (value, field, maxBytes, { required = false, pattern = null } = {}) => {
    if (typeof value !== 'string') {
        if (!required && (value === undefined || value === null)) return '';
        throw new What2EatError('INVALID_FIELD', `${field} must be a string`);
    }
    const text = value.trim();
    if (required && !text) throw new What2EatError('INVALID_FIELD', `${field} is required`);
    if (/[\u0000-\u001f\u007f]/u.test(text)) {
        throw new What2EatError('INVALID_FIELD', `${field} must not contain control characters`);
    }
    if (byteLength(text) > maxBytes) {
        throw new What2EatError('FIELD_TOO_LONG', `${field} exceeds ${maxBytes} UTF-8 bytes`);
    }
    if (pattern && text && !pattern.test(text)) {
        throw new What2EatError('INVALID_FIELD', `${field} has an invalid format`);
    }
    return text;
};

const boundedInteger = (value, field, min, max) => {
    const parsed = typeof value === 'number' ? value : Number(value);
    if (!Number.isInteger(parsed) || parsed < min || parsed > max) {
        throw new What2EatError('INVALID_FIELD', `${field} must be an integer from ${min} to ${max}`);
    }
    return parsed;
};

const normalizeScore = (value, field, { allowLegacyZero = false, mapLegacyZero = false } = {}) => {
    const parsedValue = typeof value === 'number' ? value : Number(value);
    if (allowLegacyZero && parsedValue === 0) {
        return mapLegacyZero ? WHAT2EAT_SCORE_MIN_TENTHS : 0;
    }
    const parsed = boundedInteger(
        parsedValue,
        field,
        WHAT2EAT_SCORE_MIN_TENTHS,
        WHAT2EAT_SCORE_MAX_TENTHS,
    );
    if (parsed % WHAT2EAT_SCORE_STEP_TENTHS !== 0) {
        throw new What2EatError('INVALID_FIELD', `${field} must use 0.5 increments from 1 to 10`);
    }
    return parsed;
};

const normalizePrice = (value, field) => {
    const price = boundedText(String(value ?? ''), field, LIMITS.priceBytes, { required: true });
    if (!/^\d{1,6}(?:\.\d{1,2})?$/.test(price)) {
        throw new What2EatError('INVALID_FIELD', `${field} must be a non-negative decimal with at most 2 places`);
    }
    return price;
};

// Must stay aligned with the ranges owned by the firmware's
// lv_font_16_dynamic CNFontNest profile. Rejecting unsupported codepoints at
// publish-input time avoids accepting content that the flashed board cannot
// render. Quotes/backslashes remain excluded because the fixed parser is
// deliberately fail-closed for JSON escapes.
const WHAT2EAT_NAME_PATTERN = /^(?!.*["\\])[\x20-\x7E\u00B0\u00B7\u2000-\u206F\u3000-\u303F\u3040-\u30FF\u4E00-\u9FFF\uFF00-\uFFEF]+$/u;

export const tn_what2eat_empty = () => ({ items: [] });

export const tn_what2eat_normalize = (raw = {}, scoreOptions = {}) => {
    const source = raw.what2eat ?? raw;
    if (!source || typeof source !== 'object' || !Array.isArray(source.items)) {
        throw new What2EatError('INVALID_PAYLOAD', 'what2eat.items must be an array');
    }
    if (source.items.length > WHAT2EAT_MAX_ITEMS) {
        throw new What2EatError('TOO_MANY_ITEMS', `what2eat supports at most ${WHAT2EAT_MAX_ITEMS} items`);
    }

    const ids = new Set();
    const items = source.items.map((item, index) => {
        if (!item || typeof item !== 'object') {
            throw new What2EatError('INVALID_ITEM', `what2eat.items[${index}] must be an object`);
        }
        const prefix = `what2eat.items[${index}]`;
        const id = boundedText(item.id, `${prefix}.id`, LIMITS.idBytes, {
            required: true,
            pattern: /^[A-Za-z0-9_-]+$/,
        });
        if (ids.has(id)) throw new What2EatError('DUPLICATE_ID', `${prefix}.id must be unique`);
        ids.add(id);
        return {
            id,
            name: boundedText(item.name, `${prefix}.name`, LIMITS.nameBytes, {
                required: true,
                pattern: WHAT2EAT_NAME_PATTERN,
            }),
            count: boundedInteger(item.count, `${prefix}.count`, 0, 65535),
            price: normalizePrice(item.price, `${prefix}.price`),
            score: normalizeScore(item.score, `${prefix}.score`, scoreOptions),
        };
    });
    return { items };
};

const jsonHash = (what2eat) => crypto
    .createHash('sha256')
    .update(JSON.stringify(what2eat), 'utf8')
    .digest('hex');

const readJson = (filePath, label) => {
    if (!fs.existsSync(filePath)) return null;
    try {
        return JSON.parse(fs.readFileSync(filePath, 'utf8'));
    } catch (error) {
        throw new What2EatError('CORRUPT_STORE', `${label} store is unreadable: ${error.message}`, 500);
    }
};

const atomicWriteJson = (filePath, value) => {
    fs.mkdirSync(path.dirname(filePath), { recursive: true });
    const tempPath = `${filePath}.${process.pid}.${crypto.randomBytes(6).toString('hex')}.tmp`;
    try {
        fs.writeFileSync(tempPath, `${JSON.stringify(value, null, 2)}\n`, { encoding: 'utf8', flag: 'wx' });
        fs.renameSync(tempPath, filePath);
    } finally {
        if (fs.existsSync(tempPath)) fs.rmSync(tempPath, { force: true });
    }
};

const assertSchemaVersion = (value, label) => {
    if (value?.schemaVersion !== WHAT2EAT_SCHEMA_VERSION) {
        throw new What2EatError('UNSUPPORTED_SCHEMA', `${label} schemaVersion is unsupported`, 500);
    }
};

const assertStoredInteger = (value, field, min, max) => {
    if (!Number.isInteger(value) || value < min || value > max) {
        throw new What2EatError('CORRUPT_STORE', `${field} is invalid`, 500);
    }
};

const normalizeStoredContent = (value, label, scoreOptions = {}) => {
    try {
        return tn_what2eat_normalize(value, scoreOptions);
    } catch (error) {
        throw new What2EatError('CORRUPT_STORE', `${label} content is invalid: ${error.message}`, 500);
    }
};

export const tn_create_what2eat_store = ({ draftPath, publishedPath, ackPath, now = () => new Date() }) => {
    const loadDraft = () => {
        const stored = readJson(draftPath, 'draft');
        if (!stored) {
            return {
                schemaVersion: WHAT2EAT_SCHEMA_VERSION,
                draftVersion: 0,
                updatedAt: null,
                what2eat: tn_what2eat_empty(),
            };
        }
        assertSchemaVersion(stored, 'draft');
        assertStoredInteger(stored.draftVersion, 'draft.draftVersion', 0, 0xffffffff);
        return {
            ...stored,
            // Pre-rating drafts may contain 0; display them as the new 1.0 floor
            // without allowing new writes to use the legacy value.
            what2eat: normalizeStoredContent(stored.what2eat, 'draft', {
                allowLegacyZero: true,
                mapLegacyZero: true,
            }),
        };
    };

    const loadPublished = () => {
        const stored = readJson(publishedPath, 'published');
        if (!stored) return null;
        assertSchemaVersion(stored, 'published');
        assertStoredInteger(stored.revision, 'published.revision', 1, 0xffffffff);
        const hasLegacyZero = Array.isArray(stored.what2eat?.items) &&
            stored.what2eat.items.some((item) => Number(item?.score) === 0);
        if (hasLegacyZero) {
            const legacy = normalizeStoredContent(stored.what2eat, 'published', {
                allowLegacyZero: true,
            });
            if (stored.contentHash !== jsonHash(legacy)) {
                throw new What2EatError('HASH_MISMATCH', 'published contentHash does not match payload', 500);
            }
            const migratedWhat2Eat = normalizeStoredContent(stored.what2eat, 'published', {
                allowLegacyZero: true,
                mapLegacyZero: true,
            });
            const migrated = {
                ...stored,
                contentHash: jsonHash(migratedWhat2Eat),
                what2eat: migratedWhat2Eat,
            };
            atomicWriteJson(publishedPath, migrated);
            return migrated;
        }
        const what2eat = normalizeStoredContent(stored.what2eat, 'published');
        const expectedHash = jsonHash(what2eat);
        if (stored.contentHash !== expectedHash) {
            throw new What2EatError('HASH_MISMATCH', 'published contentHash does not match payload', 500);
        }
        return { ...stored, what2eat };
    };

    const loadAck = () => {
        const stored = readJson(ackPath, 'ack');
        if (!stored) return null;
        assertSchemaVersion(stored, 'ack');
        assertStoredInteger(stored.revision, 'ack.revision', 1, 0xffffffff);
        if (stored.status !== 'applied' && stored.status !== 'rejected') {
            throw new What2EatError('CORRUPT_STORE', 'ack.status is invalid', 500);
        }
        return stored;
    };

    const saveDraft = (raw, expectedVersion) => {
        const current = loadDraft();
        if (!Number.isInteger(expectedVersion) || expectedVersion !== current.draftVersion) {
            throw new What2EatError('DRAFT_CONFLICT', `draftVersion must equal ${current.draftVersion}`, 409);
        }
        const next = {
            schemaVersion: WHAT2EAT_SCHEMA_VERSION,
            draftVersion: current.draftVersion + 1,
            updatedAt: now().toISOString(),
            what2eat: tn_what2eat_normalize(raw),
        };
        atomicWriteJson(draftPath, next);
        return next;
    };

    const publish = (expectedDraftVersion) => {
        const draft = loadDraft();
        if (!Number.isInteger(expectedDraftVersion) || expectedDraftVersion !== draft.draftVersion) {
            throw new What2EatError('DRAFT_CONFLICT', `draftVersion must equal ${draft.draftVersion}`, 409);
        }
        if (draft.what2eat.items.length === 0) {
            throw new What2EatError('EMPTY_DRAFT', 'add at least one what2eat item before publishing');
        }
        const current = loadPublished();
        if ((current?.revision ?? 0) >= 0xffffffff) {
            throw new What2EatError('REVISION_EXHAUSTED', 'published revision reached the uint32 limit', 500);
        }
        const next = {
            schemaVersion: WHAT2EAT_SCHEMA_VERSION,
            revision: (current?.revision ?? 0) + 1,
            publishedAt: now().toISOString(),
            contentHash: jsonHash(draft.what2eat),
            what2eat: draft.what2eat,
        };
        atomicWriteJson(publishedPath, next);
        return next;
    };

    const syncAfter = (afterRevision) => {
        if (!Number.isInteger(afterRevision) || afterRevision < 0 || afterRevision > 0xffffffff) {
            throw new What2EatError('INVALID_REVISION', 'after must be an unsigned 32-bit integer');
        }
        const published = loadPublished();
        if (!published || published.revision <= afterRevision) return null;
        return published;
    };

    const acknowledge = (raw = {}) => {
        const published = loadPublished();
        if (!published) throw new What2EatError('NOT_PUBLISHED', 'there is no published revision to acknowledge', 409);
        const revision = boundedInteger(raw.revision, 'revision', 1, 0xffffffff);
        if (revision > published.revision) {
            throw new What2EatError('UNKNOWN_REVISION', 'ack revision is newer than the published revision', 409);
        }
        if (raw.status !== 'applied' && raw.status !== 'rejected') {
            throw new What2EatError('INVALID_ACK', "status must be 'applied' or 'rejected'");
        }
        const error = raw.status === 'rejected'
            ? boundedText(raw.error, 'error', LIMITS.ackErrorBytes, { required: true })
            : '';
        const current = loadAck();
        if (current && current.revision > revision) return current;
        const next = {
            schemaVersion: WHAT2EAT_SCHEMA_VERSION,
            revision,
            status: raw.status,
            ...(error ? { error } : {}),
            acknowledgedAt: now().toISOString(),
        };
        atomicWriteJson(ackPath, next);
        return next;
    };

    const getAdminState = () => {
        const draft = loadDraft();
        const published = loadPublished();
        const lastAck = loadAck();
        return {
            ...draft,
            published: published ? {
                revision: published.revision,
                publishedAt: published.publishedAt,
                contentHash: published.contentHash,
            } : null,
            lastAck,
        };
    };

    return { loadDraft, loadPublished, loadAck, saveDraft, publish, syncAfter, acknowledge, getAdminState };
};

const tokenMatches = (provided, expected) => {
    if (!provided || !expected) return false;
    const a = Buffer.from(String(provided), 'utf8');
    const b = Buffer.from(String(expected), 'utf8');
    return a.length === b.length && crypto.timingSafeEqual(a, b);
};

export const tn_is_loopback = (address = '') => {
    const normalized = String(address).toLowerCase();
    return normalized === '127.0.0.1' || normalized === '::1' || normalized === '::ffff:127.0.0.1';
};

export const tn_create_admin_guard = ({ token = process.env.TN_WHAT2EAT_ADMIN_TOKEN } = {}) => (req, res, next) => {
    if (token ? tokenMatches(req.get?.('X-TokenNest-Admin-Token'), token) : tn_is_loopback(req.ip ?? req.socket?.remoteAddress)) {
        return next();
    }
    return res.status(401).json({ error: 'ADMIN_UNAUTHORIZED' });
};

const sendRouteError = (res, error) => {
    if (error instanceof What2EatError) {
        return res.status(error.status).json({ error: error.code, message: error.message });
    }
    return res.status(500).json({ error: 'INTERNAL_ERROR' });
};

export const tn_create_what2eat_routes = ({ store }) => ({
    getDraft: (_req, res) => {
        try { return res.json(store.getAdminState()); } catch (error) { return sendRouteError(res, error); }
    },
    putDraft: (req, res) => {
        try {
            return res.json(store.saveDraft(req.body?.what2eat, req.body?.draftVersion));
        } catch (error) { return sendRouteError(res, error); }
    },
    publish: (req, res) => {
        try { return res.status(201).json(store.publish(req.body?.draftVersion)); } catch (error) { return sendRouteError(res, error); }
    },
    sync: (req, res) => {
        try {
            if (typeof req.query?.after !== 'string' || !/^\d+$/.test(req.query.after)) {
                throw new What2EatError('INVALID_REVISION', 'after query parameter is required');
            }
            const envelope = store.syncAfter(Number(req.query.after));
            if (!envelope) return res.status(204).end();
            res.set('Cache-Control', 'no-store');
            return res.json({
                schemaVersion: envelope.schemaVersion,
                revision: envelope.revision,
                contentHash: envelope.contentHash,
                what2eat: envelope.what2eat,
            });
        } catch (error) { return sendRouteError(res, error); }
    },
    ack: (req, res) => {
        try { return res.json(store.acknowledge(req.body)); } catch (error) { return sendRouteError(res, error); }
    },
});
