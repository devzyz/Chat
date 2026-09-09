'use strict';

const net = require('node:net');
const nodemailer = require('nodemailer');
const { normalizeSmtpConfig } = require('./smtpConfig');

function classify(error) {
    if (error.code === 'ETIMEDOUT') return 'DeadlineExceeded';
    if (error.code === 'EAUTH' || error.code === 'EENVELOPE' || error.responseCode >= 400) return 'Rejected';
    return 'Unavailable';
}

function createSmtpAdapter(input, createTransport = nodemailer.createTransport) {
    let config;
    try { config = normalizeSmtpConfig(input, input?.credentials); } catch { /* SendMail reports InvalidConfig. */ }
    const pending = new Set();
    let closed = false;

    return {
        SendMail(mail) {
            if (!config) return Promise.resolve({ status: 'InvalidConfig' });
            if (closed) return Promise.resolve({ status: 'Unavailable' });
            return new Promise((resolve) => {
                let settled = false;
                let socket;
                let transport;
                const finish = (status) => {
                    if (settled) return;
                    settled = true;
                    clearTimeout(timer);
                    pending.delete(cancel);
                    // SMTPTransport.close() in locked 8.0.6 does not cancel active sends.
                    // Own the supported getSocket connection so a total deadline cancels
                    // I/O (including TLS), rather than leaving a late delivery running.
                    socket?.destroy();
                    transport?.close();
                    resolve({ status });
                };
                const cancel = () => finish('Unavailable');
                const timer = setTimeout(() => finish('DeadlineExceeded'), config.deadlineMs);
                pending.add(cancel);
                try {
                    transport = createTransport({
                        host: config.host, port: config.port, secure: config.secure,
                        auth: config.credentials, connectionTimeout: config.deadlineMs,
                        greetingTimeout: config.deadlineMs, socketTimeout: config.deadlineMs,
                        dnsTimeout: config.deadlineMs, logger: false, debug: false,
                        disableFileAccess: true, disableUrlAccess: true,
                        getSocket(options, callback) {
                            if (settled) { callback(new Error('SMTP operation closed')); return; }
                            socket = net.createConnection({ host: config.host, port: config.port });
                            let returned = false;
                            const complete = (error) => {
                                if (returned) return;
                                returned = true;
                                if (error || settled) {
                                    socket.destroy();
                                    callback(error || new Error('SMTP operation closed'));
                                } else callback(null, { connection: socket });
                            };
                            socket.once('error', complete);
                            socket.once('connect', () => complete());
                        }
                    });
                    transport.sendMail(mail, (error, info) => {
                        if (error) finish(classify(error));
                        else finish(info?.accepted?.length > 0 && !info?.rejected?.length ? 'Delivered' : 'Rejected');
                    });
                } catch (error) { finish(classify(error)); }
            });
        },
        close() {
            closed = true;
            for (const cancel of pending) cancel();
        }
    };
}

module.exports = { createSmtpAdapter };
