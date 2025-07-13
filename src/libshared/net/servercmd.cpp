// SPDX-License-Identifier: GPL-3.0-or-later
#include "libshared/net/servercmd.h"
#include "libshared/util/qtcompat.h"

namespace net {

net::Message ServerCommand::make(
	const QString &cmd, const QJsonArray &args, const QJsonObject &kwargs)
{
	return ServerCommand{cmd, args, kwargs}.toMessage();
}

net::Message ServerCommand::makeKick(int target, bool ban)
{
	Q_ASSERT(target > 0 && target < 256);
	QJsonObject kwargs;
	if(ban)
		kwargs["ban"] = true;

	return make("kick-user", QJsonArray() << target, kwargs);
}

net::Message ServerCommand::makeUnban(int entryId)
{
	return make("remove-ban", QJsonArray() << entryId);
}

net::Message ServerCommand::makeMute(int target, bool mute)
{
	return make("mute", QJsonArray() << target << mute);
}

net::Message ServerCommand::makeAnnounce(const QString &url)
{
	return make("announce-session", QJsonArray() << url);
}

net::Message ServerCommand::makeUnannounce(const QString &url)
{
	return make("unlist-session", QJsonArray() << url);
}

QString ServerCommand::autoresetOs()
{
#if defined(__EMSCRIPTEN__)
	return QStringLiteral("emscripten");
#elif defined(Q_OS_ANDROID)
	return QStringLiteral("android");
#elif defined(Q_OS_IOS)
	return QStringLiteral("ios");
#elif defined(Q_OS_WIN)
	return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
	return QStringLiteral("macos");
#elif defined(Q_OS_LINUX)
	return QStringLiteral("linux");
#elif defined(Q_OS_UNIX)
	return QStringLiteral("unix");
#else
	return QStringLiteral("unknown");
#endif
}

int ServerCommand::rateAutoresetOs(const QString &os)
{
	if(os == QStringLiteral("emscripten") || os == QStringLiteral("android") ||
	   os == QStringLiteral("ios")) {
		return -1; // Bad candidates. Memory limits and they fall asleep easily.
	} else if(
		os == QStringLiteral("windows") || os == QStringLiteral("macos") ||
		os == QStringLiteral("linux") || os == QStringLiteral("unix")) {
		return 1; // Good candidates. Desktop operating systems.
	} else {
		return 0; // Unknown OS or client didn't specify.
	}
}

net::Message ServerCommand::toMessage() const
{
	QJsonObject data{{QStringLiteral("cmd"), cmd}};
	if(!args.isEmpty()) {
		data["args"] = args;
	}
	if(!kwargs.isEmpty()) {
		data["kwargs"] = kwargs;
	}
	return net::makeServerCommandMessage(0, QJsonDocument{data});
}

ServerCommand ServerCommand::fromMessage(const net::Message &msg)
{
	if(msg.isNull() || msg.type() != DP_MSG_SERVER_COMMAND) {
		qWarning("ServerCommand::fromMessage: bad message");
		return ServerCommand{QString(), QJsonArray(), QJsonObject()};
	}

	size_t len;
	const char *data = DP_msg_server_command_msg(msg.toServerCommand(), &len);
	QByteArray bytes = QByteArray::fromRawData(data, compat::castSize(len));

	QJsonParseError err;
	const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
	if(err.error != QJsonParseError::NoError) {
		qWarning(
			"ServerReply::fromMessage JSON parsing error: %s",
			qPrintable(err.errorString()));
		return ServerCommand{QString(), QJsonArray(), QJsonObject()};
	}

	return fromJson(doc);
}

ServerCommand ServerCommand::fromJson(const QJsonDocument &doc)
{
	QJsonObject data = doc.object();
	return ServerCommand{
		data["cmd"].toString(), data["args"].toArray(),
		data["kwargs"].toObject()};
}


ServerReply ServerReply::fromMessage(const net::Message &msg)
{
	if(msg.isNull() || msg.type() != DP_MSG_SERVER_COMMAND) {
		qWarning("ServerReply::fromMessage: bad message");
		return ServerReply{
			ServerReply::ReplyType::Unknown, QString(), QJsonObject()};
	}

	size_t len;
	const char *data = DP_msg_server_command_msg(msg.toServerCommand(), &len);
	QByteArray bytes = QByteArray::fromRawData(data, compat::castSize(len));

	QJsonParseError err;
	const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
	if(err.error != QJsonParseError::NoError) {
		qWarning(
			"ServerReply::fromMessage JSON parsing error: %s",
			qPrintable(err.errorString()));
		return ServerReply{
			ServerReply::ReplyType::Unknown, QString(), QJsonObject()};
	}

	return fromJson(doc);
}

ServerReply ServerReply::fromJson(const QJsonDocument &doc)
{
	QJsonObject data = doc.object();
	QString typestr = data.value(QStringLiteral("type")).toString();

	ServerReply r;
	if(typestr == QStringLiteral("login")) {
		r.type = ServerReply::ReplyType::Login;
	} else if(typestr == QStringLiteral("msg")) {
		r.type = ServerReply::ReplyType::Message;
	} else if(typestr == QStringLiteral("alert")) {
		r.type = ServerReply::ReplyType::Alert;
	} else if(typestr == QStringLiteral("error")) {
		r.type = ServerReply::ReplyType::Error;
	} else if(typestr == QStringLiteral("result")) {
		r.type = ServerReply::ReplyType::Result;
	} else if(typestr == QStringLiteral("log")) {
		r.type = ServerReply::ReplyType::Log;
	} else if(typestr == QStringLiteral("sessionconf")) {
		r.type = ServerReply::ReplyType::SessionConf;
	} else if(typestr == QStringLiteral("sizelimit")) {
		r.type = ServerReply::ReplyType::SizeLimitWarning;
	} else if(typestr == QStringLiteral("status")) {
		r.type = ServerReply::ReplyType::Status;
	} else if(typestr == QStringLiteral("reset")) {
		r.type = ServerReply::ReplyType::Reset;
	} else if(typestr == QStringLiteral("autoreset")) {
		r.type = ServerReply::ReplyType::ResetRequest;
	} else if(typestr == QStringLiteral("catchup")) {
		r.type = ServerReply::ReplyType::Catchup;
	} else if(typestr == QStringLiteral("caughtup")) {
		r.type = ServerReply::ReplyType::CaughtUp;
	} else if(typestr == QStringLiteral("banimpex")) {
		r.type = ServerReply::ReplyType::BanImpEx;
	} else if(typestr == QStringLiteral("outofspace")) {
		r.type = ServerReply::ReplyType::OutOfSpace;
	} else if(typestr == QStringLiteral("sstart")) {
		r.type = ServerReply::ReplyType::StreamStart;
	} else if(typestr == QStringLiteral("sprogress")) {
		r.type = ServerReply::ReplyType::StreamProgress;
	} else if(typestr == QStringLiteral("passwordchange")) {
		r.type = ServerReply::ReplyType::PasswordChange;
	} else if(typestr == QStringLiteral("invitecreated")) {
		r.type = ServerReply::ReplyType::InviteCreated;
	} else if(typestr == QStringLiteral("thumbnail")) {
		r.type = ServerReply::ReplyType::Thumbnail;
	} else {
		r.type = ServerReply::ReplyType::Unknown;
	}

	r.message = data.value(QStringLiteral("message")).toString();
	r.reply = data;
	return r;
}

net::Message ServerReply::make(const QJsonObject &data)
{
	return net::makeServerCommandMessage(0, QJsonDocument{data});
}

net::Message ServerReply::makeError(const QString &message, const QString &code)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("error")},
		 {QStringLiteral("message"), message},
		 {QStringLiteral("code"), code}});
}

net::Message
ServerReply::makeCommandError(const QString &command, const QString &message)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("error")},
		 {QStringLiteral("message"),
		  QStringLiteral("%1: %2").arg(command, message)}});
}

net::Message ServerReply::makeBanExportResult(const QString &data)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("banimpex")},
		 {QStringLiteral("export"), data}});
}

net::Message ServerReply::makeBanImportResult(int total, int imported)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("banimpex")},
		 {QStringLiteral("imported"), imported},
		 {QStringLiteral("total"), total}});
}

net::Message
ServerReply::makeBanImpExError(const QString &message, const QString &key)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("banimpex")},
		 {QStringLiteral("error"), message},
		 {QStringLiteral("T"), key}});
}

net::Message ServerReply::makeMessage(const QString &message)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("msg")},
		 {QStringLiteral("message"), message}});
}

net::Message ServerReply::makeAlert(const QString &message)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("alert")},
		 {QStringLiteral("message"), message}});
}

net::Message ServerReply::makeKeyMessage(
	const QString &message, const QString &key, const QJsonObject &params)
{
	QJsonObject data = {
		{QStringLiteral("type"), QStringLiteral("msg")},
		{QStringLiteral("message"), message},
		{QStringLiteral("T"), key}};
	if(!params.isEmpty()) {
		data[QStringLiteral("P")] = params;
	}
	return make(data);
}

net::Message ServerReply::makeKeyAlert(
	const QString &message, const QString &key, const QJsonObject &params)
{
	QJsonObject data = {
		{QStringLiteral("type"), QStringLiteral("alert")},
		{QStringLiteral("message"), message},
		{QStringLiteral("T"), key}};
	if(!params.isEmpty()) {
		data[QStringLiteral("P")] = params;
	}
	return make(data);
}

net::Message ServerReply::makeKeyAlertReset(
	const QString &message, const QString &reset, const QString &key,
	const QJsonObject &params)
{
	QJsonObject data = {
		{QStringLiteral("type"), QStringLiteral("alert")},
		{QStringLiteral("message"), message},
		{QStringLiteral("reset"), reset},
		{QStringLiteral("T"), key}};
	if(!params.isEmpty()) {
		data[QStringLiteral("P")] = params;
	}
	return make(data);
}

net::Message ServerReply::makeCatchup(int count, int key)
{
	QJsonObject data{
		{QStringLiteral("type"), QStringLiteral("catchup")},
		{QStringLiteral("count"), count}};
	if(key != -1) {
		data[QStringLiteral("key")] = key;
	}
	return make(data);
}

net::Message ServerReply::makeCaughtUp(int key)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("caughtup")},
		 {QStringLiteral("key"), key}});
}

net::Message ServerReply::makeLog(const QString &message, QJsonObject data)
{
	data[QStringLiteral("type")] = QStringLiteral("log");
	data[QStringLiteral("message")] = message;
	return make(data);
}

net::Message ServerReply::makeLoginGreeting(
	const QString &message, int version, const QJsonArray &flags,
	const QJsonObject &methods, const QString &info, const QString &rules,
	bool preferWebSockets)
{
	QJsonObject data = {
		{QStringLiteral("type"), QStringLiteral("login")},
		{QStringLiteral("message"), message},
		{QStringLiteral("version"), version},
		{QStringLiteral("flags"), flags},
		{QStringLiteral("methods"), methods}};
	if(!info.isEmpty()) {
		data[QStringLiteral("info")] = info;
	}
	if(!rules.isEmpty()) {
		data[QStringLiteral("rules")] = rules;
	}
	if(preferWebSockets) {
		data.insert(QStringLiteral("preferWebSockets"), true);
	}
	return make(data);
}

net::Message ServerReply::makeLoginWelcome(
	const QString &message, const QString &title, const QJsonArray &sessions)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("login")},
		 {QStringLiteral("message"), message},
		 {QStringLiteral("title"), title},
		 {QStringLiteral("sessions"), sessions}});
}

net::Message
ServerReply::makeLoginTitle(const QString &message, const QString &title)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("login")},
		 {QStringLiteral("message"), message},
		 {QStringLiteral("title"), title}});
}

net::Message ServerReply::makeLoginSessions(
	const QString &message, const QJsonArray &sessions)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("login")},
		 {QStringLiteral("message"), message},
		 {QStringLiteral("sessions"), sessions}});
}

net::Message ServerReply::makeLoginRemoveSessions(
	const QString &message, const QJsonArray &remove)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("login")},
		 {QStringLiteral("message"), message},
		 {QStringLiteral("remove"), remove}});
}

net::Message
ServerReply::makeReset(const QString &message, const QString &state)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("reset")},
		 {QStringLiteral("message"), message},
		 {QStringLiteral("state"), state}});
}

net::Message ServerReply::makeResetQuery(int maxSize, const QString &payload)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("autoreset")},
		 {QStringLiteral("maxSize"), maxSize},
		 {QStringLiteral("query"), true},
		 {QStringLiteral("payload"), payload}});
}

net::Message ServerReply::makeResetRequest(int maxSize)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("autoreset")},
		 {QStringLiteral("maxSize"), maxSize},
		 {QStringLiteral("query"), false}});
}

net::Message ServerReply::makeStreamedResetRequest(
	int maxSize, const QString &correlator, const QString &stream)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("autoreset")},
		 {QStringLiteral("maxSize"), maxSize},
		 {QStringLiteral("query"), false},
		 {QStringLiteral("correlator"), correlator},
		 {QStringLiteral("stream"), stream}});
}

net::Message ServerReply::makeStreamedResetStart(
	uint8_t contextId, const QString &correlator)
{
	QJsonObject data = {
		{QStringLiteral("type"), QStringLiteral("sstart")},
		{QStringLiteral("correlator"), correlator}};
	return net::makeServerCommandMessage(contextId, QJsonDocument{data});
}

net::Message
ServerReply::makeStreamedResetProgress(uint8_t contextId, bool cancel)
{
	QJsonObject data = {
		{QStringLiteral("type"), QStringLiteral("sprogress")},
		{QStringLiteral("cancel"), cancel}};
	return net::makeServerCommandMessage(contextId, QJsonDocument{data});
}

net::Message
ServerReply::makeResultClientInfo(const QString &message, bool browser)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("result")},
		 {QStringLiteral("message"), message},
		 {QStringLiteral("cinfo"),
		  QJsonObject({{QStringLiteral("browser"), browser}})}});
}

net::Message ServerReply::makeResultHostLookup(const QString &message)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("result")},
		 {QStringLiteral("message"), message},
		 {QStringLiteral("lookup"), QStringLiteral("host")}});
}

net::Message ServerReply::makeResultJoinLookup(
	const QString &message, const QJsonObject &session)
{
	QJsonObject data = {
		{QStringLiteral("type"), QStringLiteral("result")},
		{QStringLiteral("message"), message},
		{QStringLiteral("lookup"), QStringLiteral("join")}};
	if(!session.isEmpty()) {
		data[QStringLiteral("session")] = session;
	}
	return make(data);
}

net::Message ServerReply::makeResultPasswordNeeded(
	const QString &message, const QString &state)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("result")},
		 {QStringLiteral("message"), message},
		 {QStringLiteral("state"), state}});
}

net::Message ServerReply::makeResultLoginOk(
	const QString &message, const QString &state, const QJsonArray &flags,
	const QString &ident, bool guest)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("result")},
		 {QStringLiteral("message"), message},
		 {QStringLiteral("state"), state},
		 {QStringLiteral("flags"), flags},
		 {QStringLiteral("ident"), ident},
		 {QStringLiteral("guest"), guest}});
}

net::Message ServerReply::makeResultExtAuthNeeded(
	const QString &message, const QString &state, const QString &extauthurl,
	const QString &nonce, const QString &group, bool avatar)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("result")},
		 {QStringLiteral("message"), message},
		 {QStringLiteral("state"), state},
		 {QStringLiteral("extauthurl"), extauthurl},
		 {QStringLiteral("nonce"), nonce},
		 {QStringLiteral("group"), group},
		 {QStringLiteral("avatar"), avatar}});
}

net::Message ServerReply::makeResultJoinHost(
	const QString &message, const QString &state, const QJsonObject &join)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("result")},
		 {QStringLiteral("message"), message},
		 {QStringLiteral("state"), state},
		 {QStringLiteral("join"), join}});
}

net::Message
ServerReply::makeResultStartTls(const QString &message, bool startTls)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("result")},
		 {QStringLiteral("message"), message},
		 {QStringLiteral("startTls"), startTls}});
}

net::Message ServerReply::makeResultIdentIntentMismatch(
	const QString &message, const QString &intent, const QString &method,
	bool extAuthFallback)
{
	QJsonObject data = {
		{QStringLiteral("type"), QStringLiteral("result")},
		{QStringLiteral("state"), QStringLiteral("intentMismatch")},
		{QStringLiteral("message"), message},
		{QStringLiteral("intent"), intent},
		{QStringLiteral("method"), method}};
	if(extAuthFallback) {
		data[QStringLiteral("extauthfallback")] = true;
	}
	return make(data);
}

net::Message ServerReply::makeResultGarbage()
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("result")},
		 {QStringLiteral("message"), QStringLiteral("check authentication")},
		 {QStringLiteral("state"), QStringLiteral("checkauth")}});
}

net::Message ServerReply::makeSessionConf(const QJsonObject &config)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("sessionconf")},
		 {QStringLiteral("config"), config}});
}

net::Message ServerReply::makePasswordChange(const QString &password)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("passwordchange")},
		 {QStringLiteral("password"), password}});
}

net::Message ServerReply::makeSizeLimitWarning(int size, int maxSize)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("sizelimit")},
		 {QStringLiteral("size"), size},
		 {QStringLiteral("maxSize"), maxSize}});
}

net::Message ServerReply::makeOutOfSpace()
{
	return make({{QStringLiteral("type"), QStringLiteral("outofspace")}});
}

net::Message ServerReply::makeStatusUpdate(int size)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("status")},
		 {QStringLiteral("size"), size}});
}

net::Message ServerReply::makeInviteCreated(const QString &secret)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("invitecreated")},
		 {QStringLiteral("secret"), secret}});
}

net::Message ServerReply::makeThumbnailQuery(const QString &payload)
{
	return make(
		{{QStringLiteral("type"), QStringLiteral("thumbnail")},
		 {QStringLiteral("query"), true},
		 {QStringLiteral("payload"), payload}});
}

net::Message ServerReply::makeThumbnail(
	const QString &correlator, int maxWidth, int maxHeight, int quality,
	const QString &format)
{
	QJsonObject json(
		{{QStringLiteral("type"), QStringLiteral("thumbnail")},
		 {QStringLiteral("correlator"), correlator}});
	if(maxWidth > 0) {
		json.insert(QStringLiteral("maxWidth"), maxWidth);
	}
	if(maxHeight > 0) {
		json.insert(QStringLiteral("maxHeight"), maxHeight);
	}
	if(quality > 0) {
		json.insert(QStringLiteral("quality"), quality);
	}
	if(!format.isEmpty()) {
		json.insert(QStringLiteral("format"), format);
	}
	return make(json);
}
}
