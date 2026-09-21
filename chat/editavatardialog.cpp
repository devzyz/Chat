#include "editavatardialog.h"
#include "ui_editavatardialog.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>

EditAvatarDialog::EditAvatarDialog(LocalAvatar *avatar, const QPixmap &current, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::EditAvatarDialog)
    , _avatar(avatar)
    , _chooseButton(new QPushButton(tr("选择图片"), this))
{
    ui->setupUi(this);

    // 设置按钮的属性
    ui->edit_avatar_cancel_btn->SetState("leave", "hover", "select");
    ui->edit_avatar_confirm_btn->SetState("leave", "hover", "select");

    // 设置头像
    QPixmap pixmap(":/res/chat.ico");
    ui->edit_avatar_soft_icon_label->setPixmap(pixmap.scaled(ui->edit_avatar_soft_icon_label->size(),
                                               Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->edit_avatar_soft_icon_label->setScaledContents(true);

    setWindowTitle(tr("本地头像"));
    ui->edit_avatar_dialog_name_label->setText(tr("上传头像"));
    ui->edit_avatar_close_label->setText(QStringLiteral("×"));
    ui->avatar_crop_widget->setImage(current.toImage());
    ui->avatar_crop_widget->setEnabled(false);
    _chooseButton->setObjectName(QStringLiteral("chooseAvatarButton"));

    auto *hint = new QLabel(tr("拖动图片调整位置，滚轮或滑块缩放；仅保存裁剪后的正方形"), this);
    hint->setAlignment(Qt::AlignCenter);
    ui->verticalLayout_2->addWidget(hint);
    auto *controls = new QHBoxLayout;
    controls->setContentsMargins(16, 12, 16, 12);
    controls->setSpacing(12);
    controls->addWidget(_chooseButton);
    auto *zoomOut = new QPushButton(QStringLiteral("−"), this);
    zoomOut->setObjectName(QStringLiteral("avatarZoomOutButton"));
    zoomOut->setAccessibleName(tr("缩小头像"));
    zoomOut->setFixedWidth(32);
    auto *zoom = new QSlider(Qt::Horizontal, this);
    zoom->setObjectName(QStringLiteral("avatarZoomSlider"));
    zoom->setAccessibleName(tr("头像缩放比例"));
    zoom->setRange(100, 400);
    zoom->setValue(100);
    auto *zoomIn = new QPushButton(QStringLiteral("+"), this);
    zoomIn->setObjectName(QStringLiteral("avatarZoomInButton"));
    zoomIn->setAccessibleName(tr("放大头像"));
    zoomIn->setFixedWidth(32);
    auto *percentage = new QLabel(QStringLiteral("100%"), this);
    percentage->setMinimumWidth(48);
    controls->addWidget(zoomOut);
    controls->addWidget(zoom, 1);
    controls->addWidget(zoomIn);
    controls->addWidget(percentage);
    ui->verticalLayout_2->addLayout(controls);
    zoom->setEnabled(false);
    zoomOut->setEnabled(false);
    zoomIn->setEnabled(false);
    connect(zoom, &QSlider::valueChanged, ui->avatar_crop_widget, &AvatarCropWidget::setZoom);
    connect(ui->avatar_crop_widget, &AvatarCropWidget::zoomChanged, zoom, &QSlider::setValue);
    connect(zoom, &QSlider::valueChanged, this, [percentage](int value) {
        percentage->setText(QStringLiteral("%1%").arg(value));
    });
    connect(zoomOut, &QPushButton::clicked, this, [zoom]() { zoom->setValue(zoom->value() - 10); });
    connect(zoomIn, &QPushButton::clicked, this, [zoom]() { zoom->setValue(zoom->value() + 10); });

    const auto updateControls = [this, zoom, zoomOut, zoomIn]() {
        const bool editable = !_avatar->isBusy() && !_avatar->selection().isNull();
        ui->avatar_crop_widget->setEnabled(editable);
        zoom->setEnabled(editable);
        zoomOut->setEnabled(editable);
        zoomIn->setEnabled(editable);
        ui->edit_avatar_confirm_btn->setEnabled(editable);
    };
    ui->edit_avatar_confirm_btn->setEnabled(false);
    _chooseButton->setEnabled(!_avatar->isBusy());
    connect(_chooseButton, &QPushButton::clicked, this, [this]() {
        auto *picker = new QFileDialog(this, tr("选择头像（最大 5 MB，宽高不超过 4096 像素）"));
        picker->setAttribute(Qt::WA_DeleteOnClose);
        picker->setFileMode(QFileDialog::ExistingFile);
        picker->setNameFilter(tr("图片 (*.png *.jpg *.jpeg)"));
        connect(picker, &QFileDialog::fileSelected, _avatar, &LocalAvatar::selectFile);
        picker->open();
    });
    connect(_avatar, &LocalAvatar::selectionChanged, this, [this, current, updateControls](const QImage &image) {
        const QImage local = image.isNull() ? _avatar->image() : image;
        ui->avatar_crop_widget->setImage(local.isNull() ? current.toImage() : local);
        updateControls();
    });
    connect(_avatar, &LocalAvatar::imageChanged, this, [this](const QImage &image) {
        if (_avatar->selection().isNull() && !image.isNull()) {
            ui->avatar_crop_widget->setImage(image);
        }
    });
    connect(_avatar, &LocalAvatar::busyChanged, this, [this, updateControls](bool busy) {
        _chooseButton->setEnabled(!busy);
        updateControls();
        ui->edit_avatar_cancel_btn->setEnabled(!_avatar->isSaving());
    });
    connect(_avatar, &LocalAvatar::errorOccurred, this, [this](const QString &error) {
        QMessageBox::warning(this, tr("头像上传"), error);
    });
    connect(ui->edit_avatar_confirm_btn, &QPushButton::clicked, this, [this]() {
        _avatar->saveSelection(ui->avatar_crop_widget->sourceRect());
    });
    connect(ui->edit_avatar_cancel_btn, &QPushButton::clicked, this, &EditAvatarDialog::reject);
    connect(ui->edit_avatar_close_label, &ClickedLabel::clicked, this, &EditAvatarDialog::reject);
    connect(_avatar, &LocalAvatar::saved, this, &EditAvatarDialog::accept);
    connect(this, &QDialog::finished, _avatar, &LocalAvatar::discardSelection);
}

void EditAvatarDialog::reject()
{
    if (!_avatar->isSaving()) {
        QDialog::reject();
    }
}

EditAvatarDialog::~EditAvatarDialog()
{
    delete ui;
}
