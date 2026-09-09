'use strict';

function normalizeSmtpConfig(input = {}, credentials = {}) {
    if (!input || typeof input !== 'object' || Array.isArray(input)) {
        throw new Error('Invalid email SMTP configuration');
    }
    const config = {
        host: input.host === undefined ? 'smtp.qq.com' : input.host,
        port: input.port === undefined ? 465 : input.port,
        secure: input.secure === undefined ? true : input.secure,
        auth: input.auth === undefined ? 'login' : input.auth,
        deadlineMs: input.deadlineMs === undefined ? 10000 : input.deadlineMs
    };
    if (typeof config.host !== 'string' || !config.host || /[\s/\\@]/.test(config.host) ||
        !Number.isInteger(config.port) || config.port < 1 || config.port > 65535 ||
        typeof config.secure !== 'boolean' || !['login', 'none'].includes(config.auth) ||
        !Number.isInteger(config.deadlineMs) || config.deadlineMs < 100 || config.deadlineMs > 60000) {
        throw new Error('Invalid email SMTP configuration');
    }
    if (config.auth === 'login') {
        if (typeof credentials.user !== 'string' || !credentials.user.trim() ||
            typeof credentials.pass !== 'string' || !credentials.pass.trim()) {
            throw new Error('Invalid email SMTP credentials');
        }
        config.credentials = { user: credentials.user, pass: credentials.pass };
    }
    return config;
}

module.exports = { normalizeSmtpConfig };
