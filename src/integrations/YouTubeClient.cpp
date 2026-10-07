/*
StreamMulticast — YouTube Live control-plane integration
Copyright (C) 2026 Avanatro <contact@avanatro.com>
GPLv2 — see LICENSE for full text.
*/

#include "YouTubeClient.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUrlQuery>

namespace smulti {

namespace {

constexpr const char *GOOGLE_AUTH_URL = "https://accounts.google.com/o/oauth2/v2/auth";
constexpr const char *GOOGLE_TOKEN_URL = "https://oauth2.googleapis.com/token";
constexpr const char *YOUTUBE_API_BASE = "https://www.googleapis.com/youtube/v3";

QString privacy_string(BroadcastPrivacy privacy)
{
	switch (privacy) {
	case BroadcastPrivacy::Private:
		return QStringLiteral("private");
	case BroadcastPrivacy::Unlisted:
		return QStringLiteral("unlisted");
	case BroadcastPrivacy::Public:
	default:
		return QStringLiteral("public");
	}
}

} // namespace

YouTubeClient::YouTubeClient(QObject *parent)
	: QObject(parent)
{
	connect(&m_oauth_server, &QTcpServer::newConnection,
	        this, &YouTubeClient::accept_oauth_connection);
}

QString YouTubeClient::random_urlsafe(int bytes) const
{
	QByteArray raw;
	raw.resize(bytes);
	for (int i = 0; i < bytes; ++i)
		raw[i] = static_cast<char>(QRandomGenerator::system()->generate() & 0xff);

	return QString::fromLatin1(
		raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

void YouTubeClient::start_oauth(const QString &client_id)
{
	m_oauth_server.close();
	m_client_id = client_id.trimmed();

	if (m_client_id.isEmpty()) {
		emit error(tr("Google OAuth Client ID is required."));
		return;
	}

	if (!m_oauth_server.listen(QHostAddress::LocalHost, 0)) {
		emit error(tr("Could not open the local OAuth callback listener."));
		return;
	}

	m_code_verifier = random_urlsafe(48);
	m_state = random_urlsafe(24);
	const QByteArray challenge_bytes = QCryptographicHash::hash(
		m_code_verifier.toUtf8(), QCryptographicHash::Sha256);
	const QString challenge = QString::fromLatin1(
		challenge_bytes.toBase64(
			QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));

	m_redirect_uri = QStringLiteral("http://127.0.0.1:%1")
		.arg(m_oauth_server.serverPort());

	QUrl url(QString::fromLatin1(GOOGLE_AUTH_URL));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("client_id"), m_client_id);
	query.addQueryItem(QStringLiteral("redirect_uri"), m_redirect_uri);
	query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
	query.addQueryItem(QStringLiteral("scope"),
	                   QStringLiteral("https://www.googleapis.com/auth/youtube.force-ssl"));
	query.addQueryItem(QStringLiteral("access_type"), QStringLiteral("offline"));
	query.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));
	query.addQueryItem(QStringLiteral("state"), m_state);
	query.addQueryItem(QStringLiteral("code_challenge"), challenge);
	query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
	url.setQuery(query);

	emit authorization_url_ready(url);
}

void YouTubeClient::accept_oauth_connection()
{
	QTcpSocket *socket = m_oauth_server.nextPendingConnection();
	if (!socket)
		return;

	connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
		const QByteArray request = socket->readAll();
		const int eol = request.indexOf("\r\n");
		if (eol <= 0)
			return;

		const QList<QByteArray> parts = request.left(eol).split(' ');
		if (parts.size() < 2) {
			socket->disconnectFromHost();
			return;
		}

		const QUrl callback(QStringLiteral("http://127.0.0.1") +
		                    QString::fromUtf8(parts.at(1)));
		const QUrlQuery query(callback);

		const QString returned_state = query.queryItemValue(QStringLiteral("state"));
		const QString code = query.queryItemValue(QStringLiteral("code"));
		const QString oauth_error = query.queryItemValue(QStringLiteral("error"));

		const QByteArray body = oauth_error.isEmpty()
			? QByteArray("<html><body><h2>StreamMulticast conectado ao YouTube.</h2>"
			             "<p>Voce pode fechar esta aba e voltar ao OBS.</p></body></html>")
			: QByteArray("<html><body><h2>Autorizacao cancelada.</h2>"
			             "<p>Volte ao OBS para tentar novamente.</p></body></html>");

		QByteArray response =
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: text/html; charset=utf-8\r\n"
			"Connection: close\r\n"
			"Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
		socket->write(response);
		socket->flush();
		socket->disconnectFromHost();
		m_oauth_server.close();

		if (!oauth_error.isEmpty()) {
			emit error(tr("Google authorization was cancelled or denied."));
			return;
		}
		if (returned_state != m_state) {
			emit error(tr("Google OAuth state validation failed."));
			return;
		}
		if (code.isEmpty()) {
			emit error(tr("Google did not return an authorization code."));
			return;
		}

		exchange_authorization_code(code);
	});
}

void YouTubeClient::exchange_authorization_code(const QString &code)
{
	QUrlQuery form;
	form.addQueryItem(QStringLiteral("client_id"), m_client_id);
	form.addQueryItem(QStringLiteral("code"), code);
	form.addQueryItem(QStringLiteral("code_verifier"), m_code_verifier);
	form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
	form.addQueryItem(QStringLiteral("redirect_uri"), m_redirect_uri);

	QNetworkRequest request(QUrl(QString::fromLatin1(GOOGLE_TOKEN_URL)));
	request.setHeader(QNetworkRequest::ContentTypeHeader,
	                  QStringLiteral("application/x-www-form-urlencoded"));

	auto *reply = m_network.post(
		request, form.query(QUrl::FullyEncoded).toUtf8());
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		const QByteArray payload = reply->readAll();
		reply->deleteLater();

		const QJsonObject root = QJsonDocument::fromJson(payload).object();
		const QString access_token = root.value(QStringLiteral("access_token")).toString();
		const QString refresh_token = root.value(QStringLiteral("refresh_token")).toString();

		if (access_token.isEmpty()) {
			emit_google_error(payload, tr("Could not exchange the Google authorization code."));
			return;
		}

		emit authenticated(access_token, refresh_token);
	});
}

void YouTubeClient::refresh_access_token(const QString &client_id,
                                         const QString &refresh_token)
{
	if (client_id.trimmed().isEmpty() || refresh_token.isEmpty()) {
		emit error(tr("Saved YouTube authorization is incomplete."));
		return;
	}

	QUrlQuery form;
	form.addQueryItem(QStringLiteral("client_id"), client_id.trimmed());
	form.addQueryItem(QStringLiteral("refresh_token"), refresh_token);
	form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));

	QNetworkRequest request(QUrl(QString::fromLatin1(GOOGLE_TOKEN_URL)));
	request.setHeader(QNetworkRequest::ContentTypeHeader,
	                  QStringLiteral("application/x-www-form-urlencoded"));

	auto *reply = m_network.post(
		request, form.query(QUrl::FullyEncoded).toUtf8());
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		const QByteArray payload = reply->readAll();
		reply->deleteLater();

		const QJsonObject root = QJsonDocument::fromJson(payload).object();
		const QString access_token = root.value(QStringLiteral("access_token")).toString();
		if (access_token.isEmpty()) {
			emit_google_error(payload, tr("Could not refresh YouTube authorization."));
			return;
		}

		emit access_token_refreshed(access_token);
	});
}

QNetworkRequest YouTubeClient::authorized_request(const QUrl &url,
                                                  const QString &access_token) const
{
	QNetworkRequest request(url);
	request.setRawHeader("Authorization", "Bearer " + access_token.toUtf8());
	return request;
}

void YouTubeClient::fetch_channel(const QString &access_token)
{
	QUrl url(QString::fromLatin1(YOUTUBE_API_BASE) + QStringLiteral("/channels"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("part"), QStringLiteral("id,snippet"));
	query.addQueryItem(QStringLiteral("mine"), QStringLiteral("true"));
	query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("1"));
	url.setQuery(query);

	auto *reply = m_network.get(authorized_request(url, access_token));
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		const QByteArray payload = reply->readAll();
		reply->deleteLater();

		const QJsonObject root = QJsonDocument::fromJson(payload).object();
		if (root.contains(QStringLiteral("error"))) {
			emit_google_error(payload, tr("Could not read the connected YouTube channel."));
			return;
		}

		const QJsonArray items = root.value(QStringLiteral("items")).toArray();
		if (items.isEmpty()) {
			emit error(tr("The connected Google account has no YouTube channel."));
			return;
		}

		const QJsonObject channel = items.first().toObject();
		const QString id = channel.value(QStringLiteral("id")).toString();
		const QString title = channel.value(QStringLiteral("snippet"))
			.toObject().value(QStringLiteral("title")).toString();
		emit channel_ready(id, title);
	});
}

void YouTubeClient::prepare_native_obs_broadcast(const QString &access_token,
                                                 const QString &obs_stream_key,
                                                 const BroadcastPlan &plan)
{
	if (access_token.isEmpty() || obs_stream_key.isEmpty()) {
		emit error(tr("YouTube authorization or OBS stream key is missing."));
		return;
	}

	QUrl url(QString::fromLatin1(YOUTUBE_API_BASE) + QStringLiteral("/liveStreams"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("part"), QStringLiteral("id,snippet,cdn,status"));
	query.addQueryItem(QStringLiteral("mine"), QStringLiteral("true"));
	query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("50"));
	url.setQuery(query);

	auto *reply = m_network.get(authorized_request(url, access_token));
	connect(reply, &QNetworkReply::finished, this,
	        [this, reply, access_token, obs_stream_key, plan]() {
		const QByteArray payload = reply->readAll();
		reply->deleteLater();

		const QJsonObject root = QJsonDocument::fromJson(payload).object();
		if (root.contains(QStringLiteral("error"))) {
			emit_google_error(payload, tr("Could not list YouTube live streams."));
			return;
		}

		QString stream_id;
		const QJsonArray items = root.value(QStringLiteral("items")).toArray();
		for (const auto &value : items) {
			const QJsonObject stream = value.toObject();
			const QString stream_name = stream.value(QStringLiteral("cdn"))
				.toObject().value(QStringLiteral("ingestionInfo"))
				.toObject().value(QStringLiteral("streamName")).toString();
			if (stream_name == obs_stream_key) {
				stream_id = stream.value(QStringLiteral("id")).toString();
				break;
			}
		}

		if (stream_id.isEmpty()) {
			emit error(
				tr("The YouTube stream key configured in OBS was not found among the "
				   "account's reusable YouTube live streams. Use a reusable Stream Key "
				   "in YouTube Studio/OBS, then try again."));
			return;
		}

		create_broadcast(access_token, stream_id, plan);
	});
}

void YouTubeClient::create_broadcast(const QString &access_token,
                                     const QString &stream_id,
                                     const BroadcastPlan &plan)
{
	if (plan.title.empty()) {
		emit error(tr("Enter a broadcast title first."));
		return;
	}

	QDateTime scheduled = QDateTime::fromString(
		QString::fromStdString(plan.scheduled_start_utc), Qt::ISODate);
	if (!scheduled.isValid())
		scheduled = QDateTime::currentDateTimeUtc().addSecs(60);
	else
		scheduled = scheduled.toUTC();

	if (scheduled <= QDateTime::currentDateTimeUtc().addSecs(15))
		scheduled = QDateTime::currentDateTimeUtc().addSecs(60);

	QJsonObject snippet;
	snippet.insert(QStringLiteral("title"), QString::fromStdString(plan.title));
	snippet.insert(QStringLiteral("description"), QString::fromStdString(plan.description));
	snippet.insert(QStringLiteral("scheduledStartTime"),
	               scheduled.toString(Qt::ISODate));

	QJsonObject status;
	status.insert(QStringLiteral("privacyStatus"), privacy_string(plan.privacy));

	QJsonObject content_details;
	content_details.insert(QStringLiteral("enableAutoStart"), true);
	content_details.insert(QStringLiteral("enableAutoStop"), true);
	content_details.insert(QStringLiteral("enableDvr"), true);
	content_details.insert(QStringLiteral("recordFromStart"), true);

	QJsonObject root;
	root.insert(QStringLiteral("snippet"), snippet);
	root.insert(QStringLiteral("status"), status);
	root.insert(QStringLiteral("contentDetails"), content_details);

	QUrl url(QString::fromLatin1(YOUTUBE_API_BASE) + QStringLiteral("/liveBroadcasts"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("part"),
	                   QStringLiteral("id,snippet,contentDetails,status"));
	url.setQuery(query);

	QNetworkRequest request = authorized_request(url, access_token);
	request.setHeader(QNetworkRequest::ContentTypeHeader,
	                  QStringLiteral("application/json"));

	auto *reply = m_network.post(request, QJsonDocument(root).toJson(QJsonDocument::Compact));
	connect(reply, &QNetworkReply::finished, this,
	        [this, reply, access_token, stream_id]() {
		const QByteArray payload = reply->readAll();
		reply->deleteLater();

		const QJsonObject response = QJsonDocument::fromJson(payload).object();
		if (response.contains(QStringLiteral("error"))) {
			emit_google_error(payload, tr("Could not create the YouTube broadcast."));
			return;
		}

		const QString broadcast_id = response.value(QStringLiteral("id")).toString();
		if (broadcast_id.isEmpty()) {
			emit error(tr("YouTube created a broadcast without returning its ID."));
			return;
		}

		bind_broadcast(access_token, broadcast_id, stream_id);
	});
}

void YouTubeClient::bind_broadcast(const QString &access_token,
                                   const QString &broadcast_id,
                                   const QString &stream_id)
{
	QUrl url(QString::fromLatin1(YOUTUBE_API_BASE) + QStringLiteral("/liveBroadcasts/bind"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("id"), broadcast_id);
	query.addQueryItem(QStringLiteral("streamId"), stream_id);
	query.addQueryItem(QStringLiteral("part"),
	                   QStringLiteral("id,snippet,contentDetails,status"));
	url.setQuery(query);

	QNetworkRequest request = authorized_request(url, access_token);
	request.setHeader(QNetworkRequest::ContentTypeHeader,
	                  QStringLiteral("application/json"));

	auto *reply = m_network.post(request, QByteArray());
	connect(reply, &QNetworkReply::finished, this,
	        [this, reply, broadcast_id, stream_id]() {
		const QByteArray payload = reply->readAll();
		reply->deleteLater();

		const QJsonObject root = QJsonDocument::fromJson(payload).object();
		if (root.contains(QStringLiteral("error"))) {
			emit_google_error(payload, tr("YouTube broadcast was created but could not be bound to the OBS stream."));
			return;
		}

		emit broadcast_prepared(broadcast_id, stream_id);
	});
}

void YouTubeClient::emit_google_error(const QByteArray &payload,
                                      const QString &fallback)
{
	const QJsonObject root = QJsonDocument::fromJson(payload).object();
	const QJsonObject error_obj = root.value(QStringLiteral("error")).toObject();
	QString message = error_obj.value(QStringLiteral("message")).toString();
	if (message.isEmpty())
		message = root.value(QStringLiteral("error_description")).toString();
	if (message.isEmpty())
		message = fallback;
	emit error(message);
}

} // namespace smulti
