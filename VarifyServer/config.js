const fs = require('fs'); // js内的文件读写库
const { normalizeSmtpConfig } = require('./smtpConfig');
const { normalizeRedisConfig } = require('./redis');

function requireEnvironment(name) {
    const value = process.env[name];
    if (typeof value !== 'string' || value.trim() === '') {
        throw new Error(`Missing required environment variable: ${name}`);
    }
    return value;
}

function rejectPlaintextCredentials(config) {
    const forbiddenFields = [
        ['email', 'user'],
        ['email', 'pass'],
        ['mysql', 'passwd'],
        ['redis', 'passwd']
    ];
    const hasCredentialField = forbiddenFields.some(([section, field]) =>
        config[section] && Object.prototype.hasOwnProperty.call(config[section], field));
    if (hasCredentialField) {
        throw new Error('Credential fields are not allowed in config.json; use environment variables.');
    }
}

let configPath = process.env.CHAT_CONFIG || 'config.json';
const configArgIndex = process.argv.indexOf('--config');
if (configArgIndex !== -1 && process.argv[configArgIndex + 1]) {
    configPath = process.argv[configArgIndex + 1];
}
let config = JSON.parse(fs.readFileSync(configPath, 'utf-8'));
rejectPlaintextCredentials(config);
let email_user = requireEnvironment('CHAT_VARIFY_EMAIL_USER');
let email_pass = (config.email?.auth ?? 'login') === 'none' ? undefined
    : requireEnvironment('CHAT_VARIFY_EMAIL_PASS');
const smtp = normalizeSmtpConfig(config.email, { user: email_user, pass: email_pass });
let mysql_host = config.mysql.host;
let mysql_port = config.mysql.port;
let mysql_passwd = requireEnvironment('CHAT_VARIFY_MYSQL_PASSWORD');
let redis_host = config.redis.host;
let redis_port = config.redis.port;
let redis_passwd = requireEnvironment('CHAT_VARIFY_REDIS_PASSWORD');
const redis = normalizeRedisConfig(config.redis, redis_passwd);
let code_prefix = "code_";

module.exports = {email_pass, email_user, smtp, redis, mysql_host, mysql_port, mysql_passwd, redis_host, redis_passwd, redis_port, code_prefix}
