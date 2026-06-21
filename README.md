# 分布式即时通讯系统

## 项目简介

本项目是一个基于 C++/Qt、Boost.Asio、gRPC、MySQL、Redis 实现的分布式即时通讯系统，整体采用客户端、网关服务、状态服务、聊天服务分层设计。客户端通过 HTTP 完成注册、登录、验证码、密码重置等短连接请求，登录成功后与 ChatServer 建立 TCP 长连接，完成好友管理、私聊消息、聊天记录拉取、心跳保活和跨服务器消息通知等功能。

系统服务端主要由 GateServer、StatusServer、ChatServer 组成：GateServer 负责对外 HTTP 接入和登录前置校验，StatusServer 负责分配 ChatServer、维护登录 token 并完成登录校验，ChatServer 负责用户长连接、业务消息处理、在线状态维护和跨节点 gRPC 通知。数据层使用 MySQL 持久化用户、好友、会话和聊天消息，使用 Redis 存储登录 token、用户在线节点和连接统计等运行时状态。

## 项目已实现功能

| 模块 | 已实现功能 |
| --- | --- |
| Qt 客户端 | 账号功能：登录、注册、忘记密码、验证码交互、登录后 TCP 长连接接入。<br>好友功能：用户搜索、好友申请、好友认证、联系人列表、好友信息页。<br>聊天功能：会话列表、私聊消息收发、聊天气泡展示、历史消息拉取、心跳保活和离线提示。 |
| GateServer | 提供 HTTP 网关入口，处理 `/get_varifycode`、`/user_register`、`/reset_pwd`、`/user_login` 等接口。<br>负责登录前置校验，调用 VarifyServer 获取验证码，调用 StatusServer 获取可用 ChatServer 和登录 token。 |
| StatusServer | 管理多个 ChatServer 节点，为登录用户分配可连接的 ChatServer。<br>生成并维护登录 token，写入 Redis，并通过 gRPC 为 ChatServer 提供 uid/token 登录校验。 |
| ChatServer | 网络接入：基于 Boost.Asio 实现 TCP 长连接、自定义消息协议解析、心跳响应和连接超时检测。<br>用户与好友：完成聊天登录校验、用户在线状态维护、单用户多端登录踢下线、好友搜索、好友申请和好友认证。<br>聊天业务：支持私聊会话创建、文本消息收发、消息入库、会话列表加载、历史消息拉取和跨 ChatServer 的 gRPC 消息通知。 |
| MySQL 数据层 | 持久化保存用户信息、好友关系、好友申请、私聊会话、聊天消息等核心业务数据。 |
| Redis 缓存层 | 缓存登录 token、用户在线 ChatServer 节点、连接统计等运行时状态，支撑分布式登录校验和在线消息路由。 |
| 压测模块 | 实现 ChatServer TCP 直连压测和完整登录链路压测，覆盖 HTTP 登录、TCP 连接、聊天登录和心跳保活流程。 |
