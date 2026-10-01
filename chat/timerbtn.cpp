#include "timerbtn.h"
#include "logmgr.h"
#include <QMouseEvent>
#include <QDebug>

TimerBtn::TimerBtn(QWidget *parent) : QPushButton(parent), _counter(10){
    _timer = new QTimer(this);
    connect(this, &QPushButton::clicked, this, /** @brief 有效鼠标或键盘点击后才启动倒计时。 */ [this] {
        setEnabled(false); setText(QString::number(_counter)); _timer->start(1000);
    });

    // 连接定时器的触发信号，与一个lambda槽函数
    connect(_timer, &QTimer::timeout, this,
        /** @brief 更新验证码倒计时，到时恢复可点击状态。 */
        [this]() {
        _counter --;
        if (_counter <= 0) {
            _timer->stop();
            _counter = 10;
            this->setText("获取");
            this->setEnabled(true);
            return ;
        }
        this->setText(QString::number(_counter));
    });
}

TimerBtn::~TimerBtn() {
    _timer->stop();
}
