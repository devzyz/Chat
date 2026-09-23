'use strict';

const net = require('node:net');
const nodemailer = require('nodemailer');
const { normalizeSmtpConfig } = require('./smtpConfig');

/** 将 SMTP 异常映射为期限、拒收或不可用状态，不暴露原始凭据。 */
function classify(error) {
    if (error.code === 'ETIMEDOUT') return 'DeadlineExceeded';
    if (error.code === 'EAUTH' || error.code === 'EENVELOPE' || error.responseCode >= 400) return 'Rejected';
    return 'Unavailable';
}

/** 创建可关闭的 SMTP 适配器；配置错误延迟由 sendMail 返回 InvalidConfig。 */
function createSmtpAdapter(input, createTransport = nodemailer.createTransport) {
    let config;
    try { config = normalizeSmtpConfig(input, input?.credentials); } catch { /* sendMail reports InvalidConfig. */ }
    const pending = new Set();
    let closed = false;

    return {
        /** 发送邮件并返回 Promise<{status}>；期限或关闭都会销毁连接，Delivered 仅表示 SMTP 接受。 */
        sendMail(mail) {
            if (!config) return Promise.resolve({ status: 'InvalidConfig' });
            if (closed) return Promise.resolve({ status: 'Unavailable' });
            return new Promise(
                /** 持有本次发送的 socket、transport 和唯一完成状态。 */
                (resolve) => {
                let settled = false;
                let socket;
                let transport;
                const finish =
                    /** 只完成一次发送，清理期限与连接并返回状态。 */
                    (status) => {
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
                const cancel =
                    /** 适配器关闭时把未完成发送结束为不可用。 */
                    () => finish('Unavailable');
                const timer = setTimeout(
                    /** 总期限到达时终止本次发送。 */
                    () => finish('DeadlineExceeded'), config.deadlineMs);
                pending.add(cancel);
                try {
                    transport = createTransport({
                        host: config.host, port: config.port, secure: config.secure,
                        auth: config.credentials, connectionTimeout: config.deadlineMs,
                        greetingTimeout: config.deadlineMs, socketTimeout: config.deadlineMs,
                        dnsTimeout: config.deadlineMs, logger: false, debug: false,
                        disableFileAccess: true, disableUrlAccess: true,
                        /** 为邮件库创建可取消的 socket，callback 至多调用一次。 */
                        getSocket(options, callback) {
                            if (settled) { callback(new Error('SMTP operation closed')); return; }
                            socket = net.createConnection({ host: config.host, port: config.port });
                            let returned = false;
                            const complete =
                                /** 仅交付一次连接结果，失败或已关闭时销毁 socket。 */
                                (error) => {
                                if (returned) return;
                                returned = true;
                                if (error || settled) {
                                    socket.destroy();
                                    callback(error || new Error('SMTP operation closed'));
                                } else callback(null, { connection: socket });
                            };
                            socket.once('error', complete);
                            socket.once('connect',
                                /** 连接成功后交付 socket 给邮件库。 */
                                () => complete());
                        }
                    });
                    transport.sendMail(mail,
                        /** 将发送结果映射为接受或拒收状态并完成操作。 */
                        (error, info) => {
                        if (error) finish(classify(error));
                        else finish(info?.accepted?.length > 0 && !info?.rejected?.length ? 'Delivered' : 'Rejected');
                    });
                } catch (error) { finish(classify(error)); }
            });
        },
        /** 幂等关闭适配器并取消所有未完成发送，后续发送返回不可用。 */
        close() {
            closed = true;
            for (const cancel of pending) cancel();
        }
    };
}

module.exports = { createSmtpAdapter };
