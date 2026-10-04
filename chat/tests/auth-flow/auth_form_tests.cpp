#include "logindialog.h"
#include "registerdialog.h"
#include "resetdialog.h"
#include "httpmgr.h"
#include "tcpmgr.h"
#include "usermgr.h"
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

/** @brief 通过真实表单和本机 HTTP 对端验证验证码与安全错误提示，不发送外部邮件。 */
class AuthFormTests : public QObject {
    Q_OBJECT
private:
    QTcpServer _server;
    QVector<QJsonObject> _requests;
    int _error = 0;
    QString _oldGate;
    /** @brief 填写测试表单的命名文本框。 */
    void fill(QWidget &form, const char *name, const QString &text) {
        auto *edit = form.findChild<QLineEdit *>(name); QVERIFY(edit); edit->setText(text);
    }
    /** @brief 点击表单的真实按钮，保留自动连接与本地校验路径。 */
    void click(QWidget &form, const char *name) {
        auto *button = form.findChild<QPushButton *>(name); QVERIFY(button); button->click();
    }
private slots:
    /** @brief 每项创建隔离回环端口并返回指定业务结果。 */
    void init() {
        _oldGate = gate_url_prefix; _requests.clear(); _error = 0;
        QVERIFY(_server.listen(QHostAddress::LocalHost, 0));
        gate_url_prefix = "http://127.0.0.1:" + QString::number(_server.serverPort());
        connect(&_server, &QTcpServer::newConnection, this, /** @brief 按 Content-Length 收齐请求后返回合成 JSON。 */ [this] {
            auto *socket = _server.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, /** @brief 收齐一次请求后记录正文并关闭连接。 */ [this,socket,buffer] {
                *buffer += socket->readAll();
                const int headerEnd = buffer->indexOf("\r\n\r\n"); if (headerEnd < 0) return;
                int size = 0;
                for (const auto &line : buffer->left(headerEnd).split('\n'))
                    if (line.toLower().startsWith("content-length:")) size = line.mid(15).trimmed().toInt();
                if (buffer->size() < headerEnd + 4 + size) return;
                _requests.append(QJsonDocument::fromJson(buffer->mid(headerEnd + 4, size)).object());
                const auto body = QJsonDocument(QJsonObject{{"error",_error}}).toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
    }
    /** @brief 清理端口和配置，不遗留请求到下一项。 */
    void cleanup() {
        _server.close(); _server.disconnect(this);
        qDeleteAll(_server.findChildren<QTcpSocket *>()); gate_url_prefix = _oldGate;
    }
    /** @brief 在 Qt 退出前释放账号和网络单例。 */
    void cleanupTestCase() { HttpMgr::releaseInstance(); TcpMgr::releaseInstance(); UserMgr::releaseInstance(); }
    /** @brief 首次申请验证码只校验邮箱，不要求已有验证码。 */
    void initialCodeRequest() {
        AuthFlowCoordinator flow; RegisterDialog form(flow);
        fill(form,"email_edit","fixture@example.test"); click(form,"get_code");
        QTRY_COMPARE_WITH_TIMEOUT(_requests.size(),1,1500);
        QCOMPARE(_requests.first()["email"].toString(),QString("fixture@example.test"));
        QTRY_VERIFY(form.findChild<QLabel *>("err_tip")->text().contains(QString::fromUtf8("已经发送")));
    }
    /** @brief 无效邮箱不应发起验证码请求。 */
    void invalidCodeRequestEmail() {
        AuthFlowCoordinator flow; RegisterDialog form(flow);
        fill(form,"email_edit","invalid"); click(form,"get_code");
        QCoreApplication::processEvents(); QVERIFY(_requests.isEmpty());
        QVERIFY(form.findChild<QLabel *>("err_tip")->text().contains(QString::fromUtf8("邮箱")));
    }
    /** @brief 八位码原样提交后，成功响应切换注册页或显示改密成功。 */
    void successfulSubmission() {
        for (bool registration : {false, true}) {
            _requests.clear();
            AuthFlowCoordinator flow; std::unique_ptr<QDialog> form;
            if (registration) form = std::make_unique<RegisterDialog>(flow);
            else form = std::make_unique<ResetDialog>(flow);
            fill(*form,"user_edit","fixture"); fill(*form,"email_edit","fixture@example.test");
            fill(*form,"password_edit","Fixture123"); fill(*form,"varify_edit","1a2b3c4d");
            if (registration) fill(*form,"confirm_edit","Fixture123");
            click(*form,"confirm_btn");
            QTRY_COMPARE_WITH_TIMEOUT(_requests.size(),1,1500);
            QTRY_VERIFY(form->findChild<QLabel *>("err_tip")->text().contains(QString::fromUtf8("成功")));
            if (registration) QCOMPARE(form->findChild<QStackedWidget *>("stackedWidget")->currentIndex(),1);
        }
    }
    /** @brief 未输入的重置页没有错误占位信息。 */
    void resetInitiallyClear() {
        AuthFlowCoordinator flow; ResetDialog form(flow);
        QVERIFY(form.findChild<QLabel *>("err_tip")->text().isEmpty());
    }
    /** @brief 两个表单拒绝空/短/长码，八位码原样到达 HTTP 并显示验证码业务错误。 */
    void verificationSubmission() {
        for (bool registration : {false, true}) {
            for (const QString code : {"", "1234", "1a2b3c4d", "123456789"}) {
                _requests.clear();
                AuthFlowCoordinator flow; std::unique_ptr<QDialog> form;
                if (registration) form = std::make_unique<RegisterDialog>(flow);
                else form = std::make_unique<ResetDialog>(flow);
                fill(*form,"email_edit","fixture@example.test"); fill(*form,"user_edit","fixture");
                fill(*form,"password_edit","Fixture123"); fill(*form,"varify_edit",code);
                if (registration) fill(*form,"confirm_edit","Fixture123");
                _error = 1004; click(*form,"confirm_btn");
                if (code.size() == 8) {
                    QTRY_COMPARE_WITH_TIMEOUT(_requests.size(),1,1500);
                    QCOMPARE(_requests.first()[registration ? "varifycode" : "varify"].toString(),code);
                    QTRY_VERIFY(form->findChild<QLabel *>("err_tip")->text().contains(QString::fromUtf8("验证码错误或已过期")));
                } else {
                    QCoreApplication::processEvents(); QVERIFY(_requests.isEmpty());
                    QVERIFY(form->findChild<QLabel *>("err_tip")->text().contains(QString::fromUtf8("验证码")));
                }
            }
        }
    }
    /** @brief 错误密码与账号不匹配统一提示，不泄露账号是否存在，按钮可重试。 */
    void loginError() {
        for (int error : {1006, 1007, 1009}) {
            _requests.clear(); _error = error; AuthFlowCoordinator flow; LoginDialog form(flow);
            fill(form,"email_edit","fixture@example.test"); fill(form,"password_edit","Fixture123"); click(form,"login_btn");
            QTRY_VERIFY(form.findChild<QLabel *>("err_tip")->text().contains(QString::fromUtf8("账号或密码不正确")));
            QVERIFY(form.findChild<QPushButton *>("login_btn")->isEnabled());
        }
    }
};
QTEST_MAIN(AuthFormTests)
#include "auth_form_tests.moc"
