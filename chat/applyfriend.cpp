#include "applyfriend.h"
#include "ui_applyfriend.h"

ApplyFriend::ApplyFriend(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ApplyFriend), _label_point(2, 6)
{
    ui->setupUi(this);

    // 隐藏对话框标题栏
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    this->setObjectName("ApplyFriend");
    this->setModal(true);

    // 设置编辑框默认背景文本
    ui->name_edit->setPlaceholderText(tr("请输入用户名"));
    ui->label_edit->setPlaceholderText("搜索、添加标签");
    ui->back_edit->setPlaceholderText("请输入备注名");

    // // 对编辑框进行限制
    // ui->label_edit->SetMaxLength(21);
    // ui->label_edit->move(2, 2);
    // ui->label_edit->setFixedHeight(20);
    // ui->label_edit->SetMaxLength(10);
    // ui->input_tip_widget->hide();

    // _tip_cur_point = QPoint(5, 5);
    // _tip_data = {"同学", "家人"};

    ui->tip_label_widget->hide();
    ui->label_group_widget->hide();
    ui->label_3->hide();

    connect(ui->cancel_btn, &ClickedBtn::clicked, this, &ApplyFriend::slot_apply_cancel);
    connect(ui->sure_btn, &ClickedBtn::clicked, this, &ApplyFriend::slot_apply_sure);
}

ApplyFriend::~ApplyFriend()
{
    delete ui;
    qDebug() << "ApplyFriend is destructed";
}

void ApplyFriend::InitTipLabels()
{

}

void ApplyFriend::AddTipLabels(ClickedLabel *, QPoint cur_point, QPoint &next_point, int text_width, int text_height)
{

}

void ApplyFriend::SetSearchInfo(std::shared_ptr<SearchInfo> si)
{

}

bool ApplyFriend::eventFilter(QObject *obj, QEvent *event)
{
    return QDialog::eventFilter(obj, event);
}

void ApplyFriend::resetLabels()
{

}

/**
 * @brief ApplyFriend::addLabel
 * @param name
 * 将标签为name的条目放到编辑栏里面
 */
void ApplyFriend::addLabel(QString name)
{
    // 如果条目中已经有了，则返回
    if (_friend_labels.find(name) != _friend_labels.end()) {
        return ;
    }

    auto templabel = new FriendLabel(ui->grid_widget);
    templabel->SetText(name);
    templabel->setObjectName("FriendLabel");

    auto max_width = ui->grid_widget->width();
    if (_label_point.x() + templabel->Width() > max_width) {
        _label_point.setY(_label_point.y() + templabel->height() + 6);
        _label_point.setX(2);
    }

    templabel->move(_label_point);
    templabel->show();

    // 加入到已添加的标签集合中
    _friend_labels[templabel->Text()] = templabel;
    _friend_label_keys.push_back(templabel->Text());

    // 绑定关闭事件
    connect(templabel, &FriendLabel::sig_close, this, &ApplyFriend::slot_remove_friend_label);

    // 更新下一次可以插入的位置
    _label_point.setX(_label_point.x() + templabel->Width() + 2);

    // 如果编辑框
    if (_label_point.x() + MIN_APPLY_LABEL_EDIT_LENGTH > ui->grid_widget->width()) {
        ui->label_edit->move(2, _label_point.y() + templabel->Height() + 2);
    }else {
        ui->label_edit->move(_label_point);
    }

    ui->label_edit->clear();

    // 如果grid_widget的高度不够高了，重新设置一下
    if (ui->grid_widget->height() < _label_point.y() + templabel->height() + 2) {
        ui->grid_widget->setFixedHeight(_label_point.y() + templabel->height() + 2);
    }
}

/**
 * @brief ApplyFriend::slot_label_enter
 * 在标签编辑框内输入后，点击enter会将标签添加到标签显示位置和编辑栏两处
 */
void ApplyFriend::slot_label_enter()
{
    if (ui->label_edit->text().isEmpty()) {
        return ;
    }

    // 将这个label添加到已添加标签显示位置
    addLabel(ui->label_edit->text());

    // 隐藏下拉框
    ui->input_tip_widget->hide();

    // 查找显示区域里面有没有这个标签，没有的话则加入
    auto text = ui->label_edit->text();
    auto find_it = std::find(_tip_data.begin(), _tip_data.end(), text);

    // 没招到则加入
    if (find_it == _tip_data.end()) {
        _tip_data.push_back(text);
    }

    // 判断下面显示框内有没有该标签
    auto find_add = _add_labels.find(text);
    if (find_add != _add_labels.end()) {
        find_add.value()->SetCurState(ClickLabelState::Selected);
        return ;
    }
}

void ApplyFriend::slot_show_more_label() {

}

void ApplyFriend::slot_remove_friend_label(QString) {

}

void ApplyFriend::slot_change_friend_label_by_tip(QString, ClickLabelState) {

}

void ApplyFriend::slot_label_text_change(const QString& text) {

}

void ApplyFriend::slot_add_edit_finished() {

}

void ApplyFriend::slot_add_friend_label_by_click_tip(QString text) {

}

void ApplyFriend::slot_apply_sure() {
    this->hide();
    deleteLater();
}

void ApplyFriend::slot_apply_cancel() {
    this->hide();
    deleteLater();
}
