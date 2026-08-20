const config_module = require('./config');
const Redis = require("ioredis");

// 创建redis客户端实例
const RedisCli = new Redis({
    host : config_module.redis_host, 
    port : config_module.redis_port,
    password: config_module.redis_passwd,
});

/**
 * 连接redis服务器
 */
RedisCli.on("error", function (err) {
    console.log("RedisCli connect error");
    RedisCli.quit();
});

/**
 * 根据key获取value
 * @param {*} key
 */
async function GetRedis(key) {
    try {
        const result = await RedisCli.get(key);
        if (result == null) {
            console.log('Redis key not found');
            return null;
        }

        console.log('Redis key read succeeded');
        return result;
    }catch(error) {
        console.log('Redis key read failed');
        return null;
    }
}

/**
 * 根据key查询redis中是否存在key
 * @param {*} key
 */
async function QueryRedis(key) {
    try {
        const result = await RedisCli.exists(key);
        // 
        if (result == 0) {
            console.log('Redis key not found');
            return null;
        }
        console.log('Redis key exists');
        return result;
    }catch(error) {
        console.log('Redis key query failed');
        return null;
    }
}

/**
 * 设置key,value,过期时间
 * @param {*} key
 * @param {*} value
 * @param {*} exptime
 */

async function setRedisExpire(key, value, exptime) {
    try {
        // 设置key和value
        await RedisCli.set(key, value);
        // 设置过期时间, 当时间超过exptime后，这个key会被删除掉
        await RedisCli.expire(key, exptime);
        return true;
    }catch(error) {
        console.log('Redis key write failed');
        return false;
    }
}

/**
 * 退出函数
 */
function Quit() {
    RedisCli.quit();
}

module.exports = {GetRedis, QueryRedis, setRedisExpire, Quit}
