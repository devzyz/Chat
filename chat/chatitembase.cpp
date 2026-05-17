#include "chatitembase.h"
#include <QGridLayout>

ChatItemBase::ChatItemBase(ChatRole role, QWidget *parent) : QWidget(parent), m_role(role)
{
    // 创建用户姓名label，并进行一些设置
    m_pNameLabel = new QLabel();
    m_pNameLabel->setObjectName("chat_user_name");
    QFont font("Microsoft YaHei");
    font.setPointSize(9);
    m_pNameLabel->setFont(font);
    m_pNameLabel->setFixedHeight(20); // 设置固定高度

    m_pIconLabel = new QLabel();
    m_pIconLabel->setScaledContents(true);
    m_pIconLabel->setFixedSize(42, 42);

    // 聊天气泡
    m_pBubble = new QWidget();

    QGridLayout *pGLayout = new QGridLayout();
    // 控件之间的间距和外边距
    pGLayout->setVerticalSpacing(3);
    pGLayout->setHorizontalSpacing(3);
    pGLayout->setContentsMargins(3, 3, 3, 3);

    // 创建一个弹簧，水平方向上扩充，垂直方向上维持最小高度即可
    QSpacerItem *pSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

    if(m_role == ChatRole::Self)
    {
        // spacer name    icon
        // spacer bubble  icon
        // 名字右侧有8像素，即和头像之间有8像素的距离
        m_pNameLabel->setContentsMargins(0,0,8,0);
        // 右对齐
        m_pNameLabel->setAlignment(Qt::AlignRight);
        // 添加名称，第0行第1列，占1行占1列
        pGLayout->addWidget(m_pNameLabel, 0,1, 1,1);
        // 添加头像，第0行第2列，占2行占1列
        pGLayout->addWidget(m_pIconLabel, 0, 2, 2,1, Qt::AlignTop);
        pGLayout->addItem(pSpacer, 1, 0, 1, 1);
        pGLayout->addWidget(m_pBubble, 1,1, 1,1);
        pGLayout->setColumnStretch(0, 2);
        pGLayout->setColumnStretch(1, 3);
    }else{
        m_pNameLabel->setContentsMargins(8,0,0,0);
        m_pNameLabel->setAlignment(Qt::AlignLeft);
        pGLayout->addWidget(m_pIconLabel, 0, 0, 2,1, Qt::AlignTop);
        pGLayout->addWidget(m_pNameLabel, 0,1, 1,1);
        pGLayout->addWidget(m_pBubble, 1,1, 1,1);
        pGLayout->addItem(pSpacer, 2, 2, 1, 1);
        pGLayout->setColumnStretch(1, 3);
        pGLayout->setColumnStretch(2, 2);
    }

    this->setLayout(pGLayout);
}

void ChatItemBase::setUserName(const QString &name) {
    m_pNameLabel->setText(name);
}

void ChatItemBase::setUserIcon(const QPixmap &icon) {
    m_pIconLabel->setPixmap(icon);
}

// 用于设置文本widget的，其包含气泡框，文本等信息。将他替换之前用于占位的widget
void ChatItemBase::setWidget(QWidget *w) {
    // 先拿到当前的布局
    QGridLayout * pGLayout = (qobject_cast<QGridLayout*>(this->layout()));
    pGLayout->replaceWidget(m_pBubble, w);
    // 删除原来的widget, 替换为现在的widget
    delete m_pBubble;
    m_pBubble = w;
}
