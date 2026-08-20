const nodemailer = require('nodemailer');
const config_module = require("./config");

/**
 * 创建发送邮件的代理
 */
let transport = nodemailer.createTransport({
    host: 'smtp.qq.com',
    port: 465,
    secure: true, 
    auth: {
        user: config_module.email_user, // 发送方邮箱地址
        pass: config_module.email_pass // 邮箱授权码
    }
})

/**
 * 发送邮件的函数
 * @param {*} mailOptions_ 发送邮件的参数
 * @returns
 */

function SendMail(mailOptions_) {
    return new Promise(function(resolve, reject) {
        transport.sendMail(mailOptions_, function(error, info) {
            if (error) {
                console.log('SMTP delivery failed');
                reject(error);
            }else {
                console.log('SMTP delivery succeeded');
                resolve(info.response);
            }
        });
    })
}

module.exports.SendMail = SendMail
